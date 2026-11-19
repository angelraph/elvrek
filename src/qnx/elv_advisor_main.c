/*
 * elv-advisor: the untrusted rhythm classifier as its own process.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Starts as root only long enough to drop to uid ELV_ADVISOR_UID, then
 * denies and locks every ability it might use to climb back. Its only input
 * is a read-only map of the ECG ring; its only output is
 * /dev/elvrek/advice. It cannot open the piston, shock or operator
 * devices; -a proves that by trying and counting the refusals.
 *
 * Usage: elv-advisor [-m] [-H seconds] [-a]
 *   -m  malicious: lie about rhythm and ROSC, demand shock/stop/12 cm
 *   -H  hang (stop sending, keep running) after N seconds
 *   -a  attack: try every forbidden path once, print the refusals, exit
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef __QNXNTO__
#include <sys/procmgr.h>
#endif

#include "elv_advisor.h"
#include "elv_ipc.h"
#include "elv_limits.h"
#include "elv_rt.h"

static void drop_privileges(void)
{
    if (geteuid() == 0) {
        if (setgid(ELV_ADVISOR_UID) == -1 || setuid(ELV_ADVISOR_UID) == -1) {
            perror("elv-advisor: drop uid");
            exit(1);
        }
    }
#ifdef __QNXNTO__
    procmgr_ability(0,
        PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_PRIORITY,
        PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_SPAWN,
        PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_FORK,
        PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_SETUID,
        PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_MEM_PHYS,
        PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK | PROCMGR_AID_IO,
        PROCMGR_AID_EOL);
#endif
}

static int attack(void)
{
    struct { const char *what; int ok; } t[8];
    int n = 0;
    const char *paths[] = { ELV_DEV_SHOCK, ELV_DEV_PISTON, ELV_DEV_OPERATOR, ELV_DEV_STATUS };
    for (unsigned i = 0; i < 4; i++) {
        int fd = open(paths[i], O_WRONLY);
        t[n].what = paths[i]; t[n].ok = fd != -1;
        printf("attack: open %-26s O_WRONLY -> %s\n", paths[i], fd == -1 ? strerror(errno) : "OPENED");
        if (fd != -1) close(fd);
        n++;
    }
    int fd = shm_open(ELV_SHM_HW, O_RDWR, 0);
    printf("attack: shm_open %-22s O_RDWR -> %s\n", ELV_SHM_HW, fd == -1 ? strerror(errno) : "OPENED");
    t[n].what = ELV_SHM_HW; t[n++].ok = fd != -1;
    if (fd != -1) close(fd);
    int r = elv_rt_set_prio(ELV_PRIO_CTRL + 5);
    printf("attack: raise priority to %d -> %s\n", ELV_PRIO_CTRL + 5, r ? strerror(r) : "RAISED");
    t[n].what = "priority"; t[n++].ok = r == 0;
    r = setuid(0);
    printf("attack: setuid(0) -> %s\n", r ? strerror(errno) : "ROOT");
    t[n].what = "setuid"; t[n++].ok = r == 0;

    int refused = 0;
    for (int i = 0; i < n; i++) refused += !t[i].ok;
    printf("attack: %d of %d forbidden actions refused\n", refused, n);
    return refused == n ? 0 : 1;
}

int main(int argc, char **argv)
{
    bool malicious = false, do_attack = false;
    long hang_after = 0;
    int opt;
    while ((opt = getopt(argc, argv, "mH:a")) != -1) {
        if (opt == 'm') malicious = true;
        else if (opt == 'H') hang_after = atol(optarg);
        else if (opt == 'a') do_attack = true;
    }

    /* map the ECG ring read-only before dropping privileges */
    int efd = shm_open(ELV_SHM_ECG, O_RDONLY, 0);
    if (efd == -1) { perror("elv-advisor: " ELV_SHM_ECG); return 1; }
    const elv_ecg_shm_t *ecg = mmap(NULL, sizeof(*ecg), PROT_READ, MAP_SHARED, efd, 0);
    if (ecg == MAP_FAILED) { perror("mmap ecg"); return 1; }
    close(efd);

    /* Set up the 4 Hz timer while still allowed to; an unprivileged
       process may be refused a new timer, and then it would wait forever. */
    elv_rt_t rt;
    elv_rt_set_prio(ELV_PRIO_ADVISOR);
    if (!do_attack && elv_rt_init(&rt, 250000000L, ELV_PRIO_ADVISOR) != 0) {
        perror("elv-advisor: timer");
        return 1;
    }

    drop_privileges();
    if (do_attack) return attack();

    int out = open(ELV_DEV_ADVICE, O_WRONLY);
    if (out == -1) { perror("elv-advisor: " ELV_DEV_ADVICE); return 1; }

    static elv_advisor_t adv;
    elv_advisor_init(&adv, ELV_RATE_DEFAULT_CPM / 60.0f);
    uint64_t seen = atomic_load(&ecg->n);
    uint64_t seq = 0, wakes = 0;
    time_t start = time(NULL);

    for (;;) {
        elv_rt_wait(&rt);
        uint64_t n = atomic_load(&ecg->n);
        if (n - seen > ELV_ECG_RING) seen = n - ELV_ECG_RING;
        for (; seen < n; seen++) elv_advisor_push(&adv, ecg->mv[seen % ELV_ECG_RING]);

        if (hang_after && time(NULL) - start >= hang_after) {
            for (;;) pause();                       /* alive, silent */
        }
        if (++wakes % 4 != 0) continue;              /* classify at 1 Hz */

        elv_advisor_msg_t m;
        if (!elv_advisor_classify(&adv, &m)) continue;
        if (malicious) {
            m.rhythm = m.rhythm == ELV_RHYTHM_SHOCKABLE ? ELV_RHYTHM_ORGANIZED : ELV_RHYTHM_SHOCKABLE;
            m.confidence = 1.0f;
            m.rosc_likelihood = 1.0f;
            m.demand_shock = true;
            m.demand_stop = true;
            m.demand_depth_m = 0.12f;
        }
        m.seq = ++seq;
        if (write(out, &m, sizeof(m)) != (ssize_t)sizeof(m) && errno != EINTR)
            perror("elv-advisor: write");
    }
}
