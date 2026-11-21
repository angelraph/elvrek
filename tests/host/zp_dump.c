/*
 * Elvrek: compute rhythm features for every window of real arrest ECG, clean
 * and during modelled compressions (with and without the piston-referenced
 * canceller), and write them as CSV for tools/zp_train.py.
 * SPDX-License-Identifier: Apache-2.0
 *
 * The compression artifact model is described in docs/ZERO-PAUSE.md. It
 * deliberately contains parts the canceller cannot see: a wandering chest
 * delay, a nonlinear mix of depth, speed and depth squared, drifting gain,
 * electrode motion noise not tied to the piston, and baseline wander. The
 * canceller only gets the compression phase the controller commands.
 *
 * Usage: zp_dump [-u share] [-m mu] out.csv followed by the .bin files
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elv_cancel.h"
#include "elv_core.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum { L_EXCL, L_SHOCK, L_VT, L_NSR, L_OTHER, L_ASYS };
static uint64_t rs = 88172645463325252ull;
static double urand(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (rs >> 11) * (1.0 / 9007199254740992.0); }
static double grand(void) { return (urand() + urand() + urand() + urand() - 2.0) * 1.7320508; }

static double U_SHARE = 0.15;
static float MU = 0.01f;
static const double SNRS[] = { -9, -6, -3, 0, 3 };

static void make_artifact(const float *mv, uint32_t n, double snr_db, float *art, double *ph)
{
    const double fs = ELV_ECG_HZ, fc = ELV_RATE_DEFAULT_CPM / 60.0, D = 0.053, tau = 0.015;
    double a1 = 0.6 + 0.8 * urand(), a2 = (urand() - 0.5) * 0.8, a3 = (urand() - 0.5) * 1.2;
    double sign = urand() < 0.5 ? -1 : 1, delay = 0.02, gl = 0, x = 0, cprev = 0, ph0 = urand();
    const int up = 4, dl = 64;
    double hist[64] = {0}; int hp = 0;
    double n1 = 0, n2 = 0;
    /* phase rotation for the commanded raised cosine at the upsampled rate */
    double dth = 2 * M_PI * fc / (fs * up), cr = cos(2 * M_PI * ph0), sr = sin(2 * M_PI * ph0);
    const double cd = cos(dth), sd = sin(dth);
    float *raw = malloc(n * sizeof(float)), *unc = malloc(n * sizeof(float));
    for (uint32_t i = 0; i < n; i++) {
        for (int k = 0; k < up; k++) {
            double cmd = D * 0.5 * (1 - cr);
            double c2 = cr * cd - sr * sd; sr = sr * cd + cr * sd; cr = c2;
            x += (cmd - x) / (tau * fs * up);
            hist[hp] = x; hp = (hp + 1) % dl;
        }
        if ((i & 255) == 0) { double m = sqrt(cr * cr + sr * sr); cr /= m; sr /= m; }
        delay += 0.0004 * grand(); if (delay < 0) delay = 0; if (delay > 0.04) delay = 0.04;
        int ds = (int)(delay * fs * up); if (ds > dl - 2) ds = dl - 2;
        double c = hist[(hp - 1 - ds + dl * 4) % dl];
        double v = (c - cprev) * fs; cprev = c;
        gl += 0.002 * grand(); if (gl > 0.4) gl = 0.4; if (gl < -0.4) gl = -0.4;
        raw[i] = (float)(sign * exp(gl) * (a1 * c / D + a2 * v * 0.1 / D + a3 * (c / D) * (c / D)));
        double w = grand(); n1 += 0.26 * (w - n1); n2 += 0.012 * (n1 - n2);
        unc[i] = (float)(n1 - n2);
        ph[i] = fmod(fc * (double)i / fs + ph0, 1.0);
    }
    const uint32_t seg = 10 * ELV_ECG_HZ;
    for (uint32_t s0 = 0; s0 < n; s0 += seg) {
        uint32_t s1 = s0 + seg < n ? s0 + seg : n;
        double pe = 0, me = 0, pa = 0, ma = 0, pu = 0;
        for (uint32_t i = s0; i < s1; i++) { me += mv[i]; ma += raw[i]; }
        me /= (s1 - s0); ma /= (s1 - s0);
        for (uint32_t i = s0; i < s1; i++) {
            pe += (mv[i] - me) * (mv[i] - me); pa += (raw[i] - ma) * (raw[i] - ma); pu += unc[i] * unc[i];
        }
        pe /= (s1 - s0); pa /= (s1 - s0); pu /= (s1 - s0);
        if (pe < 1e-6) pe = 1e-6;
        double target = pe / pow(10, snr_db / 10);
        double ka = sqrt(target * (1 - U_SHARE) / (pa + 1e-12)), ku = sqrt(target * U_SHARE / (pu + 1e-12));
        for (uint32_t i = s0; i < s1; i++)
            art[i] = (float)(ka * (raw[i] - ma) + ku * unc[i] +
                             0.15 * sqrt(target) * sin(2 * M_PI * 0.3 * i / ELV_ECG_HZ));
    }
    free(raw); free(unc);
}

