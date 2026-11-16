/*
 * Settings store for Vessel Motion P4 — see settings.h.
 * Keys/defaults are a 1:1 copy of the Pi project's storage.py DEFAULT_SETTINGS.
 */
#include "settings.h"
#include "globals.h"
#include "store.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "settings";

#define NVS_NAMESPACE "vm"
#define NVS_KEY       "sjson"

typedef enum { T_STR, T_INT, T_FLOAT, T_BOOL, T_ARR_F3, T_ARR_I3 } set_type_t;

typedef struct {
    const char *key;
    set_type_t  type;
    const char *sval;      /* T_STR value (also reused for CSV strings) */
    double      nval;      /* T_INT / T_FLOAT / T_BOOL value */
    float       f3[3];     /* T_ARR_F3 value */
    int         i3[3];     /* T_ARR_I3 value */
} default_t;

/* Pi DEFAULT_SETTINGS — identical keys, values and types. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static const default_t DEFAULTS[] = {
    { "vessel", T_STR, DEFAULT_VESSEL_NAME },
    { "hostname", T_STR, DEFAULT_HOSTNAME },
    { "timezone", T_STR, DEFAULT_TIMEZONE },
    { "timeFormat", T_STR, "24" },
    { "dateFormat", T_STR, "mdy" },
    { "metric", T_BOOL, NULL, 1 },
    { "rsi", T_BOOL, NULL, DEFAULT_RSI_ENABLED },
    { "mei", T_BOOL, NULL, DEFAULT_MEI_ENABLED },
    { "fullscreen", T_BOOL, NULL, 1 },
    { "productName", T_STR, DEFAULT_PRODUCT_NAME },
    { "attitudeYRange", T_INT, NULL, DEFAULT_ATTITUDE_Y_RANGE_DEG },
    { "vibrationEnabled", T_BOOL, NULL, DEFAULT_VIBRATION_ENABLED },
    { "vibrationStrength", T_INT, NULL, DEFAULT_VIBRATION_STRENGTH },
    { "productSize", T_INT, NULL, 2 },
    { "uiFont", T_INT, NULL, 0 },
    { "colorScheme", T_INT, NULL, 0 },
    { "vesselView", T_STR, "aft" },
    { "vesselModel", T_STR, "tug" },
    { "reloadOnTab", T_BOOL, NULL, 1 },
    { "axisSource", T_ARR_I3, NULL, 0, {0}, {0, 1, 2} },
    { "axisSign", T_ARR_I3, NULL, 0, {0}, {1, -1, 1} },
    { "axisX", T_INT, NULL, 1 },
    { "axisY", T_INT, NULL, 2 },
    { "axisZ", T_INT, NULL, 3 },
    { "accelOffset", T_ARR_F3, NULL, 0, {0, 0, 0} },
    { "gyroOffset", T_ARR_F3, NULL, 0, {0, 0, 0} },
    { "rollZero", T_FLOAT, NULL, 0.0 },
    { "pitchZero", T_FLOAT, NULL, 0.0 },
    { "dhcp", T_BOOL, NULL, DEFAULT_NETWORK_DHCP },
    { "ip", T_STR, DEFAULT_IP_ADDRESS },
    { "subnet", T_STR, DEFAULT_SUBNET_MASK },
    { "gateway", T_STR, DEFAULT_GATEWAY },
    { "dns", T_STR, DEFAULT_DNS_SERVER },
    { "wifiAuto", T_BOOL, NULL, 1 },
    { "resetOnTripStart", T_BOOL, NULL, 0 },
    { "hideKioskCursor", T_BOOL, NULL, 1 },
    { "garminRefresh", T_INT, NULL, 20 },
    { "alpha", T_FLOAT, NULL, DEFAULT_ALPHA },
    { "fast", T_FLOAT, NULL, DEFAULT_FAST_HZ },
    { "slow", T_FLOAT, NULL, DEFAULT_SLOW_HZ },
    { "derivative", T_FLOAT, NULL, DEFAULT_DERIVATIVE_HZ },
    { "impactStart", T_FLOAT, NULL, DEFAULT_IMPACT_START_G },
    { "impactEnd", T_FLOAT, NULL, DEFAULT_IMPACT_END_G },
    { "impactEndMs", T_INT, NULL, DEFAULT_IMPACT_END_MS },
    { "impactDeadbandMs", T_INT, NULL, DEFAULT_IMPACT_DEADBAND_MS },
    { "peakLow", T_FLOAT, NULL, DEFAULT_PEAK_LOW },
    { "peakHigh", T_FLOAT, NULL, DEFAULT_PEAK_HIGH },
    { "rmsLow", T_FLOAT, NULL, DEFAULT_RMS_LOW },
    { "rmsHigh", T_FLOAT, NULL, DEFAULT_RMS_HIGH },
    { "jerkLow", T_FLOAT, NULL, DEFAULT_JERK_LOW },
    { "jerkHigh", T_FLOAT, NULL, DEFAULT_JERK_HIGH },
    { "rollLow", T_FLOAT, NULL, DEFAULT_ROLL_LOW },
    { "rollHigh", T_FLOAT, NULL, DEFAULT_ROLL_HIGH },
    { "pitchLow", T_FLOAT, NULL, DEFAULT_PITCH_LOW },
    { "pitchHigh", T_FLOAT, NULL, DEFAULT_PITCH_HIGH },
    { "repeatLow", T_FLOAT, NULL, DEFAULT_REPEAT_LOW },
    { "repeatHigh", T_FLOAT, NULL, DEFAULT_REPEAT_HIGH },
    { "impactLow", T_FLOAT, NULL, DEFAULT_IMPACT_LOW },
    { "impactHigh", T_FLOAT, NULL, DEFAULT_IMPACT_HIGH },
    { "exponent", T_FLOAT, NULL, DEFAULT_EXPONENT },
    { "rsiWeights", T_STR, DEFAULT_RSI_WEIGHTS_CSV },
    { "attack", T_FLOAT, NULL, DEFAULT_ATTACK_SECONDS },
    { "decay", T_FLOAT, NULL, DEFAULT_DECAY_SECONDS },
    { "meiAxis", T_STR, DEFAULT_MEI_AXIS_CSV },
    { "meiRmsLow", T_FLOAT, NULL, DEFAULT_MEI_RMS_LOW },
    { "meiRmsHigh", T_FLOAT, NULL, DEFAULT_MEI_RMS_HIGH },
    { "meiShockLow", T_FLOAT, NULL, DEFAULT_MEI_SHOCK_LOW },
    { "meiShockHigh", T_FLOAT, NULL, DEFAULT_MEI_SHOCK_HIGH },
    { "meiTau", T_FLOAT, NULL, DEFAULT_MEI_TAU_HOURS },
    { "meiCurrentWeights", T_STR, DEFAULT_MEI_CURRENT_WEIGHTS_CSV },
    { "meiTripWeights", T_STR, DEFAULT_MEI_TRIP_WEIGHTS_CSV },
    { "originalScale", T_INT, NULL, 100 },
    { "originalVertical", T_INT, NULL, 0 },
    { "originalElevation", T_INT, NULL, 0 },
    { "originalYaw", T_INT, NULL, 0 },
    { "tugScale", T_INT, NULL, 100 },
    { "tugVertical", T_INT, NULL, 0 },
    { "tugElevation", T_INT, NULL, 0 },
    { "tugYaw", T_INT, NULL, 0 },
    { "yachtScale", T_INT, NULL, 100 },
    { "yachtVertical", T_INT, NULL, 0 },
    { "yachtElevation", T_INT, NULL, 0 },
    { "yachtYaw", T_INT, NULL, 0 },
    { "superyachtScale", T_INT, NULL, 100 },
    { "superyachtVertical", T_INT, NULL, 0 },
    { "superyachtElevation", T_INT, NULL, 0 },
    { "superyachtYaw", T_INT, NULL, 0 },
};
#pragma GCC diagnostic pop

/* Pi ALGORITHM_SETTING_KEYS (used by restore_algorithm_defaults + "defaults" cmd) */
static const char *ALGORITHM_KEYS[] = {
    "rsi", "mei", "alpha", "fast", "slow", "derivative", "impactStart",
    "impactEnd", "impactEndMs", "impactDeadbandMs", "peakLow", "peakHigh",
    "rmsLow", "rmsHigh", "jerkLow", "jerkHigh", "rollLow", "rollHigh",
    "pitchLow", "pitchHigh", "repeatLow", "repeatHigh", "impactLow",
    "impactHigh", "exponent", "rsiWeights", "attack", "decay", "meiAxis",
    "meiRmsLow", "meiRmsHigh", "meiShockLow", "meiShockHigh", "meiTau",
    "meiCurrentWeights", "meiTripWeights",
    "vibrationEnabled", "vibrationStrength",
};

