/* Trip & impact store for Vessel Motion P4 — see store.h.
 * Settings -> NVS; trip samples/impacts/heatmap -> binary files on SD card.
 * Binary formats match the Pi project exactly for dashboard compatibility. */
#include "store.h"
#include "storage.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "motion.h"   /* normalizef */

static const char *TAG = "store";

#define STORE_DIR        SD_MOUNT_POINT "/vessel"
#define TRIPS_DIR        STORE_DIR "/trips"
#define IMPACTS_FILE     STORE_DIR "/impacts.bin"
#define TRIP_RETENTION   100
#define TRIP_SAMPLE_HZ   5
#define MAX_TRIP_SECONDS 28800
#define HEATMAP_CELLS    400

/* On-disk trip header (fixed-size binary metadata per trip). */
typedef struct {
    uint32_t magic;            /* 'VMTP' */
    uint32_t id;
    int64_t  start_ms;
    char     label[40];
    uint32_t duration_seconds;
    uint32_t sample_count;
    float    max_roll, max_pitch, max_resultant_g, max_jerk_g;
    float    max_axis_g[3];
    float    rsi_sum, rsi_mean, rsi_max, rsi_p95, rsi_trip;
    float    mei_sum, mei_trip;
    uint32_t impact_counts[3];
    uint32_t algorithm_version;
    uint32_t completed;
    float    heatmap[HEATMAP_CELLS];
} trip_file_t;

#define TRIP_MAGIC 0x56544d50  /* 'VTMP' */

static void make_meta_path(uint32_t id, char *buf, size_t len)
{
    snprintf(buf, len, TRIPS_DIR "/trip_%03lu.meta", (unsigned long)id);
}

/* 16-byte trip sample record "<Ihhhhhh" */
#pragma pack(push, 1)
typedef struct {
    uint32_t elapsed_ms;
    int16_t  roll100, pitch100;
    int16_t  ax1000, ay1000, az1000, res1000;
} trip_sample_rec_t;
#pragma pack(pop)

/* Impact binary record. */
#pragma pack(push, 1)
typedef struct {
    uint32_t id;
    int64_t  epoch_ms;
    uint32_t trip_id;
    uint32_t elapsed_ms, duration_ms;
    float    peak_x_g, peak_y_g, peak_z_g, peak_resultant_g, peak_jerk_g;
    float    roll_deg, pitch_deg, rsi, severity;
} impact_file_rec_t;
#pragma pack(pop)

static bool     s_ready;
static uint32_t s_active_id;
static int64_t  s_active_start_ms;
static int64_t  s_active_start_mono_ms;
static uint32_t s_active_samples;
static char     s_active_label[40];

/* Rolling accumulators for the active trip */
static float    s_rsi_sum, s_rsi_max, s_mei_sum;
static uint32_t s_rsi_updates;
static uint32_t s_rsi_hist[1000];
static float    s_heatmap[HEATMAP_CELLS];        /* live always-on (dashboard) */
static float    s_trip_heatmap[HEATMAP_CELLS];   /* active trip only (saved to file) */
static float    s_max_roll, s_max_pitch, s_max_resultant, s_max_jerk;
static float    s_max_axis[3];
static float    s_sum_a2, s_dose4, s_exposure_seconds;
static float    s_weighted_impacts;
static uint32_t s_impact_counts[3];
static uint32_t s_impact_next_id = 1;

static int16_t b16(float v)
{
    long r = lroundf(v);
    return r < -32768 ? -32768 : (r > 32767 ? 32767 : (int16_t)r);
}

static int64_t epoch_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void trip_path(uint32_t id, char *buf, size_t len)
{
    snprintf(buf, len, TRIPS_DIR "/trip_%03lu.bin", (unsigned long)id);
}

static void write_trip_file(const trip_file_t *t)
{
    /* header metadata lives in a sidecar .meta file; samples live in .bin */
    char mpath[64];
    make_meta_path(t->id, mpath, sizeof(mpath));
    FILE *f = fopen(mpath, "wb");
    if (!f) return;
    fwrite(t, sizeof(*t), 1, f);
    fclose(f);
}

