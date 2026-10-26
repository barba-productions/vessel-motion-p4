/* HTTP + WebSocket telemetry server — see webserver.h. */
#include "webserver.h"
#include "storage.h"
#include "store.h"
#include "wifi_mgr.h"
#include "time_mgr.h"
#include "ota.h"
#include "settings.h"
#include "overview.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "web";
static httpd_handle_t s_server;
static SemaphoreHandle_t s_lock;
static telemetry_frame_t s_latest;
static char s_ws_json[1024];

#define WEB_ROOT "/flash"   /* LittleFS partition: dashboard assets (internal flash) */

/* forward declaration: minimal embedded dashboard fallback (defined below) */
static esp_err_t index_get(httpd_req_t *req);

/* Serialize one frame exactly like the Pi service.telemetry() JSON. */
static void build_json(const telemetry_frame_t *f, char *buf, size_t len)
{
    snprintf(buf, len,
             "{\"t\":%lld,\"roll\":%.3f,\"pitch\":%.3f,"
             "\"raw\":[%.4f,%.4f,%.4f],\"mapped\":[%.4f,%.4f,%.4f],"
             "\"gyro\":[%.4f,%.4f,%.4f],\"gravity\":[%.4f,%.4f,%.4f],"
             "\"a\":[%.4f,%.4f,%.4f],\"af\":%.4f,\"as\":%.4f,"
             "\"peak\":%.4f,\"rms\":%.4f,\"jerk\":%.4f,"
             "\"rates\":[%.3f,%.3f],"
             "\"rsi\":{\"components\":[%.4f,%.4f,%.4f,%.4f,%.4f],\"raw\":%.4f,\"current\":%.4f,\"trip\":%.4f},"
             "\"mei\":{\"rms\":%.4f,\"shock\":%.4f,\"dose\":%.4f,\"current\":%.4f,\"trip\":%.4f},"
             "\"impact\":%s,\"trip\":%s,\"tripSeconds\":%lu,\"tripStartMs\":%lld,"
             "\"hs\":[%ld,%ld,%ld,%ld,%ld,%ld],\"reset\":%lu,"
             "\"sensor\":%s,\"network\":true,\"timeVerified\":true,"
             "\"sampleHz\":%.1f,\"wsHz\":50,"
             "\"errors\":%lu,\"missed\":%lu,\"invalid\":%lu}",
             (long long)f->t_ms, (double)f->roll_deg, (double)f->pitch_deg,
             (double)f->raw[0], (double)f->raw[1], (double)f->raw[2],
             (double)f->mapped[0], (double)f->mapped[1], (double)f->mapped[2],
             (double)f->gyro[0], (double)f->gyro[1], (double)f->gyro[2],
             (double)f->gravity[0], (double)f->gravity[1], (double)f->gravity[2],
             (double)f->a[0], (double)f->a[1], (double)f->a[2],
             (double)f->af, (double)f->as,
             (double)f->peak_g, (double)f->rms_g, (double)f->jerk_gps,
             (double)f->rates_dps[0], (double)f->rates_dps[1],
             (double)f->rsi_components[0], (double)f->rsi_components[1],
             (double)f->rsi_components[2], (double)f->rsi_components[3],
             (double)f->rsi_components[4],
             (double)f->rsi_raw, (double)f->rsi_current, (double)f->rsi_trip,
             (double)f->mei_rms, (double)f->mei_shock, (double)f->mei_dose,
             (double)f->mei_current, (double)f->mei_trip,
             f->impact ? "true" : "false", f->trip ? "true" : "false",
             (unsigned long)f->trip_seconds, (long long)f->trip_start_ms,
             (long)f->hs[0], (long)f->hs[1], (long)f->hs[2],
             (long)f->hs[3], (long)f->hs[4], (long)f->hs[5],
             (unsigned long)f->reset_seq,
             f->sensor_ok ? "true" : "false",
             (double)f->sample_hz,
             (unsigned long)f->errors, (unsigned long)f->missed,
             (unsigned long)f->invalid);
}

/* GET /api/telemetry — single JSON snapshot */
static esp_err_t telemetry_get(httpd_req_t *req)
{
    char buf[1024];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    build_json(&s_latest, buf, sizeof(buf));
    xSemaphoreGive(s_lock);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

/* GET /health */
static esp_err_t health_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"status\":\"ok\",\"version\":\"0.1.7-p4\"}");
}

/* WebSocket /ws — clients register; server pushes at 50 Hz from push_telemetry */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WS client connected (fd=%d)", httpd_req_to_sockfd(req));
        return ESP_OK;  /* handshake complete */
    }
    httpd_ws_frame_t frame = { .type = HTTPD_WS_TYPE_TEXT };
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) return err;
    if (frame.len > 0) {
        uint8_t buf[64];
        frame.payload = buf;
        frame.len = frame.len < sizeof(buf) - 1 ? frame.len : sizeof(buf) - 1;
        if (httpd_ws_recv_frame(req, &frame, frame.len) == ESP_OK) {
            buf[frame.len] = 0;
            ESP_LOGI(TAG, "WS command: %s", buf);
            vessel_command((const char *)buf);
        }
    }
    return ESP_OK;
}

/* POST /api/command — body is the command text */
static esp_err_t command_post(httpd_req_t *req)
{
    char buf[64];
    int len = req->content_len < sizeof(buf) - 1 ? req->content_len : sizeof(buf) - 1;
    if (len <= 0 || httpd_req_recv(req, buf, len) <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
        return ESP_FAIL;
    }
    buf[len] = 0;
    esp_err_t err = vessel_command(buf);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown command");
        return ESP_FAIL;
    }
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/* GET /theme.js — window.vmUiSettings generated from live settings (Pi theme()). */
static esp_err_t theme_get(httpd_req_t *req)
{
    char *buf = malloc(1024);
    if (!buf) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    int w = snprintf(buf, 1024, "window.vmUiSettings={");
    const char *str_keys[] = { "productName", "vessel", "vesselModel", "timezone" };
    for (size_t i = 0; i < sizeof(str_keys) / sizeof(str_keys[0]); i++) {
        w += snprintf(buf + w, 1024 - w, "%s\"%s\":\"%s\"", i ? "," : "",
                      str_keys[i], settings_str(str_keys[i]));
    }
    const char *int_keys[] = {
        "productSize", "uiFont", "colorScheme",
        "originalScale", "originalVertical", "originalElevation", "originalYaw",
        "tugScale", "tugVertical", "tugElevation", "tugYaw",
        "yachtScale", "yachtVertical", "yachtElevation", "yachtYaw",
        "superyachtScale", "superyachtVertical", "superyachtElevation", "superyachtYaw",
        "garminRefresh",
    };
    for (size_t i = 0; i < sizeof(int_keys) / sizeof(int_keys[0]); i++) {
        w += snprintf(buf + w, 1024 - w, ",\"%s\":%d", int_keys[i],
                      (int)settings_num(int_keys[i]));
    }
    const char *bool_keys[] = { "fullscreen", "hideKioskCursor", "reloadOnTab" };
    for (size_t i = 0; i < sizeof(bool_keys) / sizeof(bool_keys[0]); i++) {
        w += snprintf(buf + w, 1024 - w, ",\"%s\":%s", bool_keys[i],
                      settings_bool(bool_keys[i]) ? "true" : "false");
    }
    w += snprintf(buf + w, 1024 - w, "};");
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, buf, w);
    free(buf);
    return r;
}

