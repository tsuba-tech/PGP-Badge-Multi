#include "pgp-auto.h"

#include <string.h>

void pgp_auto_reset(pgp_auto_client_t *client, const pgp_auto_settings_t *settings)
{
    memset(client, 0, sizeof(*client));
    client->catch_enabled = settings->catch_enabled;
    client->spin_enabled = settings->spin_enabled;
    client->last_request = PGP_LED_INVALID;
}

static bool is_pokemon(pgp_led_event_t event)
{
    return event == PGP_LED_POKEMON || event == PGP_LED_NEW_POKEMON;
}

bool pgp_auto_on_led(pgp_auto_client_t *client, const pgp_auto_settings_t *settings,
                     pgp_led_event_t event)
{
    switch (event) {
    case PGP_LED_POKEMON:
    case PGP_LED_NEW_POKEMON:
        client->last_request = event;
        if (client->restock_remaining && !client->spin_enabled) {
            /* Restocking can never finish without spinning: resume catching. */
            client->restock_remaining = 0;
        }
        return client->catch_enabled && client->restock_remaining == 0;
    case PGP_LED_POKESTOP:
        client->last_request = event;
        return client->spin_enabled;
    case PGP_LED_UNKNOWN:
        /* Legacy fallback: never silently drop a new kind of request. */
        return client->catch_enabled || client->spin_enabled;
    case PGP_LED_CAUGHT:
    case PGP_LED_FLED:
        /* A throw happened, so there is room in the box and a ball was available. */
        client->alerts &= ~(PGP_ALERT_BOX_FULL | PGP_ALERT_NO_BALLS);
        return false;
    case PGP_LED_SPUN:
        client->alerts &= ~PGP_ALERT_BAG_FULL;
        if (client->restock_remaining && --client->restock_remaining == 0) {
            client->alerts &= ~PGP_ALERT_NO_BALLS;
        }
        return false;
    case PGP_LED_BOX_FULL:
        client->alerts |= PGP_ALERT_BOX_FULL;
        return false;
    case PGP_LED_BAG_FULL:
        client->alerts |= PGP_ALERT_BAG_FULL;
        /* Spins cannot add items any more, so waiting for them would pause
         * catching forever. Try catching again instead. */
        if (client->restock_remaining) {
            client->restock_remaining = 0;
            client->alerts &= ~PGP_ALERT_NO_BALLS;
        }
        return false;
    case PGP_LED_BALLS_OR_RANGE:
        if (is_pokemon(client->last_request)) {
            client->alerts |= PGP_ALERT_NO_BALLS;
            if (settings->restock_spins && client->spin_enabled) {
                client->restock_remaining = settings->restock_spins;
            }
        }
        /* After a Pokéstop request the same pattern means "out of range". */
        return false;
    case PGP_LED_OFF:
    case PGP_LED_INVALID:
    default:
        return false;
    }
}
