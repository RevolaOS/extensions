/* SPDX-License-Identifier: GPL-3.0-only */

/* What the two modules of Channels agree on. */

#ifndef CHANNELS_SHARED_H
#define CHANNELS_SHARED_H

#include "channels.h"

enum {
    KEY_MODE = 1, /* int: CHANNELS_STEREO to CHANNELS_CUSTOM */
    KEY_WIDTH,    /* int: indexes into labels.c; 5 % per step */
};

enum {
    WIDTH_LABELS = 51,
    WIDTH_DEFAULT = 20, /* 100 % */
    MODE_DEFAULT = CHANNELS_STEREO,
};

static inline unsigned int width_percent(int index) { return 5 * (unsigned int)index; }

#endif
