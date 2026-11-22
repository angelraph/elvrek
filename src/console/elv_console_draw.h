/*
 * Elvrek crew console: drawing, independent of the display system.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ELV_CONSOLE_DRAW_H
#define ELV_CONSOLE_DRAW_H
#include <stdbool.h>
#include <stdint.h>

#define ELV_HIST 240               /* EtCO2 samples, one every 0.5 s: 2 minutes */
#define ELV_POS_RING 10            /* position reads kept for depth: 1 s at 10 Hz */
#define ELV_STROKES 9              /* rate is timed over the last 8 strokes */

typedef struct {
    /* from /dev/elvrek/status */
    char     state[24], fault[24];
    int      sw;
    unsigned long long tick;
    double   ccf, depth_mm, force_n, etco2, zp_score;
    unsigned shocks, compressions, breaths, charge, ready, zp_checks, zp_skipped, zp_sure, zp_unsure;
    unsigned next_check, in_window, preauth, max_machine, emulator;
    /* kept by the console between frames */
    double   etco2_hist[ELV_HIST];
    int      hist_n;
    double   rate_cpm;
    unsigned long long stroke_tick[ELV_STROKES];  /* controller tick at each counted stroke seen */
    unsigned stroke_comp[ELV_STROKES];
    int      stroke_n;
    unsigned prev_shocks, prev_skipped;
    double   shock_seen_at, skip_seen_at, now;  /* seconds */
    double   last_hist;
    double   pos_mm, pos_ring[ELV_POS_RING], force_ring[ELV_POS_RING];  /* last second of reads */
    int      pos_i;
} elv_view_t;

/* Parse one status line (key=value pairs) into the view. Returns false if the
   line carries no tick (the safety process is not there). */
bool elv_view_update(elv_view_t *v, const char *line, double now_s);

/* Feed one status read, empty when the read failed. The console and the host
   replay both call this, so a replayed log draws the same frames. */
bool elv_view_feed(elv_view_t *v, const char *line, double now_s);

/* Draw one frame into a 32 bit 0xAARRGGBB pixel buffer. */
void elv_console_draw(uint32_t *px, int w, int h, int stride_px, const elv_view_t *v);

#endif
