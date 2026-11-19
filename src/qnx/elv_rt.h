/*
 * Elvrek: small real-time helpers shared by the target processes.
 * SPDX-License-Identifier: Apache-2.0
 *
 * On QNX a periodic wake-up is a timer pulse on a private channel, received
 * at the pulse's priority. Elsewhere (host smoke tests) it falls back to
 * clock_nanosleep with an absolute deadline.
 */
#ifndef ELV_RT_H
#define ELV_RT_H

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef __QNXNTO__
#include <sys/neutrino.h>
#endif

typedef struct {
#ifdef __QNXNTO__
    int chid;
#else
    struct timespec next;
#endif
    long period_ns;
} elv_rt_t;

static inline int elv_rt_set_prio(int prio)
{
    struct sched_param sp = { .sched_priority = prio };
    return pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
}

static inline int elv_rt_init(elv_rt_t *rt, long period_ns, int prio)
{
    memset(rt, 0, sizeof(*rt));
    rt->period_ns = period_ns;
#ifdef __QNXNTO__
    rt->chid = ChannelCreate(_NTO_CHF_PRIVATE);
    if (rt->chid == -1) return -1;
    int coid = ConnectAttach(0, 0, rt->chid, _NTO_SIDE_CHANNEL, 0);
    if (coid == -1) return -1;
    struct sigevent ev;
    SIGEV_PULSE_INIT(&ev, coid, prio, _PULSE_CODE_MINAVAIL + 1, 0);
    timer_t tid;
    if (timer_create(CLOCK_MONOTONIC, &ev, &tid) == -1) return -1;
    struct itimerspec its = { { 0, period_ns }, { 0, period_ns } };
    return timer_settime(tid, 0, &its, NULL);
#else
    (void)prio;
    return clock_gettime(CLOCK_MONOTONIC, &rt->next);
#endif
}

static inline void elv_rt_wait(elv_rt_t *rt)
{
#ifdef __QNXNTO__
    struct _pulse p;
    while (MsgReceivePulse(rt->chid, &p, sizeof(p), NULL) == -1 && errno == EINTR)
        ;
#else
    rt->next.tv_nsec += rt->period_ns;
    while (rt->next.tv_nsec >= 1000000000L) { rt->next.tv_nsec -= 1000000000L; rt->next.tv_sec++; }
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &rt->next, NULL) == EINTR)
        ;
#endif
}

#endif
