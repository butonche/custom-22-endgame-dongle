/*
 * Dongle-side 6DOF input processor.
 * Always in the input-listener processor chain. When 6DOF mode is active,
 * accumulates rotation events and sends them as Magellan serial packets.
 * When inactive, passes events through unchanged for normal mouse operation.
 */

#define DT_DRV_COMPAT zmk_input_processor_6dof

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <drivers/input_processor.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/sixdof_mode.h>

void sixdof_serial_send(int16_t rx, int16_t ry, int16_t rz);

/* Minimum interval between serial reports (ms) */
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

    if (!sixdof_is_active()) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    struct sixdof_proc_data *data = dev->data;

    switch (event->code) {
    case INPUT_REL_X:
    case INPUT_REL_RX:
        data->rx += event->value;
        break;
    case INPUT_REL_Y:
    case INPUT_REL_RY:
        data->ry += event->value;
        break;
    case INPUT_REL_Z:
    case INPUT_REL_RZ:
        data->rz += event->value;
        break;
    default:
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (event->sync) {
        int64_t now = k_uptime_get();
        if ((data->rx != 0 || data->ry != 0 || data->rz != 0) &&
            (now - data->last_report_time >= SIXDOF_REPORT_INTERVAL_MS)) {
            sixdof_serial_send(data->rx, data->ry, data->rz);
            data->last_report_time = now;
            data->rx = 0;
            data->ry = 0;
            data->rz = 0;
        }
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
