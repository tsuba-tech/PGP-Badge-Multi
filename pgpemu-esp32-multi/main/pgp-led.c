/* LED classification adapted from spezifisch/pgpemu's pgp_led_handler.c.
 * Copyright (c) 2018, Yohanes Nugroho
 * Copyright (c) 2023, spezifisch <spezifisch23@proton.me>
 * Distributed under BSD-2-Clause; see LICENSE.LED in this directory.
 * These are observed protocol patterns, not an official game specification.
 */
#include "pgp-led.h"

pgp_led_event_t pgp_led_classify(const uint8_t *data, size_t length)
{
    if (!data || length < 4) {
        return PGP_LED_INVALID;
    }
    unsigned patterns = data[3] & 31;
    if (patterns > (length - 4) / 3) {
        return PGP_LED_INVALID;
    }
    unsigned off = 0, red = 0, green = 0, blue = 0, yellow = 0, white = 0, shakes = 0;
    for (unsigned i = 0; i < patterns; ++i) {
        unsigned r = data[5 + 3 * i] & 15;
        unsigned g = (data[5 + 3 * i] >> 4) & 15;
        unsigned b = data[6 + 3 * i] & 15;
        if (!r && !g && !b) ++off;
        else if (r && !g && !b) ++red;
        else if (!r && g && !b) ++green;
        else if (!r && !g && b) ++blue;
        else if (r && g && !b) ++yellow;
        else if (r && g && b) {
            ++white;
            if (i <= 9) ++shakes;
        }
    }
    unsigned lit = patterns - off;
    if (off && !lit) return PGP_LED_OFF;
    if (white && white == lit) return PGP_LED_BAG_FULL;
    if (red && off && red == lit) return PGP_LED_BALLS_OR_RANGE;
    if (red && !off && red == lit) return PGP_LED_BOX_FULL;
    if (green && green == lit) return PGP_LED_POKEMON;
    if (yellow && yellow == lit) return PGP_LED_NEW_POKEMON;
    if (blue && blue == lit) return PGP_LED_POKESTOP;
    if (shakes) {
        if (blue && green) return PGP_LED_CAUGHT;
        if (red) return PGP_LED_FLED;
        return PGP_LED_UNKNOWN;
    }
    if (red && green && blue && !off) return PGP_LED_SPUN;
    return PGP_LED_UNKNOWN;
}

bool pgp_led_needs_button(pgp_led_event_t event)
{
    /* Preserve legacy behavior for unknown valid patterns rather than silently
     * dropping a new game request. Log them for later classification. */
    return event == PGP_LED_POKEMON || event == PGP_LED_NEW_POKEMON ||
           event == PGP_LED_POKESTOP || event == PGP_LED_UNKNOWN;
}

const char *pgp_led_event_name(pgp_led_event_t event)
{
    static const char *names[] = {"invalid", "unknown", "off", "pokemon", "new_pokemon",
        "pokestop", "caught", "fled", "spun", "bag_full", "box_full", "balls_or_range"};
    return (unsigned)event < sizeof(names) / sizeof(names[0]) ? names[event] : "invalid";
}
