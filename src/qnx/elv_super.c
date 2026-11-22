/*
 * elv-super: restarts the advisor; never restarts the safety process.
 * SPDX-License-Identifier: Apache-2.0
 *
 * SLM starts the stack at boot. SLM sees a process die; it cannot see one
 * that is alive but silent. elv-super watches the advisor's message age on
 * /dev/elvrek/status: a dead advisor is respawned, a hung one is killed
 * and respawned. If elvrekd itself is gone, elv-super does NOT restart it:
 * the hardware watchdog has already retracted the piston, and a safety
 * process that died needs a person, not a loop.
 *
 * Usage: elv-super [-H hang_ms] -- /path/elv-advisor [args...]
 * Writes restart counts to /tmp/elv-super.state for the fault suite.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "elv_ipc.h"
#ifdef __QNX__
#include <secpol/secpol.h>
#endif

extern char **environ;

static long status_value(const char *key)
{
    /* buf starts with a space so the first key ("tick=") matches the same
       " key=" pattern as every other key */
    char buf[1200];
    buf[0] = ' ';
    int fd = open(ELV_DEV_STATUS, O_RDONLY);
    if (fd == -1) return -1;
    ssize_t n = read(fd, buf + 1, sizeof(buf) - 2);
    close(fd);
    if (n <= 0) return -1;
    buf[n + 1] = '\0';
    char pat[48];
    snprintf(pat, sizeof(pat), " %s=", key);
    char *p = strstr(buf, pat);
    return p ? atol(p + strlen(pat)) : -1;
}

static void save(unsigned deaths, unsigned hangs, int safety_down)
{
    FILE *f = fopen("/tmp/elv-super.state", "w");
    if (!f) return;
    fprintf(f, "advisor_deaths=%u advisor_hangs=%u restarts=%u safety_down=%d\n",
            deaths, hangs, deaths + hangs, safety_down);
    fclose(f);
}

int main(int argc, char **argv)
{
    long hang_ms = 3000;
    int opt;
    const char *adv_type = NULL;          /* -T: spawn the advisor under this type */
    while ((opt = getopt(argc, argv, "H:T:")) != -1) {
        if (opt == 'H') hang_ms = atol(optarg);
        else if (opt == 'T') adv_type = optarg;
    }
    if (optind >= argc) {
        fprintf(stderr, "usage: elv-super [-H hang_ms] [-T type] -- advisor [args]\n");
        return 2;
    }
    char **child = &argv[optind];
    unsigned deaths = 0, hangs = 0;
    int safety_down = 0;
    pid_t pid = -1;
    long grace = 0;
    int seen = 0, startup_waits = 0;

    for (;;) {
        if (status_value("tick") < 0) {
            /* at start-up the safety process may simply not be up yet */
            if (!seen && startup_waits++ < 20) { usleep(500000); continue; }
            if (!safety_down)
                fprintf(stderr, "elv-super: SAFETY PROCESS DOWN, not restarting it; "
                                "hardware watchdog holds the piston at recoil\n");
            safety_down = 1;
            save(deaths, hangs, safety_down);
            if (pid > 0) { kill(pid, SIGKILL); waitpid(pid, NULL, 0); pid = -1; }
            sleep(1);
            continue;
        }
        seen = 1;
        if (pid <= 0) {
            /* With a security policy in force, the advisor is spawned
               straight into its own type, so it never runs as ours. */
            posix_spawnattr_t attr, *ap = NULL;
            posix_spawnattr_init(&attr);
#ifdef __QNX__
            if (adv_type) {
                if (secpol_posix_spawnattr_settypeid(NULL, &attr, adv_type, SECPOL_TYPE_NAME) == 0)
                    ap = &attr;
                else
                    fprintf(stderr, "elv-super: no type %s (%s), spawning without one\n",
                            adv_type, strerror(errno));
            }
#else
            (void)adv_type;
#endif
            int sr = posix_spawn(&pid, child[0], NULL, ap, child, environ);
            posix_spawnattr_destroy(&attr);
            if (sr != 0) {
                perror("elv-super: spawn");
                pid = -1;
                sleep(1);
                continue;
            }
            grace = 5000;                       /* first window takes 4 s */
        }
        usleep(200000);
        grace -= 200;

        int st;
        if (waitpid(pid, &st, WNOHANG) == pid) {
            deaths++;
            fprintf(stderr, "elv-super: advisor died (%d), restart %u\n", st, deaths + hangs);
            pid = -1;
        } else if (grace <= 0 && status_value("adv_age") > hang_ms) {
            hangs++;
            fprintf(stderr, "elv-super: advisor silent > %ld ms, killing, restart %u\n",
                    hang_ms, deaths + hangs);
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            pid = -1;
        }
        save(deaths, hangs, safety_down);
    }
}
