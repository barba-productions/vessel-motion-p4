#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "settings.h"
#include "globals.h"
#include "nvs.h"

static char saved[8192];
static bool active;
bool store_trip_active(void) { return active; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out)
{
    (void)name;
    if (mode == NVS_READONLY && !saved[0]) return ESP_FAIL;
    *out = 1;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *length)
{
    (void)h; (void)key;
    if (!out) *length = strlen(saved);
    else memcpy(out, saved, *length);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t length)
{
    (void)h; (void)key;
    assert(length < sizeof(saved));
    memcpy(saved, data, length);
    saved[length] = 0;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }

int main(void)
{
    char error[128];
    assert(settings_init() == ESP_OK);
    assert(!strcmp(settings_str("ip"), DEFAULT_IP_ADDRESS));
    assert(settings_bool("dhcp") == DEFAULT_NETWORK_DHCP);
    assert(settings_num("attitudeYRange") == 15);
    assert(!settings_bool("vibrationEnabled"));
    static const double impact_levels[][2] = IMPACT_THRESHOLD_LEVELS_G;
    assert(sizeof(impact_levels) / sizeof(impact_levels[0]) == 5);
    assert(settings_num("impactStart") == impact_levels[2][0]);
    assert(settings_num("impactEnd") == impact_levels[2][1]);
    for (int i = 0; i < 5; i++) {
        assert(impact_levels[i][0] > impact_levels[i][1] && impact_levels[i][1] > 0);
        assert(fabs(impact_levels[i][1] / impact_levels[i][0] - 0.6) < 0.000001);
        if (i) assert(impact_levels[i][0] > impact_levels[i - 1][0]);
        char patch[128];
        snprintf(patch, sizeof(patch), "{\"impactStart\":%.9g,\"impactEnd\":%.9g}",
                 impact_levels[i][0], impact_levels[i][1]);
        assert(settings_patch_json(patch, error, sizeof(error)) == ESP_OK);
        assert(fabs(settings_num("impactStart") - impact_levels[i][0]) < 0.000001);
        assert(fabs(settings_num("impactEnd") - impact_levels[i][1]) < 0.000001);
    }
    assert(settings_patch_json("{\"impactStart\":0.3333,\"impactEnd\":0.1234}", error, sizeof(error)) == ESP_OK);
    assert(settings_num("impactStart") == 0.3333);
    assert(settings_update_form("impactStart=.17&impactEnd=.102") == ESP_OK);
    assert(settings_num("impactEnd") == .102);
    assert(settings_patch_json("{\"attitudeYRange\":30,\"vibrationEnabled\":true,\"vibrationStrength\":4}", error, sizeof(error)) == ESP_OK);
    assert(settings_num("attitudeYRange") == 30);
    assert(settings_num("vibrationStrength") == 4);
    const char *invalid[] = {
        "{\"attitudeYRange\":0}", "{\"attitudeYRange\":91}", "{\"attitudeYRange\":2.5}",
        "{\"attitudeYRange\":\"15\"}", "{\"vibrationStrength\":0}", "{\"vibrationStrength\":6}",
        "{\"vibrationEnabled\":\"yes\"}", "{\"fast\":0}", "{\"fast\":100}",
        "{\"impactStart\":0}", "{\"impactEnd\":-1}", "{\"impactStart\":\".25\"}",
        "{\"impactEnd\":null}", "{\"impactStart\":0.1}", "{\"impactEnd\":0.2}",
        "{\"impactStart\":0.25,\"impactEnd\":0.25}", "{\"impactStart\":1e999}"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        assert(settings_patch_json(invalid[i], error, sizeof(error)) == ESP_ERR_INVALID_ARG);
    assert(settings_update_form("attitudeYRange=0&vessel=ShouldNotSave") == ESP_ERR_INVALID_ARG);
    assert(!strcmp(settings_str("vessel"), DEFAULT_VESSEL_NAME));
    assert(settings_update_form("vibrationStrength=2.5") == ESP_ERR_INVALID_ARG);
    assert(settings_update_form("vessel=ShouldNotSave&impactStart=.1&impactEnd=.2") == ESP_ERR_INVALID_ARG);
    assert(!strcmp(settings_str("vessel"), DEFAULT_VESSEL_NAME));
    assert(settings_num("impactStart") == .17 && settings_num("impactEnd") == .102);
    assert(settings_update_form("impactStart=nan") == ESP_ERR_INVALID_ARG);
    assert(settings_update_form("impactEnd=.1oops") == ESP_ERR_INVALID_ARG);
    assert(settings_update_form("attitudeYRange=45&vibrationEnabled=yes&vibrationStrength=5") == ESP_OK);
    active = true;
    assert(settings_patch_json("{\"impactStart\":0.5}", error, sizeof(error)) == ESP_ERR_INVALID_ARG);
    assert(settings_patch_json("{\"impactEnd\":0.1}", error, sizeof(error)) == ESP_ERR_INVALID_ARG);
    assert(settings_update_form("vessel=ShouldNotSave&impactEnd=.1") == ESP_ERR_INVALID_ARG);
    assert(settings_update_form("impactStart=.17&impactEnd=.102") == ESP_OK);
    assert(settings_patch_json("{\"vibrationStrength\":1}", error, sizeof(error)) == ESP_ERR_INVALID_ARG);
    assert(settings_update_form("vessel=ShouldNotSave&vibrationStrength=1") == ESP_ERR_INVALID_ARG);
    assert(settings_update_form("attitudeYRange=20&vibrationStrength=5&vibrationEnabled=yes") == ESP_OK);
    assert(settings_num("attitudeYRange") == 20);
    settings_restore_algorithm_defaults();
    assert(settings_num("vibrationStrength") == 5);
    active = false;
    char *public_json = settings_json_string_public(ALGORITHM_VERSION);
    cJSON *public = cJSON_Parse(public_json);
    assert(cJSON_GetObjectItem(public, "maximumTrips")->valueint == TRIP_RETENTION);
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(public, "vibrationCutoffsHz")) == 5);
    cJSON *levels = cJSON_GetObjectItem(public, "impactThresholdLevels");
    assert(cJSON_GetArraySize(levels) == 5);
    for (int i = 0; i < 5; i++) {
        cJSON *level = cJSON_GetArrayItem(levels, i);
        assert(fabs(cJSON_GetObjectItem(level, "startG")->valuedouble - impact_levels[i][0]) < 0.000001);
        assert(fabs(cJSON_GetObjectItem(level, "endG")->valuedouble - impact_levels[i][1]) < 0.000001);
    }
    assert(cJSON_GetObjectItem(public, "effectiveFastHz")->valueint == 5);
    cJSON_Delete(public);
    free(public_json);
    settings_restore_algorithm_defaults();
    assert(settings_num("impactStart") == DEFAULT_IMPACT_START_G);
    assert(settings_num("impactEnd") == DEFAULT_IMPACT_END_G);
    strcpy(saved, "{\"ip\":\"10.0.0.42\",\"vessel\":\"Saved Boat\",\"vibrationStrength\":99,\"impactStart\":0.3333,\"impactEnd\":0.1234}");
    assert(settings_init() == ESP_OK);
    assert(!strcmp(settings_str("ip"), "10.0.0.42"));
    assert(!strcmp(settings_str("vessel"), "Saved Boat"));
    assert(settings_num("vibrationStrength") == DEFAULT_VIBRATION_STRENGTH);
    assert(settings_num("impactStart") == .3333 && settings_num("impactEnd") == .1234);
    strcpy(saved, "{\"impactStart\":0.1,\"impactEnd\":0.2}");
    assert(settings_init() == ESP_OK);
    assert(settings_num("impactStart") == DEFAULT_IMPACT_START_G);
    assert(settings_num("impactEnd") == DEFAULT_IMPACT_END_G);
    puts("PASS settings defaults, persistence, validation and trip locking");
    return 0;
}
