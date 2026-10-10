#ifndef PGP_LED_H
#define PGP_LED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PGP_LED_INVALID, PGP_LED_UNKNOWN, PGP_LED_OFF,
    PGP_LED_POKEMON, PGP_LED_NEW_POKEMON, PGP_LED_POKESTOP,
    PGP_LED_CAUGHT, PGP_LED_FLED, PGP_LED_SPUN,
    PGP_LED_BAG_FULL, PGP_LED_BOX_FULL, PGP_LED_BALLS_OR_RANGE
} pgp_led_event_t;

pgp_led_event_t pgp_led_classify(const uint8_t *data, size_t length);
bool pgp_led_needs_button(pgp_led_event_t event);
const char *pgp_led_event_name(pgp_led_event_t event);

#endif
