/*
 * Elvrek: safety authority core.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Design rules (enforced here, checked independently by elv_inv.c):
 *  1. Charging happens WHILE compressions continue. The only hands-off state
 *    for a shock is SHOCK_WINDOW, entered at full recoil, target < 3 s.
 *  2. The advisor (untrusted classifier) can only propose. Its worst case is
 *    to skip ONE pre-charge boundary; the next boundary is always checked.
 *    It can never move the piston, stop compressions, or close the shock line.
 *  3. Return of circulation stops compressions only on trusted inputs
 *    (pulse input, or an abrupt EtCO2 rise).
 *  4. Any fault retracts at controlled speed to full recoil and holds.
 */
#include "elv_core.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *const st_names[] = {
    "IDLE", "COMPRESS", "VENT_GAP", "SHOCK_WINDOW", "ROSC_HOLD",
    "FAULT_RETRACT", "SAFE_HOLD", "STOPPED"
};
static const char *const fault_names[] = {
    "NONE", "SENSOR_STALE", "ESTOP", "OVERFORCE", "PARK_TIMEOUT", "INVARIANT"
};

const char *elv_state_name(elv_state_t s) { return st_names[s]; }
const char *elv_fault_name(elv_fault_t f) { return fault_names[f]; }

static void logev(elv_core_t *c, elv_event_t t, int32_t v)
{
    if (c->log_n < ELV_LOG_LEN) {
        c->log[c->log_n].tick = c->tick;
        c->log[c->log_n].type = (uint16_t)t;
        c->log[c->log_n].value = v;
        c->log_n++;
    }
}

static void enter(elv_core_t *c, elv_state_t s)
{
    c->state = s;
    c->state_enter_tick = c->tick;
}

void elv_core_init(elv_core_t *c)
{
    memset(c, 0, sizeof(*c));
    c->state = ELV_ST_IDLE;
    c->period_ticks = (uint32_t)(60.0 * ELV_TICK_HZ / ELV_RATE_DEFAULT_CPM);
    c->depth_cmd_m = ELV_DEPTH_TARGET_M;
    c->sensor_stale_ticks = ELV_SENSOR_STALE_TICKS;
}

void elv_core_force_fault(elv_core_t *c, elv_fault_t f)
{
    if (c->state == ELV_ST_FAULT_RETRACT || c->state == ELV_ST_SAFE_HOLD)
        return;
    c->fault = f;
    logev(c, ELV_EV_FAULT, (int32_t)f);
    enter(c, ELV_ST_FAULT_RETRACT);
}

double elv_core_ccf(const elv_core_t *c)
{
    if (c->m.cpr_ticks == 0) return 1.0;
    return (double)c->m.hands_on_ticks / (double)c->m.cpr_ticks;
}

/* Trusted rhythm check. Deliberately simple and auditable: four numbers
   measured on hands-off ECG (no compression artifact, 250 Hz), and one
   fixed rule. The thresholds are tuned on one public database and scored
   on another (tests/host/rhythm_tune.c). */

/* Tuned on CUDB only with tests/host/rhythm_tune.c (0.8 s windows, at least
   95% of every non-shockable class left alone), then scored once on VFDB:
   VF found 75.0%, normal sinus left alone 97.7%, other rhythms 98.0%,
   asystole 90.0%. Below the AHA goals, and reported as such. */
const elv_rhythm_thr_t ELV_RHYTHM_THR = {
    .min_ptp = 0.10f, .max_flat = 0.40f, .min_steep = 0.0f,
    .min_xing = 3.0f, .max_xing = 20.0f, .min_fib = 0.50f,
};

