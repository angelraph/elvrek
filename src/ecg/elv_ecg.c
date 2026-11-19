/*
 * Elvrek: ECG source.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "elv_ecg.h"
#include "elv_limits.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void elv_ecg_init(elv_ecg_t *e, elv_rhythm_t r, uint32_t seed)
{
    memset(e, 0, sizeof(*e));
    e->rhythm = r;
    e->rng = seed ? seed : 1u;
}

static float noise(elv_ecg_t *e, float amp)
{
    e->rng = e->rng * 1664525u + 1013904223u;
    return amp * (((e->rng >> 8) & 0xFFFF) / 32768.0f - 1.0f);
}

float elv_ecg_next(elv_ecg_t *e, double artifact_m)
{
    double t = (double)e->n++ / ELV_ECG_HZ;
    float v = 0.0f;

    if (e->replay && e->replay_len) {
        v = e->replay[(e->n - 1) % e->replay_len];
    } else switch (e->rhythm) {
    case ELV_RHYTHM_SHOCKABLE: {           /* coarse VF, 4-7 Hz, drifting */
        double a1 = 0.35 + 0.15 * sin(2 * M_PI * 0.23 * t);
        double a2 = 0.25 + 0.10 * sin(2 * M_PI * 0.31 * t + 1.0);
        v = (float)(a1 * sin(2 * M_PI * 4.6 * t) +
                    a2 * sin(2 * M_PI * 6.1 * t + 0.7) +
                    0.12 * sin(2 * M_PI * 7.3 * t + 2.1));
        v += noise(e, 0.03f);
        break;
    }
    case ELV_RHYTHM_ORGANIZED: {           /* 75 bpm narrow QRS + T wave */
        double rr = 0.8, ph = fmod(t, rr);
        v = (float)(1.2 * exp(-pow((ph - 0.20) / 0.012, 2)) -
                    0.15 * exp(-pow((ph - 0.17) / 0.010, 2)) +
                    0.25 * exp(-pow((ph - 0.45) / 0.050, 2)));
        v += noise(e, 0.02f);
        break;
    }
    case ELV_RHYTHM_ASYSTOLE:
    default:
        v = noise(e, 0.02f);
        break;
    }
    /* compression artifact: proportional to chest displacement */
    v += (float)(artifact_m / 0.05 * 1.5);
    return v;
}