/* GET /api/trips — list completed trips */
static esp_err_t trips_get(httpd_req_t *req)
{
    /* trip_meta_t carries a 400-float heatmap (~1.7 KB each) — heap, not stack.
     * 100 trips land in PSRAM (CONFIG_SPIRAM_USE_MALLOC). */
    trip_meta_t *trips = malloc(100 * sizeof(trip_meta_t));
    char *buf = malloc(24576);
    if (!trips || !buf) {
        free(trips); free(buf);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }
    int n = store_list_trips(trips, 100);
    int w = snprintf(buf, 24576, "{\"trips\":[");
    for (int i = 0; i < n; i++) {
        w += snprintf(buf + w, 24576 - w,
                      "%s{\"id\":%lu,\"label\":\"%s\",\"startMs\":%lld,\"timeVerified\":true,"
                      "\"durationSeconds\":%lu,\"samples\":%lu,\"rsiTrip\":%.3f,\"meiTrip\":%.3f}",
                      i ? "," : "", (unsigned long)trips[i].id, trips[i].label,
                      (long long)trips[i].start_ms, (unsigned long)trips[i].duration_seconds,
                      (unsigned long)trips[i].sample_count, trips[i].rsi_trip, trips[i].mei_trip);
    }
    w += snprintf(buf + w, 24576 - w,
                  "],\"maximum\":100,\"sampleIntervalSeconds\":0.2,\"maximumDurationSeconds\":28800}");
    free(trips);
    httpd_resp_set_type(req, "application/json");
    esp_err_t r = httpd_resp_send(req, buf, w);
    free(buf);
    return r;
}

/* GET /api/trip/samples?id=N — 16-byte binary records */
static esp_err_t trip_samples_get(httpd_req_t *req)
{
    char q[32];
    uint32_t id = 0;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        char v[16];
        if (httpd_query_key_value(q, "id", v, sizeof(v)) == ESP_OK) id = atoi(v);
    }
    uint8_t *buf = malloc(64 * 1024);
    if (!buf) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    size_t n = store_trip_samples_binary(id, buf, 64 * 1024);
    httpd_resp_set_type(req, "application/octet-stream");
    esp_err_t r = httpd_resp_send(req, (const char *)buf, n);
    free(buf);
    return r;
}

/* GET /api/trip/detail?id=N — full trip JSON (Pi trip_detail). */
static esp_err_t trip_detail_get(httpd_req_t *req)
{
    char q[32];
    uint32_t id = 0;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        char v[16];
        if (httpd_query_key_value(q, "id", v, sizeof(v)) == ESP_OK) id = atoi(v);
    }
    trip_meta_t m;
    if (!store_trip_meta(id, &m)) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Trip not found");
    }
    impact_rec_t *impacts = malloc(64 * sizeof(impact_rec_t));
    if (!impacts) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    int ni = store_trip_impacts(id, impacts, 64);

    char *buf = malloc(12288);
    if (!buf) { free(impacts); return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); }
    int cap = 12288;
    int w = snprintf(buf, cap,
        "{\"id\":%lu,\"label\":\"%s\",\"durationSeconds\":%lu,\"samples\":%lu,"
        "\"headerBytes\":0,\"algorithmVersion\":%d,"
        "\"maxRoll\":%.3f,\"maxPitch\":%.3f,"
        "\"maxAxisG\":[%.4f,%.4f,%.4f],\"maxResultantG\":%.4f,\"maxJerkG\":%.4f,"
        "\"rsiMean\":%.4f,\"rsiMax\":%.4f,\"rsiP95\":%.4f,"
        "\"rsiTrip\":%.4f,\"meiTrip\":%.4f,\"impactCounts\":[%lu,%lu,%lu],\"impactMarkers\":[",
        (unsigned long)m.id, m.label, (unsigned long)m.duration_seconds,
        (unsigned long)m.sample_count, m.algorithm_version,
        (double)m.max_roll, (double)m.max_pitch,
        (double)m.max_axis_g[0], (double)m.max_axis_g[1], (double)m.max_axis_g[2],
        (double)m.max_resultant_g, (double)m.max_jerk_g,
        (double)m.rsi_mean, (double)m.rsi_max, (double)m.rsi_p95,
        (double)m.rsi_trip, (double)m.mei_trip,
        (unsigned long)m.impact_counts[0], (unsigned long)m.impact_counts[1],
        (unsigned long)m.impact_counts[2]);
    for (int i = 0; i < ni && w < cap - 128; i++) {
        w += snprintf(buf + w, cap - w, "%s{\"elapsedMs\":%lu,\"severity\":%.4f,\"peakG\":%.4f}",
                      i ? "," : "", (unsigned long)impacts[i].elapsed_ms,
                      (double)impacts[i].severity, (double)impacts[i].peak_resultant_g);
    }
    w += snprintf(buf + w, cap - w, "],\"heatmap\":[");
    for (int i = 0; i < 400 && w < cap - 16; i++) {
        w += snprintf(buf + w, cap - w, "%s%.2f", i ? "," : "", (double)m.heatmap[i]);
    }
    w += snprintf(buf + w, cap - w, "]}");
    free(impacts);
    httpd_resp_set_type(req, "application/json");
    esp_err_t r = httpd_resp_send(req, buf, w);
    free(buf);
    return r;
}