static bool read_trip_file(uint32_t id, trip_file_t *t)
{
    char mpath[64];
    make_meta_path(id, mpath, sizeof(mpath));
    FILE *f = fopen(mpath, "rb");
    if (!f) return false;
    bool ok = fread(t, sizeof(*t), 1, f) == 1 && t->magic == TRIP_MAGIC;
    fclose(f);
    return ok;
}

/* Collect the trip ids present on SD (files trip_<id>.meta), unsorted. */
static int scan_trip_ids(uint32_t *ids, int max_ids)
{
    DIR *d = opendir(TRIPS_DIR);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while (n < max_ids && (e = readdir(d)) != NULL) {
        unsigned long id;
        if (sscanf(e->d_name, "trip_%lu.meta", &id) == 1) ids[n++] = (uint32_t)id;
    }
    closedir(d);
    return n;
}

esp_err_t store_init(void)
{
    /* NVS is initialised in app_main before settings_init(). */
    if (!storage_sd_ready()) {
        ESP_LOGW(TAG, "SD not ready; trips/impacts will not persist");
        s_ready = false;
        return ESP_ERR_NOT_FOUND;
    }
    mkdir(STORE_DIR, 0775);
    mkdir(TRIPS_DIR, 0775);
    s_ready = true;

    /* Discover next impact id */
    FILE *f = fopen(IMPACTS_FILE, "rb");
    if (f) {
        impact_file_rec_t r;
        while (fread(&r, sizeof(r), 1, f) == 1) {
            if (r.id >= s_impact_next_id) s_impact_next_id = r.id + 1;
        }
        fclose(f);
    }
    ESP_LOGI(TAG, "Store ready on SD (%s)", STORE_DIR);
    return ESP_OK;
}

/* --- trip lifecycle --- */
uint32_t store_start_trip(int algorithm_version)
{
    if (!s_ready || s_active_id) return s_active_id;
    /* allocate id = max existing + 1 */
    uint32_t ids[256];
    int nids = scan_trip_ids(ids, 256);
    uint32_t maxid = 0;
    for (int i = 0; i < nids; i++) if (ids[i] > maxid) maxid = ids[i];
    s_active_id = maxid + 1;
    s_active_start_ms = epoch_now_ms();
    s_active_start_mono_ms = esp_timer_get_time() / 1000;
    s_active_samples = 0;

    /* label: "MM/DD/YY HH:MM:SS" in the configured local timezone */
    time_t secs = (time_t)(s_active_start_ms / 1000);
    struct tm tmv;
    localtime_r(&secs, &tmv);
    strftime(s_active_label, sizeof(s_active_label), "%m/%d/%y %H:%M:%S", &tmv);

    /* reset accumulators */
    s_rsi_sum = s_rsi_max = s_mei_sum = 0.0f;
    s_rsi_updates = 0;
    memset(s_rsi_hist, 0, sizeof(s_rsi_hist));
    memset(s_trip_heatmap, 0, sizeof(s_trip_heatmap));
    s_max_roll = s_max_pitch = s_max_resultant = s_max_jerk = 0.0f;
    s_max_axis[0] = s_max_axis[1] = s_max_axis[2] = 0.0f;
    s_sum_a2 = s_dose4 = s_exposure_seconds = 0.0f;
    s_weighted_impacts = 0.0f;
    memset(s_impact_counts, 0, sizeof(s_impact_counts));

    trip_file_t t = { 0 };
    t.magic = TRIP_MAGIC;
    t.id = s_active_id;
    t.start_ms = s_active_start_ms;
    t.algorithm_version = (uint32_t)algorithm_version;
    strncpy(t.label, s_active_label, sizeof(t.label) - 1);
    t.completed = 0;
    write_trip_file(&t);
    ESP_LOGI(TAG, "Trip %lu started (%s)", (unsigned long)s_active_id, s_active_label);
    return s_active_id;
}

static float compute_p95(void)
{
    if (s_rsi_updates == 0) return 0.0f;
    uint32_t target = (uint32_t)(s_rsi_updates * 0.95f);
    uint32_t cum = 0;
    for (int i = 0; i < 1000; i++) {
        cum += s_rsi_hist[i];
        if (cum >= target) return (i + 0.5f) * 0.01f;
    }
    return 9.995f;
}

