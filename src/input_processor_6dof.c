/*
 * Dongle-side 6DOF input processor.
 * When active (layer 5 engaged), remaps decompressed X/Y/Z events to RX/RY/RZ.
 * The peripheral already computed omega values and put them on X/Y/Z channels.
 */

#define DT_DRV_COMPAT zmk_input_processor_6dof

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <drivers/input_processor.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static int sixdof_handle_event(const struct device *dev, struct input_event *event,
                               uint32_t param1, uint32_t param2,
                               struct zmk_input_processor_state *state) {
    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    switch (event->code) {
    case INPUT_REL_X:
        event->code = INPUT_REL_RX;
        LOG_DBG("6dof_proc: x=%d -> RX", event->value);
        break;
    case INPUT_REL_Y:
        event->code = INPUT_REL_RY;
        LOG_DBG("6dof_proc: y=%d -> RY", event->value);
        break;
    case INPUT_REL_Z:
        event->code = INPUT_REL_RZ;
        LOG_DBG("6dof_proc: z=%d -> RZ", event->value);
        break;
    default:
        break;
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api sixdof_api = {
    .handle_event = sixdof_handle_event,
};

#define SIXDOF_PROC_INST(n)                                                                        \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                  \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &sixdof_api);

DT_INST_FOREACH_STATUS_OKAY(SIXDOF_PROC_INST)
