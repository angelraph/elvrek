/*
 * Elvrek: hardware boundary.
 * SPDX-License-Identifier: Apache-2.0
 *
 * The safety core only sees elv_sensors_t in and elv_actuators_t out.
 * The physics plant (src/plant) and a future Pi 5 actuator driver implement
 * the same structs, so the safety code does not change when the piston
 * becomes real.
 */
#ifndef ELV_HW_H
#define ELV_HW_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t seq;            /* increments every hardware sample           */
    double   piston_pos_m;   /* 0 = full recoil, + = into the chest        */
    double   force_n;        /* load cell on the cup                       */
    bool     recoil_switch;  /* limit switch at full recoil                */
    double   etco2_mmhg;     /* capnography on the airway                  */
    bool     pulse_present;  /* trusted pulse input (sensor or clinician)  */
    float    ecg_mv;         /* latest ECG sample                          */
    uint64_t ecg_seq;        /* increments per ECG sample (250 Hz)         */
    bool     charger_ready;  /* defibrillator capacitor full               */
    bool     estop;          /* hardware emergency stop pressed            */
    double   frame_accel_ms2;/* IMU on the frame, vertical (transport)     */
} elv_sensors_t;

typedef struct {
    double piston_target_m;  /* position setpoint, clamped by the core     */
    double piston_vmax_ms;   /* velocity ceiling for this tick             */
    bool   vent_on;          /* bag squeeze (inspiration)                  */
    bool   charge_on;        /* charge the capacitor                       */
    bool   charge_dump;      /* dump stored energy internally              */
    bool   shock_line;       /* closes the shock relay (bench: a lamp)     */
} elv_actuators_t;

#endif