/* GET /api/trip/current — active-trip info (Pi current_trip). */
static esp_err_t trip_current_get(httpd_req_t *req)
{
    char buf[320];
    if (!store_trip_active()) {
        snprintf(buf, sizeof(buf), "{\"active\":false}");
    } else {
        char label[40];
        store_active_trip_label(label, sizeof(label));
        int64_t mono = esp_timer_get_time() / 1000 - store_active_trip_start_mono_ms();
        snprintf(buf, sizeof(buf),
                 "{\"active\":true,\"id\":%lu,\"durationSeconds\":%lu,\"samples\":%lu,"
                 "\"headerBytes\":0,\"sampleIntervalSeconds\":0.2,\"startMs\":%lld,\"label\":\"%s\"}",
                 (unsigned long)store_active_trip_id(),
                 (unsigned long)(mono > 0 ? mono / 1000 : 0),
                 (unsigned long)store_active_trip_samples(),
                 (long long)store_active_trip_start_ms(), label);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, buf);
}

/* GET /api/trip/current-samples — binary samples of the active trip (404 when idle). */
static esp_err_t trip_current_samples_get(httpd_req_t *req)
{
    if (!store_trip_active()) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No active trip");
    }
    uint8_t *buf = malloc(64 * 1024);
    if (!buf) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    size_t n = store_trip_samples_binary(store_active_trip_id(), buf, 64 * 1024);
    httpd_resp_set_type(req, "application/octet-stream");
    char hdr[24];
    snprintf(hdr, sizeof(hdr), "%u", (unsigned)(n / 16));
    httpd_resp_set_hdr(req, "X-Trip-Samples", hdr);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, (const char *)buf, n);
    free(buf);
    return r;
}

/* GET /api/overview-samples — VMOVR2 binary rolling history (Pi overview_samples). */
static esp_err_t overview_samples_get(httpd_req_t *req)
{
    uint8_t *buf = malloc(OVERVIEW_MAX_BYTES);
    if (!buf) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    size_t n = overview_write_binary(buf, OVERVIEW_MAX_BYTES);
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, (const char *)buf, n);
    free(buf);
    return r;
}

/* GET /api/settings — full settings + computed extras (Pi get_settings). */
static esp_err_t settings_get_handler(httpd_req_t *req)
{
    char *js = settings_json_string_public(1 /* AlgorithmConfig.version */);
    if (!js) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, js, strlen(js));
    free(js);
    return r;
}

/* apply side effects after settings change (motion cfg, timezone) */
static void settings_applied(void)
{
    time_set_timezone(settings_str("timezone"));
    vessel_command("__apply");   /* re-read settings into the motion core */
}

/* PATCH /api/settings — JSON merge (Pi patch_settings). */
static esp_err_t settings_patch_handler(httpd_req_t *req)
{
    char *body = malloc(req->content_len + 1);
    if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    int got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) { free(body); return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv"); }
        got += r;
    }
    body[got] = 0;
    char err[128] = { 0 };
    esp_err_t rc = settings_patch_json(body, err, sizeof(err));
    free(body);
    if (rc != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, err[0] ? err : "bad settings");
    }
    settings_applied();
    return settings_get_handler(req);   /* Pi returns storage.settings() (no extras) */
}

/* POST /settings + /advanced-settings — classic form submit, 303 redirect (Pi save_settings_page). */
static esp_err_t settings_form_post(httpd_req_t *req)
{
    char *body = malloc(req->content_len + 1);
    if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    int got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) break;
        got += r;
    }
    body[got] = 0;
    bool to_advanced = strstr(body, "returnTo=advanced") != NULL;
    /* the form submits the visible tab in activeTab — return the user to it */
    char tab[24] = { 0 };
    char *at = strstr(body, "activeTab=");
    if (at) {
        at += strlen("activeTab=");
        char *e = strchr(at, '&');
        size_t n = e ? (size_t)(e - at) : strlen(at);
        if (n >= sizeof(tab)) n = sizeof(tab) - 1;
        memcpy(tab, at, n);
        tab[n] = 0;
        for (char *c = tab; *c; c++) {   /* sanitize: lowercase alnum only */
            if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9'))) { *c = 0; break; }
        }
    }
    settings_update_form(body);
    free(body);
    settings_applied();
    char loc[64];
    snprintf(loc, sizeof(loc), "%s%s%s", to_advanced ? "/advanced-settings" : "/settings",
             tab[0] ? "#" : "", tab);
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", loc);
    return httpd_resp_send(req, NULL, 0);
}

/* GET /api/timezones — curated IANA list (Pi returns the host zoneinfo list). */
static esp_err_t timezones_get(httpd_req_t *req)
{
    static const char *zones[] = {
        "UTC", "Africa/Cairo", "Africa/Johannesburg", "America/Anchorage",
        "America/Argentina/Buenos_Aires", "America/Bogota", "America/Chicago",
        "America/Denver", "America/Halifax", "America/Los_Angeles", "America/Mexico_City",
        "America/New_York", "America/Phoenix", "America/Santiago", "America/Sao_Paulo",
        "America/St_Johns", "America/Toronto", "America/Vancouver", "Asia/Dubai",
        "Asia/Hong_Kong", "Asia/Jakarta", "Asia/Jerusalem", "Asia/Karachi",
        "Asia/Kolkata", "Asia/Kuala_Lumpur", "Asia/Manila", "Asia/Riyadh",
        "Asia/Seoul", "Asia/Shanghai", "Asia/Singapore", "Asia/Taipei", "Asia/Tehran",
        "Asia/Tokyo", "Atlantic/Azores", "Atlantic/Reykjavik", "Australia/Brisbane",
        "Australia/Melbourne", "Australia/Perth", "Australia/Sydney", "Europe/Amsterdam",
        "Europe/Athens", "Europe/Berlin", "Europe/Brussels", "Europe/Copenhagen",
        "Europe/Dublin", "Europe/Helsinki", "Europe/Istanbul", "Europe/Lisbon",
        "Europe/London", "Europe/Madrid", "Europe/Moscow", "Europe/Oslo",
        "Europe/Paris", "Europe/Rome", "Europe/Stockholm", "Europe/Warsaw",
        "Europe/Zurich", "Pacific/Auckland", "Pacific/Fiji", "Pacific/Guam",
        "Pacific/Honolulu", "Pacific/Tahiti",
    };
    char *buf = malloc(2048);
    if (!buf) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    int w = snprintf(buf, 2048, "{\"timezones\":[\"%s\"", zones[0]);
    for (size_t i = 1; i < sizeof(zones) / sizeof(zones[0]); i++) {
        w += snprintf(buf + w, 2048 - w, ",\"%s\"", zones[i]);
    }
    w += snprintf(buf + w, 2048 - w, "]}");
    httpd_resp_set_type(req, "application/json");
    esp_err_t r = httpd_resp_send(req, buf, w);
    free(buf);
    return r;
}