static cJSON *s_settings;
static SemaphoreHandle_t s_mtx;
static const double IMPACT_LEVELS[][2] = IMPACT_THRESHOLD_LEVELS_G;
_Static_assert(sizeof(IMPACT_LEVELS) / sizeof(IMPACT_LEVELS[0]) == 5,
               "Impact slider requires five start/end threshold pairs");

static const char *validate_impact_pair(double start, double end)
{
    return isfinite(start) && isfinite(end) && start > 0 && end > 0 && end < start ?
           NULL : "Impact thresholds must be positive, with end below start";
}

static const char *validate_control(const char *key, const cJSON *item)
{
    if (!strcmp(key, "vibrationEnabled")) {
        return cJSON_IsBool(item) ? NULL : "Vibration enabled must be boolean";
    }
    if (!strcmp(key, "attitudeYRange") || !strcmp(key, "vibrationStrength")) {
        int max = !strcmp(key, "attitudeYRange") ? 90 : 5;
        if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
            item->valuedouble != floor(item->valuedouble) ||
            item->valuedouble < 1 || item->valuedouble > max) {
            return max == 90 ? "Attitude range must be an integer from 1 to 90" :
                               "Vibration strength must be an integer from 1 to 5";
        }
    }
    if (!strcmp(key, "fast") || !strcmp(key, "slow") || !strcmp(key, "derivative")) {
        if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
            item->valuedouble <= 0 || item->valuedouble >= SAMPLE_HZ / 2.0) {
            return "Filter cutoff must be positive and below half the sampling rate";
        }
        if (!strcmp(key, "impactStart") || !strcmp(key, "impactEnd")) {
            if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) || item->valuedouble <= 0)
                return "Impact thresholds must be positive numbers";
        }
    }
    return NULL;
}

