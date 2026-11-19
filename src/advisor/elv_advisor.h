/*
 * Elvrek: advisor: a small, boring, UNTRUSTED rhythm classifier.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Runs during compressions on artifact-corrupted ECG. Its output is only a
 * proposal. On QNX it runs as an unprivileged process; the security policy
 * refuses it access to the piston and shock devices.
 */
#ifndef ELV_ADVISOR_H
#define ELV_ADVISOR_H

#include <stdint.h>
#include "elv_proto.h"

#define ELV_ADV_WIN 1000   /* 4 s at 250 Hz */

typedef struct {
    float    buf[ELV_ADV_WIN];
    uint32_t head, count;
    uint64_t seq;
    float    cpr_hz;       /* compression rate, to notch the artifact */
} elv_advisor_t;

void elv_advisor_init(elv_advisor_t *a, float cpr_hz);
void elv_advisor_push(elv_advisor_t *a, float mv);
/* Returns 1 and fills msg when a window is available. */
int  elv_advisor_classify(elv_advisor_t *a, elv_advisor_msg_t *msg);

#endif