/* GET /garmin/config.json — Garmin watch-app descriptor (Pi garmin_config). */
static esp_err_t garmin_config_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req,
        "{\"id\":\"vessel-motion-pi\",\"path\":\"/web2/screen.html\","
        "\"title\":\"Vessel Motion\",\"icon\":\"/web2/garmin.png\"}");
}

/* Maintenance endpoints — kiosk unlock/PIN are Pi-display concepts; the P4 has
 * no kiosk, so these are accepted no-ops to keep the settings UI working. */
static esp_err_t maintenance_unlock_post(httpd_req_t *req)
{
    char drain[128];
    int left = req->content_len;
    while (left > 0) {
        int r = httpd_req_recv(req, drain, left < (int)sizeof(drain) ? left : (int)sizeof(drain));
        if (r <= 0) break;
        left -= r;
    }
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t maintenance_pin_put(httpd_req_t *req)
{
    return maintenance_unlock_post(req);   /* drain body, 204 */
}

/* GET /api/trip/heatmap — live 1612-byte snapshot (Pi semantics: always 200).
 * With ?id=N it serves the STORED heatmap of a completed trip instead. */
static esp_err_t trip_heatmap_get(httpd_req_t *req)
{
    char q[40];
    uint32_t id = 0;
    bool have_id = false;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        char v[16];
        if (httpd_query_key_value(q, "id", v, sizeof(v)) == ESP_OK) {
            id = atoi(v);
            have_id = true;
        }
    }
    float heat[400];
    bool active = store_trip_active();
    if (have_id) {
        if (store_trip_heatmap(id, heat) != ESP_OK) {
            return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no trip");
        }
    } else {
        store_live_heatmap(heat);
        id = active ? store_active_trip_id() : 0;
    }
    uint8_t header[12];
    header[0] = 1;                    /* version */
    header[1] = active ? 1 : 0;
    uint16_t count = 400;
    memcpy(header + 2, &count, 2);
    memcpy(header + 4, &id, 4);
    float total = 0;
    for (int i = 0; i < 400; i++) total += heat[i];
    memcpy(header + 8, &total, 4);

    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_send_chunk(req, (const char *)header, 12);
    httpd_resp_send_chunk(req, (const char *)heat, sizeof(heat));
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* DELETE /api/history */
static esp_err_t history_delete(httpd_req_t *req)
{
    if (!store_clear_history()) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "trip active", HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/* mkdir -p for the parent directories of a file path */
static void make_parent_dirs(const char *filepath)
{
    char tmp[300];
    strlcpy(tmp, filepath, sizeof(tmp));
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0775);
            *p = '/';
        }
    }
}

/* in-place percent-decoder for query values (%XX -> byte, '+' -> space) */
static int hex_val(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c |= 0x20;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void url_decode_inplace(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (*r == '%' && r[1] && r[2]) {
            int hi = hex_val(r[1]);
            int lo = hex_val(r[2]);
            if (hi >= 0 && lo >= 0) {
                *w++ = (char)((hi << 4) | lo);
                r += 3;
                continue;
            }
        }
        *w++ = (*r == '+') ? ' ' : *r;
        r++;
    }
    *w = 0;
}

/* POST /api/upload?path=<relative> — write body to /flash/<relative>.
 * Used to stage the dashboard assets into the LittleFS web partition over LAN. */
static esp_err_t upload_post(httpd_req_t *req)
{
    char q[160];
    char rel[128] = { 0 };
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        httpd_query_key_value(q, "path", rel, sizeof(rel));
    }
    url_decode_inplace(rel);   /* query arrives percent-encoded (%2F = '/') */
    if (!rel[0] || strstr(rel, "..") || rel[0] == '/' || strchr(rel, '\\')) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
    }
    char path[300];
    snprintf(path, sizeof(path), "%s/%s", WEB_ROOT, rel);
    make_parent_dirs(path);

    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "upload: open failed %s", path);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "open failed");
    }
    char *buf = malloc(4096);
    if (!buf) { fclose(f); return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); }

    int remaining = req->content_len;
    esp_err_t wr_err = ESP_OK;
    while (remaining > 0) {
        int want = remaining < 4096 ? remaining : 4096;
        int r = httpd_req_recv(req, buf, want);
        if (r <= 0) { wr_err = ESP_FAIL; break; }
        if (fwrite(buf, 1, r, f) != (size_t)r) { wr_err = ESP_FAIL; break; }  /* fail loudly */
        remaining -= r;
    }
    fclose(f);
    free(buf);
    if (wr_err != ESP_OK) {
        remove(path);
        ESP_LOGE(TAG, "upload WRITE FAILED %s", rel);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed");
    }
    ESP_LOGI(TAG, "uploaded %s (%d bytes)", rel, req->content_len);
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/* GET /api/storage — web LittleFS + SD logging-data capacity. */
static esp_err_t storage_get(httpd_req_t *req)
{
    size_t total = 0, used = 0;
    esp_err_t err = esp_littlefs_info("web", &total, &used);
    uint64_t sd_total = 0, sd_free = 0;
    bool sd_ok = storage_sd_info(&sd_total, &sd_free) == ESP_OK;
    char buf[400];
    if (err == ESP_OK) {
        snprintf(buf, sizeof(buf),
                 "{\"fs\":\"littlefs\",\"total\":%u,\"used\":%u,\"free\":%u,"
                 "\"logs\":{\"mounted\":%s,\"totalBytes\":%llu,\"usedBytes\":%llu,\"freeBytes\":%llu}}",
                 (unsigned)total, (unsigned)used, (unsigned)(total - used),
                 sd_ok ? "true" : "false",
                 (unsigned long long)sd_total, (unsigned long long)(sd_total - sd_free),
                 (unsigned long long)sd_free);
    } else {
        snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", esp_err_to_name(err));
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

/* GET /api/list — dump the web filesystem tree (diagnostic). */
static esp_err_t list_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    /* iterative walk to depth 3 */
    char *roots[16];
    int depth[16];
    int n = 0;
    roots[n] = strdup(""); depth[n] = 0; n++;
    while (n > 0) {
        n--;
        char *cur = roots[n];
        int d = depth[n];
        char dirpath[384];
        snprintf(dirpath, sizeof(dirpath), "/flash/%s", cur);
        DIR *dir = opendir(dirpath);
        if (!dir) { free(cur); continue; }
        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            char child[512];
            snprintf(child, sizeof(child), "%s%s%s", cur, (cur[0] ? "/" : ""), de->d_name);
            if (de->d_type == DT_DIR) {
                if (n < 16 && d < 3) { roots[n] = strdup(child); depth[n] = d + 1; n++; }
                char line[600];
                int w = snprintf(line, sizeof(line), "DIR   %s\n", child);
                httpd_resp_send_chunk(req, line, w);
            } else {
                char full[600];
                snprintf(full, sizeof(full), "/flash/%s", child);
                struct stat st;
                long sz = stat(full, &st) == 0 ? (long)st.st_size : -1;
                char line[700];
                int w = snprintf(line, sizeof(line), "FILE  %s  %ld\n", child, sz);
                httpd_resp_send_chunk(req, line, w);
            }
        }
        closedir(dir);
        free(cur);
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* POST /api/format — erase and reformat the web partition (clears all assets). */
static esp_err_t format_post(httpd_req_t *req)
{
    esp_err_t err = esp_littlefs_format("web");
    httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "format failed");
    }
    return httpd_resp_sendstr(req, "{\"formatted\":true}");
}

