#include <stdbool.h>
#include <zmk/sixdof_mode.h>

/*
 * On split peripherals (ZMK_SPLIT && !ZMK_SPLIT_ROLE_CENTRAL), keymap.c is not
 * compiled so zmk_keymap_layer_active() is unavailable. Use the flag set via
 * the GATT relay instead.
 * On central/dongle and native_sim test builds, query live layer state directly.
 */
#if !defined(CONFIG_ZMK_SPLIT) || defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/keymap.h>
#endif

static bool g_sixdof_active = false;

void sixdof_set_active(bool active)
{
    g_sixdof_active = active;
}

bool sixdof_is_active(void)
{
#if defined(CONFIG_ZMK_SPLIT) && !defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    return g_sixdof_active;
#else
    return zmk_keymap_layer_active(CONFIG_ZMK_6DOF_LAYER);
#endif
}
