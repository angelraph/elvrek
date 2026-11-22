/*
 * Elvrek crew console: drawing. Plain C on a pixel buffer, so the same code
 * draws into a QNX Screen window on the target and into an image on the host
 * for previews.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "elv_console_draw.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elv_font.h"

/* Whole numbers are rounded here, half away from zero, not by printf: C
   libraries differ on halves (QNX shows 8.5 as 9, glibc as 8), and the crew
   screen must read the same wherever it is drawn. */
static long rnd(double x) { return (long)(x < 0 ? -floor(-x + 0.5) : floor(x + 0.5)); }

#define BG      0xFF07080Au
#define PANEL   0xFF111318u
#define EDGE    0xFF1E2128u
#define TEXT    0xFFF2F4F6u
#define MUTED   0xFF8B93A0u
#define RED     0xFFE3122Du
#define AMBER   0xFFFFC233u
#define GREEN   0xFF5EE08Au
#define BLUE    0xFF7DB4FFu

typedef struct { uint32_t *px; int w, h, s; } fb_t;

static uint32_t blend(uint32_t d, uint32_t c, unsigned a)
{
    unsigned r = (((c >> 16) & 255) * a + ((d >> 16) & 255) * (255 - a)) / 255;
    unsigned g = (((c >> 8) & 255) * a + ((d >> 8) & 255) * (255 - a)) / 255;
    unsigned b = ((c & 255) * a + (d & 255) * (255 - a)) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static void rect(fb_t *f, int x, int y, int w, int h, uint32_t c)
{
    for (int j = y < 0 ? 0 : y; j < y + h && j < f->h; j++)
        for (int i = x < 0 ? 0 : x; i < x + w && i < f->w; i++) f->px[j * f->s + i] = c;
}

static void rect_a(fb_t *f, int x, int y, int w, int h, uint32_t c, unsigned a)
{
    for (int j = y < 0 ? 0 : y; j < y + h && j < f->h; j++)
        for (int i = x < 0 ? 0 : x; i < x + w && i < f->w; i++)
            f->px[j * f->s + i] = blend(f->px[j * f->s + i], c, a);
}

/* a panel with soft rounded corners and a hairline edge */
static void panel(fb_t *f, int x, int y, int w, int h, uint32_t c)
{
    const int r = 14;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int dx = i < r ? r - i : (i >= w - r ? i - (w - r - 1) : 0);
            int dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
            double d = sqrt((double)dx * dx + (double)dy * dy);
            if (dx && dy && d > r) continue;
            uint32_t col = (dx && dy && d > r - 1.2) || i == 0 || j == 0 || i == w - 1 || j == h - 1 ? EDGE : c;
            int X = x + i, Y = y + j;
            if (X >= 0 && Y >= 0 && X < f->w && Y < f->h) f->px[Y * f->s + X] = col;
        }
}

static int text_w(const elv_font_t *ft, const char *s)
{
    int w = 0;
    for (; *s; s++) if (*s >= 32 && *s < 127) w += ft->g[*s - 32].w;
    return w;
}

/* align: 0 left, 1 centre, 2 right */
static int text(fb_t *f, const elv_font_t *ft, int x, int y, const char *s, uint32_t c, int align)
{
    int w = text_w(ft, s);
    if (align == 1) x -= w / 2; else if (align == 2) x -= w;
    for (; *s; s++) {
        if (*s < 32 || *s >= 127) continue;
        const elv_glyph_t *g = &ft->g[*s - 32];
        const uint8_t *a = ft->a + g->off;
        for (int j = 0; j < ft->h; j++) {
            int Y = y + j;
            if (Y < 0 || Y >= f->h) continue;
            for (int i = 0; i < g->w; i++) {
                int X = x + i;
                unsigned al = a[j * g->w + i];
                if (al && X >= 0 && X < f->w) f->px[Y * f->s + X] = blend(f->px[Y * f->s + X], c, al);
            }
        }
        x += g->w;
    }
    return w;
}

