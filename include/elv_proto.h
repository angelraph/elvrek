/*
 * Elvrek: messages from untrusted and operator sources into the core.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ELV_PROTO_H
#define ELV_PROTO_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ELV_RHYTHM_UNKNOWN = 0,
    ELV_RHYTHM_SHOCKABLE,      /* VF / pulseless VT                       */
    ELV_RHYTHM_ORGANIZED,      /* organised rhythm (may or may not perfuse) */
    ELV_RHYTHM_ASYSTOLE
} elv_rhythm_t;

/*
 * What the advisor (the untrusted classifier) may say. It has no field that
 * can move the piston or close the shock line. Extra fields an attacker
 * might send are ignored by construction.
 */
typedef struct {
    uint32_t     magic;            /* ELV_ADVISOR_MAGIC                 */
    uint64_t     seq;
    elv_rhythm_t rhythm;
    float        confidence;       /* 0..1                              */
    float        rosc_likelihood;  /* 0..1, advisory only               */
    /* Fields below are what a misbehaving model might *ask* for. The core
       records the request for the audit log and never acts on it. */
    bool         demand_shock;
    bool         demand_stop;
    float        demand_depth_m;
} elv_advisor_msg_t;

#define ELV_ADVISOR_MAGIC 0x414E5641u /* "ANVA" */

typedef struct {
    bool start;          /* begin CPR (operator confirms cup placement)   */
    bool stop;           /* operator stop                                  */
    bool shock_press;    /* operator presses SHOCK. During charging this is a
                            pre-authorisation: the shock is delivered the
                            moment the trusted check confirms, with no
                            further human step in the hands-off window.   */
    bool mode_30_2;      /* true: 30:2, false: continuous + async breaths  */
    bool transport_comp; /* use the frame IMU to hold depth in transport   */
    bool epi_given;      /* clinician confirms epinephrine was given       */
} elv_operator_t;

#endif
