/*
 * Elvrek: inter-process layout on the target.
 * SPDX-License-Identifier: Apache-2.0
 *
 *   /elvrek_hw   shared memory, root 0600: the hardware boundary.
 *                  elv-plant (or the Pi 5 driver) writes sensors, elvrekd
 *                  writes actuators. Nobody else maps it.
 *   /elvrek_ecg  shared memory, 0644: an ECG ring the advisor maps
 *                  read-only. It is the advisor's only input.
 *   /elvrek_flt  shared memory, root 0600: fault injection for the suite.
 *
 *   /dev/elvrek/status    0444  one-line snapshot, key=value
 *   /dev/elvrek/advice    0622  advisor writes elv_advisor_msg_t
 *   /dev/elvrek/operator  0600  crew commands (elv-ctl)
 *   /dev/elvrek/piston    0400  actuator state; nobody may write
 *   /dev/elvrek/shock     0400  shock line state; nobody may write
 *
 * Writers publish with a seqlock so the 1 kHz loop never blocks on a reader.
 */
#ifndef ELV_IPC_H
#define ELV_IPC_H

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "elv_hw.h"
#include "elv_proto.h"

#define ELV_SHM_HW   "/elvrek_hw"
#define ELV_SHM_ECG  "/elvrek_ecg"
#define ELV_SHM_FLT  "/elvrek_flt"
#define ELV_DEV_DIR      "/dev/elvrek"
#define ELV_DEV_STATUS   ELV_DEV_DIR "/status"
#define ELV_DEV_ADVICE   ELV_DEV_DIR "/advice"
#define ELV_DEV_OPERATOR ELV_DEV_DIR "/operator"
#define ELV_DEV_PISTON   ELV_DEV_DIR "/piston"
#define ELV_DEV_SHOCK    ELV_DEV_DIR "/shock"

#define ELV_ADVISOR_UID  2002
#define ELV_PRIO_CTRL    60      /* elvrekd control thread              */
#define ELV_PRIO_PLANT   55      /* plant / hardware driver               */
#define ELV_PRIO_RESMGR  20      /* floor; handlers inherit client prio   */
#define ELV_PRIO_TELEM   12
#define ELV_PRIO_ADVISOR 10

/* seqlock */
typedef struct { _Atomic uint32_t v; } elv_seq_t;

static inline void elv_seq_wbegin(elv_seq_t *s)
{ atomic_fetch_add_explicit(&s->v, 1, memory_order_acq_rel); atomic_thread_fence(memory_order_release); }
static inline void elv_seq_wend(elv_seq_t *s)
{ atomic_thread_fence(memory_order_release); atomic_fetch_add_explicit(&s->v, 1, memory_order_release); }
static inline uint32_t elv_seq_rbegin(elv_seq_t *s)
{ uint32_t v; while ((v = atomic_load_explicit(&s->v, memory_order_acquire)) & 1u) ; return v; }
static inline int elv_seq_rretry(elv_seq_t *s, uint32_t v)
{ atomic_thread_fence(memory_order_acquire); return atomic_load_explicit(&s->v, memory_order_relaxed) != v; }

/* Copy with a bounded number of retries; returns 0 on success. */
#define ELV_SEQ_READ(seqp, dst, src) ({                                   \
    int _ok = -1;                                                         \
    for (int _i = 0; _i < 8 && _ok; _i++) {                               \
        uint32_t _v = elv_seq_rbegin(seqp);                               \
        memcpy((dst), (const void *)(src), sizeof(*(dst)));               \
        if (!elv_seq_rretry(seqp, _v)) _ok = 0;                           \
    }                                                                     \
    _ok; })

/* /elvrek_hw */
typedef struct {
    uint32_t        magic;          /* ELV_HW_MAGIC */
    elv_seq_t       sseq;
    elv_sensors_t   sensors;        /* written by the plant / driver */
    elv_seq_t       aseq;
    elv_actuators_t act;            /* written by elvrekd only     */
    _Atomic uint64_t act_tick;      /* elvrekd heartbeat for the hardware watchdog */
    /* ground truth the plant exposes for the test suite (not used by control) */
    double          true_depth_m, max_true_depth_m, max_force_n;
    uint32_t        strokes, strokes_in_range, shocks_received, shocks_off_recoil;
    uint32_t        hw_watchdog_trips;
} elv_hw_shm_t;
#define ELV_HW_MAGIC 0x414E5648u

/* /elvrek_ecg */
#define ELV_ECG_RING 4096
typedef struct {
    _Atomic uint64_t n;             /* samples written so far */
    float            mv[ELV_ECG_RING];
} elv_ecg_shm_t;

/* /elvrek_flt */
typedef struct {
    _Atomic int drop_sensors;
    _Atomic int estop;
    _Atomic int transport;
    _Atomic int rosc_now;           /* force return of circulation (pulse) */
    _Atomic int shock_converts;
    _Atomic int stiff_chest;
} elv_flt_shm_t;

/* status snapshot (elvrekd -> resmgr thread) */
typedef struct {
    uint64_t tick;
    int      state, sw_phase, fault;
    double   ccf;
    uint32_t shocks, windows, max_window, last_window, compressions, breaths;
    uint32_t max_machine, last_machine, max_human, last_human, press_retries, preauth_shocks;
    double   depth_cmd_m, force_n, pos_m, etco2;
    int      epi_prompt;
    uint32_t epi_prompts, epi_given;
    uint32_t inv_total;
    uint64_t missed_ticks, late_wakeups, work_overruns;
    uint64_t adv_age_ticks;
    uint32_t adv_ignored, adv_skips, adv_msgs, adv_rejected;
    int      shock_line, vent_on, charge_on, charger_ready, transport_comp, mode_30_2;
    uint32_t force_limit_events;
} elv_status_t;

#endif