int main(int argc, char **argv)
{
    int a = 1;
    for (; a < argc && argv[a][0] == '-'; a += 2) {
        if (!strcmp(argv[a], "-u")) U_SHARE = atof(argv[a + 1]);
        else if (!strcmp(argv[a], "-m")) MU = (float)atof(argv[a + 1]);
    }
    if (a >= argc) { fprintf(stderr, "usage: zp_dump [-u share] [-m mu] out.csv files...\n"); return 1; }
    FILE *out = fopen(argv[a++], "w");
    fprintf(out, "rec,db,cond,snr,start,label,old");
    for (int k = 0; k < ELV_RF_N; k++) fprintf(out, ",f%d", k);
    fprintf(out, "\n");
    const uint32_t WIN = 3 * ELV_ECG_HZ, HOP = ELV_ECG_HZ;
    int rec = 0;
    for (; a < argc; a++, rec++) {
        FILE *fp = fopen(argv[a], "rb"); if (!fp) continue;
        char mg[4]; uint32_t fs, n;
        if (fread(mg, 1, 4, fp) != 4 || memcmp(mg, "ELVE", 4) || fread(&fs, 4, 1, fp) != 1 ||
            fread(&n, 4, 1, fp) != 1 || fs != ELV_ECG_HZ) { fclose(fp); continue; }
        float *mv = malloc(n * 4); uint8_t *lab = malloc(n);
        if (fread(mv, 4, n, fp) != n || fread(lab, 1, n, fp) != n) { fclose(fp); continue; }
        fclose(fp);
        int db = strstr(argv[a], "vfdb_") ? 1 : 0;
        float *art = malloc(n * 4), *y = malloc(n * 4); double *ph = malloc(n * sizeof(double));
        /* cond 0: clean. cond 1: artifact, no canceller (0 dB). cond 2: canceller, per SNR */
        for (int cond = 0; cond < 3; cond++)
            for (int q = 0; q < 5; q++) {
                if (cond == 0 && q) break;
                if (cond == 1 && q != 3) continue;
                double snr = cond ? SNRS[q] : 99;
                rs = 1469598103934665603ull ^ ((uint64_t)rec << 20) ^ ((uint64_t)q << 8);
                if (cond) make_artifact(mv, n, snr, art, ph);
                elv_cancel_t c; elv_cancel_init(&c, MU);
                for (uint32_t i = 0; i < n; i++) {
                    float v = mv[i] + (cond ? art[i] : 0.0f);
                    y[i] = cond == 2 ? elv_cancel_step(&c, v, ph[i], true) : v;
                }
                for (uint32_t s = 5 * ELV_ECG_HZ; s + WIN <= n; s += HOP) {
                    int l = lab[s], same = 1;
                    for (uint32_t k = 1; k < WIN && same; k++) same = lab[s + k] == l;
                    if (!same || l == L_EXCL || l == L_VT) continue;
                    float f[ELV_RF_N]; elv_rhythm_features_ext(y + s, WIN, f);
                    elv_rhythm_feat_t of; elv_rhythm_features(y + s, WIN, &of);
                    int old = elv_rhythm_decide(&of, &ELV_RHYTHM_THR);
                    fprintf(out, "%d,%d,%d,%.0f,%u,%d,%d", rec, db, cond, snr, s, l, old);
                    for (int k = 0; k < ELV_RF_N; k++) fprintf(out, ",%.5g", f[k]);
                    fprintf(out, "\n");
                }
            }
        free(mv); free(lab); free(art); free(y); free(ph);
        fprintf(stderr, "%s done\n", argv[a]);
    }
    fclose(out);
    return 0;
}
