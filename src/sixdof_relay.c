/*
 * 6DOF mode relay — propagates 6DOF layer active state from central to peripheral.
 *
 * Central side: subscribes to layer_state_changed, writes 0x01/0x00 to the
 *               peripheral's GATT characteristic when CONFIG_ZMK_6DOF_LAYER toggles.
 * Peripheral side: hosts the GATT characteristic, calls sixdof_set_active()
 *                  on write.
 *
 * No DT bindings needed — single fixed characteristic per connection.
 */

#include <zephyr/types.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

#include <zmk/sixdof_mode.h>
#include <zmk/sixdof_relay_uuid.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ──────────────────────────────────────────────────────────────────────────
 * PERIPHERAL SIDE
 * ────────────────────────────────────────────────────────────────────────── */
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

static uint8_t sixdof_mode_value;

static ssize_t sixdof_relay_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  const void *buf, uint16_t len, uint16_t offset,
                                  uint8_t flags) {
    if (len < 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    const uint8_t active = ((const uint8_t *)buf)[0];
    sixdof_set_active(active != 0);
    LOG_DBG("6dof relay: mode=%d", active);
    return len;
}

BT_GATT_SERVICE_DEFINE(
    sixdof_relay_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(ZMK_6DOF_RELAY_SERVICE_UUID)),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(ZMK_6DOF_RELAY_CHAR_MODE_UUID),
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, sixdof_relay_write, &sixdof_mode_value),
);

#endif /* ZMK_SPLIT && !ZMK_SPLIT_ROLE_CENTRAL */

/* ──────────────────────────────────────────────────────────────────────────
 * CENTRAL / DONGLE SIDE
 * ────────────────────────────────────────────────────────────────────────── */
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#include <zmk/ble.h>
#include <zmk/events/layer_state_changed.h>

/*
 * Delay our GATT discovery to avoid racing with ZMK's own split service
 * discovery which runs immediately on connect. Zephyr only supports one
 * outstanding bt_gatt_discover() per connection at a time.
 */
#define RELAY_DISCOVER_DELAY_MS 5000

struct sixdof_relay_slot {
    struct bt_conn *conn;
    struct bt_gatt_discover_params discover_params;
    struct k_work_delayable discover_work;
    uint16_t char_handle;
};

static struct sixdof_relay_slot slots[ZMK_SPLIT_BLE_PERIPHERAL_COUNT];

/* ── GATT discovery ──────────────────────────────────────────────────────── */

static struct sixdof_relay_slot *slot_for_conn(struct bt_conn *conn) {
    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        if (slots[i].conn == conn) {
            return &slots[i];
        }
    }
    return NULL;
}

static uint8_t chrc_discovery_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  struct bt_gatt_discover_params *params) {
    if (!attr) {
        return BT_GATT_ITER_STOP;
    }

    struct sixdof_relay_slot *slot = slot_for_conn(conn);
    if (!slot) {
        return BT_GATT_ITER_STOP;
    }

    const struct bt_uuid *uuid = ((struct bt_gatt_chrc *)attr->user_data)->uuid;
    if (bt_uuid_cmp(uuid, BT_UUID_DECLARE_128(ZMK_6DOF_RELAY_CHAR_MODE_UUID)) == 0) {
        slot->char_handle = bt_gatt_attr_value_handle(attr);
        LOG_DBG("6dof relay: found char handle %u", slot->char_handle);
        return BT_GATT_ITER_STOP;
    }

    return BT_GATT_ITER_CONTINUE;
}

static const struct bt_uuid_128 sixdof_relay_svc_uuid =
    BT_UUID_INIT_128(ZMK_6DOF_RELAY_SERVICE_UUID);

