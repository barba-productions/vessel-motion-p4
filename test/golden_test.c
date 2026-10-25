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
#include "motion.h"

int main(void)
{
    algorithm_cfg_t cfg;
    motion_default_config(&cfg);

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
