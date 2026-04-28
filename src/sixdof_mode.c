#include <stdbool.h>
#include <zmk/sixdof_mode.h>

#ifndef CONFIG_ZMK_6DOF_RELAY
#include <zmk/keymap.h>
#endif

static bool g_sixdof_active = false;

void sixdof_set_active(bool active)
{
    g_sixdof_active = active;
}

bool sixdof_is_active(void)
{
#ifdef CONFIG_ZMK_6DOF_RELAY
    /* Flag set by relay's layer_state_listener (central) or GATT write (peripheral).
     * zmk_keymap_layer_active() is unreliable from external ZMK modules. */
    return g_sixdof_active;
#else
    /* Test builds without relay: query keymap directly */
    return zmk_keymap_layer_active(CONFIG_ZMK_6DOF_LAYER);
#endif
}