/* --- Phase 3: Wi-Fi --- */
static esp_err_t wifi_status_get(httpd_req_t *req)
{
    char ip[16] = "";
    wifi_sta_ip(ip, sizeof(ip));
    char buf[320];
    snprintf(buf, sizeof(buf),
             "{\"connected\":%s,\"connecting\":false,\"ssid\":\"%s\",\"accessPoint\":%s,"
             "\"accessPointSsid\":\"%s\",\"accessPointAddress\":\"http://%s\",\"ip\":\"%s\",\"internet\":%s}",
             wifi_is_connected() ? "true" : "false", "",
             wifi_is_fallback_active() ? "true" : "false",
             WIFI_FALLBACK_SSID, WIFI_FALLBACK_IP, ip,
             wifi_is_connected() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

static esp_err_t wifi_scan_get(httpd_req_t *req)
{
    wifi_network_t nets[24];
    int n = wifi_scan(nets, 24);
    char *buf = malloc(2048);
    if (!buf) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    int w = snprintf(buf, 2048, "{\"networks\":[");
    for (int i = 0; i < n; i++) {
        w += snprintf(buf + w, 2048 - w, "%s{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":%s}",
                      i ? "," : "", nets[i].ssid, nets[i].rssi, nets[i].secure ? "true" : "false");
    }
    w += snprintf(buf + w, 2048 - w, "]}");
    httpd_resp_set_type(req, "application/json");
    esp_err_t r = httpd_resp_send(req, buf, w);
    free(buf);
    return r;
}

static esp_err_t wifi_connect_post(httpd_req_t *req)
{
    char body[160];
    int len = req->content_len < sizeof(body) - 1 ? req->content_len : sizeof(body) - 1;
    if (len <= 0 || httpd_req_recv(req, body, len) <= 0)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
    body[len] = 0;
    char ssid[33] = { 0 }, pass[65] = { 0 };
    /* minimal JSON field extraction */
    char *p = strstr(body, "\"ssid\"");
    if (p) { p = strchr(strchr(p, ':') + 1, '"'); if (p) { p++; char *e = strchr(p, '"'); if (e) { size_t n = e - p < 32 ? e - p : 32; memcpy(ssid, p, n); } } }
    p = strstr(body, "\"password\"");
    if (p) { p = strchr(strchr(p, ':') + 1, '"'); if (p) { p++; char *e = strchr(p, '"'); if (e) { size_t n = e - p < 64 ? e - p : 64; memcpy(pass, p, n); } } }
    if (!ssid[0]) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
    esp_err_t err = vm_wifi_connect(ssid, pass[0] ? pass : NULL);
    httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "connect failed");
    httpd_resp_set_status(req, "202 Accepted");
    return httpd_resp_sendstr(req, "{\"connecting\":true}");
}

static esp_err_t wifi_disconnect_post(httpd_req_t *req)
{
    vm_wifi_disconnect();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"connected\":false}");
}

static esp_err_t wifi_clear_post(httpd_req_t *req)
{
    wifi_forget();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"connected\":false}");
}

/* --- Phase 3: Time --- */
static esp_err_t time_get(httpd_req_t *req)
{
    char local[32];
    time_local_iso(local, sizeof(local));
    char buf[320];
    snprintf(buf, sizeof(buf),
             "{\"timezone\":\"%s\",\"localTime\":\"%s\",\"current\":\"%s\",\"lastSync\":\"%s\","
             "\"syncing\":false,\"epochMs\":%lld,\"rtc\":%s,\"synced\":%s,\"syncCount\":%lu}",
             time_timezone(), local, local, time_sync_count() ? "SNTP" : "never",
             (long long)time_epoch_ms(),
             time_rtc_present() ? "true" : "false",
             time_is_synchronized() ? "true" : "false",
             (unsigned long)time_sync_count());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

/* POST /api/time/sync — force an immediate internet NTP sync. */
static esp_err_t time_sync_post(httpd_req_t *req)
{
    time_sync_now();
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"syncing\":true}");
}

