/*
 * Elvrek: physics plant: linear actuator + viscoelastic chest + sensors.
 * SPDX-License-Identifier: Apache-2.0
 *
 * This is the stand-in for real hardware. It implements the elv_hw.h
 * boundary so the Pi 5 actuator driver can replace it without touching the
 * safety core. It is a model, not a patient.
 */
#ifndef ELV_PLANT_H
#define ELV_PLANT_H

#include "elv_hw.h"
#include "elv_ecg.h"

typedef struct {
    /* chest: F = k1 x + k3 x^3 + c v  (manikin-class stiffness) */
    double k1, k3, c;
    /* scenario */
    bool   shock_converts;         /* a shock turns VF into organised rhythm */
    uint64_t rosc_delay_ticks;     /* after conversion, perfusion returns    */
    uint64_t rosc_at_tick;         /* forced ROSC time (0 = none)            */
    bool   pulse_sensor;           /* trusted pulse input wired               */
    uint64_t drop_sensors_at;      /* sensor dropout (0 = never)             */
    uint64_t estop_at;
    /* transport: vertical vehicle acceleration moves the chest surface
       relative to the cup through the strap/mattress compliance */
    bool     transport;
    double   strap_wn_hz;          /* true strap frequency (0 = nominal)     */
    uint32_t cpm_override;         /* unused placeholder for rate tests      */
} elv_patient_cfg_t;

typedef struct {
    elv_patient_cfg_t cfg;
    uint64_t tick;
    double   x, v;                 /* piston position (m) and velocity        */
    double   etco2;
    bool     rosc;
    uint64_t converted_tick;
    uint32_t charge_ticks;
    uint32_t shocks_received;
    uint32_t shocks_while_not_parked;
    double   max_force;
    double   max_depth;            /* deepest true chest compression (m)     */
    double   surf, surf_v;         /* chest surface offset away from cup     */
    double   accel;                /* current vehicle vertical accel         */
    double   bump_amp; uint64_t bump_tick;
    double   pen_peak;             /* this stroke's deepest compression      */
    uint32_t strokes, strokes_in_range;
    double   max_pen;
    uint32_t rng;
    elv_ecg_t ecg;
    elv_sensors_t s;
} elv_plant_t;

void elv_plant_init(elv_plant_t *p, const elv_patient_cfg_t *cfg, elv_rhythm_t r);
void elv_plant_step(elv_plant_t *p, const elv_actuators_t *a);

#endif