static void line(fb_t *f, double x0, double y0, double x1, double y1, uint32_t c, double th)
{
    int n = (int)(fabs(x1 - x0) + fabs(y1 - y0)) + 1;
    for (int k = 0; k <= n; k++) {
        double x = x0 + (x1 - x0) * k / n, y = y0 + (y1 - y0) * k / n;
        rect(f, (int)(x - th / 2), (int)(y - th / 2), (int)th + 1, (int)th + 1, c);
    }
}

static double kv(const char *line, const char *key, double dflt)
{
    char pat[40];
    snprintf(pat, sizeof pat, " %s=", key);
    const char *p = strstr(line, pat);
    return p ? atof(p + strlen(pat)) : dflt;
}

static void kvs(const char *line, const char *key, char *out, size_t n)
{
    char pat[40];
    snprintf(pat, sizeof pat, " %s=", key);
    const char *p = strstr(line, pat);
    out[0] = '\0';
    if (!p) return;
    p += strlen(pat);
    size_t i = 0;
    while (p[i] && p[i] != ' ' && p[i] != '\n' && i + 1 < n) { out[i] = p[i]; i++; }
    out[i] = '\0';
}

bool elv_view_update(elv_view_t *v, const char *raw, double now)
{
    char line[2048];
    snprintf(line, sizeof line, " %s", raw);       /* lead space: every key is " key=" */
    if (!strstr(line, " tick=")) return false;
    unsigned long long tick = (unsigned long long)kv(line, "tick", 0);
    if (tick < v->tick) {                           /* the controller restarted: a new rescue */
        memset(v, 0, sizeof *v);
        v->shock_seen_at = v->skip_seen_at = -100.0;
    }
    v->now = now;
    v->tick = tick;
    kvs(line, "state", v->state, sizeof v->state);
    kvs(line, "fault", v->fault, sizeof v->fault);
    v->sw = (int)kv(line, "sw", 0);
    v->ccf = kv(line, "ccf", 1);
    v->pos_mm = kv(line, "pos_mm", 0);
    /* depth shown is the deepest point of the last second, so the crew reads
       how deep each compression went, not where the piston happened to be */
    v->force_ring[v->pos_i % ELV_POS_RING] = kv(line, "force_n", 0);
    v->pos_ring[v->pos_i++ % ELV_POS_RING] = v->pos_mm;
    v->depth_mm = 0;
    v->force_n = 0;
    for (int k = 0; k < ELV_POS_RING && k < v->pos_i; k++) {
        if (v->pos_ring[k] > v->depth_mm) v->depth_mm = v->pos_ring[k];
        if (v->force_ring[k] > v->force_n) v->force_n = v->force_ring[k];
    }
    v->etco2 = kv(line, "etco2", 0);
    v->shocks = (unsigned)kv(line, "shocks", 0);
    v->compressions = (unsigned)kv(line, "compressions", 0);
    v->breaths = (unsigned)kv(line, "breaths", 0);
    v->charge = (unsigned)kv(line, "charge", 0);
    v->ready = (unsigned)kv(line, "ready", 0);
    v->preauth = (unsigned)kv(line, "preauth", 0);
    v->max_machine = (unsigned)kv(line, "max_machine", 0);
    v->zp_checks = (unsigned)kv(line, "zp_checks", 0);
    v->zp_skipped = (unsigned)kv(line, "zp_skipped", 0);
    v->zp_sure = (unsigned)kv(line, "zp_sure", 0);
    v->zp_unsure = (unsigned)kv(line, "zp_unsure", 0);
    v->zp_score = kv(line, "zp_score", 0);
    v->next_check = (unsigned)kv(line, "next_check", 0);
    v->in_window = (unsigned)kv(line, "in_window", 0);
    v->emulator = (unsigned)kv(line, "emulator", 0);
    if (now - v->last_hist >= 0.5 || v->hist_n == 0) {
        if (v->hist_n == ELV_HIST) { memmove(v->etco2_hist, v->etco2_hist + 1, sizeof(double) * (ELV_HIST - 1)); v->hist_n--; }
        v->etco2_hist[v->hist_n++] = v->etco2;
        v->last_hist = now;
    }
    if (v->shocks > v->prev_shocks) { v->shock_seen_at = now; v->prev_shocks = v->shocks; }
    if (v->zp_skipped > v->prev_skipped) { v->skip_seen_at = now; v->prev_skipped = v->zp_skipped; }
    /* compression rate, timed stroke to stroke on the controller's clock. A
       count in a fixed window would read 100 or 120 for a steady 110, because
       it only sees whole strokes. Any pause starts the timing again. */
    if (strcmp(v->state, "COMPRESS")) { v->stroke_n = 0; v->rate_cpm = 0; }
    else if (!v->stroke_n || v->compressions != v->stroke_comp[v->stroke_n - 1]) {
        if (v->stroke_n == ELV_STROKES) {
            memmove(v->stroke_tick, v->stroke_tick + 1, sizeof v->stroke_tick[0] * (ELV_STROKES - 1));
            memmove(v->stroke_comp, v->stroke_comp + 1, sizeof v->stroke_comp[0] * (ELV_STROKES - 1));
            v->stroke_n--;
        }
        v->stroke_tick[v->stroke_n] = v->tick;
        v->stroke_comp[v->stroke_n++] = v->compressions;
        if (v->stroke_n >= 3 && v->tick > v->stroke_tick[1])   /* the first entry may be mid stroke */
            v->rate_cpm = (v->compressions - v->stroke_comp[1]) * 60000.0 / (double)(v->tick - v->stroke_tick[1]);
    }
    return true;
}