void store_end_trip(const algorithm_cfg_t *cfg)
{
    if (!s_active_id) return;
    trip_file_t t;
    if (!read_trip_file(s_active_id, &t)) {
        memset(&t, 0, sizeof(t));
        t.magic = TRIP_MAGIC;
        t.id = s_active_id;
    }

    t.duration_seconds = 0;
    /* duration from samples: derive from sample_count at 5 Hz */
    t.duration_seconds = t.sample_count / TRIP_SAMPLE_HZ;
    t.rsi_mean = s_rsi_updates ? s_rsi_sum / s_rsi_updates : 0.0f;
    t.rsi_max = s_rsi_max;
    t.rsi_p95 = compute_p95();
    t.max_roll = s_max_roll;
    t.max_pitch = s_max_pitch;
    t.max_resultant_g = s_max_resultant;
    t.max_jerk_g = s_max_jerk;
    memcpy(t.max_axis_g, s_max_axis, sizeof(s_max_axis));
    memcpy(t.impact_counts, s_impact_counts, sizeof(s_impact_counts));
    memcpy(t.heatmap, s_trip_heatmap, sizeof(s_trip_heatmap));

    /* Pi service._update_trip final scores */
    float iph = s_exposure_seconds ? s_weighted_impacts * 3600.0f / s_exposure_seconds : 0.0f;
    float impact_score = normalizef(iph, cfg->trip_impact_per_hour.low,
                                    cfg->trip_impact_per_hour.high,
                                    cfg->trip_impact_per_hour.exponent);
    float rv[4] = { t.rsi_mean, t.rsi_p95, t.rsi_max, 10.0f * impact_score };
    float acc = 0.0f;
    for (int i = 0; i < 4; i++) acc += cfg->trip_rsi_weights[i] * rv[i];
    t.rsi_trip = clampf(acc, 0.0f, 10.0f);
    t.mei_trip = motion_exposure_score(s_sum_a2, s_dose4, s_exposure_seconds, cfg);

    t.completed = 1;
    write_trip_file(&t);
    ESP_LOGI(TAG, "Trip %lu ended (rsi=%.2f mei=%.2f)", (unsigned long)s_active_id, t.rsi_trip, t.mei_trip);
    s_active_id = 0;

    /* retention: delete oldest completed trips beyond TRIP_RETENTION */
    {
        uint32_t ids[TRIP_RETENTION + 16];
        int nids = scan_trip_ids(ids, (int)(sizeof(ids) / sizeof(ids[0])));
        for (int i = 0; i < nids - 1; i++)   /* ascending */
            for (int j = i + 1; j < nids; j++)
                if (ids[j] < ids[i]) { uint32_t tmp = ids[i]; ids[i] = ids[j]; ids[j] = tmp; }
        for (int i = 0; i + TRIP_RETENTION < nids; i++) {
            char p[64];
            make_meta_path(ids[i], p, sizeof(p)); remove(p);
            trip_path(ids[i], p, sizeof(p)); remove(p);
            ESP_LOGI(TAG, "Retention: deleted oldest trip %lu", (unsigned long)ids[i]);
        }
    }
}

bool     store_trip_active(void) { return s_active_id != 0; }
uint32_t store_active_trip_id(void) { return s_active_id; }
int64_t  store_active_trip_start_ms(void) { return s_active_start_ms; }
int64_t  store_active_trip_start_mono_ms(void) { return s_active_start_mono_ms; }
uint32_t store_active_trip_samples(void) { return s_active_samples; }
void     store_active_trip_label(char *buf, size_t len)
{
    strlcpy(buf, s_active_label, len);
}

void store_trip_live(trip_live_t *out)
{
    out->rsi_mean = s_rsi_updates ? s_rsi_sum / s_rsi_updates : 0.0f;
    out->rsi_max = s_rsi_max;
    out->rsi_p95 = compute_p95();
    out->weighted_impacts = s_weighted_impacts;
    out->sum_a2 = s_sum_a2;
    out->dose4 = s_dose4;
    out->exposure_seconds = s_exposure_seconds;
    out->rsi_updates = s_rsi_updates;
}

