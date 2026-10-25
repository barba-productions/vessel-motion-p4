/*
 * Settings store for Vessel Motion P4 — see settings.h.
 * Keys/defaults are a 1:1 copy of the Pi project's storage.py DEFAULT_SETTINGS.
 */
#include "settings.h"
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
    { "vessel", T_STR, "Vessel" },
    { "hostname", T_STR, "vessel-motion" },
    { "timezone", T_STR, "UTC" },
    { "timeFormat", T_STR, "24" },
    { "dateFormat", T_STR, "mdy" },
    { "metric", T_BOOL, NULL, 1 },
    { "rsi", T_BOOL, NULL, 1 },
    { "mei", T_BOOL, NULL, 1 },
    { "fullscreen", T_BOOL, NULL, 1 },
    { "productName", T_STR, "VESSEL MOTION" },
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
    { "dhcp", T_BOOL, NULL, 1 },
    { "ip", T_STR, "192.168.1.90" },
    { "subnet", T_STR, "255.255.255.0" },
    { "gateway", T_STR, "192.168.1.1" },
    { "dns", T_STR, "192.168.1.1" },
    { "wifiAuto", T_BOOL, NULL, 1 },
    { "resetOnTripStart", T_BOOL, NULL, 0 },
    { "hideKioskCursor", T_BOOL, NULL, 1 },
    { "garminRefresh", T_INT, NULL, 20 },
    { "alpha", T_FLOAT, NULL, 0.98 },
    { "fast", T_FLOAT, NULL, 25.0 },
    { "slow", T_FLOAT, NULL, 4.0 },
    { "derivative", T_FLOAT, NULL, 12.0 },
    { "impactStart", T_FLOAT, NULL, 0.25 },
    { "impactEnd", T_FLOAT, NULL, 0.15 },
    { "impactEndMs", T_INT, NULL, 100 },
    { "impactDeadbandMs", T_INT, NULL, 150 },
    { "peakLow", T_FLOAT, NULL, 0.10 },
    { "peakHigh", T_FLOAT, NULL, 1.50 },
    { "rmsLow", T_FLOAT, NULL, 0.03 },
    { "rmsHigh", T_FLOAT, NULL, 0.50 },
    { "jerkLow", T_FLOAT, NULL, 0.5 },
    { "jerkHigh", T_FLOAT, NULL, 15.0 },
    { "rollLow", T_FLOAT, NULL, 2.0 },
    { "rollHigh", T_FLOAT, NULL, 25.0 },
    { "pitchLow", T_FLOAT, NULL, 2.0 },
    { "pitchHigh", T_FLOAT, NULL, 25.0 },
    { "repeatLow", T_FLOAT, NULL, 1.0 },
    { "repeatHigh", T_FLOAT, NULL, 20.0 },
    { "impactLow", T_FLOAT, NULL, 0.25 },
    { "impactHigh", T_FLOAT, NULL, 1.50 },
    { "exponent", T_FLOAT, NULL, 2.0 },
    { "rsiWeights", T_STR, "0.35,0.25,0.20,0.10,0.10" },
    { "attack", T_FLOAT, NULL, 0.20 },
    { "decay", T_FLOAT, NULL, 5.0 },
    { "meiAxis", T_STR, "1,1,1" },
    { "meiRmsLow", T_FLOAT, NULL, 0.02 },
    { "meiRmsHigh", T_FLOAT, NULL, 0.30 },
    { "meiShockLow", T_FLOAT, NULL, 0.03 },
    { "meiShockHigh", T_FLOAT, NULL, 0.50 },
    { "meiTau", T_FLOAT, NULL, 4.0 },
    { "meiCurrentWeights", T_STR, "0.45,0.35,0.20" },
    { "meiTripWeights", T_STR, "0.40,0.40,0.20" },
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
};

static cJSON *s_settings;
static SemaphoreHandle_t s_mtx;

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
                        if (cJSON_GetObjectItem(s_settings, item->string)) {
                            cJSON_ReplaceItemInObject(s_settings, item->string,
                                                      cJSON_Duplicate(item, true));
                        }
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
