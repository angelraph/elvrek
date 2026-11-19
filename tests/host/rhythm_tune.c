/*
 * Elvrek: tune the trusted rhythm check on one database, score it on another.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tuning set: CUDB (35 records). Held-out set: VFDB (22 records), scored once
 * with the thresholds chosen on CUDB. Windows of clean ECG are taken every
 * 2 s and used only when every sample carries the same database label. VT is
 * left out of both sides (AHA: intermediate).
 *
 * Rule for choosing thresholds, fixed before looking at VFDB: among all grid
 * points whose CUDB specificity is at least 95% for every non-shockable class
 * present, take the one with the highest VF sensitivity.
 *
 * The run repeats this for three window lengths, because a longer look at
 * the rhythm costs hands-off time: 0.8 s, 1.6 s and 2.4 s.
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
typedef struct { float *mv; uint8_t *lab; uint32_t n; int db; } rec_t;

static win_t *W; static size_t NW, CAP;
static rec_t R[128]; static int NR;

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
    printf("    %-30s VF found %5.1f%%  sinus left %5.1f%%  other left %5.1f%%", name,
           100 * s->sens, 100 * s->spec[L_NSR], 100 * s->spec[L_OTHER]);
    if (s->n[L_ASYS]) printf("  asystole left %5.1f%%", 100 * s->spec[L_ASYS]);
    printf("\n");
}

static void windows(uint32_t tw)
{
    NW = 0;
    for (int r = 0; r < NR; r++)
        for (uint32_t s = 0; s + tw <= R[r].n; s += 2 * ELV_ECG_HZ) {
            int l = R[r].lab[s], same = 1;
            for (uint32_t k = 1; k < tw && same; k++) same = R[r].lab[s + k] == l;
            if (!same || l == L_EXCL || l == L_VT) continue;
            elv_rhythm_feat_t f; elv_rhythm_features(R[r].mv + s, tw, &f);
            add(&f, l, R[r].db);
        }
}

int main(int argc, char **argv)
{
    for (int a = 1; a < argc && NR < 128; a++) {
        FILE *fp = fopen(argv[a], "rb");
        if (!fp) continue;
        char mg[4]; uint32_t fs, n;
        if (fread(mg, 1, 4, fp) != 4 || memcmp(mg, "ELVE", 4) || fread(&fs, 4, 1, fp) != 1 ||
            fread(&n, 4, 1, fp) != 1 || fs != ELV_ECG_HZ) { fclose(fp); continue; }
        rec_t *r = &R[NR];
        r->mv = malloc(n * sizeof(float)); r->lab = malloc(n); r->n = n;
        r->db = strstr(argv[a], "vfdb_") ? 1 : 0;
        if (fread(r->mv, 4, n, fp) == n && fread(r->lab, 1, n, fp) == n) NR++;
        fclose(fp);
    }

    const float P[] = { 0.10f, 0.20f, 0.30f };
    const float F[] = { 0.30f, 0.40f, 0.50f, 0.70f };
    const float S[] = { 0.0f, 0.06f, 0.12f };
    const float X1[] = { 1.0f, 2.0f, 3.0f };
    const float X2[] = { 12.0f, 20.0f };
    const float FB[] = { 0.0f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f };
    const uint32_t lens[] = { 200, 400, 600 };

    for (unsigned L = 0; L < 3; L++) {
        windows(lens[L]);
        printf("Window %.1f s (%zu windows)\n", lens[L] / (double)ELV_ECG_HZ, NW);
        elv_rhythm_thr_t best = ELV_RHYTHM_THR; double bs = -1;
        for (unsigned a = 0; a < 3; a++) for (unsigned b = 0; b < 4; b++)
        for (unsigned c = 0; c < 3; c++) for (unsigned d = 0; d < 3; d++)
        for (unsigned e = 0; e < 2; e++) for (unsigned g = 0; g < 7; g++) {
            elv_rhythm_thr_t t = { P[a], F[b], S[c], X1[d], X2[e], FB[g] };
            score_t s = score(&t, 0);
            int ok = 1;
            for (int k = L_NSR; k < NL; k++) if (s.n[k] && s.spec[k] < 0.95) ok = 0;
            if (ok && s.sens > bs) { bs = s.sens; best = t; }
        }
        printf("  chosen on CUDB: ptp>=%.2f flat<%.2f steep>=%.2f xing %.0f..%.0f Hz fib>=%.2f\n",
               best.min_ptp, best.max_flat, best.min_steep, best.min_xing, best.max_xing, best.min_fib);
        score_t c1 = score(&best, 0), v1 = score(&best, 1);
        show("CUDB (tuning set)", &c1);
        show("VFDB (held out, scored once)", &v1);
    }
    return 0;
}
