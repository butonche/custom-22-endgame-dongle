/*
 * Scroll scaler with internal remainder tracking.
 * Unlike the stock ZMK scaler, this works in input-split where state is NULL.
 * Remainders are tracked per-instance so fractional values accumulate
 * correctly across events.
 */

#define DT_DRV_COMPAT zmk_scroll_scaler

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <drivers/input_processor.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct scroll_scaler_data {
    int16_t remainder_wheel;
    int16_t remainder_hwheel;
};

static int scroll_scaler_handle_event(const struct device *dev, struct input_event *event,
                                      uint32_t param1, uint32_t param2,
                                      struct zmk_input_processor_state *state) {
    struct scroll_scaler_data *data = dev->data;

    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    int16_t *remainder;
    if (event->code == INPUT_REL_WHEEL) {
        remainder = &data->remainder_wheel;
    } else if (event->code == INPUT_REL_HWHEEL) {
        remainder = &data->remainder_hwheel;
    } else {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    int16_t mul = (int16_t)param1;
    int16_t div = (int16_t)param2;
    int16_t value_mul = event->value * mul + *remainder;
    int16_t scaled = value_mul / div;
    *remainder = value_mul - (scaled * div);

    LOG_DBG("scroll_scale: %d * %d/%d = %d (rem %d)", event->value, mul, div, scaled, *remainder);

    event->value = scaled;
    return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api scroll_scaler_api = {
    .handle_event = scroll_scaler_handle_event,
};

#define SCROLL_SCALER_INST(n)                                                                      \
    static struct scroll_scaler_data scroll_scaler_data_##n = {0};                                 \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &scroll_scaler_data_##n, NULL, POST_KERNEL,               \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &scroll_scaler_api);

DT_INST_FOREACH_STATUS_OKAY(SCROLL_SCALER_INST)