static esp_err_t time_put(httpd_req_t *req)
{
    char body[160];
    int len = req->content_len < sizeof(body) - 1 ? req->content_len : sizeof(body) - 1;
    if (len <= 0 || httpd_req_recv(req, body, len) <= 0)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
    body[len] = 0;
    char val[64] = { 0 };
    char *p = strstr(body, "\"timezone\"");
    if (p) {
        p = strchr(strchr(p, ':') + 1, '"');
        if (p) { p++; char *e = strchr(p, '"'); if (e) { size_t n = e - p < 63 ? e - p : 63; memcpy(val, p, n); } }
        if (val[0]) {
            time_set_timezone(val);
            /* keep the settings store in sync: settings_applied() would otherwise
             * restore the stale settings timezone on the next settings save */
            char patch[96], perr[64];
            snprintf(patch, sizeof(patch), "{\"timezone\":\"%s\"}", val);
            settings_patch_json(patch, perr, sizeof(perr));
        }
    }
    p = strstr(body, "\"localTime\"");
    if (p) {
        p = strchr(strchr(p, ':') + 1, '"');
        if (p) { p++; char *e = strchr(p, '"'); if (e) { size_t n = e - p < 63 ? e - p : 63; memcpy(val, p, n); val[n] = 0; } }
        if (val[0]) time_set_local_iso(val);
    }
    return time_get(req);
}

/* POST /api/time — raw ASCII epoch-ms body (Pi set_time_compatibility). */
static esp_err_t time_post(httpd_req_t *req)
{
    char body[24];
    int len = req->content_len < sizeof(body) - 1 ? req->content_len : sizeof(body) - 1;
    if (len <= 0 || httpd_req_recv(req, body, len) <= 0)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty");
    body[len] = 0;
    int64_t epoch = strtoll(body, NULL, 10);
    if (epoch <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad epoch");
    time_set_epoch_ms(epoch);
    return time_get(req);
}

/* --- Phase 3: OTA --- */
static esp_err_t ota_status_get(httpd_req_t *req)
{
    ota_status_t s;
    ota_snapshot(&s);
    char buf[384];
    snprintf(buf, sizeof(buf),
             "{\"state\":\"%s\",\"currentVersion\":\"%s\",\"availableVersion\":\"%s\","
             "\"message\":\"%s\",\"received\":%lu,\"total\":%lu,\"progress\":%d}",
             s.state, s.current_version, s.available_version, s.message,
             (unsigned long)s.received, (unsigned long)s.total, s.progress);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

static esp_err_t ota_start_post(httpd_req_t *req)
{
    char body[256];
    int len = req->content_len < sizeof(body) - 1 ? req->content_len : sizeof(body) - 1;
    const char *url = "https://raw.githubusercontent.com/barba-productions/vessel-motion-p4-OTA/main/stable/manifest.json";
    char urlbuf[256];
    strlcpy(urlbuf, url, sizeof(urlbuf));
    if (len > 0 && httpd_req_recv(req, body, len) > 0) {
        body[len] = 0;
        char *p = strstr(body, "\"url\"");
        if (p) { p = strchr(strchr(p, ':') + 1, '"'); if (p) { p++; char *e = strchr(p, '"'); if (e) { size_t n = e - p < 255 ? e - p : 255; memcpy(urlbuf, p, n); urlbuf[n] = 0; } } }
    }
    esp_err_t err = ota_start(urlbuf);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "{\"error\":\"update already running\"}");
    }
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    return ota_status_get(req);
}

/* CSV exports — stream to a temp file on SD then send */
static esp_err_t export_csv(httpd_req_t *req, bool trips)
{
    char *buf = malloc(32 * 1024);
    if (!buf) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    size_t len = 0;
    if (trips) store_trips_csv(buf, 32 * 1024, &len);
    else store_impacts_csv(buf, 32 * 1024, &len);
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       trips ? "attachment; filename=trips.csv" : "attachment; filename=impacts.csv");
    esp_err_t r = httpd_resp_send(req, buf, len);
    free(buf);
    return r;
}
static esp_err_t trips_csv_get(httpd_req_t *req) { return export_csv(req, true); }
static esp_err_t impacts_csv_get(httpd_req_t *req) { return export_csv(req, false); }

/* Serve dashboard static assets from SD card WEB_ROOT */
static const char *mime_for(const char *path)
{
    const char *e = strrchr(path, '.');
    if (!e) return "application/octet-stream";
    if (!strcasecmp(e, ".html")) return "text/html";
    if (!strcasecmp(e, ".js")) return "application/javascript";
    if (!strcasecmp(e, ".css")) return "text/css";
    if (!strcasecmp(e, ".json")) return "application/json";
    if (!strcasecmp(e, ".png")) return "image/png";
    if (!strcasecmp(e, ".jpg") || !strcasecmp(e, ".jpeg")) return "image/jpeg";
    if (!strcasecmp(e, ".svg")) return "image/svg+xml";
    if (!strcasecmp(e, ".woff2")) return "font/woff2";
    if (!strcasecmp(e, ".glb")) return "model/gltf-binary";
    if (!strcasecmp(e, ".ico")) return "image/x-icon";
    return "application/octet-stream";
}

