/*
 * Elvrek: the same arrest, run two ways.
 * SPDX-License-Identifier: Apache-2.0
 *
 *   elvrek        one controller owns compressions, the rhythm check, the
 *                 charger and the shock line, on one clock
 *   aed           the conventional setup: a mechanical CPR device and a
 *                 separate AED, each with its own clock. At each AED prompt
 *                 the crew pauses the device, the AED analyses, then charges,
 *                 the crew presses SHOCK, then restarts the device
 *   aed_precharge the best conventional practice: the crew charges the
 *                 defibrillator during compressions before the pause, so only
 *                 the analysis, the press and the hand-offs remain
 *
 * Everything else is shared and identical: the same compression controller
 * (elv_core, with the defibrillator disconnected in the conventional arms),
 * the same physics model and ECG, the same rhythm algorithm and look
 * (elv_trusted_shockable on 0.8 s of hands-off ECG), the same charger (7 s),
 * and the same crew: 1 s to react to a prompt, 0.3 s to press SHOCK.
 * The AED's analysis time is swept, because it differs widely between
 * devices. Physics model, not a patient.
 *
 * Usage: workflow_ab [analysis_seconds ...]     (default 3 5 10)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elv_core.h"
#include "elv_inv.h"
#include "elv_plant.h"

#define CREW_REACT 1000      /* ticks: notice a prompt and act on it        */
#define CREW_PRESS 300       /* ticks: press SHOCK once asked               */

typedef enum { ARM_ELVREK, ARM_AED, ARM_AED_PRECHARGE, ARM_N } arm_t;
static const char *arm_name[ARM_N] = { "Elvrek, one clock", "CPR device + AED", "CPR device + AED, crew pre-charges" };

typedef enum { AED_WAIT, AED_PROMPT, AED_PARKING, AED_ANALYSE, AED_CHARGE, AED_PRESS, AED_SHOCK, AED_RESUME } aed_st_t;

typedef struct {
    const char *name;
    elv_rhythm_t rhythm;
    bool converts;
    uint32_t seconds;
} case_t;

typedef struct {
    unsigned shocks, inv, off_recoil, pauses;
    double first_shock_s, pre_sum, pre_max, post_sum, hands_off_s, ccf, in_range;
    bool rosc;
} out_t;

static elv_core_t core;
static elv_plant_t plant;
static elv_inv_t inv;

static void run(const case_t *k, arm_t arm, uint32_t analysis_ticks, out_t *o)
{
    memset(o, 0, sizeof *o);
    memset(&core, 0, sizeof core);
    elv_core_init(&core);
    core.no_defib = arm != ARM_ELVREK;
    elv_inv_init(&inv);
    elv_patient_cfg_t pc = { .shock_converts = k->converts, .rosc_delay_ticks = 20000 };
    elv_plant_init(&plant, &pc, k->rhythm);

    elv_actuators_t act; memset(&act, 0, sizeof act);
    static float ecg[1024];
    uint32_t ecg_n = 0;
    uint64_t last_ecg = 0, press_since = 0;
    aed_st_t aed = AED_WAIT;
    uint64_t aed_next = 1000 + ELV_CYCLE_TICKS, aed_t = 0;
    bool aed_shockable = false, aed_charge = false;
    uint64_t hands_on = 0, cpr_ticks = 0, pause_start = 0, shock_tick = 0;
    unsigned prev_shocks = 0;
    uint64_t T = (uint64_t)k->seconds * ELV_TICK_HZ;

    for (uint64_t t = 1; t <= T; t++) {
        elv_plant_step(&plant, &act);
        const elv_sensors_t *s = &plant.s;
        if (s->ecg_seq != last_ecg) { last_ecg = s->ecg_seq; ecg[ecg_n++ % 1024] = s->ecg_mv; }

        elv_operator_t op; memset(&op, 0, sizeof op);
        op.start = t == 1000;

        if (arm == ARM_ELVREK) {
            /* the crew answers the device's own prompt, same press time */
            if (core.state == ELV_ST_SHOCK_WINDOW && core.sw_phase == ELV_SW_AWAIT_PRESS) {
                if (!press_since) press_since = t;
                op.shock_press = t - press_since >= CREW_PRESS;
            } else press_since = 0;
        } else if (core.state != ELV_ST_ROSC_HOLD) {
            /* the separate AED and the crew, on the AED's own clock */
            if (arm == ARM_AED_PRECHARGE && aed == AED_WAIT && t + ELV_PRECHARGE_LEAD_TICKS >= aed_next)
                aed_charge = true;                       /* crew charges during compressions */
            switch (aed) {
            case AED_WAIT:    if (t >= aed_next) { aed = AED_PROMPT; aed_t = t; } break;
            case AED_PROMPT:  if (t - aed_t >= CREW_REACT) { op.stop = true; aed = AED_PARKING; } break;
            case AED_PARKING: if (plant.x < ELV_RECOIL_M) { aed = AED_ANALYSE; aed_t = t; ecg_n = 0; } break;
            case AED_ANALYSE:
                if (t - aed_t >= analysis_ticks) {
                    uint32_t n = ecg_n < 200 ? ecg_n : 200;     /* the last 0.8 s, same look */
                    float buf[200];
                    for (uint32_t i = 0; i < n; i++) buf[i] = ecg[(ecg_n - n + i) % 1024];
                    aed_shockable = elv_trusted_shockable(buf, n);
                    if (aed_shockable) { aed_charge = true; aed = AED_CHARGE; }
                    else { aed_charge = false; aed = AED_RESUME; aed_t = t; }
                }
                break;
            case AED_CHARGE:  if (s->charger_ready) { aed = AED_PRESS; aed_t = t; } break;
            case AED_PRESS:   if (t - aed_t >= CREW_PRESS) { aed = AED_SHOCK; aed_t = t; } break;
            case AED_SHOCK:
                if (t - aed_t >= ELV_SHOCK_PULSE_TICKS) { aed_charge = false; aed = AED_RESUME; aed_t = t; }
                break;
            case AED_RESUME:
                if (t - aed_t >= CREW_REACT) { op.start = true; aed = AED_WAIT; aed_next = t + ELV_CYCLE_TICKS; }
                break;
            }
        }

        elv_core_step(&core, s, NULL, &op, &act);
        (void)elv_inv_check(&inv, &core, s, &act);   /* the device's own rules */
        if (arm != ARM_ELVREK) {
            /* the AED's charger and shock line are its own, not the device's */
            act.charge_on = aed_charge && !s->charger_ready;
            act.charge_dump = !aed_charge;
            act.shock_line = aed == AED_SHOCK;
        }

        /* bookkeeping, identical for every arm. A shock reaches the chest at
           the plant step at the top of this tick, so it is noted first: in
           Elvrek's window compressions can resume on that same tick. */
        if (plant.shocks_received != prev_shocks) {
            prev_shocks = plant.shocks_received;
            shock_tick = t;
            if (!o->first_shock_s) o->first_shock_s = (t - 1000) / 1000.0;
        }
        bool started = t >= 1000, rosc = core.state == ELV_ST_ROSC_HOLD;
        if (started && !rosc && !o->rosc) {
            cpr_ticks++;
            if (core.state == ELV_ST_COMPRESS) {
                hands_on++;
                if (pause_start) {
                    double len = (t - pause_start) / 1000.0;
                    o->hands_off_s += len;
                    o->pauses++;
                    if (shock_tick && shock_tick >= pause_start) {
                        double pre = (shock_tick - pause_start) / 1000.0, post = (t - shock_tick) / 1000.0;
                        o->pre_sum += pre; o->post_sum += post;
                        if (pre > o->pre_max) o->pre_max = pre;
                    }
                    pause_start = 0; shock_tick = 0;
                }
            } else if (!pause_start && t > 1001) pause_start = t;
        }
        if (rosc) o->rosc = true;
        if (core.state == ELV_ST_COMPRESS && !pause_start) shock_tick = 0;  /* shock not in a pause */
    }
    o->shocks = plant.shocks_received;
    o->off_recoil = plant.shocks_while_not_parked;
    o->inv = elv_inv_total(&inv);
    o->ccf = cpr_ticks ? 100.0 * hands_on / cpr_ticks : 0;
    o->in_range = plant.strokes ? 100.0 * plant.strokes_in_range / plant.strokes : 0;
}

