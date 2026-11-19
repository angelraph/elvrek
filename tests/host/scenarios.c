/*
 * Elvrek: host scenario suite.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Runs the real safety core, the invariant monitor and the advisor against
 * the physics plant, faster than real time. Every scenario prints PASS or
 * FAIL with its numbers; the program exits non-zero if any scenario fails.
 * The same core and monitor objects are linked into the QNX safety process.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "elv_core.h"
#include "elv_inv.h"
#include "elv_plant.h"
#include "elv_advisor.h"

typedef enum { ADV_NORMAL, ADV_DEAD, ADV_MALICIOUS, ADV_HANG } adv_mode_t;
#define PRESS_PREAUTH 0u            /* press during charging              */
#define PRESS_NEVER   0xFFFFFFFFu   /* crew never presses                 */

typedef struct {
    const char *name;
    elv_rhythm_t rhythm;
    elv_patient_cfg_t pc;
    adv_mode_t adv;
    uint64_t adv_hang_at;
    bool mode_30_2;
    bool transport_comp;
    uint64_t seconds;
    uint32_t press_delay_ticks;   /* operator reaction to SHOCK prompt */
    uint32_t pause_every, pause_len;  /* controller stalls like a busy VM */
} scen_t;

typedef struct {
    elv_core_t core;     /* large: kept static */
    elv_plant_t plant;
    elv_inv_t inv;
    elv_advisor_t adv;
    uint64_t press_since;
    uint64_t epi_prompt_since;
    double max_true_depth, max_force;
    uint32_t parked_shock_violations;
    uint64_t rosc_detect, rosc_parked;
    uint32_t pending_replays;
} run_t;

static run_t R;

static void run(const scen_t *sc)
{
    memset(&R, 0, sizeof(R));
    elv_core_init(&R.core);
    elv_inv_init(&R.inv);
    elv_plant_init(&R.plant, &sc->pc, sc->rhythm);
    elv_advisor_init(&R.adv, ELV_RATE_DEFAULT_CPM / 60.0f);

    elv_actuators_t act; memset(&act, 0, sizeof(act));
    uint64_t last_ecg = 0, adv_seq = 0;
    uint64_t T = sc->seconds * ELV_TICK_HZ;

    for (uint64_t t = 1; t <= T; t++) {
        elv_plant_step(&R.plant, &act);
        const elv_sensors_t *s = &R.plant.s;

        /* advisor (untrusted) */
        elv_advisor_msg_t msg, *pm = NULL;
        if (s->ecg_seq != last_ecg) {
            last_ecg = s->ecg_seq;
            elv_advisor_push(&R.adv, s->ecg_mv);
            if (s->ecg_seq % ELV_ECG_HZ == 0) {
                bool alive = sc->adv == ADV_NORMAL ||
                             sc->adv == ADV_MALICIOUS ||
                             (sc->adv == ADV_HANG && t < sc->adv_hang_at);
                if (alive && elv_advisor_classify(&R.adv, &msg)) {
                    if (sc->adv == ADV_MALICIOUS) {
                        msg.rhythm = R.plant.ecg.rhythm == ELV_RHYTHM_SHOCKABLE
                                   ? ELV_RHYTHM_ORGANIZED : ELV_RHYTHM_SHOCKABLE;
                        msg.confidence = 1.0f;
                        msg.rosc_likelihood = R.plant.rosc ? 0.0f : 1.0f;
                        msg.demand_shock = true;
                        msg.demand_stop = true;
                        msg.demand_depth_m = 0.12f;
                    }
                    msg.seq = ++adv_seq;
                    pm = &msg;
                }
            }
        }

        /* operator: start, press SHOCK when prompted, give epinephrine */
        elv_operator_t op; memset(&op, 0, sizeof(op));
        op.start = t == 1000;
        op.mode_30_2 = sc->mode_30_2;
        op.transport_comp = sc->transport_comp;
        if (sc->press_delay_ticks == PRESS_PREAUTH) {
            /* crew authorises the shock while the charger runs */
            op.shock_press = R.core.state == ELV_ST_COMPRESS && R.core.charging && !R.core.preauth;
        } else if (R.core.state == ELV_ST_SHOCK_WINDOW && R.core.sw_phase == ELV_SW_AWAIT_PRESS) {
            if (!R.press_since) R.press_since = t;
            op.shock_press = sc->press_delay_ticks != PRESS_NEVER &&
                             t - R.press_since >= sc->press_delay_ticks;
        } else R.press_since = 0;
        if (R.core.epi_prompt) {
            if (!R.epi_prompt_since) R.epi_prompt_since = t;
            op.epi_given = t - R.epi_prompt_since >= 30000;
        } else R.epi_prompt_since = 0;

        /* A stalled controller, as on a busy virtual machine: the hardware
           keeps the last setpoint, then the controller replays the ticks it
           missed with the newest sample, exactly as elvrekd does. */
        if (sc->pause_every && t > 2000 && t % sc->pause_every < sc->pause_len) {
            R.pending_replays++;
            continue;
        }
        for (uint32_t k = 0; k < R.pending_replays; k++) {
            static const elv_operator_t idle_op;
            elv_operator_t ok = idle_op;
            ok.mode_30_2 = op.mode_30_2; ok.transport_comp = op.transport_comp;
            elv_sensors_t sk = *s;
            sk.seq = s->seq * 64 + k;
            elv_core_step(&R.core, &sk, NULL, &ok, &act);
            (void)elv_inv_check(&R.inv, &R.core, &sk, &act);
        }
        R.pending_replays = 0;
        elv_core_step(&R.core, s, pm, &op, &act);
        /* violations are counted inside the monitor; elvrekd forces a retract */
        (void)elv_inv_check(&R.inv, &R.core, s, &act);
    }
    R.max_true_depth = R.plant.max_depth;
    R.max_force = R.plant.max_force;
}

