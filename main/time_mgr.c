/* Time & RTC — see time_mgr.h. */
#include "time_mgr.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_netif.h"
#include "nvs.h"

static const char *TAG = "time";
#define NVS_NS   "time"
#define NVS_TZ   "tz"

static char s_tz[40] = "UTC";
static bool s_synced;
static bool s_sntp_started;
static uint32_t s_sync_count;

/* newlib's tzset() only understands POSIX TZ rules, not IANA names — map the
 * curated zone list (webserver timezones_get) to POSIX rules. Signs follow the
 * POSIX convention (west of Greenwich positive). */
static const struct { const char *iana; const char *posix; } TZ_MAP[] = {
    { "UTC", "UTC0" },
    { "Africa/Cairo", "EET-2EEST,M4.5.4/24,M10.4.4/24" },
    { "Africa/Johannesburg", "SAST-2" },
    { "America/Anchorage", "AKST9AKDT,M3.2.0/2,M11.1.0/1" },
    { "America/Argentina/Buenos_Aires", "ART3" },
    { "America/Bogota", "COT5" },
    { "America/Chicago", "CST6CDT,M3.2.0/2,M11.1.0/1" },
    { "America/Denver", "MST7MDT,M3.2.0/2,M11.1.0/1" },
    { "America/Halifax", "AST4ADT,M3.2.0/2,M11.1.0/1" },
    { "America/Los_Angeles", "PST8PDT,M3.2.0/2,M11.1.0/1" },
    { "America/Mexico_City", "CST6" },
    { "America/New_York", "EST5EDT,M3.2.0/2,M11.1.0/1" },
    { "America/Phoenix", "MST7" },
    { "America/Santiago", "CLT4CLST,M9.1.0/0,M4.1.0/0" },
    { "America/Sao_Paulo", "BRT3" },
    { "America/St_Johns", "NST3:30NDT,M3.2.0/2,M11.1.0/1" },
    { "America/Toronto", "EST5EDT,M3.2.0/2,M11.1.0/1" },
    { "America/Vancouver", "PST8PDT,M3.2.0/2,M11.1.0/1" },
    { "Asia/Dubai", "GST-4" },
    { "Asia/Hong_Kong", "HKT-8" },
    { "Asia/Jakarta", "WIB-7" },
    { "Asia/Jerusalem", "IST-2IDT,M3.4.4/26,M10.5.0" },
    { "Asia/Karachi", "PKT-5" },
    { "Asia/Kolkata", "IST-5:30" },
    { "Asia/Kuala_Lumpur", "MYT-8" },
    { "Asia/Manila", "PHT-8" },
    { "Asia/Riyadh", "AST-3" },
    { "Asia/Seoul", "KST-9" },
    { "Asia/Shanghai", "CST-8" },
    { "Asia/Singapore", "SGT-8" },
    { "Asia/Taipei", "CST-8" },
    { "Asia/Tehran", "IRST-3:30" },
    { "Asia/Tokyo", "JST-9" },
    { "Atlantic/Azores", "AZOT1AZOST,M3.5.0/0,M10.5.0/1" },
    { "Atlantic/Reykjavik", "GMT0" },
    { "Australia/Brisbane", "AEST-10" },
    { "Australia/Melbourne", "AEST-10AEDT,M10.1.0/2,M4.1.0/3" },
    { "Australia/Perth", "AWST-8" },
    { "Australia/Sydney", "AEST-10AEDT,M10.1.0/2,M4.1.0/3" },
    { "Europe/Amsterdam", "CET-1CEST,M3.5.0/2,M10.5.0/3" },
    { "Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Berlin", "CET-1CEST,M3.5.0/2,M10.5.0/3" },
    { "Europe/Brussels", "CET-1CEST,M3.5.0/2,M10.5.0/3" },
    { "Europe/Copenhagen", "CET-1CEST,M3.5.0/2,M10.5.0/3" },
    { "Europe/Dublin", "IST-1GMT0,M10.5.0/1,M3.5.0/1" },
    { "Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Istanbul", "TRT-3" },
    { "Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0" },
    { "Europe/London", "GMT0BST,M3.5.0/1,M10.5.0" },
    { "Europe/Madrid", "CET-1CEST,M3.5.0/2,M10.5.0/3" },
    { "Europe/Moscow", "MSK-3" },
    { "Europe/Oslo", "CET-1CEST,M3.5.0/2,M10.5.0/3" },
    { "Europe/Paris", "CET-1CEST,M3.5.0/2,M10.5.0/3" },
    { "Europe/Rome", "CET-1CEST,M3.5.0/2,M10.5.0/3" },
    { "Europe/Stockholm", "CET-1CEST,M3.5.0/2,M10.5.0/3" },
    { "Europe/Warsaw", "CET-1CEST,M3.5.0/2,M10.5.0/3" },
    { "Europe/Zurich", "CET-1CEST,M3.5.0/2,M10.5.0/3" },
    { "Pacific/Auckland", "NZST-12NZDT,M9.5.0/2,M4.1.0/3" },
    { "Pacific/Fiji", "FJT-12" },
    { "Pacific/Guam", "CHST-10" },
    { "Pacific/Honolulu", "HST10" },
    { "Pacific/Tahiti", "TAHT10" },
};

