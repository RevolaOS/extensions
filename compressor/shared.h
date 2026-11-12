/* SPDX-License-Identifier: GPL-3.0-only */

/* What the two modules of Compressor agree on. */

#ifndef COMPRESSOR_SHARED_H
#define COMPRESSOR_SHARED_H

#include "compressor.h"

enum {
    KEY_THRESHOLD = 1, /* int: indexes into labels.c and the helpers below; 0 is Off */
    KEY_MAKEUP,        /* int: 0 Off, 1 Auto */
    KEY_RATIO,         /* int: RATIO_2 to RATIO_LIMIT */
    KEY_KNEE,          /* int: 0 Hard, 1 Soft */
    KEY_ATTACK,
    KEY_RELEASE,
};

/* Each value is an index; these turn it into Rockbox's units. */
enum {
    THRESHOLD_LABELS = 9,
    MAKEUP_LABELS = 2,
    RATIO_LABELS = RATIO_COUNT,
    KNEE_LABELS = 2,
    ATTACK_LABELS = 7,
    RELEASE_LABELS = 10,
    THRESHOLD_DEFAULT = 0, /* Off */
    MAKEUP_DEFAULT = 1,    /* Auto */
    RATIO_DEFAULT = RATIO_4,
    KNEE_DEFAULT = 1,      /* Soft */
    ATTACK_DEFAULT = 1,    /* 5 ms */
    RELEASE_DEFAULT = 4,   /* 500 ms */
};

static inline int threshold_db(int index) { return -3 * index; }
static inline unsigned int attack_ms(int index) { return 5 * (unsigned int)index; }
static inline unsigned int release_ms(int index) { return 100 + 100 * (unsigned int)index; }

#endif