static int failures;
static int checks_in_scen;
static bool scen_ok;

static void expect(bool cond, const char *what)
{
    checks_in_scen++;
    if (!cond) { scen_ok = false; printf("      x %s\n", what); }
}

static void common_checks(void)
{
    expect(elv_inv_total(&R.inv) == 0, "0 invariant violations");
    for (int i = 0; i < ELV_INV_COUNT; i++)
        if (R.inv.violations[i])
            printf("        %s: %u (first at tick %llu)\n", elv_inv_name(i),
                   R.inv.violations[i], (unsigned long long)R.inv.first_violation_tick[i]);
    expect(R.max_true_depth <= ELV_DEPTH_MAX_M + 0.001, "true chest depth <= 6.1 cm");
    expect(R.max_force <= ELV_FORCE_ABS_N, "force <= 650 N");
    expect(R.plant.shocks_while_not_parked == 0, "no shock with the cup off recoil");
}

static void report(const scen_t *sc)
{
    const elv_metrics_t *m = &R.core.m;
    double inrange = R.plant.strokes ? 100.0 * R.plant.strokes_in_range / R.plant.strokes : 0;
    printf("%s %-34s CCF %5.1f%%  shocks %u  windows %u  hands-off max %4u ticks "
           "(machine %4u, human %4u)  retries %u  "
           "depth-in-range %5.1f%%  max depth %.1f mm  max force %.0f N  "
           "advisor demands ignored %u  state %s\n",
           scen_ok ? "PASS" : "FAIL", sc->name, 100.0 * elv_core_ccf(&R.core),
           m->shocks, m->shock_windows, m->max_window_ticks, m->max_machine_ticks,
           m->max_human_ticks, m->press_retries, inrange,
           R.max_true_depth * 1000, R.max_force, m->advisor_demands_ignored,
           elv_state_name(R.core.state));
    if (!scen_ok) failures++;
}

#define BEGIN(sc) do { scen_ok = true; checks_in_scen = 0; run(&(sc)); common_checks(); } while (0)

static double inrange_pct(void)
{
    return R.plant.strokes ? 100.0 * R.plant.strokes_in_range / R.plant.strokes : 0;
}

