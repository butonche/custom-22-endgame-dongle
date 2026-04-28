/*
 * Dongle-side 6DOF input processor.
 * Always in the input-listener processor chain. When 6DOF layer is active,
 * forwards X/Y/Z as RX/RY/RZ to the joystick HID endpoint and suppresses
 * normal mouse output. When inactive, passes events through unchanged.
 *
 * Rate-limits USB reports to avoid overwhelming the endpoint buffer.
 * Requires CONFIG_ZMK_HID_JOYSTICK=y (bundled joystick HID module).
 */

#define DT_DRV_COMPAT zmk_input_processor_6dof

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <drivers/input_processor.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/sixdof_mode.h>

#if IS_ENABLED(CONFIG_ZMK_HID_JOYSTICK)
#include <zmk/hid-joystick/endpoints.h>
#include <zmk/hid-joystick/hid.h>
#endif

/* Minimum interval between USB joystick reports (ms) */
#define SIXDOF_REPORT_INTERVAL_MS 16

struct sixdof_proc_data {
    int16_t rx, ry, rz;
    int64_t last_report_time;
};

static int sixdof_handle_event(const struct device *dev, struct input_event *event,
                               uint32_t param1, uint32_t param2,
                               struct zmk_input_processor_state *state) {
    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    /* Only intercept when 6DOF mode is active (set by relay layer_state_listener) */
    if (!sixdof_is_active()) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    struct sixdof_proc_data *data = dev->data;

    switch (event->code) {
    case INPUT_REL_X:
        data->rx += event->value;
        break;
    case INPUT_REL_Y:
        data->ry += event->value;
        break;
    case INPUT_REL_Z:
        data->rz += event->value;
        break;
    default:
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (event->sync) {
#if IS_ENABLED(CONFIG_ZMK_HID_JOYSTICK)
        int64_t now = k_uptime_get();
        if ((data->rx != 0 || data->ry != 0 || data->rz != 0) &&
            (now - data->last_report_time >= SIXDOF_REPORT_INTERVAL_MS)) {
            LOG_DBG("6dof_proc: rx=%d ry=%d rz=%d", data->rx, data->ry, data->rz);
            zmk_hid_joy2_movement_set(0, 0, 0, data->rx, data->ry, data->rz);
            int err = zmk_endpoints_send_joystick_report_alt();
            if (err) {
                LOG_WRN("6dof_proc: send failed (%d)", err);
            }
            zmk_hid_joy2_movement_set(0, 0, 0, 0, 0, 0);
            data->last_report_time = now;
            data->rx = 0;
            data->ry = 0;
            data->rz = 0;
        }
#else
        data->rx = 0;
        data->ry = 0;
        data->rz = 0;
#endif
    }

    /* Suppress normal mouse output */
    event->value = 0;
    event->sync = false;

    return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api sixdof_api = {
    .handle_event = sixdof_handle_event,
};

static int sixdof_proc_init(const struct device *dev) {
    return 0;
}

#define SIXDOF_PROC_INST(n)                                                                    \
    static struct sixdof_proc_data sixdof_data_##n = {};                                       \
    DEVICE_DT_INST_DEFINE(n, sixdof_proc_init, NULL, &sixdof_data_##n, NULL, POST_KERNEL,      \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &sixdof_api);

DT_INST_FOREACH_STATUS_OKAY(SIXDOF_PROC_INST)
