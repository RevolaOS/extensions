/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CROSSFEED_LABELS_H
#define CROSSFEED_LABELS_H

#include "shared.h"

/* The editor's values in wheel order, which is reading order: Mode | Precut,
 * then Direct | Cross | Highs | Cutoff. */
enum { PARAM_MODE, PARAM_PRECUT, PARAM_DIRECT, PARAM_CROSS, PARAM_HF, PARAM_CUTOFF, PARAM_COUNT };

enum { LABEL_BYTES = 9 }; /* "-24.0 dB" */

/* Value `index` of `parameter` as the editor shows it: a constant, or
 * formatted into `out`. */
const char *crossfeed_label(int parameter, int index, char out[LABEL_BYTES]);

#endif