void elv_rhythm_features(const float *in, uint32_t n, elv_rhythm_feat_t *f)
{
    memset(f, 0, sizeof(*f));
    if (n < 100 || n > 1024) return;
    /* remove baseline wander with a least squares line */
    float x[1024];
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (uint32_t i = 0; i < n; i++) { sx += i; sy += in[i]; sxx += (double)i * i; sxy += i * (double)in[i]; }
    double den = n * sxx - sx * sx;
    double b = den != 0 ? (n * sxy - sx * sy) / den : 0, a = (sy - b * sx) / n;
    float lo = 1e9f, hi = -1e9f;
    for (uint32_t i = 0; i < n; i++) {
        x[i] = (float)(in[i] - (a + b * i));
        if (x[i] < lo) lo = x[i];
        if (x[i] > hi) hi = x[i];
    }
    float ptp = hi - lo;
    f->ptp = ptp;
    if (ptp <= 0.0f) return;

    /* median from a 64 bin histogram */
    uint32_t hist[64] = {0};
    for (uint32_t i = 0; i < n; i++) hist[(int)((x[i] - lo) / ptp * 63.0f)]++;
    uint32_t acc = 0; int mb = 0;
    for (mb = 0; mb < 64; mb++) { acc += hist[mb]; if (acc >= n / 2) break; }
    float median = lo + (mb + 0.5f) * ptp / 63.0f;

    uint32_t flat = 0, xings = 0, steep = 0;
    float maxd = 0;
    for (uint32_t i = 1; i < n; i++) { float d = fabsf(x[i] - x[i - 1]); if (d > maxd) maxd = d; }
    for (uint32_t i = 0; i < n; i++) {
        if (fabsf(x[i] - median) < 0.10f * ptp) flat++;
        if (i && (x[i - 1] - median) * (x[i] - median) < 0) xings++;
        if (i && fabsf(x[i] - x[i - 1]) > 0.5f * maxd) steep++;
    }
    f->flat = (float)flat / n;
    f->xing_hz = (float)(xings / 2.0 / ((double)n / ELV_ECG_HZ));
    f->steep = (float)steep / n;     /* VF: many steep samples; QRS: a few spikes */

    /* share of power in the 3 to 9 Hz fibrillation band, 1 to 20 Hz total,
       on a Hann window (Goertzel per 0.5 Hz bin) */
    double tot = 0, fib = 0;
    for (double hz = 1.0; hz <= 20.0; hz += 0.5) {
        double wv = 2.0 * M_PI * hz / ELV_ECG_HZ, cw = 2.0 * cos(wv), s1 = 0, s2 = 0;
        for (uint32_t i = 0; i < n; i++) {
            double hn = 0.5 - 0.5 * cos(2.0 * M_PI * i / (n - 1));
            double s0 = x[i] * hn + cw * s1 - s2; s2 = s1; s1 = s0;
        }
        double p = s1 * s1 + s2 * s2 - cw * s1 * s2;
        tot += p;
        if (hz >= 3.0 && hz <= 9.0) fib += p;
    }
    f->fib = tot > 0 ? (float)(fib / tot) : 0.0f;
}

bool elv_rhythm_decide(const elv_rhythm_feat_t *f, const elv_rhythm_thr_t *t)
{
    return f->ptp >= t->min_ptp && f->flat < t->max_flat && f->steep >= t->min_steep &&
           f->xing_hz >= t->min_xing && f->xing_hz <= t->max_xing && f->fib >= t->min_fib;
}

bool elv_trusted_shockable(const float *mv, uint32_t n)
{
    elv_rhythm_feat_t f;
    elv_rhythm_features(mv, n, &f);
    return elv_rhythm_decide(&f, &ELV_RHYTHM_THR);
}


static bool advisor_fresh(const elv_core_t *c)
{
    return c->adv_valid && (c->tick - c->last_adv_tick) <= ELV_ADVISOR_STALE_TICKS;
}

/* Does the advisor talk us out of pre-charging at this boundary?
   Only if fresh, confident, non-shockable, and we did not already skip. */
static bool advisor_vetoes_precharge(elv_core_t *c)
{
    if (!advisor_fresh(c)) return false;
    if (c->skipped_last_boundary) return false;
    if (c->adv.rhythm == ELV_RHYTHM_SHOCKABLE) return false;
    if (c->adv.rhythm == ELV_RHYTHM_UNKNOWN) return false;
    return c->adv.confidence >= 0.90f;
}

