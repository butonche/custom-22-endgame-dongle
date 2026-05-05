/*
 * Stripped acceleration curve input processor.
 * Based on efogdev/zmk-acceleration-curves (0a00ade / v1.0.3).
 *
 * Removed: NVS persistence, shell/debug dump, deferred work, dynamic
 *          allocation, runtime config import, monitor mode.
 * Added:   Static arrays, DTS "curve-data" property parsed at init,
 *          axes coupling for consistent diagonal speed.
 */

#include <math.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/input/input.h>
#include <drivers/input_processor.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT zmk_accel_curve
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ------------------------------------------------------------------ */
/*  Data structures                                                    */
/* ------------------------------------------------------------------ */

struct point {
    int16_t x;
    int16_t y;
};

struct accel_point {
    int16_t x;
    float   y_coef;
};

struct curve {
    struct point start, end, cp1, cp2;
};

struct zip_accel_curve_config {
    uint8_t         max_curves;
    uint8_t         points;
    uint8_t         event_codes_len;
    uint8_t         curve_data_len;     /* number of int16 values in curve-data */
    bool            couple_axes;        /* apply accel to combined magnitude    */
    const int16_t  *curve_data;         /* pointer to DTS curve-data array      */
    const uint16_t  event_codes[];
};

struct zip_accel_curve_data {
    const struct device *dev;
    bool                 initialized;
    uint8_t              num_curves;
    struct curve        *curves;        /* points into static storage */
    struct accel_point  *points;        /* points into static storage */
    float               *remainders;    /* points into static storage */
    /* Axes coupling buffers */
    int32_t             *buffered_values;
    bool                *buffered_present;
    bool                *inject_pass;
};

/* ------------------------------------------------------------------ */
/*  Bezier evaluation                                                  */
/* ------------------------------------------------------------------ */

static int16_t bezier_eval(const int16_t p0, const int16_t p1,
                           const int16_t p2, const int16_t p3,
                           const float t) {
    const float u   = 1.0f - t;
    const float tt  = t * t;
    const float uu  = u * u;
    const float uuu = uu * u;
    const float ttt = tt * t;
    return (int16_t)(uuu * p0 + 3 * uu * t * p1 + 3 * u * tt * p2 + ttt * p3);
}

/* ------------------------------------------------------------------ */
/*  Build lookup table from parsed Bezier segments                     */
/* ------------------------------------------------------------------ */

static int set_curves(const struct device *dev, uint8_t curve_count) {
    const struct zip_accel_curve_data   *data   = dev->data;
    const struct zip_accel_curve_config *config  = dev->config;

    if (curve_count < 1 || curve_count > config->max_curves) {
        LOG_ERR("Invalid number of curves: %d", curve_count);
        return -EINVAL;
    }

    /* Continuity check */
    for (uint8_t i = 1; i < curve_count; i++) {
        if (data->curves[i].start.x != data->curves[i - 1].end.x ||
            data->curves[i].start.y != data->curves[i - 1].end.y) {
            LOG_ERR("Curves are not continuous at index %d", i);
            return -EINVAL;
        }
    }

    const uint32_t points_per_curve =
        curve_count > 0 ? config->points / curve_count : 0;
    uint32_t point_idx = 0;

    for (uint8_t ci = 0; ci < curve_count && point_idx < config->points; ci++) {
        const struct curve *c = &data->curves[ci];
        const uint32_t num_points = (ci == curve_count - 1)
            ? (config->points - point_idx)
            : points_per_curve;

        for (uint32_t i = 0; i < num_points && point_idx < config->points; i++) {
            const float t = (num_points > 1) ? (float)i / (float)(num_points - 1) : 0.0f;
            const int16_t x = bezier_eval(c->start.x, c->cp1.x, c->cp2.x, c->end.x, t);
            const int16_t y = bezier_eval(c->start.y, c->cp1.y, c->cp2.y, c->end.y, t);

            if (x < 100) {
                continue;
            }

            data->points[point_idx].x      = x;
            data->points[point_idx].y_coef  = y / 100.0f;
            point_idx++;
        }
    }

    /* Monotonicity check */
    for (uint32_t i = 1; i < point_idx; i++) {
        if (data->points[i].x < data->points[i - 1].x) {
            LOG_ERR("X values must be strictly increasing at index %d/%d",
                    i, point_idx);
            return -EINVAL;
        }
    }

    return curve_count;
}

/* ------------------------------------------------------------------ */
/*  Coefficient lookup helper                                          */
/* ------------------------------------------------------------------ */

