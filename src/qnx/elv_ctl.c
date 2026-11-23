/*
 * elv-ctl: crew commands and status.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Usage: elv-ctl start | stop | shock | epi | mode 30:2 | mode cont |
 *                transport on | transport off | status [key] | boot
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "elv_ipc.h"

static int status(const char *key)
{
    char buf[1200];
    int fd = open(ELV_DEV_STATUS, O_RDONLY);
    if (fd == -1) { perror(ELV_DEV_STATUS); return 1; }
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) { fprintf(stderr, "elv-ctl: no status\n"); return 1; }
    buf[n] = '\0';
    if (!key) { fputs(buf, stdout); return 0; }
    /* print one value: elv-ctl status state */
    size_t kl = strlen(key);
    for (char *p = buf; (p = strstr(p, key)); p++) {
        if ((p == buf || p[-1] == ' ') && p[kl] == '=') {
            p += kl + 1;
            size_t l = strcspn(p, " \n");
            printf("%.*s\n", (int)l, p);
            return 0;
        }
    }
    fprintf(stderr, "elv-ctl: no key %s\n", key);
    return 1;
}

static long mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static long key_val(const char *buf, const char *key)
{
    size_t kl = strlen(key);
    for (const char *p = buf; (p = strstr(p, key)); p++)
        if ((p == buf || p[-1] == ' ') && p[kl] == '=') return atol(p + kl + 1);
    return -1;
}

/* elv-ctl boot: run from the boot script. Waits for the safety process to
   answer, presses START at once (a crew holding START at power on), waits for
   the first full compression and prints the milestones on the kernel's
   monotonic clock, which starts when the kernel does. Polls every 2 ms at the
   priority it was started with; it never touches the control loop. */
static int boot_marks(void)
{
    char buf[1200];
    long seen = -1, t0 = mono_ms();
    for (;;) {
        int fd = open(ELV_DEV_STATUS, O_RDONLY);
        if (fd != -1) {
            if (seen < 0) {
                seen = mono_ms();
                int o = open(ELV_DEV_OPERATOR, O_WRONLY);
                if (o == -1 || write(o, "start", 5) != 5) { perror("start"); return 1; }
                close(o);
            }
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) {
                buf[n] = '\0';
                if (key_val(buf, "boot_comp_ms") > 0) break;
            }
        }
        if (mono_ms() - t0 > 60000) { fprintf(stderr, "elv-ctl boot: no compression in 60 s\n"); return 1; }
        usleep(2000);
    }
    printf("ELVREK BOOT kernel_ms: controller_process=%ld control_loop=%ld status_seen=%ld "
           "start=%ld first_compression=%ld state=%s\n",
           key_val(buf, "boot_proc_ms"), key_val(buf, "boot_ready_ms"), seen,
           key_val(buf, "boot_start_ms"), key_val(buf, "boot_comp_ms"),
           strstr(buf, "state=COMPRESS") ? "COMPRESS" : "other");
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "boot")) return boot_marks();
    if (argc < 2) {
        fprintf(stderr, "usage: elv-ctl start|stop|shock|epi|mode 30:2|mode cont|"
                        "transport on|transport off|status [key]\n");
        return 2;
    }
    if (!strcmp(argv[1], "status")) return status(argc > 2 ? argv[2] : NULL);

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "%s%s%s", argv[1], argc > 2 ? " " : "", argc > 2 ? argv[2] : "");
    int fd = open(ELV_DEV_OPERATOR, O_WRONLY);
    if (fd == -1) { perror(ELV_DEV_OPERATOR); return 1; }
    ssize_t w = write(fd, cmd, strlen(cmd));
    close(fd);
    if (w != (ssize_t)strlen(cmd)) { perror("write"); return 1; }
    return 0;
}