static void take_advisor(elv_core_t *c, const elv_advisor_msg_t *a)
{
    if (!a) return;
    if (a->magic != ELV_ADVISOR_MAGIC) return;
    if (a->seq <= c->last_adv_seq && c->adv_valid) return;
    if (!(a->confidence >= 0.0f && a->confidence <= 1.0f)) return; /* NaN too */
    if ((unsigned)a->rhythm > ELV_RHYTHM_ASYSTOLE) return;
    c->adv = *a;
    c->adv_valid = true;
    c->last_adv_seq = a->seq;
    c->last_adv_tick = c->tick;
    if (a->demand_shock || a->demand_stop || a->demand_depth_m != 0.0f)
        c->m.advisor_demands_ignored++;
}

static void update_etco2(elv_core_t *c, const elv_sensors_t *s)
{
    if (c->state != ELV_ST_COMPRESS && c->state != ELV_ST_VENT_GAP &&
        c->state != ELV_ST_SHOCK_WINDOW)
        return;
    double a = 1.0 / ELV_ETCO2_BASELINE_TICKS;
    if (c->etco2_baseline <= 0.0) c->etco2_baseline = s->etco2_mmhg;
    /* the baseline does not chase a jump: rises are followed 10x slower */
    if (s->etco2_mmhg > c->etco2_baseline) a *= 0.1;
    c->etco2_baseline += a * (s->etco2_mmhg - c->etco2_baseline);
}

static bool trusted_rosc(const elv_core_t *c, const elv_sensors_t *s)
{
    if (s->pulse_present) return true;
    if (c->m.compressions < 30) return false;          /* need a baseline */
    return s->etco2_mmhg >= c->etco2_baseline + ELV_ETCO2_ROSC_JUMP_MMHG &&
           s->etco2_mmhg >= ELV_ETCO2_ROSC_ABS_MMHG;
}

/* Compression trajectory: raised cosine, 50 % duty, full recoil at phase 0. */
static double compress_target(elv_core_t *c, const elv_sensors_t *s)
{
    double p = (double)c->phase / c->period_ticks;
    double x = c->depth_cmd_m * 0.5 * (1.0 - cos(2.0 * M_PI * p));
    /* In transport mode the setpoint is measured from the chest and the
       driver adds the chest motion it measures itself, every driver tick.
       All positions below are therefore taken in the chest's frame. */
    const double pos = c->transport_comp ? s->piston_pos_m - s->chest_offset_m
                                         : s->piston_pos_m;
    c->downstroke = p < 0.5;

    /* Force ceiling. The actuator needs distance to stop, so the cap latches
       at 90 % of the limit and holds for the rest of this downstroke; at the
       limit itself the setpoint backs off 2 mm. */
    if (!c->downstroke) c->force_cap_m = 0.0;
    /* stopping margin: stiffness x distance the actuator needs to stop */
    /* Speed and stiffness are measured only when a new hardware sample
       arrives, over the real time between samples. After a pause the
       controller replays the ticks it missed with the newest sample; if
       speed were taken per controller tick, the whole movement during the
       pause would look like it happened in 1 ms, the guard below would trip
       early and the stroke would come up short. */
    if (s->sample_n != c->prev_sample_n) {
        uint64_t n = s->sample_n - c->prev_sample_n;
        double dx = pos - c->prev_pos;
        if (c->prev_sample_n != 0 && n < 1000) {
            c->v_est = dx / ((double)n * ELV_TICK_S);
            if (dx > 1e-5 && s->force_n > 50.0) {
                double k = (s->force_n - c->prev_force) / dx;
                if (k > 0 && k < 1e6) c->k_est += 0.2 * (k - c->k_est);
            }
        } else {
            c->v_est = 0.0;
        }
        c->prev_sample_n = s->sample_n;
        c->prev_pos = pos;
        c->prev_force = s->force_n;
    }
    if (c->k_est < 5000.0) c->k_est = 5000.0;
    double v = c->v_est;
    double stop_m = v > 0 ? v * v / (2.0 * ELV_PISTON_AMAX_MS2) + v * ELV_TICK_S * 2 : 0.0;
    double margin = c->k_est * stop_m + 15.0;
    if (c->downstroke && s->force_n >= ELV_FORCE_LIMIT_N - margin && c->force_cap_m == 0.0) {
        c->force_cap_m = pos > 0.0005 ? pos : 0.0005;
        c->cycle_peak_force = ELV_FORCE_LIMIT_N;
    }
    if (c->force_cap_m > 0.0 && x > c->force_cap_m) x = c->force_cap_m;
    if (s->force_n >= ELV_FORCE_LIMIT_N && x > pos - 0.002)
        x = pos - 0.002;
    if (s->force_n > c->cycle_peak_force) c->cycle_peak_force = s->force_n;
    if (x > ELV_DEPTH_MAX_M) x = ELV_DEPTH_MAX_M;
    if (x < 0.0) x = 0.0;
    return x;
}

