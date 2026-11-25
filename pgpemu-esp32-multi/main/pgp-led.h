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

/* Each pattern's first byte is its duration in 50 ms units (spezifisch/pgpemu).
 * Returns how long a button press for this request stays useful, clamped to
 * [PGP_LED_PRESS_MIN_MS, PGP_LED_PRESS_MAX_MS]. Invalid packets get the minimum. */
#define PGP_LED_DURATION_UNIT_MS 50U
#define PGP_LED_PRESS_MIN_MS 2000U
#define PGP_LED_PRESS_MAX_MS 10000U
uint32_t pgp_led_press_window_ms(const uint8_t *data, size_t length);

#endif
