#include <stdbool.h>
#include <zmk/sixdof_mode.h>

/*
 * On split peripherals, keymap.c is not compiled so zmk_keymap_layer_active()
 * is unavailable. Use the flag set via the GATT relay instead.
 * On central/dongle and native_sim test builds, query live layer state directly —
 * the relay is not available in test builds (no BT).
 */
#ifndef CONFIG_ZMK_SPLIT_BLE_PERIPHERAL
#include <zmk/keymap.h>
#endif

static bool g_sixdof_active = false;

void sixdof_set_active(bool active)
{
    g_sixdof_active = active;
}

bool sixdof_is_active(void)
{
#ifdef CONFIG_ZMK_SPLIT_BLE_PERIPHERAL
    return g_sixdof_active;
#else
    return zmk_keymap_layer_active(CONFIG_ZMK_6DOF_LAYER);
#endif
}
