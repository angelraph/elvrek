/*
 * Elvrek: compression artifact canceller.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Chest compressions put a large artifact on the ECG at the compression rate
 * and its harmonics. Elvrek drives the piston, so it knows the compression
 * phase exactly on every tick. The canceller models the artifact as a sum of
 * harmonics of that phase and tracks their amplitudes with a normalised LMS
 * filter, then subtracts the estimate. This is the reference-signal approach
 * from the CPR artifact literature (Irusta et al., IEEE Trans Biomed Eng,
 * 2009), with the reference taken from the actuator instead of estimated.
 *
 * Deterministic, no allocation, runs inside the safety process.
 */
#ifndef ELV_CANCEL_H
#define ELV_CANCEL_H
#include <stdbool.h>
#include <stdint.h>

#define ELV_CANCEL_HARMONICS 6

typedef struct {
    float w[2 * ELV_CANCEL_HARMONICS];   /* cos and sin weight per harmonic */
    float mu;                            /* normalised step size            */
    double art_pow, out_pow;             /* running powers, for confidence  */
} elv_cancel_t;

void  elv_cancel_init(elv_cancel_t *c, float mu);
/* One ECG sample. phase is the compression phase in [0, 1) at the moment of
   the sample; compressing says whether the piston is cycling. Returns the
   ECG with the estimated artifact removed. */
float elv_cancel_step(elv_cancel_t *c, float ecg_mv, double phase, bool compressing);
/* Estimated artifact power over residual power (a quality hint). */
float elv_cancel_ratio(const elv_cancel_t *c);

#endif