static bool filter_key(const char *key)
{
    return !strcmp(key, "vibrationEnabled") || !strcmp(key, "vibrationStrength") ||
           !strcmp(key, "fast") || !strcmp(key, "slow") || !strcmp(key, "derivative") ||
           !strcmp(key, "impactStart") || !strcmp(key, "impactEnd");
}

static const default_t *find_default(const char *key)
{
    for (size_t i = 0; i < sizeof(DEFAULTS) / sizeof(DEFAULTS[0]); i++) {
        if (!strcmp(DEFAULTS[i].key, key)) return &DEFAULTS[i];
    }
    return NULL;
}

static cJSON *make_default_item(const default_t *d)
{
    switch (d->type) {
    case T_STR:   return cJSON_CreateString(d->sval);
    case T_BOOL:  return cJSON_CreateBool(d->nval != 0);
    case T_INT:   return cJSON_CreateNumber((int)d->nval);
    case T_FLOAT: return cJSON_CreateNumber(d->nval);
    case T_ARR_F3: {
        cJSON *a = cJSON_CreateArray();
        for (int i = 0; i < 3; i++) cJSON_AddItemToArray(a, cJSON_CreateNumber(d->f3[i]));
        return a;
    }
    case T_ARR_I3: {
        cJSON *a = cJSON_CreateArray();
        for (int i = 0; i < 3; i++) cJSON_AddItemToArray(a, cJSON_CreateNumber(d->i3[i]));
        return a;
    }
    }
    return NULL;
}

static void load_defaults(void)
{
    cJSON_Delete(s_settings);
    s_settings = cJSON_CreateObject();
    for (size_t i = 0; i < sizeof(DEFAULTS) / sizeof(DEFAULTS[0]); i++) {
        cJSON_AddItemToObject(s_settings, DEFAULTS[i].key, make_default_item(&DEFAULTS[i]));
    }
}

