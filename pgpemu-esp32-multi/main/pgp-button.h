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
    int64_t not_before_us;   /* human-like delay: do not send earlier */
    int64_t expires_us;
    uint8_t len;
    uint8_t data[2];
} pgp_button_request_t;

/* Returns true when merged into a pending request of the same kind (press or
 * release): no second press is sent, its delay is kept and the deadline is
 * extended to the later of the two. Otherwise the new request replaces
 * whatever was pending. */
bool pgp_button_request(pgp_button_request_t *request, uint32_t generation,
                        const uint8_t *data, uint8_t len,
                        int64_t not_before_us, int64_t expires_us);

/* Clears the request after a successful send, unless it was replaced since
 * `sequence` was observed (the replacement must still be sent). */
void pgp_button_complete(pgp_button_request_t *request, uint32_t sequence);

/* Button value: 10 samples of 50 ms, sample 0 in bit 9 (spezifisch/pgpemu,
 * after u/EeveesGalore's notes). {0x03, 0xff} holds all 500 ms. Starts at
 * sample 0-5 and holds at least 5 samples (250-500 ms), from two random words. */
void pgp_button_random_press(uint32_t random_start, uint32_t random_length, uint8_t out[2]);

#endif
