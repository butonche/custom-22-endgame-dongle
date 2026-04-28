/* SPDX-License-Identifier: MIT — from badjeff/zmk-hid-joystick (c9da632), USB-only */

#include <zmk/endpoints.h>
#include <zmk/usb.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/hid-joystick/endpoints.h>
#include <zmk/hid-joystick/hid.h>
#include <zmk/hid-joystick/usb_hid.h>

#if IS_ENABLED(CONFIG_ZMK_HID_JOYSTICK)
int zmk_endpoints_send_joystick_report_alt() {
    struct zmk_endpoint_instance current_instance = zmk_endpoints_selected();

    switch (current_instance.transport) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    case ZMK_TRANSPORT_USB: {
        int err = zmk_usb_hid_send_joystick_report_alt();
        if (err) {
            LOG_ERR("6dof joystick: USB send failed (%d)", err);
        }
        return err;
    }
#endif
    default:
        break;
    }
    return -ENOTSUP;
}
#endif
