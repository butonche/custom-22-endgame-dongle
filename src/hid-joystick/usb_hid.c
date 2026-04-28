/* SPDX-License-Identifier: MIT — from badjeff/zmk-hid-joystick (c9da632) */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>

#include <zmk/usb.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/hid-joystick/hid.h>
#include <zmk/hid-joystick/usb_hid.h>

static const struct device *hid_dev;

static K_SEM_DEFINE(hid_sem, 1, 1);

static void in_ready_cb(const struct device *dev) { k_sem_give(&hid_sem); }

static int get_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                         uint8_t **data) {
    if ((setup->wValue & 0xff00) != 0x0100 /* INPUT */ &&
        (setup->wValue & 0xff00) != 0x0300 /* FEATURE */) {
        return -ENOTSUP;
    }

#if IS_ENABLED(CONFIG_ZMK_HID_JOYSTICK)
    if ((setup->wValue & 0xff) == ZMK_HID_REPORT_ID__JOYSTICK) {
        struct zmk_hid_joystick_report_alt *report = zmk_hid_get_joystick_report_alt();
        *data = (uint8_t *)report;
        *len = sizeof(*report);
        return 0;
    }
#endif
    return -EINVAL;
}

static const struct hid_ops ops = {
    .int_in_ready = in_ready_cb,
    .get_report = get_report_cb,
};

static int zmk_usb_hid_send_report_alt(const uint8_t *report, size_t len) {
    switch (zmk_usb_get_status()) {
    case USB_DC_SUSPEND:
        return usb_wakeup_request();
    case USB_DC_ERROR:
    case USB_DC_RESET:
    case USB_DC_DISCONNECTED:
    case USB_DC_UNKNOWN:
        return -ENODEV;
    default:
        k_sem_take(&hid_sem, K_MSEC(30));
        int err = hid_int_ep_write(hid_dev, report, len, NULL);
        if (err) {
            k_sem_give(&hid_sem);
        }
        return err;
    }
}

#if IS_ENABLED(CONFIG_ZMK_HID_JOYSTICK)
int zmk_usb_hid_send_joystick_report_alt() {
    struct zmk_hid_joystick_report_alt *report = zmk_hid_get_joystick_report_alt();
    return zmk_usb_hid_send_report_alt((uint8_t *)report, sizeof(*report));
}
#endif

static int zmk_usb_hid_init_alt(void) {
    hid_dev = device_get_binding("HID_" STRINGIFY(CONFIG_ZMK_HID_JOYSTICK_HID_BINDING_NUM));
    if (hid_dev == NULL) {
        LOG_ERR("6dof joystick: Unable to locate HID_%d device",
                CONFIG_ZMK_HID_JOYSTICK_HID_BINDING_NUM);
        return -EINVAL;
    }

    usb_hid_register_device(hid_dev, zmk_hid_report_desc_alt, sizeof(zmk_hid_report_desc_alt), &ops);
    usb_hid_init(hid_dev);

    return 0;
}

SYS_INIT(zmk_usb_hid_init_alt, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