static void end_of_cycle_adapt(elv_core_t *c)
{
    if (c->cycle_peak_force >= ELV_FORCE_LIMIT_N) {
        c->m.force_limit_events++;
        c->depth_cmd_m -= 0.002;               /* stiff chest: go shallower */
        if (c->depth_cmd_m < 0.030) c->depth_cmd_m = 0.030;
    } else if (c->depth_cmd_m < ELV_DEPTH_TARGET_M &&
               c->cycle_peak_force < 0.85 * ELV_FORCE_LIMIT_N) {
        c->depth_cmd_m += 0.001;
        if (c->depth_cmd_m > ELV_DEPTH_TARGET_M) c->depth_cmd_m = ELV_DEPTH_TARGET_M;
    }
    c->cycle_peak_force = 0.0;
}

static void open_window(elv_core_t *c)
{
    enter(c, ELV_ST_SHOCK_WINDOW);
    c->sw_phase = ELV_SW_PARK;
    c->window_enter_tick = c->tick;
    c->sub_enter_tick = c->tick;
    c->confirm_n = 0;
    c->confirm_shockable = false;
    c->press_seen = false;
    c->m.shock_windows++;
    /* a breath cut short by the window restarts on the next upstroke */
    if (c->breath_end_tick > c->tick) { c->breath_end_tick = c->tick; c->breath_pending = true; }
    logev(c, ELV_EV_WINDOW_OPEN, 0);
}

static void close_window_ex(elv_core_t *c, bool shocked, bool keep_charge)
{
    uint32_t w = (uint32_t)(c->tick - c->window_enter_tick);
    /* human time = prompt until press (or until the window gave up) */
    uint32_t h = 0;
    if (c->await_enter_tick)
        h = (uint32_t)((c->press_tick ? c->press_tick : c->tick) - c->await_enter_tick);
    uint32_t mtime = w - h;
    c->m.last_window_ticks = w;
    if (w > c->m.max_window_ticks) c->m.max_window_ticks = w;
    c->m.last_machine_ticks = mtime;
    if (mtime > c->m.max_machine_ticks) c->m.max_machine_ticks = mtime;
    c->m.last_human_ticks = h;
    if (h > c->m.max_human_ticks) c->m.max_human_ticks = h;
    c->await_enter_tick = 0;
    c->press_tick = 0;
    if (!shocked) { c->m.windows_no_shock++; logev(c, ELV_EV_WINDOW_NO_SHOCK, (int32_t)w); }
    else logev(c, ELV_EV_SHOCK, (int32_t)w);
    c->want_window = false;
    c->phase = 0;
    if (keep_charge) {
        /* the crew was late: resume compressions now, keep the energy,
           re-offer the shock after ELV_RETRY_COMPRESSIONS compressions */
        c->m.press_retries++;
        c->retries_this_charge++;
        c->retry_at_compressions = c->m.compressions + ELV_RETRY_COMPRESSIONS;
    } else {
        c->charging = false;
        c->preauth = false;
        c->retries_this_charge = 0;
        c->retry_at_compressions = 0;
        c->next_boundary_tick = c->tick + ELV_CYCLE_TICKS;
    }
    enter(c, ELV_ST_COMPRESS);
}

static void close_window(elv_core_t *c, bool shocked)
{
    close_window_ex(c, shocked, false);
}