/* --- ingest --- */
void store_add_sample(uint32_t elapsed_ms, float roll_deg, float pitch_deg,
                      float ax_g, float ay_g, float az_g, float resultant_g,
                      float rsi, float mei)
{
    if (!s_active_id || !s_ready) return;
    char path[64];
    trip_path(s_active_id, path, sizeof(path));
    FILE *f = fopen(path, "ab");
    if (f) {
        trip_sample_rec_t r = {
            .elapsed_ms = elapsed_ms,
            .roll100 = b16(roll_deg * 100.0f), .pitch100 = b16(pitch_deg * 100.0f),
            .ax1000 = b16(ax_g * 1000.0f), .ay1000 = b16(ay_g * 1000.0f),
            .az1000 = b16(az_g * 1000.0f), .res1000 = b16(resultant_g * 1000.0f),
        };
        fwrite(&r, sizeof(r), 1, f);
        fclose(f);
    }
    /* update header metadata */
    trip_file_t t;
    if (read_trip_file(s_active_id, &t)) {
        t.sample_count++;
        s_active_samples = t.sample_count;
        t.duration_seconds = elapsed_ms / 1000;
        write_trip_file(&t);
    }
}

void store_add_impact(const impact_rec_t *impact)
{
    if (!s_ready) return;
    FILE *f = fopen(IMPACTS_FILE, "ab");
    if (!f) return;
    impact_file_rec_t r = {
        .id = s_impact_next_id++,
        .epoch_ms = impact->epoch_ms,
        .trip_id = impact->id,
        .elapsed_ms = impact->elapsed_ms,
        .duration_ms = impact->duration_ms,
        .peak_x_g = impact->peak_x_g, .peak_y_g = impact->peak_y_g, .peak_z_g = impact->peak_z_g,
        .peak_resultant_g = impact->peak_resultant_g, .peak_jerk_g = impact->peak_jerk_g,
        .roll_deg = impact->roll_deg, .pitch_deg = impact->pitch_deg,
        .rsi = impact->rsi, .severity = impact->severity,
    };
    fwrite(&r, sizeof(r), 1, f);
    fclose(f);
}

void store_trip_accumulate(float dt, float rsi, float mei,
                           float roll_deg, float pitch_deg,
                           float ax_g, float ay_g, float az_g,
                           float resultant_g, float jerk_g,
                           float slow_eff)
{
    s_rsi_sum += rsi;
    s_rsi_updates++;
    if (rsi > s_rsi_max) s_rsi_max = rsi;
    int bin = (int)(rsi * 100.0f);
    if (bin > 999) bin = 999;
    if (bin >= 0) s_rsi_hist[bin]++;
    s_mei_sum += mei;
    float ar = fabsf(roll_deg), ap = fabsf(pitch_deg);
    if (ar > s_max_roll) s_max_roll = ar;
    if (ap > s_max_pitch) s_max_pitch = ap;
    float aa[3] = { fabsf(ax_g), fabsf(ay_g), fabsf(az_g) };
    for (int i = 0; i < 3; i++) if (aa[i] > s_max_axis[i]) s_max_axis[i] = aa[i];
    if (resultant_g > s_max_resultant) s_max_resultant = resultant_g;
    if (jerk_g > s_max_jerk) s_max_jerk = jerk_g;
    /* MEI exposure integrals (Pi TripRuntime) */
    s_sum_a2 += slow_eff * slow_eff * dt;
    s_dose4 += slow_eff * slow_eff * slow_eff * slow_eff * dt;
    s_exposure_seconds += dt;
    /* heatmap 20x20: row=mei bin, col=rsi bin (per-trip, saved to file) */
    int rsi_bin = (int)(rsi * 2.0f); if (rsi_bin > 19) rsi_bin = 19; if (rsi_bin < 0) rsi_bin = 0;
    int mei_bin = (int)(mei * 2.0f); if (mei_bin > 19) mei_bin = 19; if (mei_bin < 0) mei_bin = 0;
    s_trip_heatmap[mei_bin * 20 + rsi_bin] += dt;
}

/* Live dashboard heatmap: accumulates with or without an active trip.
 * Cleared by the "reset" command (and on trip start when resetOnTripStart). */
