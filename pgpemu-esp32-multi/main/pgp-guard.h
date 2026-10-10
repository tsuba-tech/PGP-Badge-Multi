#ifndef PGP_GUARD_H
#define PGP_GUARD_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"

#define PGP_GUARD_CLIENTS 3

void pgp_guard_init(const esp_ble_adv_params_t *params);
void pgp_guard_client(int slot, bool used, uint16_t conn, const esp_bd_addr_t remote,
                      uint32_t generation, int cert_state, bool cert_enabled);
void pgp_guard_advertising(bool ready, bool active, bool pending, int clients);
void pgp_guard_tx_failure(int slot, uint32_t generation, bool certificate);
void pgp_guard_watch_current_task(void);
void pgp_guard_task_progress(void);
void pgp_guard_ble_enter(void);
void pgp_guard_ble_exit(void);

#endif