bool elv_view_feed(elv_view_t *v, const char *line, double now)
{
    if (line[0] && elv_view_update(v, line, now)) return true;
    v->state[0] = 0;            /* no safety process: the screen says WAITING */
    v->now = now;
    return false;
}

static void label(fb_t *f, int x, int y, const char *s)
{
    text(f, &ELV_FONT_SMALL, x, y, s, MUTED, 0);
}

void elv_console_draw(uint32_t *px, int w, int h, int stride, const elv_view_t *v)
{
    fb_t F = { px, w, h, stride }, *f = &F;
    char b[96];
    rect(f, 0, 0, w, h, BG);
    /* a faint red glow behind the header */
    for (int j = 0; j < 120; j++) rect_a(f, 0, j, w, 1, RED, (unsigned)(18 * (1.0 - j / 120.0)));

    /* header */
    int x = 32;
    x += text(f, &ELV_FONT_LARGE, x, 16, "E", RED, 0);
    x += text(f, &ELV_FONT_LARGE, x, 16, "LVREK", TEXT, 0);
    text(f, &ELV_FONT_SMALL, x + 18, 34, "CPR safety controller on QNX 8.0", MUTED, 0);
    const char *st = v->state[0] ? v->state : "WAITING";
    uint32_t sc = !strcmp(st, "COMPRESS") ? GREEN : !strcmp(st, "SHOCK_WINDOW") ? AMBER :
                  !strcmp(st, "ROSC_HOLD") ? BLUE : (!strcmp(st, "SAFE_HOLD") || !strcmp(st, "FAULT_RETRACT")) ? RED : MUTED;
    char stn[32]; snprintf(stn, sizeof stn, "%s", st);
    for (char *p = stn; *p; p++) if (*p == '_') *p = ' ';
    int pw = text_w(&ELV_FONT_BODY, stn) + 40;
    panel(f, w - 32 - pw, 18, pw, 46, PANEL);
    rect(f, w - 32 - pw + 16, 37, 9, 9, sc);
    text(f, &ELV_FONT_BODY, w - 32 - pw + 32, 26, stn, sc, 0);

    /* card A: compressions */
    panel(f, 32, 90, 392, 250, PANEL);
    label(f, 56, 106, "COMPRESSIONS PER MINUTE");
    snprintf(b, sizeof b, "%ld", rnd(v->rate_cpm));
    text(f, &ELV_FONT_HUGE, 56, 126, !strcmp(st, "COMPRESS") && v->rate_cpm > 0 ? b : "--", TEXT, 0);
    label(f, 56, 250, "DEPTH");
    rect(f, 120, 254, 280, 14, EDGE);
    rect_a(f, 120 + 280 * 50 / 70, 254, 280 * 10 / 70, 14, GREEN, 70);      /* 5 to 6 cm band */
    double d = v->depth_mm < 0 ? 0 : v->depth_mm > 70 ? 70 : v->depth_mm;
    rect(f, 120, 256, (int)(280 * d / 70), 10, RED);
    snprintf(b, sizeof b, "%ld mm", rnd(v->depth_mm));
    text(f, &ELV_FONT_SMALL, 400, 276, b, TEXT, 2);
    snprintf(b, sizeof b, "Peak force %ld N   (ceiling 600 N)", rnd(v->force_n));
    text(f, &ELV_FONT_SMALL, 56, 304, b, MUTED, 0);

    /* card B: next rhythm check */
    panel(f, 440, 90, 400, 250, PANEL);
    label(f, 464, 106, "NEXT RHYTHM CHECK");
    unsigned s = v->next_check / 1000;
    snprintf(b, sizeof b, "%u:%02u", s / 60, s % 60);
    text(f, &ELV_FONT_HUGE, 464, 126, !strcmp(st, "COMPRESS") ? b : "--", TEXT, 0);
    double frac = 1.0 - (v->next_check / 120000.0);
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    rect(f, 464, 254, 352, 8, EDGE);
    rect(f, 464, 254, (int)(352 * frac), 8, v->charge || v->ready ? AMBER : BLUE);
    label(f, 464, 276, "READ THROUGH THE COMPRESSIONS");
    const char *verdict = v->zp_score <= 0.0 && !v->zp_checks ? "reading..." :
                          v->zp_score >= 0.95 ? "sure shockable" : v->zp_score <= 0.23 ? "sure not shockable" : "not sure: will look 0.8 s";
    snprintf(b, sizeof b, "score %.2f   %s", v->zp_score, verdict);
    text(f, &ELV_FONT_SMALL, 464, 302, b, TEXT, 0);

    /* card C: defibrillator */
    panel(f, 856, 90, 392, 250, PANEL);
    label(f, 880, 106, "DEFIBRILLATOR");
    const char *ch = !strcmp(st, "SHOCK_WINDOW") ? "DISCHARGE" : v->ready ? "READY" : v->charge ? "CHARGING" : "IDLE";
    text(f, &ELV_FONT_LARGE, 880, 132, ch, v->ready || v->charge ? AMBER : TEXT, 0);
    label(f, 880, 196, "SHOCKS");
    snprintf(b, sizeof b, "%u", v->shocks);
    text(f, &ELV_FONT_LARGE, 880, 214, b, TEXT, 0);
    label(f, 1040, 196, "LAST HANDS-OFF");
    snprintf(b, sizeof b, "%u ms", v->max_machine);
    text(f, &ELV_FONT_LARGE, 1040, 214, v->shocks ? b : "--", TEXT, 0);
    text(f, &ELV_FONT_SMALL, 880, 300, "Charges while the piston keeps working", MUTED, 0);

    /* card D: EtCO2 trend */
    panel(f, 32, 356, 808, 200, PANEL);
    label(f, 56, 372, "END-TIDAL CO2");
    snprintf(b, sizeof b, "%ld", rnd(v->etco2));
    int tw = text(f, &ELV_FONT_LARGE, 56, 396, b, TEXT, 0);
    text(f, &ELV_FONT_SMALL, 62 + tw, 414, "mmHg", MUTED, 0);
    int gx = 256, gy = 380, gw = 560, gh = 150;
    for (int k = 0; k <= 3; k++) rect(f, gx, gy + gh * k / 3, gw, 1, EDGE);
    for (int k = 1; k < v->hist_n; k++) {
        double x0 = gx + gw * (k - 1) / (double)(ELV_HIST - 1), x1 = gx + gw * k / (double)(ELV_HIST - 1);
        double y0 = gy + gh - gh * v->etco2_hist[k - 1] / 50.0, y1 = gy + gh - gh * v->etco2_hist[k] / 50.0;
        line(f, x0, y0, x1, y1, BLUE, 2.5);
    }
    text(f, &ELV_FONT_SMALL, 56, 500, "a sudden rise means", MUTED, 0);
    text(f, &ELV_FONT_SMALL, 56, 522, "circulation returning", MUTED, 0);

    /* card E: compression fraction */
    panel(f, 856, 356, 392, 200, PANEL);
    label(f, 880, 372, "COMPRESSION FRACTION");
    snprintf(b, sizeof b, "%.1f%%", v->ccf * 100.0);
    text(f, &ELV_FONT_LARGE, 880, 398, b, TEXT, 0);
    snprintf(b, sizeof b, "Breaths %u   Checks with no pause %u", v->breaths, v->zp_skipped);
    text(f, &ELV_FONT_SMALL, 880, 468, b, MUTED, 0);
    text(f, &ELV_FONT_SMALL, 880, 496, "Guideline: at least 80%", MUTED, 0);

    /* banner */
    const char *msg = "WAITING FOR THE SAFETY PROCESS", *sub = "";
    uint32_t mc = MUTED;
    bool flash = false;
    if (!strcmp(st, "IDLE")) { msg = "READY. PRESS START"; sub = "The piston is at full recoil"; }
    else if (!strcmp(st, "COMPRESS")) {
        msg = "COMPRESSING"; mc = GREEN; sub = "The only pause will be for the shock itself";
        if (v->charge || v->ready) { msg = "CHARGING WHILE COMPRESSING"; mc = AMBER; sub = "Press SHOCK now to pre-authorise"; }
        if (v->now - v->shock_seen_at < 4.0 && v->shocks) { msg = "SHOCK DELIVERED. COMPRESSIONS RESUMED"; mc = AMBER; sub = ""; }
        if (v->now - v->skip_seen_at < 6.0 && v->zp_skipped) { msg = "NO PAUSE NEEDED"; mc = GREEN; sub = "The rhythm was read through the compressions: not shockable"; }
    } else if (!strcmp(st, "SHOCK_WINDOW")) {
        mc = AMBER;
        if (v->sw == 2) { msg = "SHOCK ADVISED: PRESS SHOCK"; flash = true; sub = "Stand clear"; }
        else if (v->sw == 3) { msg = "SHOCK"; mc = RED; sub = "Stand clear"; }
        else { msg = "STAND CLEAR"; sub = "Arm parked at full recoil"; }
    } else if (!strcmp(st, "ROSC_HOLD")) { msg = "RETURN OF CIRCULATION"; mc = BLUE; sub = "Piston parked, breaths continue. Check pulse"; }
    else if (!strcmp(st, "SAFE_HOLD") || !strcmp(st, "FAULT_RETRACT")) {
        msg = "SAFE HOLD"; mc = RED; snprintf(b, sizeof b, "Fault: %s. Piston lifted to full recoil", v->fault); sub = b;
    } else if (!strcmp(st, "STOPPED")) { msg = "STOPPED"; }
    {
        bool hi = flash && ((int)(v->now * 2.5)) % 2;   /* flashing banners pulse, never vanish */
        panel(f, 32, 572, w - 64, 150, PANEL);
        rect_a(f, 33, 573, w - 66, 148, mc, hi ? 70 : 22);
        rect(f, 32, 572, 8, 150, mc);
        text(f, &ELV_FONT_LARGE, w / 2, 598, msg, hi ? TEXT : mc, 1);
        if (sub[0]) text(f, &ELV_FONT_BODY, w / 2, 660, sub, TEXT, 1);
    }

    /* footer */
    snprintf(b, sizeof b, "tick %llu%s", v->tick, v->emulator ? "   emulator mode" : "");
    text(f, &ELV_FONT_SMALL, w - 32, 736, b, MUTED, 2);
    text(f, &ELV_FONT_SMALL, 32, 736, "Research controller tested against a physics model. Not a medical device.", MUTED, 0);
}
