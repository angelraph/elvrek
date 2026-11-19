/*
 * Elvrek: independent invariant monitor.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "elv_inv.h"
#include <string.h>

static const char *const names[ELV_INV_COUNT] = {
    "I1 shock gate", "I2 force/depth ceiling", "I3 shock window < 3 s",
    "I4 trusted ROSC stops within one cycle", "I8 fault retract to recoil",
    "breath onset not on downstroke"
};

const char *elv_inv_name(int i) { return names[i]; }

void elv_inv_init(elv_inv_t *v) { memset(v, 0, sizeof(*v)); }

static void viol(elv_inv_t *v, int i, uint64_t tick)
{
    if (v->violations[i]++ == 0) v->first_violation_tick[i] = tick;
}

uint32_t elv_inv_total(const elv_inv_t *v)
{
    uint32_t t = 0;
    for (int i = 0; i < ELV_INV_COUNT; i++) t += v->violations[i];
    return t;
}

bool elv_inv_check(elv_inv_t *v, const elv_core_t *c, const elv_sensors_t *s,
                   const elv_actuators_t *a)
{
    uint32_t before = elv_inv_total(v);
    uint64_t t = c->tick;

    /* I1: shock line closed only when every gate is satisfied */
    if (a->shock_line) {
        bool ok = c->state == ELV_ST_SHOCK_WINDOW &&
                  c->sw_phase == ELV_SW_DELIVER &&
                  c->confirm_shockable && c->press_seen &&
                  s->piston_pos_m < ELV_RECOIL_M && s->recoil_switch &&
                  s->charger_ready && !c->downstroke;
        if (!ok) viol(v, ELV_INV_SHOCK_GATE, t);
    }

    /* I2: force and depth ceilings */
    /* the setpoint may include transport compensation; true chest depth is
       checked against ELV_DEPTH_MAX_M by the plant-side oracle in tests */
    if (s->force_n > ELV_FORCE_ABS_N ||
        a->piston_target_m > ELV_DEPTH_MAX_M + ELV_COMP_MAX_M ||
        s->piston_pos_m > ELV_DEPTH_MAX_M + ELV_COMP_MAX_M + 0.001)
        viol(v, ELV_INV_FORCE_DEPTH, t);

    /* I3: shock window must close inside the target */
    if (c->state == ELV_ST_SHOCK_WINDOW &&
        t - c->window_enter_tick > ELV_SHOCK_WIN_TARGET_TICKS)
        viol(v, ELV_INV_WINDOW_LEN, t);

    /* I4: after trusted ROSC, no new downstroke and parked within one period */
    if (c->rosc_trusted && v->rosc_tick == 0 &&
        (c->state == ELV_ST_ROSC_HOLD))
        v->rosc_tick = t;
    if (c->state != ELV_ST_ROSC_HOLD) v->rosc_tick = 0;
    if (v->rosc_tick) {
        if (a->piston_target_m > 0.0) viol(v, ELV_INV_ROSC_STOP, t);
        if (t - v->rosc_tick > c->period_ticks && s->piston_pos_m >= ELV_RECOIL_M)
            viol(v, ELV_INV_ROSC_STOP, t);
    }

    /* I8: fault -> setpoint 0 at controlled speed, energy dumped */
    if (c->state == ELV_ST_FAULT_RETRACT || c->state == ELV_ST_SAFE_HOLD) {
        if (a->piston_target_m != 0.0 || a->piston_vmax_ms > ELV_RETRACT_V_MS ||
            a->shock_line || a->charge_on)
            viol(v, ELV_INV_FAULT_RETRACT, t);
    }

    /* breath onset never during a downstroke */
    if (a->vent_on && !v->prev_vent && c->downstroke)
        viol(v, ELV_INV_BREATH_PHASE, t);
    v->prev_vent = a->vent_on;

    return elv_inv_total(v) == before;
}
