/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CROSSFEED_H
#define CROSSFEED_H

#include <stdint.h>

struct rv_api;

enum { CROSSFEED_OFF, CROSSFEED_SIMPLE, CROSSFEED_CUSTOM };

/* The Custom cross path at one sample rate: the direct gain and the first-order
 * shelf's b0, b1 and negated a1, all Q31. */
struct crossfeed_custom {
    int32_t gain;
    int32_t coefs[3];
};

/* What the settings say: the mode, and the Custom values in Rockbox's units,
 * gains in tenths of a decibel (at most 0) and the cutoff in hertz. */
struct crossfeed_set {
    uint8_t mode;
    int16_t direct_tenths, cross_tenths, hf_tenths;
    uint16_t cutoff_hz;
};

/* numerator / rate as Q31, for numerator < rate <= 65535, in 32-bit steps. */
static inline uint32_t crossfeed_ratio_q31(uint32_t numerator, uint32_t rate)
{
    uint32_t high = (numerator << 15) / rate;
    uint32_t remainder = (numerator << 15) % rate;
    return (high << 16) + (remainder << 16) / rate;
}

/* design.c: the Custom cross path for `rate` from a set's values. */
void crossfeed_design(struct crossfeed_custom *out, unsigned int rate, int direct_tenths,
                      int cross_tenths, int hf_tenths, unsigned int cutoff_hz);

/* crossfeed.c: `publish` leaves a set for the pump task from any task;
 * `process` runs the set that plays on `frames` interleaved stereo S16 frames in
 * place, on the pump task only. */
void crossfeed_start(const struct rv_api *core);
void crossfeed_publish(const struct crossfeed_set *set);
void crossfeed_process(int16_t *samples, uint32_t frames, uint32_t rate);

#endif