void store_heatmap_accumulate(float dt, float rsi, float mei)
{
    int rsi_bin = (int)(rsi * 2.0f); if (rsi_bin > 19) rsi_bin = 19; if (rsi_bin < 0) rsi_bin = 0;
    int mei_bin = (int)(mei * 2.0f); if (mei_bin > 19) mei_bin = 19; if (mei_bin < 0) mei_bin = 0;
    s_heatmap[mei_bin * 20 + rsi_bin] += dt;
}

void store_trip_add_impact_severity(float severity)
{
    s_weighted_impacts += severity;
    int bucket = severity < 0.33f ? 0 : (severity < 0.66f ? 1 : 2);
    s_impact_counts[bucket]++;
}

void store_heatmap_reset(void)
{
    memset(s_heatmap, 0, sizeof(s_heatmap));
}

/* --- queries --- */
bool store_trip_meta(uint32_t trip_id, trip_meta_t *out)
{
    trip_file_t t;
    if (!read_trip_file(trip_id, &t)) return false;
    memset(out, 0, sizeof(*out));
    out->id = t.id;
    out->start_ms = t.start_ms;
    strlcpy(out->label, t.label, sizeof(out->label));
    out->duration_seconds = t.duration_seconds;
    out->sample_count = t.sample_count;
    out->max_roll = t.max_roll; out->max_pitch = t.max_pitch;
    memcpy(out->max_axis_g, t.max_axis_g, sizeof(t.max_axis_g));
    out->max_resultant_g = t.max_resultant_g; out->max_jerk_g = t.max_jerk_g;
    out->rsi_mean = t.rsi_mean; out->rsi_max = t.rsi_max;
    out->rsi_p95 = t.rsi_p95; out->rsi_trip = t.rsi_trip;
    out->mei_trip = t.mei_trip;
    memcpy(out->impact_counts, t.impact_counts, sizeof(t.impact_counts));
    memcpy(out->heatmap, t.heatmap, sizeof(t.heatmap));
    out->algorithm_version = (int)t.algorithm_version;
    out->active = (trip_id == s_active_id);
    return true;
}

int store_list_trips(trip_meta_t *out, int max)
{
    uint32_t ids[256];
    int nids = scan_trip_ids(ids, 256);
    /* newest first */
    for (int i = 0; i < nids - 1; i++)
        for (int j = i + 1; j < nids; j++)
            if (ids[j] > ids[i]) { uint32_t tmp = ids[i]; ids[i] = ids[j]; ids[j] = tmp; }
    int n = 0;
    for (int k = 0; k < nids && n < max; k++) {
        uint32_t i = ids[k];
        trip_file_t t;
        if (!read_trip_file(i, &t) || !t.completed) continue;
        trip_meta_t *m = &out[n];
        memset(m, 0, sizeof(*m));
        m->id = t.id;
        m->start_ms = t.start_ms;
        strncpy(m->label, t.label, sizeof(m->label) - 1);
        m->duration_seconds = t.duration_seconds;
        m->sample_count = t.sample_count;
        m->max_roll = t.max_roll; m->max_pitch = t.max_pitch;
        m->max_resultant_g = t.max_resultant_g; m->max_jerk_g = t.max_jerk_g;
        m->rsi_mean = t.rsi_mean; m->rsi_max = t.rsi_max; m->rsi_p95 = t.rsi_p95; m->rsi_trip = t.rsi_trip;
        m->mei_trip = t.mei_trip;
        memcpy(m->impact_counts, t.impact_counts, sizeof(t.impact_counts));
        memcpy(m->heatmap, t.heatmap, sizeof(t.heatmap));
        m->active = false;
        n++;
    }
    return n;
}