esp_err_t settings_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    load_defaults();

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return ESP_OK;
    size_t len = 0;
    if (nvs_get_blob(h, NVS_KEY, NULL, &len) == ESP_OK && len > 0) {
        char *buf = malloc(len + 1);
        if (buf) {
            if (nvs_get_blob(h, NVS_KEY, buf, &len) == ESP_OK) {
                buf[len] = 0;
                cJSON *stored = cJSON_Parse(buf);
                if (stored) {
                    /* merge stored values over defaults (unknown keys ignored) */
                    xSemaphoreTake(s_mtx, portMAX_DELAY);
                    cJSON *item;
                    cJSON_ArrayForEach(item, stored) {
                        if (cJSON_GetObjectItem(s_settings, item->string) &&
                            !validate_control(item->string, item)) {
                            cJSON_ReplaceItemInObject(s_settings, item->string,
                                                      cJSON_Duplicate(item, true));
                        } else if (validate_control(item->string, item)) {
                            ESP_LOGW(TAG, "Invalid saved %s; using firmware default", item->string);
                        }
                    }
                    if (validate_impact_pair(
                            cJSON_GetObjectItem(s_settings, "impactStart")->valuedouble,
                            cJSON_GetObjectItem(s_settings, "impactEnd")->valuedouble)) {
                        ESP_LOGW(TAG, "Invalid saved impact threshold pair; using firmware defaults");
                        cJSON_ReplaceItemInObject(s_settings, "impactStart", cJSON_CreateNumber(DEFAULT_IMPACT_START_G));
                        cJSON_ReplaceItemInObject(s_settings, "impactEnd", cJSON_CreateNumber(DEFAULT_IMPACT_END_G));
                    }
                    xSemaphoreGive(s_mtx);
                    cJSON_Delete(stored);
                    ESP_LOGI(TAG, "Settings loaded from NVS");
                }
            }
            free(buf);
        }
    }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t settings_save(void)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    char *js = cJSON_PrintUnformatted(s_settings);
    xSemaphoreGive(s_mtx);
    if (!js) return ESP_ERR_NO_MEM;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY, js, strlen(js));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    free(js);
    return err;
}

char *settings_json_string(void)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    char *js = cJSON_PrintUnformatted(s_settings);
    xSemaphoreGive(s_mtx);
    return js;   /* cJSON_Print* uses malloc; caller frees */
}

char *settings_json_string_public(int algorithm_version)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    cJSON *copy = cJSON_Duplicate(s_settings, true);
    xSemaphoreGive(s_mtx);
    if (!copy) return NULL;
    /* Pi api.py get_settings extras */
    cJSON_AddNumberToObject(copy, "algorithmVersion", algorithm_version);
    cJSON_AddBoolToObject(copy, "wirelessAllowed", WIRELESS_ALLOWED);
    if (WIRELESS_ALLOWED) {
        cJSON_AddStringToObject(copy, "fallbackSsid", WIFI_FALLBACK_SSID);
        cJSON_AddStringToObject(copy, "fallbackIp", WIFI_FALLBACK_IP);
        cJSON_AddStringToObject(copy, "fallbackPassword", WIFI_FALLBACK_PASS);
    }
    cJSON_AddNumberToObject(copy, "maximumTrips", TRIP_RETENTION);
    cJSON_AddNumberToObject(copy, "maximumTripSeconds", MAX_TRIP_SECONDS);
    static const float cutoffs[] = VIBRATION_CUTOFFS_HZ;
    cJSON *levels = cJSON_CreateArray();
    for (size_t i = 0; i < sizeof(cutoffs) / sizeof(cutoffs[0]); i++)
        cJSON_AddItemToArray(levels, cJSON_CreateNumber(cutoffs[i]));
    cJSON_AddItemToObject(copy, "vibrationCutoffsHz", levels);
    cJSON *impact_levels = cJSON_AddArrayToObject(copy, "impactThresholdLevels");
    for (size_t i = 0; i < sizeof(IMPACT_LEVELS) / sizeof(IMPACT_LEVELS[0]); i++) {
        cJSON *level = cJSON_CreateObject();
        cJSON_AddNumberToObject(level, "startG", IMPACT_LEVELS[i][0]);
        cJSON_AddNumberToObject(level, "endG", IMPACT_LEVELS[i][1]);
        cJSON_AddItemToArray(impact_levels, level);
    }
    cJSON_AddNumberToObject(copy, "effectiveFastHz",
        cJSON_IsTrue(cJSON_GetObjectItem(copy, "vibrationEnabled")) ?
        motion_vibration_cutoff(cJSON_GetObjectItem(copy, "vibrationStrength")->valueint,
                               (float)cJSON_GetObjectItem(copy, "fast")->valuedouble) :
        cJSON_GetObjectItem(copy, "fast")->valuedouble);
    cJSON_AddNumberToObject(copy, "garminRefresh", 20);
    cJSON_AddNumberToObject(copy, "graphSeconds", 600);
    cJSON_AddNumberToObject(copy, "smoothing", 0.15);
    cJSON_AddBoolToObject(copy, "legacy", false);
    char *js = cJSON_PrintUnformatted(copy);
    cJSON_Delete(copy);
    return js;
}

