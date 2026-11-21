/*
 * Elvrek: ECG source. Synthetic rhythms for tests; tools/fetch_ecg.py
 * converts PhysioNet arrest records into the same 250 Hz float stream.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ELV_ECG_H
#define ELV_ECG_H

#include <stdint.h>
#include "elv_proto.h"

typedef struct {
    elv_rhythm_t rhythm;
    uint64_t     n;            /* sample index */
    uint32_t     rng;
    const float *replay;       /* optional recorded stream (mV) */
    uint64_t     replay_len;
    /* compression artifact: the chest lags the piston and adds noise that
       is not tied to it, so the canceller cannot simply copy the piston */
    double       art_hist[8];
    uint32_t     art_head;
    double       art_n1, art_n2;
} elv_ecg_t;

void  elv_ecg_init(elv_ecg_t *e, elv_rhythm_t r, uint32_t seed);
/* artifact_m: current piston depth; adds compression artifact like real CPR */
float elv_ecg_next(elv_ecg_t *e, double artifact_m);

#endif
