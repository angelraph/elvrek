/*
 * Elvrek: independent invariant monitor.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Written separately from the core and checks only what the core outputs
 * and what the hardware reports. On QNX it runs every tick inside elvrekd;
 * a violation forces a controlled retract. The tests use it as the oracle.
 */
#ifndef ELV_INV_H
#define ELV_INV_H

#include "elv_core.h"

enum {
    ELV_INV_SHOCK_GATE = 0,   /* I1 shock only in window, parked, confirmed, pressed */
    ELV_INV_FORCE_DEPTH,      /* I2 force <= ABS, setpoint <= max depth            */
    ELV_INV_WINDOW_LEN,       /* I3 hands-off shock window < 3 s                   */
    ELV_INV_ROSC_STOP,        /* I4 trusted ROSC -> parked within one cycle        */
    ELV_INV_FAULT_RETRACT,    /* I8 fault -> setpoint 0, controlled speed           */
    ELV_INV_BREATH_PHASE,     /* breath onset never on a downstroke                 */
    ELV_INV_COUNT
};

typedef struct {
    uint32_t violations[ELV_INV_COUNT];
    uint64_t first_violation_tick[ELV_INV_COUNT];
    bool     prev_vent;
    uint64_t rosc_tick;
} elv_inv_t;

void elv_inv_init(elv_inv_t *v);
/* Returns true if this tick is clean. */
bool elv_inv_check(elv_inv_t *v, const elv_core_t *c, const elv_sensors_t *s,
                   const elv_actuators_t *a);
uint32_t elv_inv_total(const elv_inv_t *v);
const char *elv_inv_name(int i);

#endif