static esp_err_t static_get(httpd_req_t *req)
{
    char path[768];
    const char *uri = req->uri;
    if (strcmp(uri, "/") == 0) uri = "/index.html";
    /* prevent traversal */
    if (strstr(uri, "..")) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
    size_t uri_len = strlen(uri);
    /* strip query string (cache-bust ?v=... etc.) — req->uri includes it */
    const char *qmark = memchr(uri, '?', uri_len);
    if (qmark) uri_len = qmark - uri;
    size_t root_len = strlen(WEB_ROOT);
    if (uri_len + root_len >= sizeof(path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "path too long");
    }
    memcpy(path, WEB_ROOT, root_len);
    memcpy(path + root_len, uri, uri_len);
    path[root_len + uri_len] = 0;

    FILE *f = fopen(path, "rb");
    if (!f && !memchr(uri, '.', uri_len) && uri_len + root_len + 5 < sizeof(path)) {
        /* extensionless page route (Pi maps /diagnostics -> diagnostics.html) */
        memcpy(path + root_len + uri_len, ".html", 6);
        f = fopen(path, "rb");
    }
    if (!f) {
        /* fall back to minimal embedded dashboard */
        return index_get(req);
    }
    httpd_resp_set_type(req, mime_for(path));
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    /* heap-allocate: a 4KB stack buffer overflowed the httpd task */
    char *chunk = malloc(2048);
    if (!chunk) {
        fclose(f);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }
    size_t n;
    while ((n = fread(chunk, 1, 2048, f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) break;
    }
    free(chunk);
    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* Minimal placeholder dashboard (served only when /flash/index.html is absent). */
static esp_err_t index_get(httpd_req_t *req)
{
    const char *html =
        "<!doctype html><html><head><meta charset='utf-8'><title>Vessel Motion P4</title>"
        "<meta http-equiv='refresh' content='1'></head><body style='font-family:monospace;background:#111;color:#0f0'>"
        "<h2>Vessel Motion P4</h2>"
        "<p>Live telemetry: <a href='/api/telemetry' style='color:#0ff'>/api/telemetry</a></p>"
        "<pre id='t'>connecting...</pre>"
        "<script>var ws=new WebSocket('ws://'+location.host+'/ws');"
        "ws.onmessage=function(e){document.getElementById('t').textContent=e.data;};"
        "ws.onclose=function(){setTimeout(function(){location.reload()},1000);};</script>"
        "</body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, html);
}

/* ---- Pi-compatible template rendering (api.py render_shared_header) ---- */

#define P4_VERSION "1.0.4"

static char *read_text_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 128 * 1024) { fclose(f); return NULL; }
    char *buf = malloc(n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    buf[n] = 0;
    fclose(f);
    return buf;
}

/* replace every occurrence of `from` in `in` with `to`; returns malloc'd string */
static char *replace_all(const char *in, const char *from, const char *to)
{
    size_t from_len = strlen(from), to_len = strlen(to), in_len = strlen(in);
    size_t count = 0;
    const char *p = in;
    while ((p = strstr(p, from))) { count++; p += from_len; }
    char *out = malloc(in_len + count * to_len - count * from_len + 1);
    if (!out) return NULL;
    char *w = out;
    p = in;
    const char *m;
    while ((m = strstr(p, from))) {
        size_t pre = m - p;
        memcpy(w, p, pre); w += pre;
        memcpy(w, to, to_len); w += to_len;
        p = m + from_len;
    }
    strcpy(w, p);
    return out;
}

/* render _header.html with the dashboard/settings substitutions (Pi render_shared_header) */
static char *render_header(bool dashboard)
{
    char *h = read_text_file(WEB_ROOT "/_header.html");
    if (!h) return NULL;
    const char *attr = dashboard ? "data-view" : "data-dashboard-view";
    const char *names[] = { "overview", "attitude", "trends", "heatmap", "trip_history" };
    for (int i = 0; i < 5; i++) {
        const char *route = names[i];
        if (i == 4) route = "trip-history";
        if (i == 2 && !dashboard) route = "trends/acceleration";
        char ph[40], rep[96];
        snprintf(ph, sizeof(ph), "{{%s_attributes}}", names[i]);
        snprintf(rep, sizeof(rep), "%s%s=\"%s\"",
                 (dashboard && i == 0) ? "class=\"active\" " : "", attr, route);
        char *nh = replace_all(h, ph, rep);
        free(h);
        if (!nh) return NULL;
        h = nh;
    }
    char *nh = replace_all(h, "{{connection_id}}", dashboard ? "dashboardConnection" : "settingsConnection");
    free(h); h = nh;
    nh = h ? replace_all(h, "{{status_dot_id}}", dashboard ? "statusDot" : "settingsStatusDot") : NULL;
    free(h);
    return nh;
}

/* GET / — index.html with rendered shared header + cache-busted assets (Pi index()) */
static esp_err_t dash_index_get(httpd_req_t *req)
{
    char *content = read_text_file(WEB_ROOT "/index.html");
    if (!content) return index_get(req);   /* embedded fallback */
    char *header = render_header(true);
    if (header) {
        char *nc = replace_all(content, "{{shared_header}}", header);
        free(content); free(header);
        content = nc;
        if (!content) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }
    const char *assets[] = { "app.css", "large-type.css", "header.css", "app.js" };
    for (int i = 0; i < 4; i++) {
        char from[32], to[56];
        snprintf(from, sizeof(from), "\"/%s\"", assets[i]);
        snprintf(to, sizeof(to), "\"/%s?v=%s\"", assets[i], P4_VERSION);
        char *nc = replace_all(content, from, to);
        free(content);
        if (!nc) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        content = nc;
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, content, strlen(content));
    free(content);
    return r;
}

/* GET /settings and /advanced-settings — Pi settings_page() template rendering */
static esp_err_t settings_get(httpd_req_t *req)
{
    bool advanced = strstr(req->uri, "advanced") != NULL;
    char *content = read_text_file(WEB_ROOT "/settings.html");
    if (!content) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no settings page");

    /* splice: <div class="topbar">...</div> up to settings-tabs nav -> {{shared_header}} */
    char *ts = strstr(content, "<div class=\"topbar\">");
    char *te = ts ? strstr(ts, "<nav class=\"settings-tabs\"") : NULL;
    if (ts && te) {
        const char *mid = "<div class=\"topbar\">{{shared_header}}</div>";
        size_t head_len = ts - content, mid_len = strlen(mid), tail_len = strlen(te);
        char *nc = malloc(head_len + mid_len + tail_len + 1);
        if (!nc) { free(content); return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); }
        memcpy(nc, content, head_len);
        memcpy(nc + head_len, mid, mid_len);
        memcpy(nc + head_len + mid_len, te, tail_len + 1);
        free(content);
        content = nc;
    }
    char *header = render_header(false);
    if (header) {
        char *nc = replace_all(content, "{{shared_header}}", header);
        free(content); free(header);
        content = nc;
        if (!content) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }
    /* header.css link before </head> */
    {
        char *nc = replace_all(content, "</head>",
                               "<link rel=\"stylesheet\" href=\"/header.css?v=" P4_VERSION "\"></head>");
        free(content);
        if (!nc) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        content = nc;
    }
    /* scripts before </body> (Pi appends settings-pi.js only on /settings) */
    const char *dom_patch =
        "<script>addEventListener(\"DOMContentLoaded\",function(){"
        "if(location.pathname===\"/advanced-settings\")"
        "document.querySelectorAll(\"[data-tab=general],[data-tab=motion],"
        "[data-panel=general],[data-panel=motion]\").forEach(function(e){e.remove()});"
        "var s=document.querySelector('select[name=\"vesselModel\"]');"
        "if(s&&!s.querySelector('option[value=\"superyacht\"]')){"
        "var o=document.createElement(\"option\");o.value=\"superyacht\";"
        "o.textContent=\"SUPERYACHT\";s.appendChild(o)}})</script>";
    const char *settings_js = "<script src=\"/settings-pi.js\"></script>";
    size_t tail_len = strlen(dom_patch) + (advanced ? 0 : strlen(settings_js));
    char *tail = malloc(tail_len + 1);
    if (!tail) { free(content); return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); }
    tail[0] = 0;
    if (!advanced) strcpy(tail, settings_js);
    strcat(tail, dom_patch);
    char *body_close = strstr(content, "</body>");
    if (body_close) {
        size_t pre = body_close - content;
        size_t new_len = pre + strlen(tail) + strlen(body_close);
        char *nc = malloc(new_len + 1);
        if (!nc) { free(content); free(tail); return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); }
        memcpy(nc, content, pre);
        strcpy(nc + pre, tail);
        strcat(nc, body_close);
        free(content);
        content = nc;
    }
    free(tail);
    httpd_resp_set_type(req, "text/html");
    esp_err_t r = httpd_resp_send(req, content, strlen(content));
    free(content);
    return r;
}

