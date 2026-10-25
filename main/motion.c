/*
 * Motion engine for Vessel Motion P4 — 1:1 C port of vessel-motion-pi motion.py.
 * See motion.h for provenance and the exact-equivalence guarantee.
 */
#include "motion.h"
#include <math.h>

float clampf(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

float normalizef(float value, float low, float high, float exponent)
{
    if (high <= low) {
        return value >= high ? 1.0f : 0.0f;
    }
    return powf(clampf((value - low) / (high - low), 0.0f, 1.0f), exponent);
}

float vec3_magnitude(vec3_t v)
{
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

vec3_t vec3_sub(vec3_t a, vec3_t b)
{
    vec3_t r = { a.x - b.x, a.y - b.y, a.z - b.z };
    return r;
}

void motion_default_config(algorithm_cfg_t *cfg)
{
    *cfg = (algorithm_cfg_t) {
        .version = 1,
        .complementary_alpha = 0.98f,
        .fast_cutoff_hz = 25.0f,
        .slow_cutoff_hz = 4.0f,
        .derivative_cutoff_hz = 12.0f,
        .peak = { 0.10f, 1.50f, 2.0f },
        .rms = { 0.03f, 0.50f, 2.0f },
        .jerk = { 0.5f, 15.0f, 2.0f },
        .roll_rate = { 2.0f, 25.0f, 2.0f },
        .pitch_rate = { 2.0f, 25.0f, 2.0f },
        .repeat = { 1.0f, 20.0f, 2.0f },
        .rsi_weights = { 0.35f, 0.25f, 0.20f, 0.10f, 0.10f },
        .rsi_attack_seconds = 0.20f,
        .rsi_decay_seconds = 5.0f,
        .impact_start_g = 0.25f,
        .impact_end_g = 0.15f,
        .impact_end_ms = 100,
        .impact_deadband_ms = 150,
        .impact_accel = { 0.25f, 1.50f, 2.0f },
        .impact_jerk = { 1.0f, 15.0f, 2.0f },
        .trip_rsi_weights = { 0.40f, 0.35f, 0.15f, 0.10f },
        .trip_impact_per_hour = { 1.0f, 20.0f, 2.0f },
        .mei_axis_weights = { 1.0f, 1.0f, 1.0f },
        .mei_rms = { 0.02f, 0.30f, 2.0f },
        .mei_shock = { 0.03f, 0.50f, 2.0f },
        .mei_duration_tau_hours = 4.0f,
        .mei_current_weights = { 0.45f, 0.35f, 0.20f },
        .mei_trip_weights = { 0.40f, 0.40f, 0.20f },
        .idle_accel_rms_g = 0.015f,
        .idle_rate_deg_s = 0.5f,
        .idle_seconds = 30.0f,
    };
}

/* --- axis mapping --- */
void axis_map_init(axis_map_t *m)
{
    m->source[0] = 0; m->source[1] = 1; m->source[2] = 2;
    m->sign[0] = 1;   m->sign[1] = -1;  m->sign[2] = 1;
}

bool axis_map_valid(const axis_map_t *m)
{
    bool seen[3] = { false, false, false };
    for (int i = 0; i < 3; i++) {
        if (m->source[i] < 0 || m->source[i] > 2 || seen[m->source[i]]) return false;
        seen[m->source[i]] = true;
        if (m->sign[i] != -1 && m->sign[i] != 1) return false;
    }
    return true;
}

vec3_t axis_map_apply(const axis_map_t *m, vec3_t v)
{
    const float values[3] = { v.x, v.y, v.z };
    vec3_t r;
    r.x = values[m->source[0]] * m->sign[0];
    r.y = values[m->source[1]] * m->sign[1];
    r.z = values[m->source[2]] * m->sign[2];
    return r;
}

static int axis_map_determinant(const axis_map_t *m)
{
    int inversions = 0;
    for (int i = 0; i < 3; i++)
        for (int j = i + 1; j < 3; j++)
            if (m->source[i] > m->source[j]) inversions++;
    int sign_product = m->sign[0] * m->sign[1] * m->sign[2];
    return (inversions & 1 ? -1 : 1) * sign_product;
}

vec3_t axis_map_apply_axial(const axis_map_t *m, vec3_t v)
{
    vec3_t mapped = axis_map_apply(m, v);
    int o = axis_map_determinant(m);
    vec3_t r = { mapped.x * o, mapped.y * o, mapped.z * o };
    return r;
}

/* --- orientation --- */
void orientation_filter_init(orientation_filter_t *f)
{
    f->roll = f->pitch = f->roll_zero = f->pitch_zero = 0.0f;
    f->initialized = false;
}

void orientation_filter_reset(orientation_filter_t *f, vec3_t accel)
{
    f->roll = atan2f(accel.y, accel.z);
    f->pitch = atan2f(-accel.x, sqrtf(accel.y * accel.y + accel.z * accel.z));
    f->initialized = true;
}

orientation_t orientation_filter_update(orientation_filter_t *f, vec3_t accel,
                                        vec3_t gyro, float dt, float alpha)
{
    if (!f->initialized) {
        orientation_filter_reset(f, accel);
    }
    float roll_acc = atan2f(accel.y, accel.z);
    float pitch_acc = atan2f(-accel.x, sqrtf(accel.y * accel.y + accel.z * accel.z));
    if (dt > 0.0f && dt <= 0.05f) {
        f->roll = alpha * (f->roll + gyro.x * dt) + (1.0f - alpha) * roll_acc;
        f->pitch = alpha * (f->pitch + gyro.y * dt) + (1.0f - alpha) * pitch_acc;
    }
    orientation_t o;
    o.roll = f->roll;
    o.pitch = f->pitch;
    o.display_roll = f->roll - f->roll_zero;
    o.display_pitch = f->pitch - f->pitch_zero;
    o.gravity.x = -MOTION_G0 * sinf(f->pitch);
    o.gravity.y = MOTION_G0 * sinf(f->roll) * cosf(f->pitch);
    o.gravity.z = MOTION_G0 * cosf(f->roll) * cosf(f->pitch);
    return o;
}

void orientation_filter_zero(orientation_filter_t *f)
{
    f->roll_zero = f->roll;
    f->pitch_zero = f->pitch;
}

/* --- low-pass --- */
void lowpass3_init(lowpass3_t *f)
{
    f->state = (vec3_t){ 0.0f, 0.0f, 0.0f };
}

vec3_t lowpass3_update(lowpass3_t *f, vec3_t value, float cutoff_hz, float dt)
{
    float alpha = dt / (1.0f / (2.0f * (float)M_PI * cutoff_hz) + dt);
    f->state.x += alpha * (value.x - f->state.x);
    f->state.y += alpha * (value.y - f->state.y);
    f->state.z += alpha * (value.z - f->state.z);
    return f->state;
}

/* --- motion analyzer (windowed, ring buffer) --- */
void motion_analyzer_init(motion_analyzer_t *a)
{
    lowpass3_init(&a->fast_filter);
    lowpass3_init(&a->slow_filter);
    lowpass3_init(&a->jerk_filter);
    a->previous_fast = (vec3_t){ 0.0f, 0.0f, 0.0f };
    a->head = 0;
    a->count = 0;
}

static void ring_push(float *buf, int *head, int *count, float value)
{
    buf[*head] = value;
    *head = (*head + 1) % MOTION_WINDOW;
    if (*count < MOTION_WINDOW) (*count)++;
}

static float ring_max(const float *buf, int count)
{
    float m = 0.0f;
    for (int i = 0; i < count; i++) if (buf[i] > m) m = buf[i];
    return m;
}

static float ring_rms(const float *buf, int count)
{
    if (count == 0) return 0.0f;
    float acc = 0.0f;
    for (int i = 0; i < count; i++) acc += buf[i] * buf[i];
    return sqrtf(acc / count);
}

motion_t motion_analyzer_update(motion_analyzer_t *a, vec3_t accel, vec3_t gravity,
                                vec3_t gyro, float dt, const algorithm_cfg_t *cfg)
{
    motion_t m;
    m.linear = vec3_sub(accel, gravity);
    m.fast = lowpass3_update(&a->fast_filter, m.linear, cfg->fast_cutoff_hz, dt);
    m.slow = lowpass3_update(&a->slow_filter, m.linear, cfg->slow_cutoff_hz, dt);
    vec3_t raw_jerk = {
        (m.fast.x - a->previous_fast.x) / dt,
        (m.fast.y - a->previous_fast.y) / dt,
        (m.fast.z - a->previous_fast.z) / dt,
    };
    m.jerk = lowpass3_update(&a->jerk_filter, raw_jerk, cfg->derivative_cutoff_hz, dt);
    a->previous_fast = m.fast;

    float fast_mag = vec3_magnitude(m.fast);
    float jerk_mag = vec3_magnitude(m.jerk);
    ring_push(a->accel, &a->head, &a->count, fast_mag);
    /* jerk/roll/pitch share the same head; push with local indices in lockstep */
    a->jerk[a->head == 0 ? MOTION_WINDOW - 1 : a->head - 1] = jerk_mag;
    a->roll[a->head == 0 ? MOTION_WINDOW - 1 : a->head - 1] = fabsf(gyro.x);
    a->pitch[a->head == 0 ? MOTION_WINDOW - 1 : a->head - 1] = fabsf(gyro.y);

    m.fast_resultant = fast_mag;
    m.slow_resultant = vec3_magnitude(m.slow);
    m.jerk_resultant = jerk_mag;
    m.roll_rate = gyro.x;
    m.pitch_rate = gyro.y;
    m.accel_peak = ring_max(a->accel, a->count);
    m.accel_rms = ring_rms(a->accel, a->count);
    m.jerk_peak = ring_max(a->jerk, a->count);
    m.roll_rate_peak = ring_max(a->roll, a->count);
    m.pitch_rate_peak = ring_max(a->pitch, a->count);
    return m;
}

/* --- RSI --- */
void ride_severity_init(ride_severity_t *r)
{
    r->state = (severity_t){ 0 };
}

severity_t ride_severity_update(ride_severity_t *r, const motion_t *motion,
                                float impacts_per_minute, float dt,
                                const algorithm_cfg_t *cfg, bool enabled)
{
    float peak = normalizef(motion->accel_peak / MOTION_G0, cfg->peak.low, cfg->peak.high, cfg->peak.exponent);
    float rms = normalizef(motion->accel_rms / MOTION_G0, cfg->rms.low, cfg->rms.high, cfg->rms.exponent);
    float jerk = normalizef(motion->jerk_peak / MOTION_G0, cfg->jerk.low, cfg->jerk.high, cfg->jerk.exponent);
    float roll_n = normalizef(motion->roll_rate_peak * (180.0f / (float)M_PI), cfg->roll_rate.low, cfg->roll_rate.high, cfg->roll_rate.exponent);
    float pitch_n = normalizef(motion->pitch_rate_peak * (180.0f / (float)M_PI), cfg->pitch_rate.low, cfg->pitch_rate.high, cfg->pitch_rate.exponent);
    float angular = roll_n > pitch_n ? roll_n : pitch_n;
    float repeat = normalizef(impacts_per_minute, cfg->repeat.low, cfg->repeat.high, cfg->repeat.exponent);

    float comps[5] = { peak, rms, jerk, angular, repeat };
    float raw = 0.0f;
    if (enabled) {
        float acc = 0.0f;
        for (int i = 0; i < 5; i++) acc += cfg->rsi_weights[i] * comps[i];
        raw = 10.0f * clampf(acc, 0.0f, 1.0f);
    }
    float tau = raw > r->state.current ? cfg->rsi_attack_seconds : cfg->rsi_decay_seconds;
    float denom = tau > 0.001f ? tau : 0.001f;
    float current = r->state.current + (1.0f - expf(-dt / denom)) * (raw - r->state.current);

    r->state.peak = peak; r->state.rms = rms; r->state.jerk = jerk;
    r->state.angular = angular; r->state.repeat = repeat;
    r->state.raw = raw;
    r->state.current = enabled ? current : 0.0f;
    return r->state;
}

/* --- MEI / motion exposure --- */
void motion_exposure_init(motion_exposure_t *e)
{
    e->state = (exposure_t){ 0 };
    e->recent_a2 = e->recent_dose4 = e->recent_seconds = 0.0f;
}

exposure_t motion_exposure_update(motion_exposure_t *e, vec3_t slow, float dt,
                                  bool active, const algorithm_cfg_t *cfg, bool enabled)
{
    float effective = sqrtf(
        powf(cfg->mei_axis_weights[0] * slow.x, 2.0f) +
        powf(cfg->mei_axis_weights[1] * slow.y, 2.0f) +
        powf(cfg->mei_axis_weights[2] * slow.z, 2.0f));
    if (active && enabled) {
        e->state.sum_a2 += effective * effective * dt;
        e->state.dose4 += powf(effective, 4.0f) * dt;
        e->state.seconds += dt;
        float decay = expf(-dt / 600.0f);
        e->recent_a2 = e->recent_a2 * decay + effective * effective * dt;
        e->recent_dose4 = e->recent_dose4 * decay + powf(effective, 4.0f) * dt;
        e->recent_seconds = e->recent_seconds * decay + dt;
    }
    float recent_rms = e->recent_seconds ? sqrtf(e->recent_a2 / e->recent_seconds) : 0.0f;
    float recent_shock = e->recent_seconds ? powf(e->recent_dose4 / e->recent_seconds, 0.25f) : 0.0f;
    e->state.rms = normalizef(recent_rms / MOTION_G0, cfg->mei_rms.low, cfg->mei_rms.high, cfg->mei_rms.exponent);
    e->state.shock = normalizef(recent_shock / MOTION_G0, cfg->mei_shock.low, cfg->mei_shock.high, cfg->mei_shock.exponent);
    float duration = 1.0f - expf(-(e->recent_seconds / 3600.0f) / cfg->mei_duration_tau_hours);
    float vals[3] = { e->state.rms, e->state.shock, duration };
    float acc = 0.0f;
    for (int i = 0; i < 3; i++) acc += cfg->mei_current_weights[i] * vals[i];
    e->state.current = enabled ? 10.0f * clampf(acc, 0.0f, 1.0f) : 0.0f;
    return e->state;
}

float motion_exposure_score(float sum_a2, float dose4, float seconds, const algorithm_cfg_t *cfg)
{
    if (seconds <= 0.0f) return 0.0f;
    float rms = normalizef(sqrtf(sum_a2 / seconds) / MOTION_G0, cfg->mei_rms.low, cfg->mei_rms.high, cfg->mei_rms.exponent);
    float shock = normalizef(powf(dose4 / seconds, 0.25f) / MOTION_G0, cfg->mei_shock.low, cfg->mei_shock.high, cfg->mei_shock.exponent);
    float duration = 1.0f - expf(-(seconds / 3600.0f) / cfg->mei_duration_tau_hours);
    float vals[3] = { rms, shock, duration };
    float acc = 0.0f;
    for (int i = 0; i < 3; i++) acc += cfg->mei_trip_weights[i] * vals[i];
    return 10.0f * clampf(acc, 0.0f, 1.0f);
}

/* --- impact detection --- */
void impact_detector_init(impact_detector_t *d)
{
    d->active = false;
    d->below_since = 0;
    d->last_end = 0;
    d->has_current = false;
    d->recent_head = 0;
    d->recent_count = 0;
}

float impact_detector_weighted_per_minute(const impact_detector_t *d, int64_t now_ms)
{
    float sum = 0.0f;
    for (int i = 0; i < d->recent_count; i++) {
        if (now_ms - d->recent_ms[i] <= 60000) sum += d->recent_sev[i];
    }
    return sum;
}

bool impact_detector_update(impact_detector_t *d, const motion_t *motion,
                            const orientation_t *orientation, float rsi,
                            int64_t now_ms, const algorithm_cfg_t *cfg,
                            impact_t *out)
{
    float acceleration_g = motion->fast_resultant / MOTION_G0;
    if (!d->active && acceleration_g >= cfg->impact_start_g &&
        now_ms - d->last_end >= cfg->impact_deadband_ms) {
        d->active = true;
        d->below_since = 0;
        d->current.start_ms = now_ms;
        d->current.end_ms = 0;
        d->current.duration_ms = 0;
        d->current.peak = motion->fast;
        d->current.peak_resultant = motion->fast_resultant;
        d->current.peak_jerk = motion->jerk_resultant;
        d->current.roll = orientation->display_roll;
        d->current.pitch = orientation->display_pitch;
        d->current.rsi = rsi;
        d->current.severity = 0.0f;
        d->has_current = true;
    }
    if (!d->active || !d->has_current) return false;

    if (motion->fast_resultant >= d->current.peak_resultant) {
        d->current.peak = motion->fast;
        d->current.peak_resultant = motion->fast_resultant;
        d->current.roll = orientation->display_roll;
        d->current.pitch = orientation->display_pitch;
        d->current.rsi = rsi;
    }
    if (motion->jerk_resultant > d->current.peak_jerk)
        d->current.peak_jerk = motion->jerk_resultant;

    if (acceleration_g > cfg->impact_end_g) {
        d->below_since = 0;
        return false;
    }
    if (d->below_since == 0) {
        d->below_since = now_ms;
        return false;
    }
    if (now_ms - d->below_since < cfg->impact_end_ms) return false;

    d->current.end_ms = now_ms;
    d->current.duration_ms = (int)(now_ms - d->current.start_ms);
    float accel_score = normalizef(d->current.peak_resultant / MOTION_G0,
                                   cfg->impact_accel.low, cfg->impact_accel.high, cfg->impact_accel.exponent);
    float jerk_score = normalizef(d->current.peak_jerk / MOTION_G0,
                                  cfg->impact_jerk.low, cfg->impact_jerk.high, cfg->impact_jerk.exponent);
    d->current.severity = clampf(0.75f * accel_score + 0.25f * jerk_score, 0.0f, 1.0f);

    *out = d->current;
    d->recent_ms[d->recent_head] = now_ms;
    d->recent_sev[d->recent_head] = d->current.severity;
    d->recent_head = (d->recent_head + 1) % IMPACT_RECENT_MAX;
    if (d->recent_count < IMPACT_RECENT_MAX) d->recent_count++;
    d->active = false;
    d->last_end = now_ms;
    d->has_current = false;
    return true;
}

/* --- trip scoring helpers --- */
float trip_rsi_p95(const uint32_t histogram[RSI_HISTOGRAM_BINS], uint32_t updates)
{
    if (updates == 0) return 0.0f;
    uint32_t target = (uint32_t)(updates * 0.95);
    uint32_t cumulative = 0;
    for (int i = 0; i < RSI_HISTOGRAM_BINS; i++) {
        cumulative += histogram[i];
        if (cumulative >= target) {
            return (i + 0.5f) * 0.01f;
        }
    }
    return (RSI_HISTOGRAM_BINS - 0.5f) * 0.01f;
}

float trip_rsi_score(float rsi_mean, float rsi_p95, float rsi_max,
                     float impacts_per_hour, const algorithm_cfg_t *cfg)
{
    float impact_score = normalizef(impacts_per_hour, cfg->trip_impact_per_hour.low,
                                    cfg->trip_impact_per_hour.high, cfg->trip_impact_per_hour.exponent);
    float acc = cfg->trip_rsi_weights[0] * rsi_mean +
                cfg->trip_rsi_weights[1] * rsi_p95 +
                cfg->trip_rsi_weights[2] * rsi_max +
                cfg->trip_rsi_weights[3] * (10.0f * impact_score);
    return clampf(acc, 0.0f, 10.0f);
}
