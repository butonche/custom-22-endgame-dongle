/* SPDX-License-Identifier: MIT — from badjeff/zmk-hid-joystick (c9da632) */

#include <zephyr/device.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/hid-joystick/hid.h>
#include <zmk/hid-joystick/hid_joystick.h>

#if IS_ENABLED(CONFIG_ZMK_HID_JOYSTICK)

static struct zmk_hid_joystick_report_alt joystick_report_alt = {
    .report_id = ZMK_HID_REPORT_ID__JOYSTICK,
    .body = {0}};

void zmk_hid_joy2_movement_set(int16_t x, int16_t y, int16_t z, int16_t rx, int16_t ry, int16_t rz) {
    joystick_report_alt.body.d_x =  (int8_t)CLAMP(x , -127, 127);
    joystick_report_alt.body.d_y =  (int8_t)CLAMP(y , -127, 127);
    joystick_report_alt.body.d_z =  (int8_t)CLAMP(z , -127, 127);
    joystick_report_alt.body.d_rx = (int8_t)CLAMP(rx, -127, 127);
    joystick_report_alt.body.d_ry = (int8_t)CLAMP(ry, -127, 127);
    joystick_report_alt.body.d_rz = (int8_t)CLAMP(rz, -127, 127);
}

void zmk_hid_joy2_movement_update(int16_t x, int16_t y, int16_t z, int16_t rx, int16_t ry, int16_t rz) {
    joystick_report_alt.body.d_x =  (int8_t)CLAMP(joystick_report_alt.body.d_x  + x,  -127, 127);
    joystick_report_alt.body.d_y =  (int8_t)CLAMP(joystick_report_alt.body.d_y  + y,  -127, 127);
    joystick_report_alt.body.d_z =  (int8_t)CLAMP(joystick_report_alt.body.d_z  + z,  -127, 127);
    joystick_report_alt.body.d_rx = (int8_t)CLAMP(joystick_report_alt.body.d_rx + rx, -127, 127);
    joystick_report_alt.body.d_ry = (int8_t)CLAMP(joystick_report_alt.body.d_ry + ry, -127, 127);
    joystick_report_alt.body.d_rz = (int8_t)CLAMP(joystick_report_alt.body.d_rz + rz, -127, 127);
}

int zmk_hid_joy2_button_press(zmk_joystick_button_t button) {
    if (button >= ZMK_HID_JOYSTICK_NUM_BUTTONS) return -EINVAL;
    WRITE_BIT(joystick_report_alt.body.buttons, button, true);
    return 0;
}

int zmk_hid_joy2_button_release(zmk_joystick_button_t button) {
    if (button >= ZMK_HID_JOYSTICK_NUM_BUTTONS) return -EINVAL;
    WRITE_BIT(joystick_report_alt.body.buttons, button, false);
    return 0;
}

int zmk_hid_joy2_buttons_press(zmk_joystick_button_flags_t buttons) {
    for (zmk_joystick_button_t i = 0; i < ZMK_HID_JOYSTICK_NUM_BUTTONS; i++) {
        if (buttons & BIT(i)) zmk_hid_joy2_button_press(i);
    }
    return 0;
}

int zmk_hid_joy2_buttons_release(zmk_joystick_button_flags_t buttons) {
    for (zmk_joystick_button_t i = 0; i < ZMK_HID_JOYSTICK_NUM_BUTTONS; i++) {
        if (buttons & BIT(i)) zmk_hid_joy2_button_release(i);
    }
    return 0;
}

void zmk_hid_joy2_clear(void) {
    memset(&joystick_report_alt.body, 0, sizeof(joystick_report_alt.body));
}

struct zmk_hid_joystick_report_alt *zmk_hid_get_joystick_report_alt(void) {
    return &joystick_report_alt;
}

#endif
