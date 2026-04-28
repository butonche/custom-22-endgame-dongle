/*
 * Mock input sensor for native_sim tests.
 * Emits a DTS-specified sequence of REL_X/REL_Y event pairs at boot.
 * Two instances mirror the dual PAW3395 setup used by the 2-sensor mixer.
 */

#define DT_DRV_COMPAT zmk_input_mock_sensor

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(input_mock_sensor, CONFIG_INPUT_LOG_LEVEL);

struct mock_sensor_config {
    const int32_t *events; /* flat: dx0 dy0 delay0 dx1 dy1 delay1 ... */
    size_t num_events;     /* number of triplets */
};

struct mock_sensor_data {
    const struct device *dev;
    struct k_work_delayable work;
    size_t idx; /* index of next triplet to fire */
};

static void mock_work_cb(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct mock_sensor_data *data =
        CONTAINER_OF(dwork, struct mock_sensor_data, work);
    const struct mock_sensor_config *cfg = data->dev->config;

    if (data->idx >= cfg->num_events) {
        return;
    }

    size_t base = data->idx * 3;
    int32_t dx       = cfg->events[base + 0];
    int32_t dy       = cfg->events[base + 1];
    /* delay already consumed when this work item was scheduled */

    LOG_DBG("mock_sensor %s: dx=%d dy=%d", data->dev->name, dx, dy);

    input_report_rel(data->dev, INPUT_REL_X, dx, false, K_FOREVER);
    input_report_rel(data->dev, INPUT_REL_Y, dy, true,  K_FOREVER);

    data->idx++;

    if (data->idx < cfg->num_events) {
        uint32_t next_delay = (uint32_t)cfg->events[data->idx * 3 + 2];
        k_work_schedule(&data->work, K_MSEC(next_delay));
    }
}

static int mock_sensor_init(const struct device *dev)
{
    struct mock_sensor_data *data = dev->data;
    const struct mock_sensor_config *cfg = dev->config;

    data->dev = dev;
    data->idx = 0;
    k_work_init_delayable(&data->work, mock_work_cb);

    if (cfg->num_events > 0) {
        uint32_t first_delay = (uint32_t)cfg->events[2]; /* delay of triplet 0 */
        k_work_schedule(&data->work, K_MSEC(first_delay));
    }
    return 0;
}

/*
 * DT_INST_PROP(n, events) expands to a brace-enclosed initialiser list.
 * We store it as a flat int32_t array and compute the triplet count at
 * compile time from the array size — no LISTIFY division needed.
 */
#define MOCK_SENSOR_DEFINE(n)                                                   \
    static const int32_t mock_events_##n[] = DT_INST_PROP(n, events);          \
    static struct mock_sensor_data mock_data_##n;                               \
    static const struct mock_sensor_config mock_cfg_##n = {                     \
        .events     = mock_events_##n,                                          \
        .num_events = ARRAY_SIZE(mock_events_##n) / 3,                          \
    };                                                                          \
    DEVICE_DT_INST_DEFINE(n, mock_sensor_init, NULL,                            \
        &mock_data_##n, &mock_cfg_##n,                                          \
        POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(MOCK_SENSOR_DEFINE)
