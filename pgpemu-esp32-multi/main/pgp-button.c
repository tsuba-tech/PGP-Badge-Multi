#include "pgp-button.h"

#include <string.h>

static bool is_press(const uint8_t *data, uint8_t len)
{
    for (uint8_t i = 0; i < len; ++i) {
        if (data[i]) return true;
    }
    return false;
}

bool pgp_button_request(pgp_button_request_t *request, uint32_t generation,
                        const uint8_t *data, uint8_t len,
                        int64_t not_before_us, int64_t expires_us)
{
    if (len > sizeof(request->data)) {
        len = sizeof(request->data);
    }
    /* Press patterns are randomized, so merge by kind rather than exact bytes. */
    if (request->pending && request->generation == generation &&
        is_press(request->data, request->len) == is_press(data, len)) {
        if (expires_us > request->expires_us) {
            request->expires_us = expires_us;
        }
        return true;
    }
    request->pending = true;
    request->generation = generation;
    ++request->sequence;
    request->not_before_us = not_before_us;
    request->expires_us = expires_us;
    request->len = len;
    memset(request->data, 0, sizeof(request->data));
    memcpy(request->data, data, len);
    return false;
}

void pgp_button_complete(pgp_button_request_t *request, uint32_t sequence)
{
    if (request->pending && request->sequence == sequence) {
        request->pending = false;
    }
}

void pgp_button_random_press(uint32_t random_start, uint32_t random_length, uint8_t out[2])
{
    unsigned start = random_start % 6;
    unsigned last = start + 4 + random_length % (10 - start - 4);
    uint16_t pattern = 0;
    for (unsigned sample = 0; sample < 10; ++sample) {
        pattern = (uint16_t)(pattern << 1);
        if (sample >= start && sample <= last) {
            pattern |= 1;
        }
    }
    out[0] = (pattern >> 8) & 0x03;
    out[1] = pattern & 0xff;
}
