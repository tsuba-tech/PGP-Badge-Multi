#include "pgp-guard.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "pgp-diag.h"

#define SECOND 1000000LL
#define TASK_TIMEOUT_S 10
#define SETUP_TIMEOUT_US (120 * SECOND)
#define AUTH_TIMEOUT_US (45 * SECOND)
#define CLOSE_RETRY_US (15 * SECOND)
#define CLOSE_STUCK_US (120 * SECOND)
#define ADV_RETRY_US (5 * SECOND)
#define ADV_PENDING_US (20 * SECOND)
#define FAILURE_WINDOW_US (30 * SECOND)
#define STABLE_BOOT_US (600 * SECOND)
#define RTC_MAGIC 0x50475057U

typedef struct {
    bool used;
    uint16_t conn;
    esp_bd_addr_t remote;
    uint32_t generation;
    int cert_state;
    bool cert_enabled;
    int64_t connected_at;
    int64_t auth_progress_at;
    int64_t failure_window_at;
    unsigned failures;
    bool fault;
    int64_t close_started_at;
    int64_t close_retry_at;
    unsigned close_attempts;
} guarded_client_t;

/* Survives software/watchdog resets without requiring flash from a panic ISR. */
typedef struct {
    uint32_t magic;
    uint32_t repeated_resets;
    uint32_t planned_restart;
    uint32_t restart_cause;
} reboot_record_t;
static RTC_NOINIT_ATTR reboot_record_t reboot_record;

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static guarded_client_t links[PGP_GUARD_CLIENTS];
static esp_ble_adv_params_t advertising_params;
static bool adv_ready, adv_active, adv_pending;
static int connected_count;
static int64_t adv_changed_at, adv_retry_at;
static int64_t adv_failure_since;
static bool initialized;
static bool restart_allowed;
static unsigned callback_depth;
static int64_t callback_started_at;
static const char *TAG = "PGP_GUARD";

void pgp_guard_ble_enter(void)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    if (callback_depth++ == 0) {
        callback_started_at = now;
    }
    portEXIT_CRITICAL(&lock);
}

void pgp_guard_ble_exit(void)
{
    portENTER_CRITICAL(&lock);
    if (callback_depth) {
        --callback_depth;
    }
    portEXIT_CRITICAL(&lock);
}

void pgp_guard_watch_current_task(void)
{
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
}

void pgp_guard_task_progress(void)
{
    ESP_ERROR_CHECK(esp_task_wdt_reset());
}

void pgp_guard_client(int slot, bool used, uint16_t conn, const esp_bd_addr_t remote,
                      uint32_t generation, int cert_state, bool cert_enabled)
{
    if (!initialized || slot < 0 || slot >= PGP_GUARD_CLIENTS) {
        return;
    }
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    guarded_client_t *client = &links[slot];
    if (!used) {
        memset(client, 0, sizeof(*client));
    } else {
        if (!client->used || client->generation != generation) {
            memset(client, 0, sizeof(*client));
            client->connected_at = now;
            client->auth_progress_at = now;
        } else if (client->cert_state != cert_state || client->cert_enabled != cert_enabled) {
            client->auth_progress_at = now;
        }
        client->used = true;
        client->conn = conn;
        memcpy(client->remote, remote, sizeof(client->remote));
        client->generation = generation;
        client->cert_state = cert_state;
        client->cert_enabled = cert_enabled;
    }
    portEXIT_CRITICAL(&lock);
}

void pgp_guard_advertising(bool ready, bool active, bool pending, int clients)
{
    if (!initialized) {
        return;
    }
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    if (adv_ready != ready || adv_active != active || adv_pending != pending ||
        connected_count != clients) {
        adv_changed_at = now;
    }
    adv_ready = ready;
    adv_active = active;
    adv_pending = pending;
    connected_count = clients;
    if (active || clients > 0) {
        adv_failure_since = 0;
    }
    portEXIT_CRITICAL(&lock);
}