int main(void)
{
    printf("Elvrek host scenario suite (physics plant, not a patient)\n\n");

    /* 1. VF, one shock converts, perfusion returns 20 s later (EtCO2 only) */
    scen_t s1 = { "vf_shock_rosc", ELV_RHYTHM_SHOCKABLE,
                  { .shock_converts = true, .rosc_delay_ticks = 20000 },
                  ADV_NORMAL, 0, false, false, 200, 300 };
    BEGIN(s1);
    expect(R.core.m.shocks == 1, "exactly 1 shock");
    expect(R.core.m.max_window_ticks < ELV_SHOCK_WIN_TARGET_TICKS, "total hands-off < 3 s");
    expect(R.core.m.max_machine_ticks < ELV_SHOCK_WIN_STRETCH_TICKS, "machine time < 1.5 s");
    expect(R.core.m.max_human_ticks >= 300 && R.core.m.max_human_ticks <= 301, "human time reported separately (scripted 300 ms)");
    expect(R.core.state == ELV_ST_ROSC_HOLD, "ends parked in ROSC_HOLD");
    expect(R.core.m.rosc_parked_tick &&
           R.core.m.rosc_parked_tick - R.core.m.rosc_detect_tick <= R.core.period_ticks,
           "parked within one compression cycle of trusted ROSC");
    expect(elv_core_ccf(&R.core) >= 0.90, "CCF >= 90%");
    report(&s1);

    /* 1b. Shock pre-authorised during charging: no human step in the window */
    scen_t s1b = s1; s1b.name = "vf_preauthorised_shock"; s1b.press_delay_ticks = PRESS_PREAUTH;
    BEGIN(s1b);
    expect(R.core.m.shocks == 1, "exactly 1 shock");
    expect(R.core.m.preauth_shocks == 1, "delivered on pre-authorisation");
    expect(R.core.m.max_human_ticks == 0, "human time 0");
    expect(R.core.m.max_machine_ticks < ELV_SHOCK_WIN_STRETCH_TICKS, "machine time < 1.5 s");
    report(&s1b);

    /* 1c. Slow crew: press 1.8 s after the prompt, still inside the window */
    scen_t s1c = s1; s1c.name = "vf_slow_press_1800ms"; s1c.press_delay_ticks = 1800;
    BEGIN(s1c);
    expect(R.core.m.shocks == 1, "shock delivered");
    expect(R.core.m.max_human_ticks >= 1800 && R.core.m.max_human_ticks <= 1801, "human time 1.8 s, reported separately");
    expect(R.core.m.max_window_ticks < ELV_SHOCK_WIN_TARGET_TICKS, "total hands-off < 3 s");
    report(&s1c);

    /* 1d. Crew never presses: the window closes at 3 s, compressions resume,
           the charge is kept and re-offered, then dumped after 3 tries */
    scen_t s1d = { "vf_crew_never_presses", ELV_RHYTHM_SHOCKABLE, { 0 },
                   ADV_NORMAL, 0, false, false, 200, PRESS_NEVER };
    BEGIN(s1d);
    expect(R.core.m.shocks == 0, "no shock without a press");
    expect(R.core.m.press_retries == ELV_MAX_RETRIES, "re-offered 3 times");
    expect(R.core.m.max_window_ticks <= ELV_SHOCK_WIN_ABORT_TICKS, "each window <= 3 s");
    expect(elv_core_ccf(&R.core) >= 0.90, "CCF >= 90%");
    report(&s1d);

    /* 2. Same in 30:2 mode */
    scen_t s2 = s1; s2.name = "vf_shock_rosc_30_2"; s2.mode_30_2 = true;
    BEGIN(s2);
    expect(R.core.m.shocks == 1, "exactly 1 shock");
    expect(R.core.m.breaths > 0, "breaths delivered");
    expect(elv_core_ccf(&R.core) >= ELV_CCF_MIN, "CCF >= 80%");
    report(&s2);

    /* 3. Refractory VF for 10 min: repeated shocks, epinephrine clock */
    scen_t s3 = { "refractory_vf_acls_clock", ELV_RHYTHM_SHOCKABLE, { 0 },
                  ADV_NORMAL, 0, false, false, 600, 300 };
    BEGIN(s3);
    expect(R.core.m.shocks >= 4, ">= 4 shocks in 10 min");
    expect(R.core.m.epi_prompts >= 2, "epinephrine prompted after 2nd shock and again");
    expect(elv_core_ccf(&R.core) >= 0.90, "CCF >= 90%");
    report(&s3);

    /* 4. Advisor never runs: pre-charge is the default, still shocks */
    scen_t s4 = s1; s4.name = "advisor_dead"; s4.adv = ADV_DEAD;
    BEGIN(s4);
    expect(R.core.m.shocks == 1, "shock delivered without the advisor");
    report(&s4);

    /* 5. Advisor hangs at 30 s */
    scen_t s5 = s1; s5.name = "advisor_hang_30s"; s5.adv = ADV_HANG; s5.adv_hang_at = 30000;
    BEGIN(s5);
    expect(R.core.m.shocks == 1, "shock delivered after advisor hang");
    expect(elv_core_ccf(&R.core) >= 0.90, "compressions continue");
    report(&s5);

    /* 6. Malicious advisor: lies about rhythm and ROSC, demands shock/stop/12 cm */
    scen_t s6 = { "advisor_malicious", ELV_RHYTHM_SHOCKABLE, { 0 },
                  ADV_MALICIOUS, 0, false, false, 300, 300 };
    BEGIN(s6);
    expect(R.core.m.advisor_demands_ignored > 100, "demands recorded and ignored");
    expect(R.core.m.advisor_skips <= R.core.m.shocks + 1, "advisor delays at most one boundary at a time");
    expect(R.core.m.shocks >= 1, "shock still delivered by the 2nd boundary");
    expect(R.core.state == ELV_ST_COMPRESS, "false ROSC claim did not stop compressions");
    report(&s6);

    /* 7. True ROSC (pulse input) while the advisor insists on VF */
    scen_t s7 = { "true_rosc_advisor_denies", ELV_RHYTHM_SHOCKABLE,
                  { .rosc_at_tick = 60000, .pulse_sensor = true },
                  ADV_MALICIOUS, 0, false, false, 90, 300 };
    BEGIN(s7);
    expect(R.core.state == ELV_ST_ROSC_HOLD, "stopped on trusted ROSC");
    expect(R.core.m.rosc_parked_tick &&
           R.core.m.rosc_parked_tick - R.core.m.rosc_detect_tick <= R.core.period_ticks,
           "parked within one cycle");
    expect(R.core.m.shocks == 0, "no shock");
    report(&s7);

    /* 8. Stiff chest: force ceiling holds, depth adapts */
    scen_t s8 = { "stiff_chest", ELV_RHYTHM_ASYSTOLE,
                  { .k1 = 11000, .k3 = 2.8e6 }, ADV_NORMAL, 0, false, false, 60, 300 };
    BEGIN(s8);
    expect(R.core.m.force_limit_events > 0, "force ceiling engaged");
    report(&s8);

    /* 9. Sensor dropout at 30 s: controlled retract, hold */
    scen_t s9 = { "sensor_dropout", ELV_RHYTHM_SHOCKABLE,
                  { .drop_sensors_at = 30000 }, ADV_NORMAL, 0, false, false, 40, 300 };
    BEGIN(s9);
    expect(R.core.state == ELV_ST_SAFE_HOLD, "SAFE_HOLD");
    expect(R.core.fault == ELV_FAULT_SENSOR_STALE, "fault SENSOR_STALE");
    expect(R.plant.x < ELV_RECOIL_M, "piston at recoil");
    report(&s9);

    /* 10. E-stop mid-downstroke */
    scen_t s10 = { "estop_mid_stroke", ELV_RHYTHM_SHOCKABLE,
                   { .estop_at = 30130 }, ADV_NORMAL, 0, false, false, 40, 300 };
    BEGIN(s10);
    expect(R.core.state == ELV_ST_SAFE_HOLD, "SAFE_HOLD");
    expect(R.core.fault == ELV_FAULT_ESTOP, "fault ESTOP");
    expect(R.plant.x < ELV_RECOIL_M, "piston at recoil");
    report(&s10);

    /* 11. Asystole: never shock */
    scen_t s11 = { "asystole_no_shock", ELV_RHYTHM_ASYSTOLE, { 0 },
                   ADV_NORMAL, 0, false, false, 300, 300 };
    BEGIN(s11);
    expect(R.core.m.shocks == 0, "no shock");
    expect(elv_core_ccf(&R.core) >= 0.95, "CCF >= 95%");
    report(&s11);

    /* 12. PEA (organised rhythm, no pulse): no shock, compressions continue */
    scen_t s12 = { "pea_no_shock", ELV_RHYTHM_ORGANIZED, { 0 },
                   ADV_NORMAL, 0, false, false, 300, 300 };
    BEGIN(s12);
    expect(R.core.m.shocks == 0, "no shock");
    expect(R.core.state == ELV_ST_COMPRESS, "still compressing");
    expect(elv_core_ccf(&R.core) >= 0.95, "CCF >= 95%");
    report(&s12);

    /* 13. Transport: compensation off vs on, and with a mismatched strap */
    scen_t s13 = { "transport_no_comp", ELV_RHYTHM_ASYSTOLE, { .transport = true },
                   ADV_NORMAL, 0, false, false, 120, 300 };
    BEGIN(s13);
    double off = inrange_pct();
    report(&s13);
    scen_t s14 = s13; s14.name = "transport_imu_comp"; s14.transport_comp = true;
    BEGIN(s14);
    double on = inrange_pct();
    expect(on >= 90.0, "depth in 5-6 cm on >= 90% of strokes");
    expect(on >= off + 10.0, "compensation beats no compensation by >= 10 points");
    report(&s14);
    scen_t s15 = s14; s15.name = "transport_comp_strap_mismatch";
    s15.pc.strap_wn_hz = 4.6;
    BEGIN(s15);
    expect(inrange_pct() >= 80.0, "depth in range >= 80% with a 15% model mismatch");
    report(&s15);

    /* 14. Transport on a controller that stalls 40 ms every 0.7 s, then
       catches up, as the QEMU target does on a busy host */
    scen_t s16 = s14; s16.name = "transport_controller_stalls";
    s16.pause_every = 700; s16.pause_len = 40;
    BEGIN(s16);
    expect(inrange_pct() >= 90.0, "depth in 5-6 cm on >= 90% of strokes despite stalls");
    report(&s16);

    /* 15. Stiff chest with the same stalls: the force limit still holds */
    scen_t s17 = s8; s17.name = "stiff_chest_controller_stalls";
    s17.pause_every = 700; s17.pause_len = 40;
    BEGIN(s17);
    expect(R.max_force < ELV_FORCE_ABS_N, "force under the 650 N hard limit despite stalls");
    report(&s17);

    printf("\n%s: %d scenario(s) failed\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
