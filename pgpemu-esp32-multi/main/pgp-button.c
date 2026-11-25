#include "pgp-button.h"

#include <string.h>

bool pgp_button_request(pgp_button_request_t *request, uint32_t generation,
                        const uint8_t *data, uint8_t len, int64_t expires_us)
{
    if (len > sizeof(request->data)) {
        len = sizeof(request->data);
    }
    if (request->pending && request->generation == generation &&
        request->len == len && memcmp(request->data, data, len) == 0) {
        if (expires_us > request->expires_us) {
            request->expires_us = expires_us;
        }
        return true;
    }
    request->pending = true;
    request->generation = generation;
    ++request->sequence;
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
