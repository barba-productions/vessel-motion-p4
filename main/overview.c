/* Rolling overview history — see overview.h. */
#include "overview.h"
#include <string.h>
#include "esp_timer.h"

#define OVERVIEW_MAX_SAMPLES 1900
#define OVERVIEW_MAX_IMPACTS 256
#define TWELVE_H_MS  (12u * 60u * 60u * 1000u)

/* exactly 16 bytes on the wire ("<I6h") */
#pragma pack(push, 1)
typedef struct {
    uint32_t ms;
    int16_t  roll100, pitch100, ax1000, ay1000, az1000, res1000;
} ov_sample_t;
#pragma pack(pop)

static ov_sample_t s_samples[OVERVIEW_MAX_SAMPLES];
static int         s_count;
static uint32_t    s_impacts[OVERVIEW_MAX_IMPACTS];
static int         s_impact_head;   /* oldest */
static int         s_impact_count;

static uint32_t mono_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Pi _compact_overview: drop > 12 h, decimate by age-based spacing, cap 1900. */
static void compact(uint32_t now_ms)
{
    ov_sample_t *dst = s_samples;
    int kept = 0;
    uint32_t last_kept = 0;
    for (int i = 0; i < s_count; i++) {
        uint32_t age = now_ms - s_samples[i].ms;
        if (age > TWELVE_H_MS) continue;
        uint32_t spacing = age <= 10u * 60 * 1000 ? 2000 :
                           age <= 60u * 60 * 1000 ? 6000 :
                           age <= 6u * 3600 * 1000 ? 30000 : 60000;
        if (kept == 0 || s_samples[i].ms - last_kept >= spacing || i == s_count - 1) {
            *dst++ = s_samples[i];
            kept++;
            last_kept = s_samples[i].ms;
        }
    }
    s_count = kept;
    while (s_count > OVERVIEW_MAX_SAMPLES) {
        memmove(s_samples, s_samples + 1, (s_count - 1) * sizeof(ov_sample_t));
        s_count--;
    }
    while (s_impact_count > 0 && now_ms - s_impacts[s_impact_head] > TWELVE_H_MS) {
        s_impact_head = (s_impact_head + 1) % OVERVIEW_MAX_IMPACTS;
        s_impact_count--;
    }
}

void overview_record(uint32_t ms, int16_t roll100, int16_t pitch100,
                     int16_t ax1000, int16_t ay1000, int16_t az1000, int16_t res1000)
{
    if (s_count >= OVERVIEW_MAX_SAMPLES) compact(ms);
    if (s_count >= OVERVIEW_MAX_SAMPLES) {
        memmove(s_samples, s_samples + 1, (s_count - 1) * sizeof(ov_sample_t));
        s_count--;
    }
    s_samples[s_count++] = (ov_sample_t){ ms, roll100, pitch100, ax1000, ay1000, az1000, res1000 };
    compact(ms);
}

void overview_note_impact(uint32_t ms)
{
    int tail = (s_impact_head + s_impact_count) % OVERVIEW_MAX_IMPACTS;
    s_impacts[tail] = ms;
    if (s_impact_count < OVERVIEW_MAX_IMPACTS) {
        s_impact_count++;
    } else {
        s_impact_head = (s_impact_head + 1) % OVERVIEW_MAX_IMPACTS;
    }
}

void overview_reset(void)
{
    s_count = 0;
    s_impact_head = 0;
    s_impact_count = 0;
}

static void put_u16(uint8_t **p, uint16_t v)
{
    (*p)[0] = v & 0xff;
    (*p)[1] = (v >> 8) & 0xff;
    *p += 2;
}

static void put_u32(uint8_t **p, uint32_t v)
{
    memcpy(*p, &v, 4);
    *p += 4;
}

size_t overview_write_binary(uint8_t *out, size_t cap)
{
    uint32_t now = mono_ms();
    compact(now);
    size_t need = 24 + (size_t)s_count * sizeof(ov_sample_t) + (size_t)s_impact_count * 4;
    if (cap < need) return 0;

    uint8_t *p = out;
    memcpy(p, "VMOVR2\0\0", 8); p += 8;
    put_u16(&p, 2);              /* version */
    put_u16(&p, 24);             /* header bytes */
    put_u16(&p, (uint16_t)s_count);
    put_u16(&p, 2);              /* Pi constant */
    put_u16(&p, (uint16_t)s_impact_count);
    put_u16(&p, 0);              /* reserved */
    put_u32(&p, now);
    memcpy(p, s_samples, (size_t)s_count * sizeof(ov_sample_t));
    p += (size_t)s_count * sizeof(ov_sample_t);
    for (int i = 0; i < s_impact_count; i++) {
        put_u32(&p, s_impacts[(s_impact_head + i) % OVERVIEW_MAX_IMPACTS]);
    }
    return need;
}
