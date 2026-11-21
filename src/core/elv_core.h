/*
 * Elvrek: safety authority core (pure, deterministic, OS-independent).
 * SPDX-License-Identifier: Apache-2.0
 *
 * elv_core_step() is called once per 1 ms control tick. It is the only code
 * that decides the piston setpoint, ventilation, charging, and the shock line.
 * The QNX process `elvrekd` wraps it with a timer pulse, resource managers,
 * and the security policy; the host tests call it directly.
 */
#ifndef ELV_CORE_H
#define ELV_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include "elv_hw.h"
#include "elv_cancel.h"
#include "elv_proto.h"
#include "elv_limits.h"

typedef enum {
    ELV_ST_IDLE = 0,
    ELV_ST_COMPRESS,       /* hands on; may be charging in the background   */
    ELV_ST_VENT_GAP,       /* 30:2 mode only: two breaths                   */
    ELV_ST_SHOCK_WINDOW,   /* the ONLY hands-off state for a shock          */
    ELV_ST_ROSC_HOLD,      /* trusted return of circulation: parked, breathe */
    ELV_ST_FAULT_RETRACT,  /* controlled lift to full recoil                */
    ELV_ST_SAFE_HOLD,      /* parked at recoil, alarm, waits for operator   */
    ELV_ST_STOPPED
} elv_state_t;

typedef enum {
    ELV_SW_PARK = 0,       /* finish upstroke, reach recoil                 */
    ELV_SW_CONFIRM,        /* trusted rhythm check on clean ECG             */
    ELV_SW_AWAIT_PRESS,    /* operator presses SHOCK                        */
    ELV_SW_DELIVER         /* shock line closed for ELV_SHOCK_PULSE_TICKS   */
} elv_sw_phase_t;

typedef enum {
    ELV_FAULT_NONE = 0,
    ELV_FAULT_SENSOR_STALE,
    ELV_FAULT_ESTOP,
    ELV_FAULT_OVERFORCE,
    ELV_FAULT_PARK_TIMEOUT,
    ELV_FAULT_INVARIANT
} elv_fault_t;

typedef enum {
    ELV_EV_START = 1, ELV_EV_WINDOW_OPEN, ELV_EV_SHOCK, ELV_EV_WINDOW_NO_SHOCK,
    ELV_EV_ROSC, ELV_EV_REARREST, ELV_EV_FAULT, ELV_EV_EPI_DUE, ELV_EV_EPI_GIVEN,
    ELV_EV_ADVISOR_SKIP, ELV_EV_STOP, ELV_EV_ZP_SKIP, ELV_EV_ZP_SURE
} elv_event_t;

typedef struct { uint64_t tick; uint16_t type; int32_t value; } elv_log_entry_t;
#define ELV_LOG_LEN 512

typedef struct {
    uint64_t hands_on_ticks;     /* ticks in COMPRESS                         */
    uint64_t cpr_ticks;          /* ticks CPR is required (excl. ROSC/idle)    */
    uint32_t compressions;
    uint32_t breaths;
    uint32_t shock_windows;
    uint32_t shocks;
    uint32_t windows_no_shock;   /* confirm said non-shockable, energy dumped */
    uint32_t max_window_ticks;   /* total hands-off for the shock          */
    uint32_t last_window_ticks;
    uint32_t max_machine_ticks;  /* park + confirm + discharge: the device */
    uint32_t last_machine_ticks;
    uint32_t max_human_ticks;    /* waiting for the operator's press       */
    uint32_t last_human_ticks;
    uint32_t press_retries;      /* windows closed to resume compressions  */
    uint32_t preauth_shocks;     /* shocks pre-authorised during charging  */
    uint32_t force_limit_events;
    uint32_t advisor_demands_ignored;
    uint32_t advisor_skips;      /* boundaries the advisor talked us out of   */
    uint64_t rosc_detect_tick;   /* 0 = never                                 */
    uint64_t rosc_parked_tick;   /* piston reached recoil after ROSC          */
    uint32_t epi_prompts;
    uint32_t epi_given;
    uint64_t first_shock_tick;
    /* rhythm read through compressions (zero pause) */
    uint32_t zp_checks;          /* 2-minute checks decided with a verdict */
    uint32_t zp_skipped;         /* sure not shockable: no pause at all    */
    uint32_t zp_sure_shock;      /* sure shockable: no 0.8 s look          */
    uint32_t zp_unsure;          /* classic 0.8 s look                     */
} elv_metrics_t;

/* Extended features for the rhythm model (see elv_rhythm_model.h). */
#define ELV_RF_N 12
#define ELV_RF_MAXN 1024
#define ELV_RF_BINS 77
/* Staged feature computation, so the work can be spread over control ticks. */
typedef struct {
    float    xw[ELV_RF_MAXN];
    double   p[ELV_RF_BINS];
    float    out[ELV_RF_N];
    uint32_t n;
    int      k;
    bool     active;
} elv_rf_t;
void elv_rf_begin(elv_rf_t *st, const float *mv, uint32_t n);
bool elv_rf_step(elv_rf_t *st);          /* true when the features are ready */
void elv_rhythm_features_ext(const float *mv, uint32_t n, float out[ELV_RF_N]);
/* Logistic score in [0, 1] from the extended features and frozen weights. */
float elv_rhythm_score(const float feat[ELV_RF_N]);
/* Verdict from the mean of 8 scores: +1 sure shockable, -1 sure not, 0 unsure. */
int8_t elv_rhythm_verdict(float mean_score);