void elv_core_step(elv_core_t *c, const elv_sensors_t *s,
                   const elv_advisor_msg_t *adv, const elv_operator_t *op,
                   elv_actuators_t *out)
{
    c->tick++;
    memset(out, 0, sizeof(*out));
    out->piston_vmax_ms = ELV_PISTON_VMAX_MS;
    c->downstroke = false;

    take_advisor(c, adv);
    c->transport_comp = op->transport_comp;
    if (op->mode_30_2 != c->mode_30_2 && c->state == ELV_ST_IDLE)
        c->mode_30_2 = op->mode_30_2;

    /* hardware health: checked before anything else */
    if (s->seq != c->last_sensor_seq) {
        c->last_sensor_seq = s->seq;
        c->last_sensor_tick = c->tick;
    }
    if (c->state != ELV_ST_IDLE && c->state != ELV_ST_STOPPED) {
        if (s->estop)
            elv_core_force_fault(c, ELV_FAULT_ESTOP);
        else if (c->tick - c->last_sensor_tick > c->sensor_stale_ticks)
            elv_core_force_fault(c, ELV_FAULT_SENSOR_STALE);
        else if (s->force_n > ELV_FORCE_ABS_N)
            elv_core_force_fault(c, ELV_FAULT_OVERFORCE);
    }

    /* collect clean ECG while parked in the window */
    bool new_ecg = s->ecg_seq != c->last_ecg_seq;
    c->last_ecg_seq = s->ecg_seq;

    update_etco2(c, s);

    /* trusted return of circulation */
    bool rosc = (c->state == ELV_ST_COMPRESS || c->state == ELV_ST_VENT_GAP)
                && trusted_rosc(c, s);
    c->rosc_trusted = trusted_rosc(c, s);
    if (rosc) {
        c->m.rosc_detect_tick = c->tick;
        c->m.rosc_parked_tick = 0;
        c->charging = false;
        c->want_window = false;
        c->rosc_lost_since = 0;
        c->rosc_hold_vent_tick = c->tick + ELV_VENT_CONT_PERIOD_TICKS;
        c->epi_due_tick = 0;
        c->epi_prompt = false;
        logev(c, ELV_EV_ROSC, s->pulse_present ? 1 : 0);
        enter(c, ELV_ST_ROSC_HOLD);
    }

    bool cpr_required = c->state == ELV_ST_COMPRESS || c->state == ELV_ST_VENT_GAP ||
                        c->state == ELV_ST_SHOCK_WINDOW;
    if (cpr_required) c->m.cpr_ticks++;

    switch (c->state) {
    case ELV_ST_IDLE:
        out->piston_target_m = 0.0;
        if (op->start) {
            c->mode_30_2 = op->mode_30_2;
            c->next_boundary_tick = c->tick + ELV_CYCLE_TICKS;
            c->phase = 0;
            c->next_breath_tick = c->tick + ELV_VENT_CONT_PERIOD_TICKS;
            logev(c, ELV_EV_START, c->mode_30_2);
            enter(c, ELV_ST_COMPRESS);
        }
        break;

    case ELV_ST_COMPRESS: {
        c->m.hands_on_ticks++;
        out->piston_target_m = compress_target(c, s);
        out->chest_ref = c->transport_comp;

        /* pre-charge while compressing, ahead of the 2-minute boundary */
        if (!c->charging && !c->want_window &&
            c->tick + ELV_PRECHARGE_LEAD_TICKS >= c->next_boundary_tick) {
            if (advisor_vetoes_precharge(c)) {
                c->skipped_last_boundary = true;
                c->m.advisor_skips++;
                logev(c, ELV_EV_ADVISOR_SKIP, 0);
                c->next_boundary_tick += ELV_CYCLE_TICKS;
            } else {
                c->skipped_last_boundary = false;
                c->charging = true;
            }
        }
        if (c->charging) {
            out->charge_on = !s->charger_ready;
            if (op->shock_press) c->preauth = true;
            if (s->charger_ready && c->tick >= c->next_boundary_tick &&
                c->m.compressions >= c->retry_at_compressions)
                c->want_window = true;
        }

        /* continuous mode: async breath started on the upstroke only */
        if (!c->mode_30_2) {
            if (c->tick >= c->next_breath_tick) c->breath_pending = true;
            if (c->breath_pending && !c->downstroke && c->tick >= c->breath_end_tick) {
                c->breath_pending = false;
                c->breath_end_tick = c->tick + ELV_VENT_INSP_TICKS;
                c->next_breath_tick = c->tick + ELV_VENT_CONT_PERIOD_TICKS;
                c->m.breaths++;
            }
            out->vent_on = c->tick < c->breath_end_tick;
        }

        c->phase++;
        if (c->phase >= c->period_ticks) {        /* back at full recoil */
            c->phase = 0;
            c->m.compressions++;
            c->cycle_comp_count++;
            end_of_cycle_adapt(c);
            if (c->want_window) {
                open_window(c);
            } else if (c->mode_30_2 && c->cycle_comp_count >= 30) {
                c->cycle_comp_count = 0;
                c->gap_breaths = 0;
                c->sub_enter_tick = c->tick;
                enter(c, ELV_ST_VENT_GAP);
            }
        }
        break;
    }

    case ELV_ST_VENT_GAP: {
        out->piston_target_m = 0.0;
        if (c->charging) out->charge_on = !s->charger_ready;
        uint64_t t = c->tick - c->state_enter_tick;
        /* breath 1: 0..1000, breath 2: 1300..2300, resume at 2600 */
        out->vent_on = (t < ELV_VENT_INSP_TICKS) ||
                       (t >= 1300 && t < 1300 + ELV_VENT_INSP_TICKS);
        if (t == 0 || t == 1300) c->m.breaths++;
        if (t >= ELV_VENT_30_2_GAP_TICKS) {
            c->phase = 0;
            enter(c, ELV_ST_COMPRESS);
        }
        break;
    }

    case ELV_ST_SHOCK_WINDOW: {
        out->piston_target_m = 0.0;
        uint64_t w = c->tick - c->window_enter_tick;
        bool parked = s->piston_pos_m < ELV_RECOIL_M && s->recoil_switch;

        if (w >= ELV_SHOCK_WIN_ABORT_TICKS && c->sw_phase != ELV_SW_DELIVER) {
            bool retry = c->sw_phase == ELV_SW_AWAIT_PRESS &&
                         c->retries_this_charge < ELV_MAX_RETRIES;
            if (!retry) out->charge_dump = true;   /* never leave energy armed */
            close_window_ex(c, false, retry);
            break;
        }
        switch (c->sw_phase) {
        case ELV_SW_PARK:
            if (parked) {
                c->sw_phase = ELV_SW_CONFIRM;
                c->sub_enter_tick = c->tick;
                c->confirm_n = 0;
            } else if (c->tick - c->sub_enter_tick > ELV_PARK_TIMEOUT_TICKS) {
                out->charge_dump = true;
                c->charging = false;
                elv_core_force_fault(c, ELV_FAULT_PARK_TIMEOUT);
            }
            break;
        case ELV_SW_CONFIRM:
            if (new_ecg && c->confirm_n < sizeof(c->confirm_buf) / sizeof(float))
                c->confirm_buf[c->confirm_n++] = s->ecg_mv;
            if (c->tick - c->sub_enter_tick >= ELV_CONFIRM_TICKS) {
                c->confirm_shockable = elv_trusted_shockable(c->confirm_buf, c->confirm_n);
                if (c->confirm_shockable && s->charger_ready && c->preauth && parked) {
                    /* pre-authorised: no human step inside the window */
                    c->m.preauth_shocks++;
                    c->press_seen = true;
                    c->sw_phase = ELV_SW_DELIVER;
                    c->sub_enter_tick = c->tick;
                } else if (c->confirm_shockable && s->charger_ready) {
                    c->sw_phase = ELV_SW_AWAIT_PRESS;
                    c->sub_enter_tick = c->tick;
                    c->await_enter_tick = c->tick;
                } else {
                    out->charge_dump = true;
                    close_window(c, false);
                }
            }
            break;
        case ELV_SW_AWAIT_PRESS:
            if (op->shock_press && parked && s->charger_ready) {
                c->press_seen = true;
                c->press_tick = c->tick;
                c->sw_phase = ELV_SW_DELIVER;
                c->sub_enter_tick = c->tick;
            }
            break;
        case ELV_SW_DELIVER:
            /* close the relay only while energy is stored; one discharge */
            if (parked && s->charger_ready &&
                c->tick - c->sub_enter_tick < ELV_SHOCK_PULSE_TICKS) {
                out->shock_line = true;
            } else {
                c->m.shocks++;
                if (c->m.shocks == 1) c->m.first_shock_tick = c->tick;
                /* ACLS: epinephrine after the 2nd shock, then every 3-5 min */
                if (c->m.shocks == 2 && c->epi_due_tick == 0) c->epi_due_tick = c->tick;
                close_window(c, true);         /* compressions resume now */
            }
            break;
        }
        break;
    }

    case ELV_ST_ROSC_HOLD:
        out->piston_target_m = 0.0;
        out->charge_dump = true;
        if (c->m.rosc_parked_tick == 0 && s->piston_pos_m < ELV_RECOIL_M)
            c->m.rosc_parked_tick = c->tick;
        /* keep ventilating at 10/min */
        if (c->tick >= c->rosc_hold_vent_tick) {
            c->breath_end_tick = c->tick + ELV_VENT_INSP_TICKS;
            c->rosc_hold_vent_tick = c->tick + ELV_VENT_CONT_PERIOD_TICKS;
            c->m.breaths++;
        }
        out->vent_on = c->tick < c->breath_end_tick;
        /* re-arrest: trusted signals gone for 2 s -> resume compressions */
        if (!s->pulse_present && s->etco2_mmhg < ELV_ETCO2_ROSC_ABS_MMHG - 10.0) {
            if (c->rosc_lost_since == 0) c->rosc_lost_since = c->tick;
            if (c->tick - c->rosc_lost_since > 2000) {
                c->phase = 0;
                c->next_boundary_tick = c->tick + ELV_CYCLE_TICKS;
                c->etco2_baseline = s->etco2_mmhg;
                logev(c, ELV_EV_REARREST, 0);
                enter(c, ELV_ST_COMPRESS);
            }
        } else {
            c->rosc_lost_since = 0;
        }
        break;

    case ELV_ST_FAULT_RETRACT:
        out->piston_target_m = 0.0;              /* never hold at depth */
        out->piston_vmax_ms = ELV_RETRACT_V_MS;
        out->charge_dump = true;
        c->charging = false;
        /* parked, or sensors dead: setpoint is 0 and the longest retract
           (6 cm at the retract speed) has elapsed with margin */
        if (s->piston_pos_m < ELV_RECOIL_M ||
            c->tick - c->state_enter_tick >
                (uint64_t)((ELV_DEPTH_MAX_M + ELV_COMP_MAX_M) / ELV_RETRACT_V_MS * ELV_TICK_HZ) + 200)
            enter(c, ELV_ST_SAFE_HOLD);
        break;

    case ELV_ST_SAFE_HOLD:
        out->piston_target_m = 0.0;
        out->piston_vmax_ms = ELV_RETRACT_V_MS;
        out->charge_dump = true;
        if (op->stop) enter(c, ELV_ST_STOPPED);
        break;

    case ELV_ST_STOPPED:
        out->piston_target_m = 0.0;
        out->charge_dump = true;
        break;
    }

    /* ACLS drug clock: advisory prompt to the crew, never actuates */
    if (c->epi_due_tick && cpr_required) {
        if (!c->epi_prompt && c->tick >= c->epi_due_tick) {
            c->epi_prompt = true;
            c->m.epi_prompts++;
            logev(c, ELV_EV_EPI_DUE, (int32_t)c->m.epi_prompts);
        }
        if (c->epi_prompt && op->epi_given) {
            c->epi_prompt = false;
            c->m.epi_given++;
            c->epi_due_tick = c->tick + ELV_EPI_INTERVAL_TICKS;
            logev(c, ELV_EV_EPI_GIVEN, (int32_t)c->m.epi_given);
        }
    }

    if (op->stop && c->state != ELV_ST_FAULT_RETRACT &&
        c->state != ELV_ST_SAFE_HOLD && c->state != ELV_ST_IDLE) {
        out->piston_target_m = 0.0;
        out->charge_dump = true;
        out->shock_line = false;
        logev(c, ELV_EV_STOP, 0);
        enter(c, ELV_ST_STOPPED);
    }

    /* last line of defence inside the core itself */
    if (out->piston_target_m > ELV_DEPTH_MAX_M + ELV_COMP_MAX_M)
        out->piston_target_m = ELV_DEPTH_MAX_M + ELV_COMP_MAX_M;
    if (c->state != ELV_ST_SHOCK_WINDOW) out->shock_line = false;
}
