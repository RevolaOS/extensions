/* SPDX-License-Identifier: GPL-3.0-only */

/* What the two modules of Crossfeed agree on. */

#ifndef CROSSFEED_SHARED_H
#define CROSSFEED_SHARED_H

#include "crossfeed.h"

enum {
    KEY_CUSTOM_SHOWN = 1, /* bool: Custom is chosen; shows the Edit Crossfeed row */
    KEY_MODE,             /* int: CROSSFEED_OFF, _SIMPLE or _CUSTOM */
    KEY_DIRECT,           /* int: indexes into labels.c and the helpers below */
    KEY_CROSS,
    KEY_HF,
    KEY_CUTOFF,
    KEY_PRECUT,
};

/* Each Custom value is an index; these turn it into Rockbox's units, tenths of
 * a decibel or hertz. */
enum {
    DIRECT_LABELS = 13,
    CROSS_LABELS = 19,
    HF_LABELS = 37,
    CUTOFF_LABELS = 16,
    PRECUT_LABELS = 14,  /* Auto, then 0.0 to -6.0 dB */
    DIRECT_DEFAULT = 9,  /* -1.5 dB */
    CROSS_DEFAULT = 12,  /* -6.0 dB */
    HF_DEFAULT = 16,     /* -16.0 dB */
    CUTOFF_DEFAULT = 2,  /* 700 Hz */
    PRECUT_DEFAULT = 0,  /* Auto */
};

static inline int direct_tenths(int index) { return -60 + 5 * index; }
static inline int cross_tenths(int index) { return -120 + 5 * index; }
static inline int hf_tenths(int index) { return -240 + 5 * index; }
static inline unsigned int cutoff_hz(int index) { return 500 + 100 * (unsigned int)index; }
static inline int precut_tenths(int index)
{
    return index == 0 ? CROSSFEED_PRECUT_AUTO : -5 * (index - 1);
}

/* response.c: a set's cross path at `rate`, its gain at `points` frequencies
 * from 20 Hz to 20 kHz evenly spaced in octaves, in tenths of a decibel, with
 * the precut applied. Returns the precut applied, in tenths at most 0. */
int crossfeed_response(const struct crossfeed_set *set, unsigned int rate, int16_t *tenths,
                       unsigned int points);

#endif