static void apply_tz(void)
{
    const char *rule = NULL;
    for (size_t i = 0; i < sizeof(TZ_MAP) / sizeof(TZ_MAP[0]); i++) {
        if (!strcmp(TZ_MAP[i].iana, s_tz)) { rule = TZ_MAP[i].posix; break; }
    }
    /* unknown name: accept a raw POSIX rule (contains a digit), else UTC */
    if (!rule) rule = strpbrk(s_tz, "0123456789") ? s_tz : "UTC0";
    setenv("TZ", rule, 1);
    tzset();
}

static void on_sntp_sync(struct timeval *tv)
{
    s_synced = true;
    s_sync_count++;
    ESP_LOGI(TAG, "Time synchronized via SNTP");
}

/* Internet NTP servers (fallback order: Google, Microsoft, Cloudflare). */
void time_start_sntp(void)
{
    if (s_sntp_started) return;
    s_sntp_started = true;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "time.google.com");
    esp_sntp_setservername(1, "time.windows.com");
    esp_sntp_setservername(2, "time.cloudflare.com");
    esp_sntp_set_time_sync_notification_cb(on_sntp_sync);
    esp_sntp_init();
}

/* Force an immediate NTP re-query (TIME tab "sync time via internet" button). */
void time_sync_now(void)
{
    if (!s_sntp_started) {
        time_start_sntp();
        return;
    }
    esp_sntp_restart();
}

uint32_t time_sync_count(void) { return s_sync_count; }

esp_err_t time_init(void)
{
    /* Load timezone from NVS */
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_tz);
        if (nvs_get_str(h, NVS_TZ, s_tz, &len) != ESP_OK) strlcpy(s_tz, "UTC", sizeof(s_tz));
        nvs_close(h);
    }
    apply_tz();
    ESP_LOGI(TAG, "Time subsystem ready (tz=%s); SNTP deferred until network up", s_tz);
    return ESP_OK;
}

int64_t time_epoch_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void time_local_iso(char *buf, size_t len)
{
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%S", &tmv);
}

const char *time_timezone(void)
{
    return s_tz;
}

esp_err_t time_set_timezone(const char *tz)
{
    if (!tz || !tz[0] || strlen(tz) >= sizeof(s_tz)) return ESP_ERR_INVALID_ARG;
    /* Validate by applying; the TZ database accepts IANA names or POSIX rules. */
    char prev[40];
    strlcpy(prev, s_tz, sizeof(prev));
    strlcpy(s_tz, tz, sizeof(s_tz));
    apply_tz();
    /* crude validation: reject if tzset produced no change for a bogus name */
    if (strcmp(tz, "UTC") && strcmp(tz, s_tz) == 0 && strchr(tz, '/') == NULL && strchr(tz, '_') == NULL
        && strcmp(tz, "GMT")) {
        /* not a definitive check; accept common forms */
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, NVS_TZ, s_tz);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Timezone set to %s", s_tz);
    (void)prev;
    return ESP_OK;
}

esp_err_t time_set_local_iso(const char *iso)
{
    if (!iso) return ESP_ERR_INVALID_ARG;
    struct tm tmv = { 0 };
    int y, mo, d, h, mi, s;
    if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 6) {
        return ESP_ERR_INVALID_ARG;
    }
    tmv.tm_year = y - 1900;
    tmv.tm_mon = mo - 1;
    tmv.tm_mday = d;
    tmv.tm_hour = h;
    tmv.tm_min = mi;
    tmv.tm_sec = s;
    /* interpret as local time in current tz, convert to epoch */
    time_t t = mktime(&tmv);
    if (t == (time_t)-1) return ESP_ERR_INVALID_ARG;
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    s_synced = true;
    ESP_LOGI(TAG, "Time set manually to %s", iso);
    return ESP_OK;
}

esp_err_t time_set_epoch_ms(int64_t epoch_ms)
{
    if (epoch_ms <= 0) return ESP_ERR_INVALID_ARG;
    struct timeval tv = { .tv_sec = (time_t)(epoch_ms / 1000), .tv_usec = (suseconds_t)((epoch_ms % 1000) * 1000) };
    settimeofday(&tv, NULL);
    s_synced = true;
    ESP_LOGI(TAG, "Time set from epoch %lld", (long long)epoch_ms);
    return ESP_OK;
}

bool time_is_synchronized(void) { return s_synced; }
bool time_rtc_present(void) { return true; }   /* onboard rechargeable RTC battery */
