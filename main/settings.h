/*
 * Settings store for Vessel Motion P4 — NVS-persisted, Pi-compatible.
 *
 * Mirrors the Pi project's storage.py DEFAULT_SETTINGS exactly: same keys,
 * same default values, same JSON representation. The whole settings dict is
 * kept as a cJSON object in RAM and persisted to NVS as a compact JSON blob
 * on every update.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load settings from NVS (or create defaults). Safe to call once at boot. */
esp_err_t settings_init(void);

/* Full settings dict as a compact JSON string (malloc'd; caller frees). */
char *settings_json_string(void);

/* GET /api/settings variant: settings + computed extras (algorithmVersion etc.). */
char *settings_json_string_public(int algorithm_version);

/* Merge a JSON object body into settings (PATCH semantics).
 * Validates keys against the known defaults (Pi rules). On error returns
 * ESP_ERR_INVALID_ARG and fills errbuf with a human-readable message. */
esp_err_t settings_patch_json(const char *json_body, char *errbuf, size_t errlen);

/* Merge an application/x-www-form-urlencoded body (POST /settings form).
 * Unknown keys are skipped; values are coerced to the default value's type. */
esp_err_t settings_update_form(const char *body);

/* Restore the ALGORITHM_SETTING_KEYS to defaults (Pi restore_algorithm_defaults). */
void settings_restore_algorithm_defaults(void);

/* Persist current settings to NVS. */
esp_err_t settings_save(void);

/* Typed getters (valid until the next patch/save). */
double      settings_num(const char *key);
int         settings_bool(const char *key);
const char *settings_str(const char *key);
/* Read a 3-element numeric array setting. Returns false if wrong shape. */
bool        settings_arr3(const char *key, float out[3]);

#ifdef __cplusplus
}
#endif
