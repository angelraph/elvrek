/*
 * Elvrek: tune the trusted rhythm check on one database, score it on another.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tuning set: CUDB (35 records). Held-out set: VFDB (22 records), scored once
 * with the thresholds chosen on CUDB. Windows are 0.8 s of clean ECG, as in
 * the shock window, taken every 2 s, and used only when every sample carries
 * the same database label. VT is left out of both sides (AHA: intermediate).
 *
 * Rule for choosing thresholds, fixed before looking at VFDB: among all grid
 * points whose specificity on CUDB is at least 95% for every non-shockable
 * class present, take the one with the highest VF sensitivity.
 *
 * Usage: rhythm_tune followed by the .bin files in build/ecg
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elv_core.h"

enum { L_EXCL, L_SHOCK, L_VT, L_NSR, L_OTHER, L_ASYS, NL };
typedef struct { elv_rhythm_feat_t f; uint8_t lab, db; } win_t;

static win_t *W; static size_t NW, CAP;

static void add(const elv_rhythm_feat_t *f, int lab, int db)
{
    if (NW == CAP) { CAP = CAP ? CAP * 2 : 4096; W = realloc(W, CAP * sizeof(win_t)); }
    W[NW].f = *f; W[NW].lab = (uint8_t)lab; W[NW].db = (uint8_t)db; NW++;
}

typedef struct { double sens, spec[NL]; uint32_t n[NL]; } score_t;

static score_t score(const elv_rhythm_thr_t *t, int db)
{
    score_t s; memset(&s, 0, sizeof(s));
    uint32_t hit[NL] = {0};
    for (size_t i = 0; i < NW; i++) {
        if (W[i].db != db) continue;
        s.n[W[i].lab]++;
        if (elv_rhythm_decide(&W[i].f, t)) hit[W[i].lab]++;
    }
    s.sens = s.n[L_SHOCK] ? (double)hit[L_SHOCK] / s.n[L_SHOCK] : 0;
    for (int c = L_NSR; c < NL; c++) s.spec[c] = s.n[c] ? 1.0 - (double)hit[c] / s.n[c] : 1;
    return s;
}

static void show(const char *name, const score_t *s)
{
    printf("  %s\n", name);
    printf("    VF/flutter found        %5.1f%% of %u windows (AHA goal above 90%%)\n", 100 * s->sens, s->n[L_SHOCK]);
    if (s->n[L_NSR])   printf("    normal sinus left alone %5.1f%% of %u (goal above 99%%)\n", 100 * s->spec[L_NSR], s->n[L_NSR]);
    if (s->n[L_OTHER]) printf("    other rhythms left alone %5.1f%% of %u (goal above 95%%)\n", 100 * s->spec[L_OTHER], s->n[L_OTHER]);
    if (s->n[L_ASYS])  printf("    asystole left alone     %5.1f%% of %u (goal above 95%%)\n", 100 * s->spec[L_ASYS], s->n[L_ASYS]);
}

int main(int argc, char **argv)
{
    const uint32_t tw = ELV_CONFIRM_TICKS / ELV_ECG_TICKS_PER_SAMPLE;
    for (int a = 1; a < argc; a++) {
        FILE *fp = fopen(argv[a], "rb");
        if (!fp) continue;
        char mg[4]; uint32_t fs, n;
        if (fread(mg, 1, 4, fp) != 4 || memcmp(mg, "ELVE", 4) || fread(&fs, 4, 1, fp) != 1 ||
            fread(&n, 4, 1, fp) != 1 || fs != ELV_ECG_HZ) { fclose(fp); continue; }
        float *mv = malloc(n * sizeof(float)); uint8_t *lab = malloc(n);
        if (fread(mv, 4, n, fp) == n && fread(lab, 1, n, fp) == n) {
            int db = strstr(argv[a], "vfdb_") ? 1 : 0;
            for (uint32_t s = 0; s + tw <= n; s += 2 * ELV_ECG_HZ) {
                int l = lab[s], same = 1;
                for (uint32_t k = 1; k < tw && same; k++) same = lab[s + k] == l;
                if (!same || l == L_EXCL || l == L_VT) continue;
                elv_rhythm_feat_t f; elv_rhythm_features(mv + s, tw, &f);
                add(&f, l, db);
            }
        }
        fclose(fp); free(mv); free(lab);
    }
    printf("Windows: %zu\n\n", NW);

    printf("Current thresholds\n");
    score_t c0 = score(&ELV_RHYTHM_THR, 0), v0 = score(&ELV_RHYTHM_THR, 1);
    show("CUDB", &c0); show("VFDB", &v0);

    const float P[] = { 0.10f, 0.15f, 0.20f, 0.30f, 0.40f };
    const float F[] = { 0.20f, 0.25f, 0.30f, 0.35f, 0.40f, 0.45f, 0.50f, 0.60f };
    const float S[] = { 0.0f, 0.04f, 0.08f, 0.12f, 0.16f, 0.20f, 0.25f };
    const float X1[] = { 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f };
    const float X2[] = { 8.0f, 10.0f, 12.0f, 15.0f, 20.0f, 30.0f };
    elv_rhythm_thr_t best = ELV_RHYTHM_THR; double bs = -1;
    for (unsigned a = 0; a < sizeof P / sizeof *P; a++)
    for (unsigned b = 0; b < sizeof F / sizeof *F; b++)
    for (unsigned c = 0; c < sizeof S / sizeof *S; c++)
    for (unsigned d = 0; d < sizeof X1 / sizeof *X1; d++)
    for (unsigned e = 0; e < sizeof X2 / sizeof *X2; e++) {
        elv_rhythm_thr_t t = { P[a], F[b], S[c], X1[d], X2[e] };
        score_t s = score(&t, 0);
        int ok = 1;
        for (int k = L_NSR; k < NL; k++) if (s.n[k] && s.spec[k] < 0.95) ok = 0;
        if (ok && s.sens > bs) { bs = s.sens; best = t; }
    }
    printf("\nChosen on CUDB only: min_ptp %.2f  max_flat %.2f  min_steep %.2f  xing %.1f to %.1f Hz\n",
           best.min_ptp, best.max_flat, best.min_steep, best.min_xing, best.max_xing);
    score_t c1 = score(&best, 0), v1 = score(&best, 1);
    show("CUDB (tuning set)", &c1);
    show("VFDB (held out, scored once)", &v1);
    return 0;
}