/* coerce one cJSON value to the default's type; returns NULL or static error msg */
static const char *coerce(const default_t *d, const cJSON *in, cJSON **out)
{
    switch (d->type) {
    case T_STR:
        if (!cJSON_IsString(in)) return "must be a string";
        *out = cJSON_CreateString(in->valuestring);
        return NULL;
    case T_BOOL:
        if (cJSON_IsBool(in)) { *out = cJSON_CreateBool(cJSON_IsTrue(in)); return NULL; }
        return "must be a boolean";
    case T_INT:
        if (!cJSON_IsNumber(in)) return "must be an integer";
        *out = cJSON_CreateNumber((int)in->valuedouble);
        return NULL;
    case T_FLOAT:
        if (!cJSON_IsNumber(in)) return "must be numeric";
        *out = cJSON_CreateNumber(in->valuedouble);
        return NULL;
    case T_ARR_F3:
    case T_ARR_I3:
        if (!cJSON_IsArray(in) || cJSON_GetArraySize((cJSON *)in) != 3) return "must be a 3-element array";
        {
            cJSON *a = cJSON_CreateArray();
            cJSON *el;
            cJSON_ArrayForEach(el, (cJSON *)in) {
                if (!cJSON_IsNumber(el)) { cJSON_Delete(a); return "array must be numeric"; }
                cJSON_AddItemToArray(a, cJSON_CreateNumber(d->type == T_ARR_I3 ? (int)el->valuedouble : el->valuedouble));
            }
            *out = a;
        }
        return NULL;
    }
    return "unknown type";
}