typedef struct {
    /* public, read-only for telemetry and the invariant monitor */
    elv_state_t    state;
    elv_sw_phase_t sw_phase;
    elv_fault_t    fault;
    uint64_t       tick;
    bool           downstroke;      /* piston is travelling into the chest   */
    bool           confirm_shockable;
    bool     preauth;            /* SHOCK pressed while charging           */
    bool           press_seen;
    bool           rosc_trusted;
    double         depth_cmd_m;     /* adaptive depth for current cycle      */
    double         etco2_baseline;
    elv_metrics_t  m;

    /* private */
    bool     mode_30_2;
    bool     transport_comp;
    uint32_t sensor_stale_ticks;    /* ELV_SENSOR_STALE_TICKS, or wider in
                                       the explicit emulator mode            */
    uint32_t period_ticks;
    uint32_t phase;
    uint32_t cycle_comp_count;      /* compressions since last 30:2 gap      */
    double   cycle_peak_force;
    uint64_t next_boundary_tick;    /* next 2-minute rhythm-check boundary   */
    bool     charging;
    bool     want_window;           /* go to window at next recoil           */
    bool     skipped_last_boundary;
    uint64_t state_enter_tick;
    uint64_t window_enter_tick;
    uint64_t sub_enter_tick;
    uint64_t await_enter_tick;      /* 0 = never waited for a press          */
    uint64_t press_tick;            /* when the press was accepted (0 = none) */
    uint32_t retries_this_charge;
    uint32_t retry_at_compressions;
    uint64_t last_sensor_seq;
    uint64_t last_sensor_tick;
    uint64_t last_adv_seq;
    uint64_t last_adv_tick;
    elv_advisor_msg_t adv;
    bool     adv_valid;
    uint64_t next_breath_tick;
    bool     breath_pending;
    uint64_t breath_end_tick;
    uint32_t gap_breaths;
    uint64_t rosc_lost_since;
    uint64_t rosc_hold_vent_tick;
    float    confirm_buf[ELV_CONFIRM_TICKS / ELV_ECG_TICKS_PER_SAMPLE];
    uint32_t confirm_n;
    uint64_t last_ecg_seq;
    bool     trusted_rosc_latched;
    /* transport: disturbance observer on the frame IMU */
    double   s_hat, sv_hat;
    double   force_cap_m;          /* depth cap latched this downstroke     */
    double   prev_pos, prev_force, k_est, v_est;
    uint64_t prev_sample_n;
    /* ACLS clock */
    uint64_t epi_due_tick;          /* 0 = not running                       */
    bool     epi_prompt;            /* crew prompt showing                   */
    /* rhythm read through compressions: artifact canceller on the
       commanded compression phase, a 3 s window scored every second,
       the last 8 scores (10 s) decide at the 2-minute check */
    elv_cancel_t canc;
    float    zp_buf[3 * ELV_ECG_HZ];
    uint32_t zp_head, zp_since, zp_run;
    float    zp_score[8];
    uint32_t zp_nscore;
    int8_t   zp_verdict;            /* +1 sure shockable, -1 sure not, 0 unsure */
    bool     zp_sure;               /* this window was opened on a sure verdict */
    bool     zp_enabled;
    float    zp_mean;
    elv_rf_t zp_rf;                 /* features being computed, a bin per tick */
    /* event record */
    elv_log_entry_t log[ELV_LOG_LEN];
    uint32_t log_n;
} elv_core_t;

void elv_core_init(elv_core_t *c);

/* adv may be NULL when no new advisor message arrived this tick. */
void elv_core_step(elv_core_t *c,
                   const elv_sensors_t *s,
                   const elv_advisor_msg_t *adv,
                   const elv_operator_t *op,
                   elv_actuators_t *out);

/* Force a fault from outside the core (e.g. the invariant monitor). */
void elv_core_force_fault(elv_core_t *c, elv_fault_t f);

double elv_core_ccf(const elv_core_t *c);
const char *elv_state_name(elv_state_t s);
const char *elv_fault_name(elv_fault_t f);

/* Trusted, deterministic rhythm check used only on hands-off ECG. */
bool elv_trusted_shockable(const float *mv, uint32_t n);

/* The same check, split so it can be measured and tuned. */
typedef struct { float ptp, flat, xing_hz, steep, fib; } elv_rhythm_feat_t;
typedef struct { float min_ptp, max_flat, min_steep, min_xing, max_xing, min_fib; } elv_rhythm_thr_t;
extern const elv_rhythm_thr_t ELV_RHYTHM_THR;
void elv_rhythm_features(const float *mv, uint32_t n, elv_rhythm_feat_t *f);

bool elv_rhythm_decide(const elv_rhythm_feat_t *f, const elv_rhythm_thr_t *t);

#endif