esp_err_t webserver_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.lru_purge_enable = true;
    config.max_open_sockets = 7;
    config.max_uri_handlers = 44;   /* we register 41 routes; do not rely on Kconfig */
    config.stack_size = 8192;       /* static file serving needs more than the 4K default */
    config.uri_match_fn = httpd_uri_match_wildcard;  /* enables the wildcard static handler */

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = dash_index_get },
        { .uri = "/settings", .method = HTTP_GET, .handler = settings_get },
        { .uri = "/advanced-settings", .method = HTTP_GET, .handler = settings_get },
        { .uri = "/health", .method = HTTP_GET, .handler = health_get },
        { .uri = "/theme.js", .method = HTTP_GET, .handler = theme_get },
        { .uri = "/api/telemetry", .method = HTTP_GET, .handler = telemetry_get },
        { .uri = "/api/command", .method = HTTP_POST, .handler = command_post },
        { .uri = "/api/trips", .method = HTTP_GET, .handler = trips_get },
        { .uri = "/api/trip/detail", .method = HTTP_GET, .handler = trip_detail_get },
        { .uri = "/api/trip/current", .method = HTTP_GET, .handler = trip_current_get },
        { .uri = "/api/trip/current-samples", .method = HTTP_GET, .handler = trip_current_samples_get },
        { .uri = "/api/trip/samples", .method = HTTP_GET, .handler = trip_samples_get },
        { .uri = "/api/trip/heatmap", .method = HTTP_GET, .handler = trip_heatmap_get },
        { .uri = "/api/overview-samples", .method = HTTP_GET, .handler = overview_samples_get },
        { .uri = "/api/settings", .method = HTTP_GET, .handler = settings_get_handler },
        { .uri = "/api/settings", .method = HTTP_PATCH, .handler = settings_patch_handler },
        { .uri = "/settings", .method = HTTP_POST, .handler = settings_form_post },
        { .uri = "/advanced-settings", .method = HTTP_POST, .handler = settings_form_post },
        { .uri = "/api/timezones", .method = HTTP_GET, .handler = timezones_get },
        { .uri = "/garmin/config.json", .method = HTTP_GET, .handler = garmin_config_get },
        { .uri = "/api/maintenance/unlock", .method = HTTP_POST, .handler = maintenance_unlock_post },
        { .uri = "/api/maintenance/pin", .method = HTTP_PUT, .handler = maintenance_pin_put },
        { .uri = "/api/history", .method = HTTP_DELETE, .handler = history_delete },
        { .uri = "/api/upload", .method = HTTP_POST, .handler = upload_post },
        { .uri = "/api/storage", .method = HTTP_GET, .handler = storage_get },
        { .uri = "/api/list", .method = HTTP_GET, .handler = list_get },
        { .uri = "/api/format", .method = HTTP_POST, .handler = format_post },
        { .uri = "/api/export/trips.csv", .method = HTTP_GET, .handler = trips_csv_get },
        { .uri = "/api/export/impacts.csv", .method = HTTP_GET, .handler = impacts_csv_get },
        { .uri = "/api/wifi/status", .method = HTTP_GET, .handler = wifi_status_get },
        { .uri = "/api/wifi/scan", .method = HTTP_GET, .handler = wifi_scan_get },
        { .uri = "/api/wifi/connect", .method = HTTP_POST, .handler = wifi_connect_post },
        { .uri = "/api/wifi/disconnect", .method = HTTP_POST, .handler = wifi_disconnect_post },
        { .uri = "/api/wifi/clear", .method = HTTP_POST, .handler = wifi_clear_post },
        { .uri = "/api/time", .method = HTTP_GET, .handler = time_get },
        { .uri = "/api/time", .method = HTTP_PUT, .handler = time_put },
        { .uri = "/api/time", .method = HTTP_POST, .handler = time_post },
        { .uri = "/api/time/sync", .method = HTTP_POST, .handler = time_sync_post },
        { .uri = "/api/ota/status", .method = HTTP_GET, .handler = ota_status_get },
        { .uri = "/api/ota/start", .method = HTTP_POST, .handler = ota_start_post },
        { .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }
    /* Wildcard: dashboard static assets (css/js/models/fonts) from SD */
    const httpd_uri_t wildcard = {
        .uri = "/*", .method = HTTP_GET, .handler = static_get
    };
    httpd_register_uri_handler(s_server, &wildcard);
    ESP_LOGI(TAG, "HTTP server started on port 80 (/ws at 50 Hz, root=%s)", WEB_ROOT);
    return ESP_OK;
}

void webserver_push_telemetry(const telemetry_frame_t *frame)
{
    if (!s_server || !s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_latest = *frame;
    build_json(frame, s_ws_json, sizeof(s_ws_json));
    xSemaphoreGive(s_lock);

    /* Broadcast to all /ws clients */
    httpd_ws_frame_t ws = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)s_ws_json,
        .len = strlen(s_ws_json),
    };
    int fds[8];
    size_t n = 8;
    if (httpd_get_client_list(s_server, &n, fds) == ESP_OK) {
        for (size_t i = 0; i < n; i++) {
            /* Only push to real WebSocket clients — blasting telemetry into a
             * plain HTTP socket (e.g. an upload) corrupts its response. */
            if (httpd_ws_get_fd_info(s_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                httpd_ws_send_frame_async(s_server, fds[i], &ws);
            }
        }
    }
}
