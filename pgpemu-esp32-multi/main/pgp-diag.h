#ifndef PGP_DIAG_H
#define PGP_DIAG_H

#include <stdbool.h>

/* Only pass operational metadata, never authentication payloads or addresses. */
void pgp_diag_init(void);
void pgp_diag_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
void pgp_diag_ble_event(void);
void pgp_diag_status(int clients, bool advertising, bool pending);

#endif
