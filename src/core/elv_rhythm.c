/*
 * Elvrek: extended rhythm features and the frozen rhythm model.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Twelve standard features of a short ECG window feed a small neural
 * network whose weights are trained offline on CUDB only (tools/zp_train.py)
 * and frozen into include/elv_rhythm_model.h.
 *
 * The features are computed in stages so the safety process can spread the
 * work over many control ticks: elv_rf_begin() does the time domain part
 * once, then each elv_rf_step() computes one of the 77 spectral bins (about
 * 750 multiply-adds), and the last step finishes the spectral features.
 * elv_rhythm_features_ext() runs the same stages back to back for the
 * offline tools, so both give identical numbers. Deterministic, no
 * allocation, fixed cost per call.
 */
#include <math.h>
#include <string.h>
#include "elv_core.h"
#include "elv_rhythm_model.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void elv_rf_begin(elv_rf_t *st, const float *in, uint32_t n)
{
    float *out = st->out;
    memset(st, 0, sizeof(*st));
    if (n < 100 || n > ELV_RF_MAXN) return;
    float x[ELV_RF_MAXN];
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (uint32_t i = 0; i < n; i++) { sx += i; sy += in[i]; sxx += (double)i * i; sxy += i * (double)in[i]; }
    double den = n * sxx - sx * sx;
    double b = den != 0 ? (n * sxy - sx * sy) / den : 0, a = (sy - b * sx) / n;
    float lo = 1e9f, hi = -1e9f;
    double m2 = 0, m4 = 0, dsum = 0;
    for (uint32_t i = 0; i < n; i++) {
        x[i] = (float)(in[i] - (a + b * i));
        if (x[i] < lo) lo = x[i];
        if (x[i] > hi) hi = x[i];
        m2 += (double)x[i] * x[i];
        if (i) dsum += fabs(x[i] - x[i - 1]);
    }
    float ptp = hi - lo;
    out[0] = (float)log10(ptp + 1e-3);
    if (ptp <= 1e-6f) return;                /* nothing to analyse: done */
    m2 /= n;
    for (uint32_t i = 0; i < n; i++) { double d = x[i]; m4 += d * d * d * d; }
    m4 /= n;

    uint32_t hist[64] = {0};
    for (uint32_t i = 0; i < n; i++) hist[(int)((x[i] - lo) / ptp * 63.0f)]++;
    uint32_t acc = 0; int mb;
    for (mb = 0; mb < 64; mb++) { acc += hist[mb]; if (acc >= n / 2) break; }
    float median = lo + (mb + 0.5f) * ptp / 63.0f;
    float maxd = 0;
    for (uint32_t i = 1; i < n; i++) { float d = fabsf(x[i] - x[i - 1]); if (d > maxd) maxd = d; }
    uint32_t flat = 0, xings = 0, steep = 0, peaks = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (fabsf(x[i] - median) < 0.10f * ptp) flat++;
        if (i && (x[i - 1] - median) * (x[i] - median) < 0) xings++;
        if (i && fabsf(x[i] - x[i - 1]) > 0.5f * maxd) steep++;
        if (i && i + 1 < n && x[i] > x[i - 1] && x[i] >= x[i + 1] && x[i] - median > 0.3f * ptp) peaks++;
    }
    double secs = (double)n / ELV_ECG_HZ;
    out[1] = (float)flat / n;
    out[2] = (float)(xings / 2.0 / secs);
    out[3] = (float)steep / n;
    out[9] = (float)log10(m4 / (m2 * m2 + 1e-12) + 1e-6);   /* kurtosis: QRS spikes are peaky */
    out[10] = (float)(peaks / secs);
    out[11] = (float)(dsum / (n - 1) / ptp * ELV_ECG_HZ);    /* how fast it moves, per amplitude */

    static float hann[ELV_RF_MAXN];
    static uint32_t hann_n;
    if (hann_n != n) {
        for (uint32_t i = 0; i < n; i++) hann[i] = (float)(0.5 - 0.5 * cos(2.0 * M_PI * i / (n - 1)));
        hann_n = n;
    }
    for (uint32_t i = 0; i < n; i++) st->xw[i] = x[i] * hann[i];
    st->n = n;
    st->k = 0;
    st->active = true;
}

