/*
 * Elvrek: render the crew console for a few controller states into PPM
 * images, so the design can be checked on the host.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Usage: console_preview outdir
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elv_console_draw.h"

static void save(const char *path, const uint32_t *px, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        unsigned char c[3] = { (unsigned char)(px[i] >> 16), (unsigned char)(px[i] >> 8), (unsigned char)px[i] };
        fwrite(c, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const int W = 1280, H = 768;
    uint32_t *px = malloc(sizeof(uint32_t) * W * H);
    static elv_view_t v;
    char line[1024], path[256];
    static const struct { const char *name, *fmt; } st[] = {
        { "1-compress", "state=COMPRESS sw=0 fault=NONE ccf=0.9990 shocks=0 compressions=%u breaths=9 pos_mm=52.4 force_n=512 etco2=%.1f charge=0 ready=0 zp_checks=0 zp_skipped=0 zp_sure=0 zp_unsure=0 zp_score=0.00 next_check=%u in_window=0 max_machine=0 emulator=1" },
        { "2-charging", "state=COMPRESS sw=0 fault=NONE ccf=0.9990 shocks=0 compressions=%u breaths=19 pos_mm=31.0 force_n=240 etco2=%.1f charge=1 ready=0 zp_checks=0 zp_skipped=0 zp_sure=0 zp_unsure=0 zp_score=0.97 next_check=%u in_window=0 max_machine=0 emulator=1" },
        { "3-press", "state=SHOCK_WINDOW sw=2 fault=NONE ccf=0.9990 shocks=0 compressions=%u breaths=20 pos_mm=0.0 force_n=0 etco2=%.1f charge=0 ready=1 zp_checks=1 zp_skipped=0 zp_sure=1 zp_unsure=0 zp_score=0.97 next_check=%u in_window=120 max_machine=0 emulator=1" },
        { "4-shocked", "state=COMPRESS sw=3 fault=NONE ccf=0.9995 shocks=1 compressions=%u breaths=20 pos_mm=44.0 force_n=410 etco2=%.1f charge=0 ready=0 zp_checks=1 zp_skipped=0 zp_sure=1 zp_unsure=0 zp_score=0.97 next_check=%u in_window=0 max_machine=3 emulator=1" },
        { "5-rosc", "state=ROSC_HOLD sw=3 fault=NONE ccf=0.9946 shocks=1 compressions=%u breaths=26 pos_mm=0.0 force_n=0 etco2=%.1f charge=0 ready=0 zp_checks=1 zp_skipped=0 zp_sure=1 zp_unsure=0 zp_score=0.97 next_check=%u in_window=0 max_machine=3 emulator=1" },
        { "6-nopause", "state=COMPRESS sw=0 fault=NONE ccf=1.0000 shocks=0 compressions=%u breaths=20 pos_mm=50.0 force_n=500 etco2=%.1f charge=0 ready=0 zp_checks=1 zp_skipped=1 zp_sure=0 zp_unsure=0 zp_score=0.08 next_check=%u in_window=0 max_machine=0 emulator=1" },
    };
    double t = 0;
    unsigned comp = 0;
    unsigned long long tick = 2000;
    for (int s = 0; s < 6; s++) {
        for (int k = 0; k < 60; k++) {                  /* 30 s of history per state */
            t += 0.5; tick += 500; comp += 1;
            double et = s >= 4 ? 30 + k * 0.15 : 10 + (s * 60 + k) * 0.01;
            int n = snprintf(line, sizeof line, "tick=%llu ", tick);
            snprintf(line + n, sizeof line - n, st[s].fmt, comp, et, (unsigned)(83000 - k * 500));
            elv_view_update(&v, line, t);
        }
        if (s == 3) v.shock_seen_at = t - 1.0;
        if (s == 5) v.skip_seen_at = t - 1.0;
        elv_console_draw(px, W, H, W, &v);
        snprintf(path, sizeof path, "%s/console-%s.ppm", argc > 1 ? argv[1] : ".", st[s].name);
        save(path, px, W, H);
    }
    free(px);
    return 0;
}
