/*
 * Magellan SpaceMouse serial emulator over CDC-ACM.
 *
 * Emulates a Magellan/SpaceMouse serial device so spacenavd can read 6DOF
 * rotation data via `serial = /dev/ttyACMx` in spnavrc.
 *
 * Protocol:
 *   - spacenavd opens at 9600 8n2 CTS/RTS (irrelevant for CDC-ACM)
 *   - Sends "vQ\r" → we respond with version string
 *   - Sends "m3\r", "c3B\r" → we ignore (already in 3D mode)
 *   - We send "d" + 24 nibble chars + "\r" for motion data
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define SIXDOF_SERIAL_NODE DT_NODELABEL(sixdof_cdc_acm)

#if !DT_NODE_EXISTS(SIXDOF_SERIAL_NODE)
#error "sixdof_cdc_acm node not found in DTS"
#endif

static const struct device *serial_dev = DEVICE_DT_GET(SIXDOF_SERIAL_NODE);

#define RX_BUF_SIZE 32
static char rx_buf[RX_BUF_SIZE];
static int rx_len;

static const char version_response[] = "vMAGELLAN  Version 6.60  3Dconnexion GmbH\r";

/* Encode a signed 16-bit value as 4 Magellan nibble chars */
static void encode_axis(int16_t value, char *out) {
    uint16_t raw = (uint16_t)(value + 0x8000);
    out[0] = ((raw >> 12) & 0x0f) + '0';
    out[1] = ((raw >> 8) & 0x0f) + '0';
    out[2] = ((raw >> 4) & 0x0f) + '0';
    out[3] = (raw & 0x0f) + '0';
}

void sixdof_serial_send(int16_t rx, int16_t ry, int16_t rz) {
    if (!device_is_ready(serial_dev)) {
        return;
    }

    char pkt[26]; /* 'd' + 24 nibbles + '\r' */
    pkt[0] = 'd';

    /* TX=0, TY=0, TZ=0 (no translation) */
    encode_axis(0, &pkt[1]);   /* TX */
    encode_axis(0, &pkt[5]);   /* TY */
    encode_axis(0, &pkt[9]);   /* TZ — spacenavd negates, so send as-is */

    /* RX, RY, RZ rotation */
    encode_axis(rx, &pkt[13]); /* RX */
    encode_axis(ry, &pkt[17]); /* RY */
    encode_axis(-rz, &pkt[21]); /* RZ — spacenavd negates idx 5, pre-negate */

    pkt[25] = '\r';

    for (int i = 0; i < 26; i++) {
        uart_poll_out(serial_dev, pkt[i]);
    }
}

static void process_rx_command(void) {
    if (rx_len < 1) return;

    /* Check for version query: "vQ" */
    if (rx_buf[0] == 'v' && rx_len >= 2 && rx_buf[1] == 'Q') {
        for (int i = 0; version_response[i]; i++) {
            uart_poll_out(serial_dev, version_response[i]);
        }
        LOG_DBG("6dof serial: version query responded");
    }
    /* "m3" = 3D mode, "c3B" = compress+extended keys — just ignore */
}

static void uart_rx_handler(const struct device *dev, void *user_data) {
    if (!uart_irq_update(dev)) return;

    while (uart_irq_rx_ready(dev)) {
        uint8_t c;
        int n = uart_fifo_read(dev, &c, 1);
        if (n <= 0) break;

        if (c == '\r') {
            rx_buf[rx_len] = '\0';
            process_rx_command();
            rx_len = 0;
        } else if (rx_len < RX_BUF_SIZE - 1) {
            rx_buf[rx_len++] = (char)c;
        }
    }
}

static int sixdof_serial_init(void) {
    if (!device_is_ready(serial_dev)) {
        LOG_ERR("6dof serial: CDC-ACM device not ready");
        return -ENODEV;
    }

    uart_irq_callback_set(serial_dev, uart_rx_handler);
    uart_irq_rx_enable(serial_dev);

    LOG_DBG("6dof serial: initialized");
    return 0;
}

SYS_INIT(sixdof_serial_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
