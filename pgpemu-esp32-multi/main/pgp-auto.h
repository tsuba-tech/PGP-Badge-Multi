#ifndef PGP_AUTO_H
#define PGP_AUTO_H

#include <stdbool.h>
#include <stdint.h>
#include "pgp-led.h"

/* Automation policy: which LED requests get a button press. Pure logic without
 * FreeRTOS; callers hold their own lock around every call. */

#define PGP_AUTO_RESTOCK_DEFAULT 10
#define PGP_AUTO_RESTOCK_MAX 99

/* Status shown on the GPIO2 LED; cleared by the matching success or disconnect. */
enum {
    PGP_ALERT_BOX_FULL = 1 << 0,
    PGP_ALERT_BAG_FULL = 1 << 1,
    PGP_ALERT_NO_BALLS = 1 << 2,
};

/* When to press after the app asks (randomized like other open-source catchers). */
typedef enum {
    PGP_PRESS_FAST,     /* 0.3-0.8 s, random hold 250-500 ms (default) */
    PGP_PRESS_NORMAL,   /* 1.0-2.5 s, random hold 250-500 ms */
    PGP_PRESS_INSTANT,  /* immediately, fixed 500 ms hold (original firmware) */
    PGP_PRESS_MODE_COUNT
} pgp_press_mode_t;

typedef struct {
    bool catch_enabled;
    bool spin_enabled;
    uint8_t press_mode;
    /* Pokéstop spins required after running out of balls before catching
     * resumes. 0 disables the pause. */
    uint8_t restock_spins;
} pgp_auto_settings_t;

typedef struct {
    bool catch_enabled;
    bool spin_enabled;
    /* Last request the app showed: tells "out of balls" from "Pokéstop out of
     * range", which share the blinking-red pattern. */
    pgp_led_event_t last_request;
    uint8_t restock_remaining;
    uint8_t alerts;
} pgp_auto_client_t;

/* A new connection starts from the saved defaults. */
void pgp_auto_reset(pgp_auto_client_t *client, const pgp_auto_settings_t *settings);

/* Updates the client's state for one LED pattern and returns true when the
 * button should be pressed. */
bool pgp_auto_on_led(pgp_auto_client_t *client, const pgp_auto_settings_t *settings,
                     pgp_led_event_t event);

#endif