esp_err_t settings_patch_json(const char *json_body, char *errbuf, size_t errlen)
{
    cJSON *patch = cJSON_Parse(json_body);
    if (!patch || !cJSON_IsObject(patch)) {
        snprintf(errbuf, errlen, "Settings payload must be an object");
        cJSON_Delete(patch);
        return ESP_ERR_INVALID_ARG;
    }
    /* validate everything first (Pi raises before writing) */
    cJSON *item;
    cJSON_ArrayForEach(item, patch) {
        const default_t *d = find_default(item->string);
        if (!d) {
            snprintf(errbuf, errlen, "Unsupported settings: %s", item->string);
            cJSON_Delete(patch);
            return ESP_ERR_INVALID_ARG;
        }
        const char *control_err = validate_control(item->string, item);
        bool changed = cJSON_IsBool(item) ? settings_bool(item->string) != cJSON_IsTrue(item) :
                                           settings_num(item->string) != item->valuedouble;
        if (!control_err && changed && filter_key(item->string) && store_trip_active()) {
            control_err = "Stop the trip before changing measurement filters or impact thresholds";
        }
        if (control_err) {
            snprintf(errbuf, errlen, "%s", control_err);
            cJSON_Delete(patch);
            return ESP_ERR_INVALID_ARG;
        }
        if (!strcmp(item->string, "timeFormat") &&
            strcmp(cJSON_GetStringValue(item) ?: "", "12") &&
            strcmp(cJSON_GetStringValue(item) ?: "", "24")) {
            snprintf(errbuf, errlen, "Time format must be 12 or 24");
            cJSON_Delete(patch);
            return ESP_ERR_INVALID_ARG;
        }
        if (!strcmp(item->string, "dateFormat") &&
            strcmp(cJSON_GetStringValue(item) ?: "", "mdy") &&
            strcmp(cJSON_GetStringValue(item) ?: "", "dmy")) {
            snprintf(errbuf, errlen, "Date format must be mdy or dmy");
            cJSON_Delete(patch);
            return ESP_ERR_INVALID_ARG;
        }
        if (!strcmp(item->string, "vesselModel")) {
            const char *v = cJSON_GetStringValue(item) ?: "";
            if (strcmp(v, "original") && strcmp(v, "tug") && strcmp(v, "yacht") && strcmp(v, "superyacht")) {
                snprintf(errbuf, errlen, "Unsupported vessel model");
                cJSON_Delete(patch);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    const cJSON *start = cJSON_GetObjectItem(patch, "impactStart");
    const cJSON *end = cJSON_GetObjectItem(patch, "impactEnd");
    const char *pair_error = validate_impact_pair(
        start ? start->valuedouble : settings_num("impactStart"),
        end ? end->valuedouble : settings_num("impactEnd"));
    if (pair_error) {
        snprintf(errbuf, errlen, "%s", pair_error);
        cJSON_Delete(patch);
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    cJSON_ArrayForEach(item, patch) {
        const default_t *d = find_default(item->string);
        cJSON *coerced = NULL;
        if (d && coerce(d, item, &coerced) == NULL && coerced) {
            cJSON_ReplaceItemInObject(s_settings, item->string, coerced);
        }
    }
    xSemaphoreGive(s_mtx);
    cJSON_Delete(patch);
    return settings_save();
}

/* url-decode one form token in place (+' ' and %XX) */
static void form_decode(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (*r == '%' && r[1] && r[2]) {
            int hi = r[1] >= 'A' ? (r[1] | 0x20) - 'a' + 10 : r[1] - '0';
            int lo = r[2] >= 'A' ? (r[2] | 0x20) - 'a' + 10 : r[2] - '0';
            if (hi >= 0 && hi < 16 && lo >= 0 && lo < 16) { *w++ = (char)((hi << 4) | lo); r += 3; continue; }
        }
        *w++ = (*r == '+') ? ' ' : *r;
        r++;
    }
    *w = 0;
}

esp_err_t settings_update_form(const char *body)
{
    /* Preflight before writing any fields so rejected filters cannot partly save. */
    char *check = strdup(body);
    if (!check) return ESP_ERR_NO_MEM;
    double impact_start = settings_num("impactStart");
    double impact_end = settings_num("impactEnd");
    char *check_save = NULL;
    for (char *pair = strtok_r(check, "&", &check_save); pair;
         pair = strtok_r(NULL, "&", &check_save)) {
        char *eq = strchr(pair, '=');
        if (!eq) continue;
        *eq++ = 0;
        form_decode(pair);
        form_decode(eq);
        if (strcmp(pair, "attitudeYRange") && !filter_key(pair)) continue;
        cJSON *value;
        if (!strcmp(pair, "vibrationEnabled")) {
            if (strcmp(eq, "yes") && strcmp(eq, "no")) {
                ESP_LOGW(TAG, "Invalid vibration enabled form value");
                free(check);
                return ESP_ERR_INVALID_ARG;
            }
            value = cJSON_CreateBool(!strcmp(eq, "yes"));
        } else {
            char *end;
            double number = strtod(eq, &end);
            if (end == eq || *end || !isfinite(number)) {
                ESP_LOGW(TAG, "Invalid numeric form value for %s", pair);
                free(check);
                return ESP_ERR_INVALID_ARG;
            }
            value = cJSON_CreateNumber(number);
        }
        if (!value) { free(check); return ESP_ERR_NO_MEM; }
        const char *error = validate_control(pair, value);
        bool changed = cJSON_IsBool(value) ? settings_bool(pair) != cJSON_IsTrue(value) :
                                           settings_num(pair) != value->valuedouble;
        if (!error && changed && filter_key(pair) && store_trip_active())
            error = "Stop the trip before changing measurement filters or impact thresholds";
        if (!strcmp(pair, "impactStart")) impact_start = value->valuedouble;
        if (!strcmp(pair, "impactEnd")) impact_end = value->valuedouble;
        cJSON_Delete(value);
        if (error) {
            ESP_LOGW(TAG, "%s", error);
            free(check);
            return ESP_ERR_INVALID_ARG;
        }
    }
    free(check);
    const char *pair_error = validate_impact_pair(impact_start, impact_end);
    if (pair_error) {
        ESP_LOGW(TAG, "%s", pair_error);
        return ESP_ERR_INVALID_ARG;
    }
    char *copy = strdup(body);
    if (!copy) return ESP_ERR_NO_MEM;
    bool touched_tz = false;
    char *save = NULL;
    for (char *pair = strtok_r(copy, "&", &save); pair; pair = strtok_r(NULL, "&", &save)) {
        char *eq = strchr(pair, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = pair, *val = eq + 1;
        form_decode(key);
        form_decode(val);
        const default_t *d = find_default(key);
        if (!d) continue;   /* Pi skips unknown form keys */
        cJSON *item = NULL;
        switch (d->type) {
        case T_BOOL:
            item = cJSON_CreateBool(!strcasecmp(val, "1") || !strcasecmp(val, "true") ||
                                    !strcasecmp(val, "yes") || !strcasecmp(val, "on"));
            break;
        case T_INT:   item = cJSON_CreateNumber((int)strtod(val, NULL)); break;
        case T_FLOAT: item = cJSON_CreateNumber(strtod(val, NULL)); break;
        case T_STR:   item = cJSON_CreateString(val); break;
        case T_ARR_F3:
        case T_ARR_I3: {
            float v3[3]; int n = 0;
            char *p = val, *sv = NULL;
            for (char *t = strtok_r(p, ",", &sv); t && n < 3; t = strtok_r(NULL, ",", &sv)) v3[n++] = strtof(t, NULL);
            if (n == 3) {
                item = cJSON_CreateArray();
                for (int i = 0; i < 3; i++)
                    cJSON_AddItemToArray(item, cJSON_CreateNumber(d->type == T_ARR_I3 ? (int)v3[i] : v3[i]));
            }
            break;
        }
        }
        if (!item) continue;
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        cJSON_ReplaceItemInObject(s_settings, key, item);
        xSemaphoreGive(s_mtx);
        if (!strcmp(key, "timezone")) touched_tz = true;
    }
    free(copy);
    (void)touched_tz;   /* timezone application happens via time_mgr in the caller */
    return settings_save();
}

void settings_restore_algorithm_defaults(void)
{
    if (store_trip_active()) {
        ESP_LOGW(TAG, "Stop the trip before restoring algorithm defaults");
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (size_t i = 0; i < sizeof(ALGORITHM_KEYS) / sizeof(ALGORITHM_KEYS[0]); i++) {
        const default_t *d = find_default(ALGORITHM_KEYS[i]);
        if (d) cJSON_ReplaceItemInObject(s_settings, d->key, make_default_item(d));
    }
    xSemaphoreGive(s_mtx);
    settings_save();
}

double settings_num(const char *key)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    cJSON *it = cJSON_GetObjectItem(s_settings, key);
    double v = cJSON_IsNumber(it) ? it->valuedouble : 0.0;
    xSemaphoreGive(s_mtx);
    return v;
}

int settings_bool(const char *key)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    cJSON *it = cJSON_GetObjectItem(s_settings, key);
    int v = cJSON_IsTrue(it) ? 1 : 0;
    xSemaphoreGive(s_mtx);
    return v;
}

const char *settings_str(const char *key)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    cJSON *it = cJSON_GetObjectItem(s_settings, key);
    const char *v = cJSON_IsString(it) ? it->valuestring : "";
    static char buf[128];
    strlcpy(buf, v, sizeof(buf));
    xSemaphoreGive(s_mtx);
    return buf;
}

bool settings_arr3(const char *key, float out[3])
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    cJSON *it = cJSON_GetObjectItem(s_settings, key);
    bool ok = false;
    if (cJSON_IsArray(it) && cJSON_GetArraySize(it) == 3) {
        int i = 0;
        cJSON *el;
        cJSON_ArrayForEach(el, it) out[i++] = (float)el->valuedouble;
        ok = true;
    }
    xSemaphoreGive(s_mtx);
    return ok;
}
