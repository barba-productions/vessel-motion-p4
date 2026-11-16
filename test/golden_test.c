/*
 * Host-side golden test for the ported motion engine.
 * Replicates the Pi test: MEI trip score for
 *   sum_a2 = (0.15*G0)^2 * 3600
 *   dose4  = (0.20*G0)^4 * 3600
 *   seconds= 3600
 * must equal 1.8279 +/- 0.001.
 *
 * Build (on the Pi project's venv is irrelevant; this is pure C):
 *   gcc -I../main golden_test.c ../main/motion.c -o golden_test -lm
 */
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include "motion.h"

int main(void)
{
    algorithm_cfg_t cfg;
    motion_default_config(&cfg);
    const float impact_levels[][2] = IMPACT_THRESHOLD_LEVELS_G;
    for (int level = 0; level < 5; level++) {
        algorithm_cfg_t impact_cfg = cfg;
        impact_cfg.impact_start_g = impact_levels[level][0];
        impact_cfg.impact_end_g = impact_levels[level][1];
        impact_detector_t detector;
        impact_detector_init(&detector);
        motion_t motion = {0};
        orientation_t orientation = {0};
        impact_t event;
        motion.fast_resultant = impact_cfg.impact_start_g * 0.99f * MOTION_G0;
        assert(!impact_detector_update(&detector, &motion, &orientation, 0, 1000, &impact_cfg, &event));
        assert(!detector.active);
        motion.fast_resultant = impact_cfg.impact_start_g * 1.01f * MOTION_G0;
        assert(!impact_detector_update(&detector, &motion, &orientation, 0, 1010, &impact_cfg, &event));
        assert(detector.active);
        motion.fast_resultant = (impact_cfg.impact_start_g + impact_cfg.impact_end_g) * 0.5f * MOTION_G0;
        assert(!impact_detector_update(&detector, &motion, &orientation, 0, 1500, &impact_cfg, &event));
        assert(detector.active);
        motion.fast_resultant = impact_cfg.impact_end_g * 0.99f * MOTION_G0;
        assert(!impact_detector_update(&detector, &motion, &orientation, 0, 2000, &impact_cfg, &event));
        assert(!impact_detector_update(&detector, &motion, &orientation, 0, 2000 + impact_cfg.impact_end_ms - 1, &impact_cfg, &event));
        assert(impact_detector_update(&detector, &motion, &orientation, 0, 2000 + impact_cfg.impact_end_ms, &impact_cfg, &event));
        assert(!detector.active);
        assert(event.peak_resultant > impact_cfg.impact_start_g * MOTION_G0);
    }
    const float expected[] = VIBRATION_CUTOFFS_HZ;
    for (int level = 1; level <= 5; level++) {
        float cutoff = motion_vibration_cutoff(level, DEFAULT_FAST_HZ);
        if (fabsf(cutoff - expected[level - 1]) > 0.0001f ||
            motion_vibration_cutoff(level, 3.0f) > 3.0f) {
            printf("FAIL vibration level %d\n", level);
            return 1;
        }
    }
    if (motion_vibration_cutoff(0, 25.0f) != 25.0f ||
        motion_vibration_cutoff(6, 25.0f) != 25.0f) return 1;

    /* Compare steady-state engine-band attenuation and retained slow motion. */
    float rms[2][2] = {{0}};
    for (int mode = 0; mode < 2; mode++) {
        for (int band = 0; band < 2; band++) {
            lowpass3_t filter;
            lowpass3_init(&filter);
            float frequency = band ? 30.0f : 0.5f;
            float cutoff = mode ? motion_vibration_cutoff(3, DEFAULT_FAST_HZ) : DEFAULT_FAST_HZ;
            for (int sample = 0; sample < 10 * SAMPLE_HZ; sample++) {
                float input = sinf(2.0f * 3.14159265f * frequency * sample / SAMPLE_HZ);
                vec3_t out = lowpass3_update(&filter, (vec3_t){input, 0, 0},
                                             cutoff, 1.0f / SAMPLE_HZ);
                if (sample >= 5 * SAMPLE_HZ) rms[mode][band] += out.x * out.x;
            }
            rms[mode][band] = sqrtf(rms[mode][band] / (5 * SAMPLE_HZ));
        }
    }
    if (rms[1][0] / rms[0][0] < 0.98f || rms[1][1] / rms[0][1] > 0.6f) {
        printf("FAIL vibration attenuation or low-frequency preservation\n");
        return 1;
    }

    float g0 = MOTION_G0;
    float sum_a2 = (0.15f * g0) * (0.15f * g0) * 3600.0f;
    float dose4 = powf(0.20f * g0, 4.0f) * 3600.0f;
    float seconds = 3600.0f;

    float score = motion_exposure_score(sum_a2, dose4, seconds, &cfg);
    printf("MEI trip score = %.4f (expected 1.8279 +/- 0.001)\n", score);

    float diff = fabsf(score - 1.8279f);
    if (diff <= 0.0015f) {
        printf("PASS (diff=%.5f)\n", diff);
        return 0;
    }
    printf("FAIL (diff=%.5f)\n", diff);
    return 1;
}
