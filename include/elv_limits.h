/*
 * Elvrek: compile-time safety limits.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Every number the safety authority enforces lives here, so a reviewer can
 * audit them in one file. Units are SI unless the name says otherwise.
 * Adult values follow the AHA 2020 / ERC 2021 CPR guidance. This is a
 * research controller, not a medical device.
 */
#ifndef ELV_LIMITS_H
#define ELV_LIMITS_H

/* Control loop */
#define ELV_TICK_HZ             1000            /* 1 ms control tick          */
#define ELV_TICK_S              (1.0 / ELV_TICK_HZ)

/* Compression (adult) */
#define ELV_RATE_MIN_CPM        100
#define ELV_RATE_MAX_CPM        120
#define ELV_RATE_DEFAULT_CPM    110
#define ELV_DEPTH_MIN_M         0.050           /* 5 cm                       */
#define ELV_DEPTH_MAX_M         0.060           /* 6 cm, hard software stop   */
#define ELV_DEPTH_TARGET_M      0.053
#define ELV_RECOIL_M            0.002           /* cup within 2 mm = recoiled */
#define ELV_DUTY                0.50            /* compression : cycle        */

/* Force on the cup */
#define ELV_FORCE_LIMIT_N       600.0           /* controller stops pushing   */
#define ELV_FORCE_ABS_N         650.0           /* invariant: never exceeded  */

/* Actuator */
#define ELV_PISTON_VMAX_MS      0.60            /* m/s                        */
#define ELV_RETRACT_V_MS        0.25            /* controlled fault retract   */
#define ELV_PISTON_AMAX_MS2     30.0            /* actuator decel capability  */

/* Shock window: the ONLY hands-off state for a shock */
#define ELV_SHOCK_WIN_TARGET_TICKS   3000       /* must stay under 3 s        */
#define ELV_SHOCK_WIN_STRETCH_TICKS  1500       /* stretch goal 1.5 s         */
#define ELV_SHOCK_WIN_ABORT_TICKS    3000       /* resume compressions anyway */
#define ELV_CONFIRM_TICKS            800        /* trusted rhythm look        */
#define ELV_PARK_TIMEOUT_TICKS       400        /* piston must reach recoil   */
#define ELV_SHOCK_PULSE_TICKS        10         /* shock line closed for 10ms */
#define ELV_RETRY_COMPRESSIONS       10         /* late press: resume, re-offer */
#define ELV_MAX_RETRIES              3          /* then dump the charge         */

/* Defibrillator charger (simulated capacitor) */
#define ELV_CHARGE_TICKS        7000            /* typical 5-8 s to full      */
#define ELV_PRECHARGE_LEAD_TICKS (ELV_CHARGE_TICKS + 500)

/* 2-minute CPR cycle, rhythm check at each boundary */
#define ELV_CYCLE_TICKS         120000

/* Ventilation */
#define ELV_VENT_CONT_PERIOD_TICKS 6000         /* 10 breaths/min, async mode */
#define ELV_VENT_INSP_TICKS     1000            /* 1 s inspiration            */
#define ELV_VENT_30_2_GAP_TICKS 2600            /* two breaths, then resume   */

/* Physiologic return of circulation (trusted inputs only) */
#define ELV_ETCO2_ROSC_JUMP_MMHG 10.0           /* abrupt rise over baseline  */
#define ELV_ETCO2_ROSC_ABS_MMHG  35.0
#define ELV_ETCO2_BASELINE_TICKS 20000          /* 20 s rolling baseline      */
#define ELV_ROSC_RECHECK_TICKS   10000

/* Transport: frame/strap model used by the disturbance observer */
#define ELV_STRAP_WN_HZ         4.0             /* nominal strap natural freq */
#define ELV_STRAP_ZETA          0.30
#define ELV_COMP_MAX_M          0.015           /* max surface compensation   */

/* ACLS clock */
#define ELV_EPI_INTERVAL_TICKS  240000          /* epinephrine every 4 min    */

/* Watchdogs */
#define ELV_ADVISOR_STALE_TICKS 2000            /* advisor silent > 2 s       */
#define ELV_SENSOR_STALE_TICKS  5               /* hardware silent > 5 ms     */
/* Emulator mode (elvrekd -e, elv-plant -e) only: a virtual machine on a busy
   host can pause for tens of milliseconds. These wider limits are used only
   when asked for, and every result taken with them is labelled as such. */
#define ELV_EMU_SENSOR_STALE_TICKS 50
#define ELV_EMU_HW_WD_TICKS        100

/* Pass/fail metric */
#define ELV_CCF_MIN             0.80            /* chest-compression fraction */

/* ECG */
#define ELV_ECG_HZ              250
#define ELV_ECG_TICKS_PER_SAMPLE (ELV_TICK_HZ / ELV_ECG_HZ)

#endif
