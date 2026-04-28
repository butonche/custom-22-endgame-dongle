/* SPDX-License-Identifier: MIT — from badjeff/zmk-hid-joystick (c9da632) */

#pragma once

#include <stdint.h>

#if IS_ENABLED(CONFIG_ZMK_HID_JOYSTICK)
int zmk_usb_hid_send_joystick_report_alt(void);
#endif
