/*
 * Elvrek: physics plant.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "elv_plant.h"
#include "elv_limits.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define SUBSTEPS   10
#define ACCEL_MAX  ELV_PISTON_AMAX_MS2
#define SERVO_KP   60.0      /* 1/s position loop gain            */

void elv_plant_init(elv_plant_t *p, const elv_patient_cfg_t *cfg, elv_rhythm_t r)
{
    memset(p, 0, sizeof(*p));
    p->cfg = *cfg;
    if (p->cfg.k1 == 0) p->cfg.k1 = 6000.0;
    if (p->cfg.k3 == 0) p->cfg.k3 = 1.5e6;
    if (p->cfg.c  == 0) p->cfg.c  = 150.0;
    p->etco2 = 6.0;
    p->rng = 12345u;
    elv_ecg_init(&p->ecg, r, 777u);
}

static double lc_noise(elv_plant_t *p)
{
    p->rng = p->rng * 1103515245u + 12345u;
    return (((p->rng >> 8) & 0xFFFF) / 32768.0 - 1.0) * 2.0;   /* +/-2 N */
}

void elv_plant_step(elv_plant_t *p, const elv_actuators_t *a)
{
    p->tick++;
    double dt = ELV_TICK_S / SUBSTEPS;
    double vmax = a->piston_vmax_ms > 0 ? a->piston_vmax_ms : ELV_PISTON_VMAX_MS;

    /* Driver: in chest-referenced mode it adds its own estimate of the chest
       motion to the controller's setpoint, every driver tick, so a late
       controller cannot make the depth wrong. */
    double drive_target = a->piston_target_m;
    if (a->chest_ref) {
        double comp = p->drv_s;
        if (comp >  ELV_COMP_MAX_M) comp =  ELV_COMP_MAX_M;
        if (comp < -ELV_COMP_MAX_M) comp = -ELV_COMP_MAX_M;
        drive_target += comp;
        if (drive_target < 0.0) drive_target = 0.0;
    }

    /* Driver force limit, like a motor current limit: it will not push any
       deeper above the limit, whatever the controller asks or however late
       it is. The faster the piston is moving in, the earlier it stops, so
       the stopping distance still ends under the 650 N hard limit. In a
       normal stroke the force peaks at the bottom, where the piston is
       nearly still, so this does not cut normal strokes short. */
    {
        double vin = p->v > 0 ? p->v : 0.0;
        double lim = ELV_FORCE_LIMIT_N - 300.0 * vin;
        if (p->s.force_n >= lim && drive_target > p->x) {
            drive_target = p->x;
            p->driver_limits++;
        }
    }

    /* actuator: velocity-limited, acceleration-limited position servo */
    for (int i = 0; i < SUBSTEPS; i++) {
        double vcmd = SERVO_KP * (drive_target - p->x);
        if (vcmd >  vmax) vcmd =  vmax;
        if (vcmd < -vmax) vcmd = -vmax;
        double dv = vcmd - p->v, dvmax = ACCEL_MAX * dt;
        if (dv >  dvmax) dv =  dvmax;
        if (dv < -dvmax) dv = -dvmax;
        p->v += dv;
        p->x += p->v * dt;
        if (p->x < 0) { p->x = 0; if (p->v < 0) p->v = 0; }
    }

    /* transport disturbance: road vibration + random bumps (m/s^2) */
    p->accel = 0.0;
    if (p->cfg.transport) {
        double t = p->tick * ELV_TICK_S;
        p->accel = 1.2 * sin(2 * M_PI * 3.1 * t) + 0.6 * sin(2 * M_PI * 11.7 * t + 0.4);
        p->rng = p->rng * 1103515245u + 12345u;
        if (p->tick - p->bump_tick > 1500 && ((p->rng >> 16) & 0x3FF) < 2) {
            p->bump_tick = p->tick;
            p->bump_amp = 4.0 + ((p->rng >> 8) & 0xFF) / 64.0;        /* 4-8 m/s^2 */
            if ((p->rng >> 4) & 1) p->bump_amp = -p->bump_amp;
        }
        double tb = (p->tick - p->bump_tick) * ELV_TICK_S;
        if (p->bump_tick && tb < 0.3) p->accel += p->bump_amp * exp(-tb / 0.06) * cos(2 * M_PI * 6.0 * tb);
        /* strap: s'' = -wn^2 s - 2 z wn s' - a */
        double wn = 2 * M_PI * (p->cfg.strap_wn_hz > 0 ? p->cfg.strap_wn_hz : ELV_STRAP_WN_HZ);
        for (int i = 0; i < SUBSTEPS; i++) {
            double sa = -wn * wn * p->surf - 2 * ELV_STRAP_ZETA * wn * p->surf_v - p->accel;
            p->surf_v += sa * dt;
            p->surf += p->surf_v * dt;
        }
    }
    /* the driver's own observer: the nominal strap model run on its IMU
       reading (it does not see the true chest position) */
    {
        double wn = 2.0 * M_PI * ELV_STRAP_WN_HZ;
        for (int i = 0; i < SUBSTEPS; i++) {
            double sa = -wn * wn * p->drv_s - 2.0 * ELV_STRAP_ZETA * wn * p->drv_sv - p->accel;
            p->drv_sv += sa * dt;
            p->drv_s += p->drv_sv * dt;
        }
    }

    /* true chest compression = piston travel minus surface offset */
    double pen = p->x - p->surf;
    double f = 0.0;
    if (pen > 0) {
        f = p->cfg.k1 * pen + p->cfg.k3 * pen * pen * pen + p->cfg.c * (p->v - p->surf_v);
        if (f < 0) f = 0;
    }
    if (f > p->max_force) p->max_force = f;
    if (pen > p->max_depth) p->max_depth = pen;
    if (pen > p->pen_peak) p->pen_peak = pen;
    /* A stroke ends at full recoil measured against the chest (the cup back
       within 3 mm of the chest surface), not against the frame: in
       transport the chest itself moves, and the cup moves with it. */
    if (pen < 0.003 && p->pen_peak > 0.02) {
        p->strokes++;
        if (p->pen_peak >= ELV_DEPTH_MIN_M && p->pen_peak <= ELV_DEPTH_MAX_M) p->strokes_in_range++;
        p->peaks_mm[p->strokes % 64] = (float)(p->pen_peak * 1000.0);
        p->pen_peak = 0;
    }

    /* defibrillator capacitor */
    if (a->charge_dump) p->charge_ticks = 0;
    else if (a->charge_on && p->charge_ticks < ELV_CHARGE_TICKS) p->charge_ticks++;
    bool ready = p->charge_ticks >= ELV_CHARGE_TICKS;

    if (a->shock_line && ready) {
        p->shocks_received++;
        if (p->x >= ELV_RECOIL_M) p->shocks_while_not_parked++;
        p->charge_ticks = 0;
        ready = false;
        if (p->cfg.shock_converts && p->ecg.rhythm == ELV_RHYTHM_SHOCKABLE) {
            p->ecg.rhythm = ELV_RHYTHM_ORGANIZED;
            p->converted_tick = p->tick;
        }
    }

    /* return of circulation */
    if (!p->rosc) {
        if (p->cfg.rosc_at_tick && p->tick >= p->cfg.rosc_at_tick) p->rosc = true;
        if (p->converted_tick && p->tick >= p->converted_tick + p->cfg.rosc_delay_ticks)
            p->rosc = true;
        if (p->rosc && p->ecg.rhythm != ELV_RHYTHM_ORGANIZED)
            p->ecg.rhythm = ELV_RHYTHM_ORGANIZED;
    }

    /* EtCO2: tracks perfusion. CPR quality ~ depth; ROSC -> ~40 mmHg */
    double target;
    if (p->rosc) target = 40.0;
    else {
        double q = p->x > 0.01 ? 1.0 : 0.0;    /* compressions happening */
        target = 4.0 + 12.0 * q;
    }
    double tau = p->rosc ? 4000.0 : 15000.0;
    p->etco2 += (target - p->etco2) / tau;

    /* ECG at 250 Hz */
    uint64_t ecg_seq = p->s.ecg_seq;
    float ecg_mv = p->s.ecg_mv;
    if (p->tick % ELV_ECG_TICKS_PER_SAMPLE == 0) {
        ecg_mv = elv_ecg_next(&p->ecg, p->x);
        ecg_seq++;
    }

    bool dropped = p->cfg.drop_sensors_at && p->tick >= p->cfg.drop_sensors_at;
    if (!dropped) {
        p->s.seq++;
        p->s.sample_n = p->s.seq;
        p->s.piston_pos_m = p->x;
        p->s.force_n = f > 0 ? f + lc_noise(p) : 0.0;
        p->s.recoil_switch = p->x < 0.001;
        p->s.etco2_mmhg = p->etco2;
        p->s.pulse_present = p->cfg.pulse_sensor && p->rosc;
        p->s.ecg_mv = ecg_mv;
        p->s.ecg_seq = ecg_seq;
        p->s.charger_ready = ready;
    }
    p->s.estop = p->cfg.estop_at && p->tick >= p->cfg.estop_at;
    if (!dropped) { p->s.frame_accel_ms2 = p->accel; p->s.chest_offset_m = p->drv_s; }
}
