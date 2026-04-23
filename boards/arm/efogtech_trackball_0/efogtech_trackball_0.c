#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/devicetree.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/settings/settings.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <zephyr/input/input.h>

#include "zephyr/bluetooth/bluetooth.h"
#include "zmk/endpoints.h"
#include "zmk/settings.h"

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <drivers/input_processor.h>
#include <dt-bindings/zmk/p2sm.h>
#endif

#define DT_DRV_COMPAT zmk_endgame
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_SHELL)
#define shprint(_sh, _fmt, ...) \
do { \
    if ((_sh) != NULL) \
        shell_print((_sh), _fmt, ##__VA_ARGS__); \
} while (0)

static int cmd_version(const struct shell *sh, const size_t argc, char **argv) {
    shprint(sh, "Firmware version: %d.%d.%d", CONFIG_BOARD_EFOGTECH_0_VER_MAJOR, CONFIG_BOARD_EFOGTECH_0_VER_MINOR, CONFIG_BOARD_EFOGTECH_0_VER_PATCH);
    return 0;
}

static int cmd_output(const struct shell *sh, const size_t argc, char **argv) {
    if (argc < 1) {
        shprint(sh, "Usage: board output [usb|ble]");
        return -EINVAL;
    }

    if (strcmp(argv[1], "usb") == 0) {
        zmk_endpoints_select_transport(ZMK_TRANSPORT_USB);
    } else if (strcmp(argv[1], "ble") == 0) {
        zmk_endpoints_select_transport(ZMK_TRANSPORT_BLE);
    } else {
        if (zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB) {
            shprint(sh, "Output: USB");
        } else {
            shprint(sh, "Output: BLE");
        }

        return 0;
    }

    shprint(sh, "Done.");
    return 0;
}

static int cmd_reboot(const struct shell *sh, const size_t argc, char **argv) {
    shprint(sh, "Rebooting device...");
    k_sleep(K_MSEC(100));
    sys_reboot(SYS_REBOOT_COLD);
    return 0;
}

static int cmd_erase(const struct shell *sh, const size_t argc, char **argv) {
    shprint(sh, "I hope you know what you're doing.");

    bt_unpair(BT_ID_DEFAULT, NULL);

    for (int i = 0; i < 8; i++) {
        char setting_name[15];
        snprintf(setting_name, sizeof(setting_name), "ble/profiles/%d", i);

        const int err = settings_delete(setting_name);
        if (err) {
            LOG_ERR("Failed to delete setting: %d", err);
        }
    }

    for (int i = 0; i < 8; i++) {
        char setting_name[32];
        snprintf(setting_name, sizeof(setting_name), "ble/peripheral_addresses/%d", i);

        const int err = settings_delete(setting_name);
        if (err) {
            LOG_ERR("Failed to delete setting: %d", err);
        }
    }

    return zmk_settings_erase();
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_board,
    SHELL_CMD(output, NULL, "Get or set output channel (USB/BLE)", cmd_output),
    SHELL_CMD(reboot, NULL, "Reboot the device", cmd_reboot),
    SHELL_CMD(erase, NULL, "Erase all settings", cmd_erase),
    SHELL_CMD(version, NULL, "Read firmware version", cmd_version),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(board, &sub_board, "Control the device", NULL);
#endif

static const struct device *p0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static const struct device *p1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
static const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(uart0));
#endif

static void set_3v3_en(const bool en) {
    gpio_pin_configure(p1, 0, GPIO_OUTPUT);
    gpio_pin_set(p1, 0, en);
}

static void set_rgb_en(const bool en) {
    gpio_pin_configure(p1, 3, GPIO_OUTPUT);
    gpio_pin_set(p1, 3, en);
}

static void set_bl_en(const bool en) {
    gpio_pin_configure(p0, 20, GPIO_OUTPUT);
    gpio_pin_set(p0, 20, en);
}

static int pinmux_efgtch_trckbl_init(void) {
    set_3v3_en(false);
    set_rgb_en(false);
    set_bl_en(false);
    return 0;
}

SYS_INIT(pinmux_efgtch_trckbl_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
static int usb_conn_chg(const zmk_event_t *eh) {
    if (zmk_usb_get_conn_state() == ZMK_USB_CONN_HID) {
        pm_device_action_run(uart, PM_DEVICE_ACTION_TURN_ON);
        pm_device_action_run(uart, PM_DEVICE_ACTION_RESUME);
    } else {
        pm_device_action_run(uart, PM_DEVICE_ACTION_SUSPEND);
        pm_device_action_run(uart, PM_DEVICE_ACTION_TURN_OFF);
    }

    return 0;
}

ZMK_SUBSCRIPTION(board_root, zmk_usb_conn_state_changed);
ZMK_LISTENER(board_root, usb_conn_chg)
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

/*
 * Sensor-to-mixer bridge for split peripheral builds.
 *
 * On non-split (standalone) builds, zmk,input-listener nodes feed PAW3395
 * events into the mixer via input-processors. But input-listeners are
 * compiled out on split peripherals (ZMK_SPLIT && !ZMK_SPLIT_ROLE_CENTRAL).
 *
 * This bridge registers raw Zephyr input callbacks for both sensors and
 * calls the mixer's input-processor handle_event API directly, restoring
 * the data path: PAW3395 → mixer → input-split → BLE.
 */
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) && \
    DT_NODE_HAS_STATUS(DT_NODELABEL(trackball_primary), okay) && \
    DT_NODE_HAS_STATUS(DT_NODELABEL(trackball_secondary), okay)

static const struct device *mixer = DEVICE_DT_GET(DT_NODELABEL(zip_2s_mixer));

static void sensor1_cb(struct input_event *evt) {
    struct zmk_input_processor_state state = {0};
    zmk_input_processor_handle_event(mixer, evt, INPUT_MIXER_SENSOR1, 0, &state);
}

static void sensor2_cb(struct input_event *evt) {
    struct zmk_input_processor_state state = {0};
    zmk_input_processor_handle_event(mixer, evt, INPUT_MIXER_SENSOR2, 0, &state);
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(trackball_primary)), sensor1_cb);
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(trackball_secondary)), sensor2_cb);

#endif