int main(int argc, char **argv)
{
    static const case_t cases[] = {
        { "VF, first shock converts", ELV_RHYTHM_SHOCKABLE, true, 300 },
        { "VF, refractory (10 min)", ELV_RHYTHM_SHOCKABLE, false, 600 },
        { "Asystole (10 min)", ELV_RHYTHM_ASYSTOLE, false, 600 },
        { "Organised rhythm, no pulse (10 min)", ELV_RHYTHM_ORGANIZED, false, 600 },
    };
    double an[8] = { 3, 5, 10 };
    int na = 3;
    if (argc > 1) { na = 0; for (int i = 1; i < argc && na < 8; i++) an[na++] = atof(argv[i]); }

    printf("Elvrek workflow A/B (physics model, not a patient). Crew: %.1f s to react, %.1f s to press.\n",
           CREW_REACT / 1000.0, CREW_PRESS / 1000.0);
    printf("Pauses are hands-off time while CPR is needed; pre = stop to shock, post = shock to compressions.\n\n");
    int bad = 0;
    for (int a = 0; a < na; a++) {
        printf("== AED analysis %.0f s\n", an[a]);
        printf("%-36s %-36s %6s %8s %8s %8s %8s %9s %6s %6s %4s\n", "case", "workflow", "shocks",
               "1st shk", "pre avg", "pre max", "post avg", "hands-off", "CCF", "depth", "inv");
        for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
            for (int arm = 0; arm < ARM_N; arm++) {
                if (arm == ARM_ELVREK && a > 0) continue;    /* Elvrek has no AED analysis */
                out_t o;
                run(&cases[c], (arm_t)arm, (uint32_t)(an[a] * 1000), &o);
                unsigned ns = o.shocks ? o.shocks : 1;
                printf("%-36s %-36s %6u %7.1fs %7.2fs %7.2fs %7.2fs %8.1fs %5.1f%% %5.1f%% %4u%s\n",
                       cases[c].name, arm_name[arm], o.shocks, o.first_shock_s,
                       o.pre_sum / ns, o.pre_max, o.post_sum / ns, o.hands_off_s, o.ccf, o.in_range,
                       o.inv, o.off_recoil ? "  SHOCK OFF RECOIL" : "");
                if (o.off_recoil || (arm == ARM_ELVREK && o.inv)) bad++;
            }
        }
        printf("\n");
    }
    return bad ? 1 : 0;
}