static uint8_t svc_discovery_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  struct bt_gatt_discover_params *params) {
    if (!attr) {
        /* Service not found on this peripheral (e.g. keyboard halves) — OK */
        memset(params, 0, sizeof(*params));
        return BT_GATT_ITER_STOP;
    }

    struct sixdof_relay_slot *slot = slot_for_conn(conn);
    if (!slot) {
        return BT_GATT_ITER_STOP;
    }

    if (bt_uuid_cmp(params->uuid, &sixdof_relay_svc_uuid.uuid) != 0) {
        return BT_GATT_ITER_CONTINUE;
    }

    LOG_DBG("6dof relay: found service");
    slot->discover_params.uuid = NULL;
    slot->discover_params.func = chrc_discovery_cb;
    slot->discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

    int err = bt_gatt_discover(conn, &slot->discover_params);
    if (err) {
        LOG_ERR("6dof relay: chrc discover failed (%d)", err);
    }
    return BT_GATT_ITER_STOP;
}

/* ── Delayed discovery work handler ──────────────────────────────────────── */

static void relay_discover_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct sixdof_relay_slot *slot =
        CONTAINER_OF(dwork, struct sixdof_relay_slot, discover_work);

    if (!slot->conn) {
        return; /* disconnected before timer fired */
    }

    slot->discover_params.uuid = &sixdof_relay_svc_uuid.uuid;
    slot->discover_params.func = svc_discovery_cb;
    slot->discover_params.start_handle = 0x0001;
    slot->discover_params.end_handle = 0xffff;
    slot->discover_params.type = BT_GATT_DISCOVER_PRIMARY;

    int ret = bt_gatt_discover(slot->conn, &slot->discover_params);
    if (ret) {
        LOG_ERR("6dof relay: service discover failed (%d)", ret);
    }
}

/* ── Connection callbacks ────────────────────────────────────────────────── */

static void on_connected(struct bt_conn *conn, uint8_t err) {
    if (err) {
        return;
    }

    struct bt_conn_info info;
    bt_conn_get_info(conn, &info);
    if (info.role != BT_CONN_ROLE_CENTRAL) {
        return;
    }

    /* Find a free slot */
    struct sixdof_relay_slot *slot = NULL;
    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        if (slots[i].conn == NULL) {
            slot = &slots[i];
            break;
        }
    }
    if (!slot) {
        LOG_WRN("6dof relay: no free slot for connection");
        return;
    }

    slot->conn = conn;
    slot->char_handle = 0;

    /* Delay discovery to let ZMK's split service discovery complete first */
    k_work_schedule(&slot->discover_work, K_MSEC(RELAY_DISCOVER_DELAY_MS));
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason) {
    struct sixdof_relay_slot *slot = slot_for_conn(conn);
    if (slot) {
        k_work_cancel_delayable(&slot->discover_work);
        slot->conn = NULL;
        slot->char_handle = 0;
    }
}

static struct bt_conn_cb conn_callbacks = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};

/* ── Send mode to all peripherals ────────────────────────────────────────── */

static void send_sixdof_mode(bool active) {
    static uint8_t payload;
    payload = active ? 1 : 0;

    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        if (!slots[i].conn || !slots[i].char_handle) {
            continue;
        }

        int err = bt_gatt_write_without_response(slots[i].conn, slots[i].char_handle,
                                                  &payload, sizeof(payload), false);
        if (err) {
            LOG_ERR("6dof relay: write failed slot %d (%d)", i, err);
        } else {
            LOG_DBG("6dof relay: sent mode=%d to slot %d", active, i);
        }
    }
}

/* ── Layer event listener ────────────────────────────────────────────────── */

static int layer_state_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (!ev || ev->layer != CONFIG_ZMK_6DOF_LAYER) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    send_sixdof_mode(ev->state);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(sixdof_relay, layer_state_listener);
ZMK_SUBSCRIPTION(sixdof_relay, zmk_layer_state_changed);

/* ── Init ────────────────────────────────────────────────────────────────── */

static int sixdof_relay_central_init(void) {
    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT; i++) {
        k_work_init_delayable(&slots[i].discover_work, relay_discover_work_handler);
    }
    bt_conn_cb_register(&conn_callbacks);
    return 0;
}

SYS_INIT(sixdof_relay_central_init, APPLICATION, CONFIG_ZMK_BLE_INIT_PRIORITY);

#endif /* CONFIG_ZMK_SPLIT_ROLE_CENTRAL */
