#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#define rmdir(path) _rmdir(path)
#endif

static int64_t monotonic_us;
static int test_clock_gettime(int clock_id, struct timespec *ts)
{
    (void)clock_id;
    ts->tv_sec = 1700000000;
    ts->tv_nsec = 0;
    return 0;
}
#define CLOCK_REALTIME 0
#define clock_gettime test_clock_gettime
#define SD_MOUNT_POINT "trip-store-fixture"
#include "../main/store.c"

int64_t esp_timer_get_time(void) { return monotonic_us; }
bool storage_sd_ready(void) { return true; }
int settings_bool(const char *key) { (void)key; return false; }
double settings_num(const char *key)
{
    if (!strcmp(key, "fast")) return DEFAULT_FAST_HZ;
    if (!strcmp(key, "slow")) return DEFAULT_SLOW_HZ;
    if (!strcmp(key, "vibrationStrength")) return DEFAULT_VIBRATION_STRENGTH;
    if (!strcmp(key, "impactStart")) return .3333;
    if (!strcmp(key, "impactEnd")) return .1234;
    return 0;
}

static void seed_trip(uint32_t id, bool completed)
{
    trip_file_t meta = {.magic = TRIP_MAGIC, .id = id, .completed = completed};
    assert(write_trip_file(&meta) == ESP_OK);
    char p[96];
    trip_path(id, p, sizeof(p));
    FILE *f = fopen(p, "wb");
    assert(f);
    fclose(f);
    snprintf(p, sizeof(p), TRIPS_DIR "/trip_%03lu.filter.json", (unsigned long)id);
    f = fopen(p, "wb");
    assert(f);
    fputs("{}", f);
    fclose(f);
}

int main(void)
{
    assert(mkdir(SD_MOUNT_POINT, 0775) == 0);
    assert(mkdir(STORE_DIR, 0775) == 0);
    assert(mkdir(TRIPS_DIR, 0775) == 0);
    for (uint32_t i = 1; i <= TRIP_RETENTION + 30; i++) seed_trip(i, true);
    seed_trip(4000, false);
    assert(store_init() == ESP_OK);
    trip_meta_t *list = malloc(TRIP_RETENTION * sizeof(*list));
    assert(list);
    assert(store_list_trips(list, TRIP_RETENTION) == TRIP_RETENTION);
    assert(list[0].id == TRIP_RETENTION + 30);
    assert(list[TRIP_RETENTION - 1].id == 31);
    trip_file_t meta;
    assert(!read_trip_file(1, &meta));
    assert(read_trip_file(4000, &meta) && !meta.completed);
    algorithm_cfg_t cfg;
    motion_default_config(&cfg);
    assert(store_start_trip(cfg.version) == 4001);
    char *filter = store_trip_filter_json(4001);
    assert(filter && strstr(filter, "\"fastHz\":25") && strstr(filter, "\"tripSampleHz\":5"));
    cJSON *config = cJSON_Parse(filter);
    assert(config);
    assert(cJSON_GetObjectItem(config, "impactStartG")->valuedouble == .3333);
    assert(cJSON_GetObjectItem(config, "impactEndG")->valuedouble == .1234);
    cJSON_Delete(config);
    free(filter);
    store_add_sample(200, 1, 2, 0, 0, 0, 0, 0, 0);
    monotonic_us = (int64_t)MAX_TRIP_SECONDS * 1000000;
    store_end_trip(&cfg);
    assert(!store_trip_active());
    assert(store_trip_meta(4001, list));
    assert(list[0].duration_seconds == MAX_TRIP_SECONDS);
    assert(list[0].sample_count == 1);
    assert(store_list_trips(list, TRIP_RETENTION) == TRIP_RETENTION);
    assert(!read_trip_file(31, &meta));
    size_t cap = TRIP_RETENTION * 320u + 256u, length = 0;
    char *csv = malloc(cap);
    assert(csv);
    assert(store_trips_csv(csv, cap, &length) == ESP_OK);
    int lines = 0;
    for (size_t i = 0; i < length; i++) if (csv[i] == '\n') lines++;
    assert(lines == TRIP_RETENTION + 1);
    free(csv);
    free(list);
    assert(store_clear_history());
    assert(rmdir(TRIPS_DIR) == 0);
    assert(rmdir(STORE_DIR) == 0);
    assert(rmdir(SD_MOUNT_POINT) == 0);
    puts("PASS retention rollover, incomplete-trip preservation, duration, filter metadata and full CSV");
    return 0;
}
