#include <stdbool.h>
#include <zmk/sixdof_mode.h>

static bool g_sixdof_active = false;

void sixdof_set_active(bool active)
{
    g_sixdof_active = active;
}

bool sixdof_is_active(void)
{
    return g_sixdof_active;
}

/*
 * On builds with CONFIG_ZMK_6DOF_RELAY, the relay's layer_state_listener sets the flag.
 * On other builds with a keymap (tests, standalone), subscribe to layer_state_changed.
 * Settings_reset builds have neither — the flag stays false, which is fine.
 */
#if !defined(CONFIG_ZMK_6DOF_RELAY) && defined(CONFIG_ZMK_6DOF_LAYER)
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>

static int sixdof_mode_layer_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev && ev->layer == CONFIG_ZMK_6DOF_LAYER) {
        sixdof_set_active(ev->state);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(sixdof_mode, sixdof_mode_layer_listener);
ZMK_SUBSCRIPTION(sixdof_mode, zmk_layer_state_changed);
#endif