bool elv_rf_step(elv_rf_t *st)
{
    if (!st->active) return true;
    if (st->k < ELV_RF_BINS) {
        /* one spectral bin, 1 to 20 Hz in 0.25 Hz steps, Goertzel */
        double hz = 1.0 + 0.25 * st->k, cw = 2.0 * cos(2.0 * M_PI * hz / ELV_ECG_HZ), s1 = 0, s2 = 0;
        for (uint32_t i = 0; i < st->n; i++) { double s0 = st->xw[i] + cw * s1 - s2; s2 = s1; s1 = s0; }
        st->p[st->k++] = s1 * s1 + s2 * s2 - cw * s1 * s2;
        if (st->k < ELV_RF_BINS) return false;
    }
    double tot = 0, fib = 0, low = 0, high = 0, best = -1;
    int bi = 0;
    for (int k = 0; k < ELV_RF_BINS; k++) {
        double hz = 1.0 + 0.25 * k;
        tot += st->p[k];
        if (hz >= 3.0 && hz <= 9.0) fib += st->p[k];
        if (hz < 3.0) low += st->p[k];
        if (hz > 9.0) high += st->p[k];
        if (st->p[k] > best) { best = st->p[k]; bi = k; }
    }
    double conc = 0;
    for (int k = bi - 4; k <= bi + 4; k++) if (k >= 0 && k < ELV_RF_BINS) conc += st->p[k];
    st->out[4] = tot > 0 ? (float)(fib / tot) : 0;
    st->out[5] = (float)(1.0 + 0.25 * bi);
    st->out[6] = tot > 0 ? (float)(conc / tot) : 0;
    st->out[7] = tot > 0 ? (float)(low / tot) : 0;
    st->out[8] = tot > 0 ? (float)(high / tot) : 0;
    st->active = false;
    return true;
}

void elv_rhythm_features_ext(const float *in, uint32_t n, float out[ELV_RF_N])
{
    static elv_rf_t st;
    elv_rf_begin(&st, in, n);
    while (!elv_rf_step(&st)) { }
    memcpy(out, st.out, sizeof(st.out));
}

float elv_rhythm_score(const float f[ELV_RF_N])
{
    if (f[0] < ELV_RM_GATE) return 0.0f;      /* too small to shock: fine VF, asystole */
    float z[ELV_RF_N], h1[ELV_RM_H1], h2[ELV_RM_H2];
    for (int i = 0; i < ELV_RF_N; i++) z[i] = (f[i] - ELV_RM_MEAN[i]) / ELV_RM_STD[i];
    for (int j = 0; j < ELV_RM_H1; j++) {
        float a = ELV_RM_B1[j];
        for (int i = 0; i < ELV_RF_N; i++) a += z[i] * ELV_RM_W1[i * ELV_RM_H1 + j];
        h1[j] = tanhf(a);
    }
    for (int j = 0; j < ELV_RM_H2; j++) {
        float a = ELV_RM_B2[j];
        for (int i = 0; i < ELV_RM_H1; i++) a += h1[i] * ELV_RM_W2[i * ELV_RM_H2 + j];
        h2[j] = tanhf(a);
    }
    float o = ELV_RM_B3;
    for (int i = 0; i < ELV_RM_H2; i++) o += h2[i] * ELV_RM_W3[i];
    return 1.0f / (1.0f + expf(-o));
}

int8_t elv_rhythm_verdict(float m)
{
    if (m >= ELV_RM_SURE_SHOCK) return 1;
    if (m <= ELV_RM_SURE_NOT) return -1;
    return 0;
}
