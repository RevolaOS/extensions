/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef COMPRESSOR_LABELS_H
#define COMPRESSOR_LABELS_H

#include "shared.h"

/* The editor's values in wheel order, which is reading order: Threshold |
 * Ratio | Knee, then Makeup | Attack | Release. */
enum {
    PARAM_THRESHOLD,
    PARAM_RATIO,
    PARAM_KNEE,
    PARAM_MAKEUP,
    PARAM_ATTACK,
    PARAM_RELEASE,
    PARAM_COUNT
};

enum { LABEL_BYTES = 8 }; /* "1000 ms" */

/* Value `index` of `parameter` as the editor shows it: a constant, or
 * formatted into `out`. */
const char *compressor_label(int parameter, int index, char out[LABEL_BYTES]);

#endif
