/*
 * Elvrek: compression artifact canceller. See elv_cancel.h.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "elv_cancel.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void elv_cancel_init(elv_cancel_t *c, float mu)
{
    memset(c, 0, sizeof(*c));
    c->mu = mu;
}

float elv_cancel_step(elv_cancel_t *c, float x, double phase, bool compressing)
{
    if (!compressing) {
        c->out_pow += 0.01 * ((double)x * x - c->out_pow);
        return x;
    }
    float r[2 * ELV_CANCEL_HARMONICS];
    float est = 0.0f;
    for (int k = 0; k < ELV_CANCEL_HARMONICS; k++) {
        double a = 2.0 * M_PI * (k + 1) * phase;
        r[2 * k] = (float)cos(a);
        r[2 * k + 1] = (float)sin(a);
        est += c->w[2 * k] * r[2 * k] + c->w[2 * k + 1] * r[2 * k + 1];
    }
    float e = x - est;
    /* normalised LMS: the reference power is fixed at H, so mu / H */
    const float g = c->mu / (float)ELV_CANCEL_HARMONICS;
    for (int j = 0; j < 2 * ELV_CANCEL_HARMONICS; j++) c->w[j] += g * e * r[j];
    c->art_pow += 0.01 * ((double)est * est - c->art_pow);
    c->out_pow += 0.01 * ((double)e * e - c->out_pow);
    return e;
}

float elv_cancel_ratio(const elv_cancel_t *c)
{
    return c->out_pow > 1e-12 ? (float)(c->art_pow / c->out_pow) : 0.0f;
}
