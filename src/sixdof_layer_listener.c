/*
 * Sets sixdof_is_active() flag from layer_state_changed events.
 * Only compiled on builds without the relay (tests, standalone trackball).
 * On relay builds, the relay's layer_state_listener handles this.
 */

#include <zmk/sixdof_mode.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>

static int sixdof_layer_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev && ev->layer == CONFIG_ZMK_6DOF_LAYER) {
        sixdof_set_active(ev->state);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(sixdof_layer, sixdof_layer_listener);
ZMK_SUBSCRIPTION(sixdof_layer, zmk_layer_state_changed);