static float sample_coef(const struct accel_point *points, const uint32_t num_points,
                         const int64_t abs_input_mult_int, const float input_mult_smooth) {
    if (abs_input_mult_int <= 100) {
        return points[0].y_coef;
    }
    if (abs_input_mult_int >= points[num_points - 1].x) {
        return points[num_points - 1].y_coef;
    }
    if (abs_input_mult_int <= points[0].x) {
        return points[0].y_coef;
    }
    for (uint32_t i = 0; i < num_points - 1; i++) {
        if (abs_input_mult_int >= points[i].x && abs_input_mult_int < points[i + 1].x) {
            const struct accel_point *p0 = &points[i];
            const struct accel_point *p1 = &points[i + 1];
            const float t = (input_mult_smooth - (float)p0->x) / (float)(p1->x - p0->x);
            return p0->y_coef + t * (p1->y_coef - p0->y_coef);
        }
    }
    return 1.0f;
}

/* ------------------------------------------------------------------ */
/*  Event handler  (lookup + interpolation + remainder tracking)       */
/* ------------------------------------------------------------------ */

#define ACCEL_CURVE_MAX_COUPLED_AXES 8

static int sy_handle_event(const struct device *dev,
                           struct input_event *event,
                           const uint32_t p1, const uint32_t p2,
                           struct zmk_input_processor_state *s) {
    const struct zip_accel_curve_data   *data   = dev->data;
    const struct zip_accel_curve_config *config  = dev->config;

    if (unlikely(!data->initialized)) {
        return 0;
    }

    uint8_t event_idx = 0;
    bool relevant = false;
    for (uint8_t i = 0; i < config->event_codes_len; i++) {
        if (event->code == config->event_codes[i]) {
            relevant  = true;
            event_idx = i;
            break;
        }
    }

    if (!relevant) {
        return 0;
    }

    if (config->points == 0 || !data->points || !data->remainders) {
        return 0;
    }

    /* --- Axes coupling path --- */
    if (config->couple_axes && config->event_codes_len <= ACCEL_CURVE_MAX_COUPLED_AXES) {
        if (!data->buffered_values || !data->buffered_present || !data->inject_pass) {
            return 0;
        }

        /* If this is a re-injected event, pass through */
        if (data->inject_pass[event_idx]) {
            data->inject_pass[event_idx] = false;
            return 0;
        }

        /* Buffer this axis value */
        data->buffered_values[event_idx] = event->value;
        data->buffered_present[event_idx] = true;

        /* Wait for sync event (all axes in this report) */
        if (!event->sync) {
            event->value = 0;
            return 0;
        }

        /* Compute combined magnitude */
        float effective[ACCEL_CURVE_MAX_COUPLED_AXES] = {0};
        float mag_sq = 0.0f;
        for (uint8_t i = 0; i < config->event_codes_len; i++) {
            if (!data->buffered_present[i]) continue;
            const int32_t v = data->buffered_values[i];
            const float av = (float)((v >= 0) ? v : -v);
            effective[i] = av;
            mag_sq += av * av;
        }

        if (mag_sq <= 0.0f) {
            for (uint8_t i = 0; i < config->event_codes_len; i++) {
                data->buffered_present[i] = false;
            }
            event->value = 0;
            return 0;
        }

        const float magnitude = sqrtf(mag_sq);
        const int64_t abs_input_mult = (int64_t)(magnitude * 100.0f);
        const float input_mult = magnitude * 100.0f;
        const float coef = sample_coef(data->points, config->points, abs_input_mult, input_mult);

        /* Find last buffered axis for sync flag on re-inject */
        int8_t last_idx = -1;
        for (int16_t i = (int16_t)config->event_codes_len - 1; i >= 0; i--) {
            if (data->buffered_present[i]) {
                last_idx = (int8_t)i;
                break;
            }
        }

        /* Apply coefficient proportionally to each axis and re-inject */
        for (uint8_t i = 0; i < config->event_codes_len; i++) {
            if (!data->buffered_present[i]) continue;
            const int32_t v = data->buffered_values[i];
            const int32_t sign = (v >= 0) ? 1 : -1;
            const float result = effective[i] * coef + data->remainders[i];
            const int32_t out_int = (int32_t)result;
            data->remainders[i] = result - (float)out_int;
            const int32_t scaled = out_int * sign;

            data->inject_pass[i] = true;
            input_report_rel(event->dev, config->event_codes[i], scaled,
                             i == (uint8_t)last_idx, K_NO_WAIT);
        }

        /* Clear buffers */
        for (uint8_t i = 0; i < config->event_codes_len; i++) {
            data->buffered_present[i] = false;
        }

        event->value = 0;
        event->sync = false;
        return 0;
    }

    /* --- Standard per-axis path --- */
    const int32_t input_val = event->value;
    if (input_val == 0) {
        return 0;
    }

    const int32_t abs_input = abs(input_val);
    const int64_t abs_input_mult = (int64_t)abs_input * 100;
    const float input_mult = (float)abs_input * 100.0f;
    const int32_t sign = (input_val >= 0) ? 1 : -1;

    const float coef = sample_coef(data->points, config->points, abs_input_mult, input_mult);

    const float result = (float)abs_input * coef + data->remainders[event_idx];
    const int32_t result_int = (int32_t)result;
    data->remainders[event_idx] = result - (float)result_int;
    event->value = result_int * sign;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Init: parse DTS curve-data and build lookup table                  */
/* ------------------------------------------------------------------ */

static int sy_init(const struct device *dev) {
    if (!dev) {
        LOG_ERR("Unexpected NULL device pointer");
        return -EINVAL;
    }

    const struct zip_accel_curve_config *config = dev->config;
    struct zip_accel_curve_data         *data   = dev->data;
    data->dev = dev;

    /* Sanity-check the DTS property length */
    if (config->curve_data_len == 0 || (config->curve_data_len % 8) != 0) {
        LOG_ERR("curve-data length (%d) must be a positive multiple of 8",
                config->curve_data_len);
        return -EINVAL;
    }

    const uint8_t curve_count = config->curve_data_len / 8;
    if (curve_count > config->max_curves) {
        LOG_ERR("curve-data contains %d segments but max_curves is %d",
                curve_count, config->max_curves);
        return -EINVAL;
    }

    /* Zero the remainder accumulators */
    for (uint8_t i = 0; i < config->event_codes_len; i++) {
        data->remainders[i] = 0.0f;
    }

    /* Parse curve-data groups of 8 int16 values into curve structs. */
    const int16_t *cd = config->curve_data;
    for (uint8_t i = 0; i < curve_count; i++) {
        const uint8_t base = i * 8;
        if (i == 0) {
            data->curves[i] = (struct curve){
                .start = {0, 10},
                .end   = {cd[base + 2], cd[base + 3]},
                .cp1   = {cd[base + 4], cd[base + 5]},
                .cp2   = {cd[base + 6], cd[base + 7]},
            };
        } else {
            data->curves[i] = (struct curve){
                .start = {cd[base + 0], cd[base + 1]},
                .end   = {cd[base + 2], cd[base + 3]},
                .cp1   = {cd[base + 4], cd[base + 5]},
                .cp2   = {cd[base + 6], cd[base + 7]},
            };
        }
    }

    const int rc = set_curves(dev, curve_count);
    if (rc > 0) {
        data->initialized = true;
        data->num_curves  = rc;
        LOG_INF("Acceleration curve initialised with %d segment(s), %d points",
                rc, config->points);
    } else {
        data->initialized = false;
        LOG_ERR("Failed to build acceleration curve (rc=%d)", rc);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Driver plumbing                                                    */
/* ------------------------------------------------------------------ */

static struct zmk_input_processor_driver_api sy_driver_api = {
    .handle_event = sy_handle_event,
};

#define ACCEL_CURVE_INST(n)                                                     \
    static struct curve         _curves_##n[DT_INST_PROP_OR(n, max_curves, 8)]; \
    static struct accel_point   _points_##n[DT_INST_PROP_OR(n, points, 64)];    \
    static float                _remainders_##n[DT_INST_PROP_LEN(n, event_codes)]; \
    static int32_t              _buffered_values_##n[DT_INST_PROP_LEN(n, event_codes)]; \
    static bool                 _buffered_present_##n[DT_INST_PROP_LEN(n, event_codes)]; \
    static bool                 _inject_pass_##n[DT_INST_PROP_LEN(n, event_codes)]; \
    static const int16_t       _curve_data_##n[] =                              \
        DT_INST_PROP(n, curve_data);                                            \
    static struct zip_accel_curve_data data_##n = {                             \
        .initialized     = false,                                               \
        .curves          = _curves_##n,                                         \
        .points          = _points_##n,                                         \
        .remainders      = _remainders_##n,                                     \
        .buffered_values = _buffered_values_##n,                                \
        .buffered_present = _buffered_present_##n,                              \
        .inject_pass     = _inject_pass_##n,                                    \
    };                                                                          \
    static const struct zip_accel_curve_config config_##n = {                   \
        .max_curves     = DT_INST_PROP_OR(n, max_curves, 8),                   \
        .points         = DT_INST_PROP_OR(n, points, 64),                      \
        .couple_axes    = DT_INST_PROP_OR(n, couple_axes, false),               \
        .curve_data_len = ARRAY_SIZE(_curve_data_##n),                          \
        .curve_data     = _curve_data_##n,                                      \
        .event_codes_len = DT_INST_PROP_LEN(n, event_codes),                   \
        .event_codes    = DT_INST_PROP(n, event_codes),                         \
    };                                                                          \
    DEVICE_DT_INST_DEFINE(n, &sy_init, NULL, &data_##n, &config_##n,           \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,     \
                          &sy_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ACCEL_CURVE_INST)