size_t store_trip_samples_binary(uint32_t trip_id, uint8_t *buf, size_t cap)
{
    char path[64];
    trip_path(trip_id, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

esp_err_t store_trip_heatmap(uint32_t trip_id, float *out400)
{
    trip_file_t t;
    if (!read_trip_file(trip_id, &t)) return ESP_ERR_NOT_FOUND;
    memcpy(out400, t.heatmap, sizeof(t.heatmap));
    return ESP_OK;
}

void store_live_heatmap(float *out400)
{
    memcpy(out400, s_heatmap, sizeof(s_heatmap));
}

int store_trip_impacts(uint32_t trip_id, impact_rec_t *out, int max)
{
    FILE *f = fopen(IMPACTS_FILE, "rb");
    if (!f) return 0;
    int n = 0;
    impact_file_rec_t r;
    while (fread(&r, sizeof(r), 1, f) == 1 && n < max) {
        if (r.trip_id != trip_id) continue;
        impact_rec_t *o = &out[n++];
        o->id = r.id; o->epoch_ms = r.epoch_ms; o->elapsed_ms = r.elapsed_ms;
        o->duration_ms = r.duration_ms;
        o->peak_x_g = r.peak_x_g; o->peak_y_g = r.peak_y_g; o->peak_z_g = r.peak_z_g;
        o->peak_resultant_g = r.peak_resultant_g; o->peak_jerk_g = r.peak_jerk_g;
        o->roll_deg = r.roll_deg; o->pitch_deg = r.pitch_deg; o->rsi = r.rsi; o->severity = r.severity;
    }
    fclose(f);
    return n;
}

bool store_clear_history(void)
{
    if (s_active_id) return false;
    for (uint32_t i = 1; i < 1000; i++) {
        char p[64];
        trip_path(i, p, sizeof(p)); remove(p);
        snprintf(p, sizeof(p), TRIPS_DIR "/trip_%03lu.meta", (unsigned long)i); remove(p);
    }
    remove(IMPACTS_FILE);
    return true;
}

static void iso_utc(int64_t ms, char *buf, size_t len)
{
    time_t s = (time_t)(ms / 1000);
    struct tm tmv;
    gmtime_r(&s, &tmv);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%S.000Z", &tmv);
}

esp_err_t store_trips_csv(char *buf, size_t cap, size_t *out_len)
{
    trip_meta_t trips[32];
    int n = store_list_trips(trips, 32);
    int w = snprintf(buf, cap, "trip_id,start_utc,end_utc,timezone,label,duration_s,samples,rsi_trip,mei_trip,max_roll_deg,max_pitch_deg,max_resultant_g\r\n");
    for (int i = 0; i < n && w < (int)cap - 128; i++) {
        char iso[32]; iso_utc(trips[i].start_ms, iso, sizeof(iso));
        w += snprintf(buf + w, cap - w, "%lu,%s,,%s,\"%s\",%lu,%lu,%.3f,%.3f,%.2f,%.2f,%.4f\r\n",
                      (unsigned long)trips[i].id, iso, "UTC", trips[i].label,
                      (unsigned long)trips[i].duration_seconds, (unsigned long)trips[i].sample_count,
                      trips[i].rsi_trip, trips[i].mei_trip, trips[i].max_roll, trips[i].max_pitch,
                      trips[i].max_resultant_g);
    }
    if (out_len) *out_len = w;
    return ESP_OK;
}

esp_err_t store_impacts_csv(char *buf, size_t cap, size_t *out_len)
{
    int w = snprintf(buf, cap, "trip_id,timestamp_utc,elapsed_ms,duration_ms,peak_x_g,peak_y_g,peak_z_g,peak_resultant_g,peak_jerk_g_s,roll_deg,pitch_deg,rsi,severity\r\n");
    FILE *f = fopen(IMPACTS_FILE, "rb");
    if (f) {
        impact_file_rec_t r;
        while (fread(&r, sizeof(r), 1, f) == 1 && w < (int)cap - 160) {
            char iso[32]; iso_utc(r.epoch_ms, iso, sizeof(iso));
            w += snprintf(buf + w, cap - w, "%lu,%s,%lu,%lu,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f,%.2f,%.3f,%.3f\r\n",
                          (unsigned long)r.trip_id, iso, (unsigned long)r.elapsed_ms, (unsigned long)r.duration_ms,
                          r.peak_x_g, r.peak_y_g, r.peak_z_g, r.peak_resultant_g, r.peak_jerk_g,
                          r.roll_deg, r.pitch_deg, r.rsi, r.severity);
        }
        fclose(f);
    }
    if (out_len) *out_len = w;
    return ESP_OK;
}
