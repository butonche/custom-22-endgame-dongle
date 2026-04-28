/* SPDX-License-Identifier: MIT — from badjeff/zmk-hid-joystick (c9da632) */

#pragma once

#include <zmk/endpoints.h>

#if IS_ENABLED(CONFIG_ZMK_HID_JOYSTICK)
int zmk_endpoints_send_joystick_report_alt();
#endif
