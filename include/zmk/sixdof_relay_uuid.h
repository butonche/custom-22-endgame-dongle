#pragma once

#include <zephyr/bluetooth/uuid.h>

/* 6DOF mode relay service — custom 128-bit UUIDs */
#define ZMK_6DOF_RELAY_UUID(num) BT_UUID_128_ENCODE(num, 0x6d6f, 0x6433, 0x6600, 0xc2482c000000)

#define ZMK_6DOF_RELAY_SERVICE_UUID     ZMK_6DOF_RELAY_UUID(0x00000000)
#define ZMK_6DOF_RELAY_CHAR_MODE_UUID   ZMK_6DOF_RELAY_UUID(0x00000001)
