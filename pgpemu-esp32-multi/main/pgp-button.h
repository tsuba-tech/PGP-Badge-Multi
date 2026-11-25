#ifndef PGP_BUTTON_H
#define PGP_BUTTON_H

#include <stdbool.h>
#include <stdint.h>

/* At most one outstanding button press per client. Pure logic without
 * FreeRTOS: callers hold their own lock around every call. */
typedef struct {
    bool pending;
    uint32_t generation;
    uint32_t sequence;
    int64_t expires_us;
    uint8_t len;
    uint8_t data[2];
} pgp_button_request_t;

/* Returns true when merged into an identical pending press: no second press is
 * sent, but the deadline is extended to the later of the two. Otherwise the new
 * request replaces whatever was pending. */
bool pgp_button_request(pgp_button_request_t *request, uint32_t generation,
                        const uint8_t *data, uint8_t len, int64_t expires_us);

/* Clears the request after a successful send, unless it was replaced since
 * `sequence` was observed (the replacement must still be sent). */
void pgp_button_complete(pgp_button_request_t *request, uint32_t sequence);

#endif
