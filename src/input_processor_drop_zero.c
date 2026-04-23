/*
 * Smart zero-value input event filter for the trackball input-split chain.
 *
 * After the acceleration curves dampen mixer output, many events have value=0.
 * Forwarding all zeros over BLE wastes bandwidth and can cause the dongle's
 * input-listener to fire empty HID reports.
 *
 * Rules:
 *   1. Non-zero events: always pass through
 *   2. Zero-value scroll (WHEEL/HWHEEL): always drop — the mixer only emits
 *      non-zero scroll, so zeros come from curve dampening and are noise
 *   3. Zero-value pointer with sync=true: pass if the preceding X was non-zero
 *      (preserves the X+Y atomic pair); drop if both X and Y are zero
 *   4. Zero-value pointer with sync=false: drop (the subsequent sync=true Y
 *      event will still trigger the HID report on the dongle)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <drivers/input_processor.h>

struct drop_zero_data {
    bool pending_nonzero;
};

static int drop_zero_handle_event(const struct device *dev,
                                  struct input_event *event,
                                  uint32_t param1, uint32_t param2,
                                  struct zmk_input_processor_state *state) {
    struct drop_zero_data *data = dev->data;

    /* Non-zero events always pass through */
    if (event->value != 0) {
        if (!event->sync) {
            data->pending_nonzero = true;
        } else {
            data->pending_nonzero = false;
        }
        return ZMK_INPUT_PROC_CONTINUE;
    }

    /* Zero-value scroll events: always drop */
    if (event->code == INPUT_REL_WHEEL || event->code == INPUT_REL_HWHEEL) {
        return ZMK_INPUT_PROC_STOP;
    }

    /* Zero-value pointer with sync=true (Y in the X+Y pair):
     * Pass if the preceding X was non-zero — we need the sync to
     * trigger the HID report on the dongle. Drop if both are zero. */
    if (event->sync) {
        bool pass = data->pending_nonzero;
        data->pending_nonzero = false;
        return pass ? ZMK_INPUT_PROC_CONTINUE : ZMK_INPUT_PROC_STOP;
    }

    /* Zero-value pointer with sync=false (X in the pair): drop */
    data->pending_nonzero = false;
    return ZMK_INPUT_PROC_STOP;
                                  }

                                  static struct zmk_input_processor_driver_api drop_zero_api = {
                                      .handle_event = drop_zero_handle_event,
                                  };

                                  #define DT_DRV_COMPAT zmk_input_processor_drop_zero

                                  #define DZ_INST(n) \
                                  static struct drop_zero_data drop_zero_data_##n = { .pending_nonzero = false }; \
                                  DEVICE_DT_INST_DEFINE(n, NULL, NULL, &drop_zero_data_##n, NULL, \
                                  POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, \
                                  &drop_zero_api);

                                  DT_INST_FOREACH_STATUS_OKAY(DZ_INST)
