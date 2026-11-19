/*
 * Elvrek: advisor classifier.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Spectral features with the compression fundamental notched out:
 *   residual power, and the share of it in the 3.5-9 Hz fibrillation band.
 * Intentionally simple. The safety argument does not depend on it being right.
 */
#include "elv_advisor.h"
#include "elv_limits.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void elv_advisor_init(elv_advisor_t *a, float cpr_hz)
{
    memset(a, 0, sizeof(*a));
    a->cpr_hz = cpr_hz;
}

void elv_advisor_push(elv_advisor_t *a, float mv)
{
    a->buf[a->head] = mv;
    a->head = (a->head + 1) % ELV_ADV_WIN;
    if (a->count < ELV_ADV_WIN) a->count++;
}

static double goertzel(const float *x, int n, double hz)
{
    double w = 2.0 * M_PI * hz / ELV_ECG_HZ, cw = 2.0 * cos(w);
    double s0, s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) { s0 = x[i] + cw * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - cw * s1 * s2;
}

int elv_advisor_classify(elv_advisor_t *a, elv_advisor_msg_t *msg)
{
    if (a->count < ELV_ADV_WIN) return 0;
    static float x[ELV_ADV_WIN];
    double mean = 0;
    for (int i = 0; i < ELV_ADV_WIN; i++) {
        x[i] = a->buf[(a->head + i) % ELV_ADV_WIN];
        mean += x[i];
    }
    mean /= ELV_ADV_WIN;
    for (int i = 0; i < ELV_ADV_WIN; i++) {
        /* Hann window */
        double h = 0.5 - 0.5 * cos(2.0 * M_PI * i / (ELV_ADV_WIN - 1));
        x[i] = (float)((x[i] - mean) * h);
    }

    double total = 0, fib = 0;
    for (double f = 0.75; f <= 15.0; f += 0.25) {
        bool notch = false;
        for (int k = 1; k <= 3; k++)
            if (fabs(f - k * a->cpr_hz) < 0.6) notch = true;
        if (notch) continue;
        double p = goertzel(x, ELV_ADV_WIN, f);
        total += p;
        if (f >= 3.5 && f <= 9.0) fib += p;
    }
    double rms = sqrt(total) / ELV_ADV_WIN * 2.0;   /* rough residual amplitude */
    double share = total > 0 ? fib / total : 0;

    memset(msg, 0, sizeof(*msg));
    msg->magic = ELV_ADVISOR_MAGIC;
    msg->seq = ++a->seq;
    if (rms < 0.02) {
        msg->rhythm = ELV_RHYTHM_ASYSTOLE;
        msg->confidence = 0.8f;
    } else if (share > 0.60) {
        msg->rhythm = ELV_RHYTHM_SHOCKABLE;
        msg->confidence = (float)fmin(1.0, 0.5 + (share - 0.6));
    } else {
        msg->rhythm = ELV_RHYTHM_ORGANIZED;
        msg->confidence = (float)fmin(1.0, 0.5 + (0.6 - share));
    }
    msg->rosc_likelihood = msg->rhythm == ELV_RHYTHM_ORGANIZED ? 0.5f : 0.05f;
    return 1;
}
