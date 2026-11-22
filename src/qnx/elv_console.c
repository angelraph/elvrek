/*
 * Elvrek crew console on QNX Screen.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reads /dev/elvrek/status ten times a second and draws the crew screen into
 * a QNX Screen window. It only reads: the console has no path to the piston,
 * the charger or the shock line, and if it dies or hangs the safety process
 * does not notice. It runs at a low priority for the same reason.
 *
 * Usage: elv-console [-o dir] [-e every] [-l log] [-n frames]
 *   -o dir    also save frames from the Screen buffer as PPM files into dir
 *   -l log    write every status line drawn, with its time, so the host can
 *             replay the run frame by frame through the same drawing code
 *   -n N      stop after N frames (0 means run forever)
 *   -e K      save every Kth frame (default 5, so two per second)
 */
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <screen/screen.h>
#include "elv_ipc.h"
#include "elv_console_draw.h"

#define W 1280
#define H 768

static volatile sig_atomic_t stop_req;
static void on_stop(int sig) { (void)sig; stop_req = 1; }

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void save_ppm(const char *path, const uint32_t *px, int stride_px)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    static unsigned char row[W * 3];
    for (int y = 0; y < H; y++) {
        const uint32_t *p = px + (size_t)y * stride_px;
        for (int x = 0; x < W; x++) {
            row[3 * x] = (unsigned char)(p[x] >> 16);
            row[3 * x + 1] = (unsigned char)(p[x] >> 8);
            row[3 * x + 2] = (unsigned char)p[x];
        }
        fwrite(row, 1, sizeof row, f);
    }
    fclose(f);
}

#define CHECK(call) do { if ((call) != 0) { perror(#call); return 1; } } while (0)

int main(int argc, char **argv)
{
    const char *outdir = NULL, *logpath = NULL;
    long max_frames = 0, every = 5;
    int c;
    while ((c = getopt(argc, argv, "o:n:e:l:")) != -1) {
        if (c == 'o') outdir = optarg;
        else if (c == 'l') logpath = optarg;
        else if (c == 'n') max_frames = atol(optarg);
        else if (c == 'e') every = atol(optarg) > 0 ? atol(optarg) : 1;
        else { fprintf(stderr, "usage: elv-console [-o dir] [-e every] [-l log] [-n frames]\n"); return 2; }
    }
    FILE *lg = logpath ? fopen(logpath, "w") : NULL;
    if (logpath && !lg) { perror(logpath); return 1; }

    struct sched_param sp = { .sched_priority = 8 };   /* below the advisor's ceiling, far below the 1 ms loop */
    sched_setscheduler(0, SCHED_OTHER, &sp);

    screen_context_t ctx;
    screen_window_t win;
    CHECK(screen_create_context(&ctx, SCREEN_APPLICATION_CONTEXT));
    CHECK(screen_create_window(&win, ctx));
    int fmt = SCREEN_FORMAT_RGBX8888;
    int usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE;
    int size[2] = { W, H };
    CHECK(screen_set_window_property_iv(win, SCREEN_PROPERTY_FORMAT, &fmt));
    CHECK(screen_set_window_property_iv(win, SCREEN_PROPERTY_USAGE, &usage));
    CHECK(screen_set_window_property_iv(win, SCREEN_PROPERTY_BUFFER_SIZE, size));
    CHECK(screen_set_window_property_iv(win, SCREEN_PROPERTY_SIZE, size));
    CHECK(screen_set_window_property_cv(win, SCREEN_PROPERTY_ID_STRING, 12, "elv-console"));
    CHECK(screen_create_window_buffers(win, 2));

    static elv_view_t v;
    char line[1024];
    double t0 = now_s(), next = t0;
    long frame = 0, saved = 0, misses = 0;
    printf("elv-console: %dx%d on QNX Screen%s%s\n", W, H, outdir ? ", saving frames to " : "", outdir ? outdir : "");
    fflush(stdout);

    signal(SIGINT, on_stop);
    signal(SIGTERM, on_stop);
    while (!stop_req) {
        /* read the status once; a missing safety process just shows WAITING */
        int fd = open(ELV_DEV_STATUS, O_RDONLY);
        ssize_t n = -1;
        if (fd != -1) { n = read(fd, line, sizeof line - 1); close(fd); }
        double t = now_s() - t0;
        if (n > 0) {
            line[n] = 0;
            char *nl = strchr(line, '\n');
            if (nl) *nl = 0;
        } else line[0] = 0;
        if (!elv_view_feed(&v, line, t)) misses++;
        if (lg) fprintf(lg, "%ld %.17g %s\n", frame, t, line);

        screen_buffer_t buf[2];
        void *ptr = NULL;
        int stride = 0;
        CHECK(screen_get_window_property_pv(win, SCREEN_PROPERTY_RENDER_BUFFERS, (void **)buf));
        CHECK(screen_get_buffer_property_pv(buf[0], SCREEN_PROPERTY_POINTER, &ptr));
        CHECK(screen_get_buffer_property_iv(buf[0], SCREEN_PROPERTY_STRIDE, &stride));
        elv_console_draw((uint32_t *)ptr, W, H, stride / 4, &v);
        int rect[4] = { 0, 0, W, H };
        CHECK(screen_post_window(win, buf[0], 1, rect, 0));

        if (outdir && frame % every == 0) {
            char path[512];
            snprintf(path, sizeof path, "%s/frame-%05ld.ppm", outdir, saved++);
            save_ppm(path, (const uint32_t *)ptr, stride / 4);
        }
        frame++;
        if (max_frames && frame >= max_frames) break;

        next += 0.1;
        double d = next - now_s();
        if (d > 0) {
            struct timespec ts = { (time_t)d, (long)((d - (time_t)d) * 1e9) };
            nanosleep(&ts, NULL);
        } else next = now_s();
    }
    if (lg) fclose(lg);
    printf("elv-console: %ld frames drawn, %ld saved, %ld reads without a status line\n", frame, saved, misses);
    screen_destroy_window(win);
    screen_destroy_context(ctx);
    return 0;
}
