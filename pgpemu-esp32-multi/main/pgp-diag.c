#include "pgp-diag.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"

#define DIAG_MAGIC 0x50475044U
#define DIAG_SECTOR_SIZE 4096U
#define DIAG_PARTITION_SIZE 0x80000U
#define DIAG_BANK_SIZE (DIAG_PARTITION_SIZE / 2)
#define DIAG_HEARTBEAT_US (60LL * 1000000LL)
#define DIAG_IDLE_HEARTBEAT_US (600LL * 1000000LL)

/* Fixed little-endian wire format shared with tools/download-diagnostics.py. */
typedef struct {
    uint32_t magic;
    uint32_t sequence;
    uint64_t uptime_ms;
    char message[236];
    uint32_t crc;
} diag_record_t;

_Static_assert(sizeof(diag_record_t) == 256, "Diagnostic wire format size");
_Static_assert(offsetof(diag_record_t, crc) == 252, "Diagnostic CRC offset");

static const char *TAG = "PGP_DIAG";
static const esp_partition_t *partition;
static QueueHandle_t records;
static size_t write_offset;
static size_t bank_start;
static uint32_t sequence;
static portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t dropped;
static uint32_t ble_events;
static int client_count;
static bool adv_active;
static bool adv_pending;

/* Standard CRC-32, matching Python zlib.crc32, detects interrupted writes. */
static uint32_t record_crc(const diag_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < offsetof(diag_record_t, crc); ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

void pgp_diag_ble_event(void)
{
    portENTER_CRITICAL(&stats_lock);
    ++ble_events;
    portEXIT_CRITICAL(&stats_lock);
}

void pgp_diag_status(int clients, bool advertising, bool pending)
{
    portENTER_CRITICAL(&stats_lock);
    client_count = clients;
    adv_active = advertising;
    adv_pending = pending;
    portEXIT_CRITICAL(&stats_lock);
}

void pgp_diag_log(const char *format, ...)
{
    if (records == NULL) {
        return;
    }
    diag_record_t record = {0};
    record.uptime_ms = esp_timer_get_time() / 1000;
    va_list args;
    va_start(args, format);
    vsnprintf(record.message, sizeof(record.message), format, args);
    va_end(args);
    /* BLE callbacks never wait for flash I/O or a full diagnostic queue. */
    if (xQueueSend(records, &record, 0) != pdTRUE) {
        portENTER_CRITICAL(&stats_lock);
        ++dropped;
        portEXIT_CRITICAL(&stats_lock);
    }
}

static bool persist(diag_record_t *record)
{
    esp_err_t err = ESP_OK;
    if (write_offset % DIAG_SECTOR_SIZE == 0) {
        err = esp_partition_erase_range(partition, write_offset, DIAG_SECTOR_SIZE);
    }
    if (err == ESP_OK) {
        record->magic = DIAG_MAGIC;
        record->sequence = ++sequence;
        record->crc = record_crc(record);
        err = esp_partition_write(partition, write_offset, record, sizeof(*record));
        if (err == ESP_OK) {
            diag_record_t check;
            err = esp_partition_read(partition, write_offset, &check, sizeof(check));
            if (err == ESP_OK && memcmp(&check, record, sizeof(check)) != 0) {
                err = ESP_ERR_INVALID_CRC;
            }
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Diagnostic flash failed: %s; recording stopped", esp_err_to_name(err));
        return false;
    }
    write_offset = bank_start + (write_offset - bank_start + sizeof(*record)) % DIAG_BANK_SIZE;
    return true;
}

static void diagnostic_task(void *unused)
{
    int64_t next_heartbeat = esp_timer_get_time() + DIAG_HEARTBEAT_US;
    bool previously_connected = false;
    for (;;) {
        diag_record_t record;
        if (xQueueReceive(records, &record, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (!persist(&record)) {
                break;
            }
        }
        portENTER_CRITICAL(&stats_lock);
        bool currently_connected = client_count > 0;
        portEXIT_CRITICAL(&stats_lock);
        if (currently_connected && !previously_connected) {
            int64_t due = esp_timer_get_time() + DIAG_HEARTBEAT_US;
            if (next_heartbeat > due) next_heartbeat = due;
        }
        previously_connected = currently_connected;
        if (esp_timer_get_time() >= next_heartbeat) {
            uint32_t lost, events;
            int connected;
            bool active, pending;
            portENTER_CRITICAL(&stats_lock);
            lost = dropped;
            events = ble_events;
            connected = client_count;
            active = adv_active;
            pending = adv_pending;
            portEXIT_CRITICAL(&stats_lock);
            memset(&record, 0, sizeof(record));
            record.uptime_ms = esp_timer_get_time() / 1000;
            snprintf(record.message, sizeof(record.message),
                     "HEARTBEAT clients=%d adv=%d pending=%d ble_events=%" PRIu32
                     " heap=%" PRIu32 " min_heap=%" PRIu32 " lost=%" PRIu32,
                     connected, active, pending, events, esp_get_free_heap_size(),
                     esp_get_minimum_free_heap_size(), lost);
            if (!persist(&record)) {
                break;
            }
            next_heartbeat = esp_timer_get_time() +
                (connected ? DIAG_HEARTBEAT_US : DIAG_IDLE_HEARTBEAT_US);
        }
    }
    vTaskDelete(NULL);
}

void pgp_diag_init(void)
{
    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                          ESP_PARTITION_SUBTYPE_ANY, "pgp_diag");
    if (partition == NULL || partition->size != DIAG_PARTITION_SIZE) {
        ESP_LOGE(TAG, "Missing diagnostic partition; flash the custom partition table");
        return;
    }
    /* Recover the newest valid record. Partial records after power loss are ignored. */
    size_t newest_offset = 0;
    bool found = false;
    unsigned foreign = 0, crc_bad = 0;
    for (size_t offset = 0; offset < partition->size; offset += sizeof(diag_record_t)) {
        diag_record_t record;
        esp_err_t err = esp_partition_read(partition, offset, &record, sizeof(record));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Diagnostic scan failed: %s", esp_err_to_name(err));
            return;
        }
        if (record.magic == DIAG_MAGIC) {
            if (record.crc != record_crc(&record)) {
                ++crc_bad;
            } else if (!found || record.sequence > sequence) {
                sequence = record.sequence;
                newest_offset = offset;
                found = true;
            }
        } else {
            const uint8_t *raw = (const uint8_t *)&record;
            bool erased = true;
            for (size_t i = 0; i < sizeof(record); ++i) erased &= raw[i] == 0xff;
            if (!erased) ++foreign;
        }
    }
    /* Alternate two circular banks on reboot. Keep the previous boot's tail intact
     * while playing again; only the current bank is overwritten when it fills.
     * Beginning at a sector boundary also handles an interrupted flash write. */
    bank_start = found && newest_offset < DIAG_BANK_SIZE ? DIAG_BANK_SIZE : 0;
    write_offset = bank_start;
    records = xQueueCreate(32, sizeof(diag_record_t));
    if (records == NULL) {
        ESP_LOGE(TAG, "No RAM for diagnostic queue");
        return;
    }
    if (xTaskCreate(diagnostic_task, "pgp_diag", 3072, NULL, 2, NULL) != pdPASS) {
        vQueueDelete(records);
        records = NULL;
        ESP_LOGE(TAG, "No RAM for diagnostic task");
        return;
    }
    pgp_diag_log("BOOT reset_reason=%d version=%s previous_seq=%" PRIu32 " bank=%u",
                 esp_reset_reason(), esp_ota_get_app_description()->version, sequence,
                 (unsigned)(bank_start / DIAG_BANK_SIZE));
    pgp_diag_log("DIAG_SCAN non_record=%u crc_bad=%u write_verify=1", foreign, crc_bad);
    ESP_LOGI(TAG, "Persistent diagnostics enabled: 512 KiB, previous boot retained");
}