void pgp_guard_tx_failure(int slot, uint32_t generation, bool certificate)
{
    if (!initialized || slot < 0 || slot >= PGP_GUARD_CLIENTS) {
        return;
    }
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    guarded_client_t *client = &links[slot];
    if (client->used && client->generation == generation) {
        if (!client->failures || now - client->failure_window_at > FAILURE_WINDOW_US) {
            client->failure_window_at = now;
            client->failures = 0;
        }
        ++client->failures;
        /* One lost authentication message, or repeated failed button messages. */
        if (certificate || client->failures >= 3) {
            client->fault = true;
        }
    }
    portEXIT_CRITICAL(&lock);
}

static guarded_client_t snapshot(int slot)
{
    portENTER_CRITICAL(&lock);
    guarded_client_t result = links[slot];
    portEXIT_CRITICAL(&lock);
    return result;
}

static void restart_system(const char *reason)
{
    if (!restart_allowed) {
        /* Do not create a permanent reboot loop. Manual power cycle clears this mode. */
        return;
    }
    reboot_record.planned_restart = 1;
    reboot_record.restart_cause = strcmp(reason, "ble_callback_stalled") == 0 ? 1 :
        strcmp(reason, "all_client_closes_stalled") == 0 ? 2 :
        strcmp(reason, "advertising_stalled") == 0 ? 3 : 4;
    pgp_diag_log("GUARD_RESTART reason=%s prior_resets=%lu", reason,
                 (unsigned long)reboot_record.repeated_resets);
    ESP_LOGE(TAG, "Restarting: %s", reason);
    /* Best effort only: the logger may itself be unhealthy. RTC records the decision. */
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

static void guard_task(void *unused)
{
    pgp_guard_watch_current_task();
    bool stable_recorded = false;
    for (;;) {
        int64_t now = esp_timer_get_time();
        bool callback_stalled;
        portENTER_CRITICAL(&lock);
        callback_stalled = callback_depth && now - callback_started_at >= TASK_TIMEOUT_S * SECOND;
        portEXIT_CRITICAL(&lock);
        if (callback_stalled) {
            restart_system("ble_callback_stalled");
        }
        unsigned active = 0, stuck = 0;
        bool clients_unhealthy = false;
        for (int slot = 0; slot < PGP_GUARD_CLIENTS; ++slot) {
            guarded_client_t client = snapshot(slot);
            if (!client.used) {
                continue;
            }
            ++active;
            const char *reason = NULL;
            if (client.fault) {
                reason = "notification_failures";
            } else if (client.cert_state != 6) {
                if (now - client.connected_at >= SETUP_TIMEOUT_US) {
                    reason = "setup_timeout";
                } else if (client.cert_enabled && now - client.auth_progress_at >= AUTH_TIMEOUT_US) {
                    reason = "auth_stalled";
                }
            }
            /* There is deliberately no inactivity timeout for authenticated clients. */
            if (reason == NULL && !client.close_started_at) {
                continue;
            }
            clients_unhealthy = true;
            if (client.close_started_at && now - client.close_started_at >= CLOSE_STUCK_US) {
                ++stuck;
            }
            if (now < client.close_retry_at || client.close_attempts >= 3) {
                continue;
            }
            bool valid;
            portENTER_CRITICAL(&lock);
            guarded_client_t *current = &links[slot];
            valid = current->used && current->generation == client.generation;
            if (valid) {
                if (!current->close_started_at) {
                    current->close_started_at = now;
                }
                current->close_retry_at = now + CLOSE_RETRY_US;
                ++current->close_attempts;
            }
            portEXIT_CRITICAL(&lock);
            if (valid) {
                /* Address-targeted recovery cannot disconnect a different phone
                 * that reuses this conn_id between validation and the API call. */
                esp_err_t err = esp_ble_gap_disconnect(client.remote);
                pgp_diag_log("GUARD_CLOSE conn=%u gen=%lu reason=%s attempt=%u result=%s",
                             client.conn, (unsigned long)client.generation,
                             reason ? reason : "close_pending", client.close_attempts + 1,
                             esp_err_to_name(err));
                ESP_LOGW(TAG, "Client recovery conn=%u reason=%s result=%s", client.conn,
                         reason ? reason : "close_pending", esp_err_to_name(err));
            }
        }
        /* Never restart the whole ESP32 for one stuck link while another remains healthy. */
        if (active && stuck == active) {
            restart_system("all_client_closes_stalled");
        }

        bool retry_advertising = false, setup_stalled, advertising_stalled, ready;
        portENTER_CRITICAL(&lock);
        ready = adv_ready;
        setup_stalled = !adv_ready && now >= SETUP_TIMEOUT_US;
        advertising_stalled = adv_ready && !adv_active && connected_count == 0 &&
            adv_failure_since && now - adv_failure_since >= SETUP_TIMEOUT_US;
        if (adv_ready && !adv_active && connected_count < PGP_GUARD_CLIENTS &&
            now >= adv_retry_at && now - adv_changed_at >=
                (adv_pending ? ADV_PENDING_US : ADV_RETRY_US)) {
            retry_advertising = true;
            adv_retry_at = now + ADV_PENDING_US;
            if (!adv_failure_since) {
                adv_failure_since = now;
            }
        }
        portEXIT_CRITICAL(&lock);
        if (setup_stalled) {
            restart_system("ble_setup_stalled");
        } else if (advertising_stalled) {
            restart_system("advertising_stalled");
        }
        if (retry_advertising) {
            /* Does not touch authentication, slots, or existing connections. */
            esp_err_t err = esp_ble_gap_start_advertising(&advertising_params);
            pgp_diag_log("GUARD_ADV_RETRY result=%s", esp_err_to_name(err));
            ESP_LOGW(TAG, "Advertising recovery: %s", esp_err_to_name(err));
        }
        if (!stable_recorded && now >= STABLE_BOOT_US && !callback_stalled &&
            !clients_unhealthy && ready && !advertising_stalled) {
            /* A stable boot renews the budget, but suppressed panic stays suppressed
             * for this boot to avoid immediately restarting an already stuck task. */
            reboot_record.repeated_resets = 0;
            stable_recorded = true;
            pgp_diag_log("GUARD_STABLE restart_enabled=%d", restart_allowed);
        }
        pgp_guard_task_progress();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void pgp_guard_init(const esp_ble_adv_params_t *params)
{
    esp_reset_reason_t reset = esp_reset_reason();
    bool retained = reboot_record.magic == RTC_MAGIC &&
        reboot_record.repeated_resets <= 3 && reboot_record.planned_restart <= 1 &&
        reboot_record.restart_cause <= 4;
    uint32_t previous_cause = retained ? reboot_record.restart_cause : 0;
    bool automatic = retained && (reset == ESP_RST_TASK_WDT || reset == ESP_RST_INT_WDT ||
        reset == ESP_RST_WDT || (reset == ESP_RST_SW && reboot_record.planned_restart));
    if (!automatic) {
        memset(&reboot_record, 0, sizeof(reboot_record));
        reboot_record.magic = RTC_MAGIC;
    } else if (reboot_record.repeated_resets < 3) {
        ++reboot_record.repeated_resets;
    }
    reboot_record.planned_restart = 0;
    reboot_record.restart_cause = 0;
    restart_allowed = reboot_record.repeated_resets < 3;
    advertising_params = *params;
    initialized = true;
    /* Explicit runtime configuration also applies to existing sdkconfig files. */
    ESP_ERROR_CHECK(esp_task_wdt_init(TASK_TIMEOUT_S, restart_allowed));
    pgp_diag_log("GUARD_BOOT reset=%d automatic=%d repeated=%lu restart_enabled=%d task_timeout=%d cause=%lu",
                 reset, automatic, (unsigned long)reboot_record.repeated_resets,
                 restart_allowed, TASK_TIMEOUT_S, (unsigned long)previous_cause);
    ESP_LOGI(TAG, "Watchdog enabled: tasks=%ds auto_restart=%d repeated=%lu",
             TASK_TIMEOUT_S, restart_allowed, (unsigned long)reboot_record.repeated_resets);
    if (!restart_allowed) {
        ESP_LOGE(TAG, "Repeated-reset protection: automatic task/global restart disabled for this boot");
    }
    BaseType_t result = xTaskCreate(guard_task, "pgp_guard", 4096, NULL, 4, NULL);
    ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
