/*
 * Elvrek: replay a crew console log recorded on the QNX target (elv-console -l)
 * through the same drawing code, frame by frame.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Usage:
 *   console_replay log.txt > frames.rgb          raw RGB frames on stdout (for ffmpeg)
 *   console_replay log.txt -c target.ppm N       compare frame N with a frame saved
 *                                                from the Screen buffer on the target
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elv_console_draw.h"

#define W 1280
#define H 768

static unsigned char *load_ppm(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    int w, h, mx;
    if (fscanf(f, "P6 %d %d %d", &w, &h, &mx) != 3 || w != W || h != H) { fclose(f); return NULL; }
    fgetc(f);
    unsigned char *p = malloc((size_t)W * H * 3);
    if (fread(p, 1, (size_t)W * H * 3, f) != (size_t)W * H * 3) { free(p); p = NULL; }
    fclose(f);
    return p;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: console_replay log [-c target.ppm frame]\n"); return 2; }
    FILE *in = fopen(argv[1], "r");
    if (!in) { perror(argv[1]); return 1; }
    const char *cmp = NULL;
    long cmp_frame = -1;
    if (argc >= 5 && !strcmp(argv[2], "-c")) { cmp = argv[3]; cmp_frame = atol(argv[4]); }

    static elv_view_t v;
    static uint32_t px[W * H];
    static unsigned char rgb[W * H * 3];
    static char buf[4096];
    long frames = 0;
    while (fgets(buf, sizeof buf, in)) {
        long fr; double t; int off = 0;
        if (sscanf(buf, "%ld %lf %n", &fr, &t, &off) < 2) continue;
        char *line = buf + off;
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        elv_view_feed(&v, line, t);
        if (cmp && fr != cmp_frame) continue;
        elv_console_draw(px, W, H, W, &v);
        for (int i = 0; i < W * H; i++) {
            rgb[3 * i] = (unsigned char)(px[i] >> 16);
            rgb[3 * i + 1] = (unsigned char)(px[i] >> 8);
            rgb[3 * i + 2] = (unsigned char)px[i];
        }
        if (cmp) {
            unsigned char *tp = load_ppm(cmp);
            if (!tp) { fprintf(stderr, "cannot read %s\n", cmp); return 1; }
            long diff = 0; int maxd = 0;
            for (long i = 0; i < (long)W * H * 3; i++) {
                int d = abs((int)tp[i] - (int)rgb[i]);
                if (d) diff++;
                if (d > maxd) maxd = d;
            }
            printf("frame %ld: %ld of %d channel values differ, largest difference %d\n", fr, diff, W * H * 3, maxd);
            free(tp);
            return diff ? 3 : 0;
        }
        fwrite(rgb, 1, sizeof rgb, stdout);
        frames++;
    }
    fprintf(stderr, "console_replay: %ld frames\n", frames);
    return 0;
}
