/*
 * Elvrek: rhythm evaluation on real arrest ECG (PhysioNet CUDB and VFDB).
 * SPDX-License-Identifier: Apache-2.0
 *
 * Two checks are measured, each on recordings the thresholds were never
 * tuned on (they were set on synthetic signals before this data was seen):
 *
 *  1. The trusted check inside the safety core, elv_trusted_shockable(),
 *     on 0.8 s windows of clean ECG. This is what decides a shock while the
 *     piston is parked.
 *  2. The untrusted advisor on 4 s windows with a compression artifact
 *     added, the way it runs while the piston is moving. The artifact is a
 *     model of a mechanical compression (raised cosine at the compression
 *     rate, random amplitude and phase), not a recorded one; real recordings
 *     with mechanical CPR artifact are not public.
 *
 * Window labels come from the databases' own rhythm annotations. A window is
 * used only if every sample carries the same label. VT is reported but not
 * scored, as the AHA performance recommendations treat it as intermediate.
 *
 * Usage: ecg_eval followed by the .bin files in build/ecg
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elv_core.h"
#include "elv_advisor.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum { L_EXCL, L_SHOCK, L_VT, L_NSR, L_OTHER, L_ASYS, NL };
static const char *lname[NL] = { "excluded", "VF/flutter", "VT", "normal sinus",
                                 "other non-shockable", "asystole" };

typedef struct { uint32_t n[NL], said_shock[NL]; } tally_t;

static uint32_t rng = 2463534242u;
static double urand(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng / 4294967296.0; }

static int uniform_label(const uint8_t *lab, uint32_t from, uint32_t len)
{
    for (uint32_t k = 1; k < len; k++) if (lab[from + k] != lab[from]) return -1;
    return lab[from];
}

static void print_tally(const char *title, const tally_t *t)
{
    printf("  %s\n", title);
    for (int c = 1; c < NL; c++) {
        if (!t->n[c]) continue;
        double pct = 100.0 * t->said_shock[c] / t->n[c];
        if (c == L_SHOCK)
            printf("    %-20s %6u windows  sensitivity %5.1f%%   (AHA goal for coarse VF: > 90%%)\n",
                   lname[c], t->n[c], pct);
        else if (c == L_VT)
            printf("    %-20s %6u windows  called shockable %5.1f%%   (intermediate, not scored)\n",
                   lname[c], t->n[c], pct);
        else
            printf("    %-20s %6u windows  specificity %5.1f%%   (AHA goal: %s)\n",
                   lname[c], t->n[c], 100.0 - pct,
                   c == L_NSR ? "> 99%" : c == L_ASYS ? "> 95%" : "> 95%");
    }
}

int main(int argc, char **argv)
{
    tally_t trusted[2], advisor[2];
    memset(trusted, 0, sizeof(trusted));
    memset(advisor, 0, sizeof(advisor));
    static elv_advisor_t adv;
    int files = 0;

    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        if (!f) continue;
        char magic[4]; uint32_t fs, n;
        if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "ELVE", 4) ||
            fread(&fs, 4, 1, f) != 1 || fread(&n, 4, 1, f) != 1 || fs != ELV_ECG_HZ) {
            fprintf(stderr, "skip %s (format or rate)\n", argv[a]);
            fclose(f);
            continue;
        }
        float *mv = malloc(n * sizeof(float));
        uint8_t *lab = malloc(n);
        if (fread(mv, sizeof(float), n, f) != n || fread(lab, 1, n, f) != n) {
            fclose(f); free(mv); free(lab); continue;
        }
        fclose(f);
        int db = strstr(argv[a], "vfdb_") ? 1 : 0;
        files++;

        /* 1. trusted check: 0.8 s clean windows every 2 s */
        const uint32_t tw = ELV_CONFIRM_TICKS / ELV_ECG_TICKS_PER_SAMPLE;
        for (uint32_t s = 0; s + tw <= n; s += 2 * ELV_ECG_HZ) {
            int l = uniform_label(lab, s, tw);
            if (l <= L_EXCL) continue;
            trusted[db].n[l]++;
            if (elv_trusted_shockable(mv + s, tw)) trusted[db].said_shock[l]++;
        }

        /* 2. advisor: 4 s windows every 4 s, with a compression artifact */
        const double fc = ELV_RATE_DEFAULT_CPM / 60.0;
        for (uint32_t s = 0; s + ELV_ADV_WIN <= n; s += ELV_ADV_WIN) {
            int l = uniform_label(lab, s, ELV_ADV_WIN);
            if (l <= L_EXCL) continue;
            double amp = 0.5 + 1.5 * urand(), ph = urand();
            elv_advisor_init(&adv, (float)fc);
            for (uint32_t k = 0; k < ELV_ADV_WIN; k++) {
                double t = (double)k / ELV_ECG_HZ;
                double art = amp * 0.5 * (1.0 - cos(2 * M_PI * (fc * t + ph)));
                elv_advisor_push(&adv, mv[s + k] + (float)art);
            }
            elv_advisor_msg_t m;
            if (!elv_advisor_classify(&adv, &m)) continue;
            advisor[db].n[l]++;
            if (m.rhythm == ELV_RHYTHM_SHOCKABLE) advisor[db].said_shock[l]++;
        }
        free(mv);
        free(lab);
    }

    printf("Elvrek rhythm evaluation on real arrest ECG (%d records)\n", files);
    printf("Thresholds were fixed on synthetic signals before this data was used.\n\n");
    const char *dbn[2] = { "CUDB (Creighton, 35 records)", "VFDB (MIT-BIH malignant ventricular, 22 records)" };
    for (int d = 0; d < 2; d++) {
        printf("%s\n", dbn[d]);
        print_tally("Trusted check, 0.8 s, piston parked (decides the shock):", &trusted[d]);
        print_tally("Advisor, 4 s, during compressions (proposes only):", &advisor[d]);
        printf("\n");
    }
    return files ? 0 : 1;
}
