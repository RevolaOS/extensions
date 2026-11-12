/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef EQUALIZER_BIQUAD_H
#define EQUALIZER_BIQUAD_H
#include "shared.h"

/* One biquad as b0, b1, b2, a1, a2: signed with 24 fractional bits. Returns 0
 * when a value is out of range or the quantized filter is not stable. */
int eq_biquad(int32_t out[5], unsigned int type, unsigned int hz, unsigned int q_tenths,
              int gain_tenths_db, unsigned int precut_tenths_db, unsigned int rate);
/* The response of a quantized cascade in tenths of a decibel at `points` (at
 * least 7) frequencies from 20 Hz to 20 kHz, evenly spaced in octaves. */
void eq_response(const int32_t coefficients[EQ_BANDS][5], unsigned int rate, int16_t *tenths,
                 unsigned int points);
/* Scales b0, b1, b2 by a precut of 0..100 tenths of a decibel. */
void eq_attenuate(int32_t coefficients[3], unsigned int precut_tenths_db);
/* The peak of a quantized cascade of at most EQ_BANDS biquads over 20 Hz to
 * 20 kHz in tenths of a decibel, rounded up. */
int eq_peak(const int32_t (*coefficients)[5], unsigned int count, unsigned int rate);

#endif
