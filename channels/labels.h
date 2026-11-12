/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CHANNELS_LABELS_H
#define CHANNELS_LABELS_H

#include "modes.h"
#include "shared.h"

/* The editor's values in wheel order. */
enum { PARAM_MODE, PARAM_WIDTH, PARAM_COUNT };

enum { LABEL_BYTES = 6 }; /* "250 %" */

/* Value `index` of `parameter` as the editor shows it: a constant, or
 * formatted into `out`. */
const char *channels_label(int parameter, int index, char out[LABEL_BYTES]);

#endif
