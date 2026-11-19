/*
 * elv-fault: fault injection for the on-target suite (root only).
 * SPDX-License-Identifier: Apache-2.0
 *
 * Usage: elv-fault sensor-drop | sensor-restore | estop | estop-release |
 *                  transport on|off | rosc | stiff | convert on|off |
 *                  cpu-storm <threads> <seconds> <priority> |
 *                  hw-truth
 */
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "elv_ipc.h"
#include "elv_rt.h"

static void *map(const char *name, size_t len, int prot)
{
    int fd = shm_open(name, (prot & PROT_WRITE) ? O_RDWR : O_RDONLY, 0);
    if (fd == -1) { perror(name); exit(1); }
    void *p = mmap(NULL, len, prot, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(1); }
    close(fd);
    return p;
}

static int g_storm_prio;
static struct timespec g_until;

/* Each spinner watches the clock and stops itself. A main thread that
   sleeps at normal priority would never get the CPU back from spinners
   running above it, so it cannot be the one to stop them. */
static void *spin(void *arg)
{
    (void)arg;
    elv_rt_set_prio(g_storm_prio);
    volatile unsigned long x = 0;
    struct timespec now;
    for (;;) {
        for (int i = 0; i < 100000; i++) x++;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > g_until.tv_sec ||
            (now.tv_sec == g_until.tv_sec && now.tv_nsec >= g_until.tv_nsec)) break;
    }
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "see source for usage\n"); return 2; }
    const char *c = argv[1];

    if (!strcmp(c, "cpu-storm")) {
        int n = argc > 2 ? atoi(argv[2]) : 4;
        int secs = argc > 3 ? atoi(argv[3]) : 10;
        g_storm_prio = argc > 4 ? atoi(argv[4]) : ELV_PRIO_CTRL - 1;
        pthread_t t[64];
        if (n > 64) n = 64;
        clock_gettime(CLOCK_MONOTONIC, &g_until);
        g_until.tv_sec += secs;
        for (int i = 0; i < n; i++) pthread_create(&t[i], NULL, spin, NULL);
        for (int i = 0; i < n; i++) pthread_join(t[i], NULL);
        printf("cpu-storm: %d threads at priority %d for %d s\n", n, g_storm_prio, secs);
        return 0;
    }
    if (!strcmp(c, "hw-truth")) {
        const elv_hw_shm_t *hw = map(ELV_SHM_HW, sizeof(*hw), PROT_READ);
        printf("true_depth_mm=%.1f max_true_depth_mm=%.1f max_force_n=%.0f strokes=%u "
               "in_range=%u shocks_received=%u shocks_off_recoil=%u hw_watchdog_trips=%u "
               "driver_force_limits=%u\n",
               hw->true_depth_m * 1000, hw->max_true_depth_m * 1000, hw->max_force_n,
               hw->strokes, hw->strokes_in_range, hw->shocks_received,
               hw->shocks_off_recoil, hw->hw_watchdog_trips, hw->driver_force_limits);
        return 0;
    }

    elv_flt_shm_t *f = map(ELV_SHM_FLT, sizeof(*f), PROT_READ | PROT_WRITE);
    const char *v = argc > 2 ? argv[2] : "on";
    int on = strcmp(v, "off") != 0;
    if      (!strcmp(c, "sensor-drop"))    atomic_store(&f->drop_sensors, 1);
    else if (!strcmp(c, "sensor-restore")) atomic_store(&f->drop_sensors, 0);
    else if (!strcmp(c, "estop"))          atomic_store(&f->estop, 1);
    else if (!strcmp(c, "estop-release"))  atomic_store(&f->estop, 0);
    else if (!strcmp(c, "transport"))      atomic_store(&f->transport, on);
    else if (!strcmp(c, "convert"))        atomic_store(&f->shock_converts, on);
    else if (!strcmp(c, "rosc"))           atomic_store(&f->rosc_now, 1);
    else if (!strcmp(c, "stiff"))          atomic_store(&f->stiff_chest, 1);
    else { fprintf(stderr, "elv-fault: unknown %s\n", c); return 2; }
    return 0;
}
