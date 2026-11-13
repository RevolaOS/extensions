/* SPDX-License-Identifier: GPL-3.0-only */

/* Box filter from a 128 px RGB565 cover to the grid's sizes. */

#ifndef COVER_GRID_SCALE_H
#define COVER_GRID_SCALE_H
#include <stdint.h>

enum { SCALE_SOURCE = 128, SCALE_MAX = 87 };

/* The source pixels each output pixel covers at one output size: the first of
 * 2 or 3 per axis. */
struct scale {
    uint8_t first[SCALE_MAX], span[SCALE_MAX];
};

void scale_init(struct scale *scale, int size);

/* Pixels [x0, x1) of output row `y`, written from `out` onward: each the
 * average of the 2 or 3 by 2 or 3 source pixels it covers, from the source
 * rows `row_bytes` apart. */
void scale_row(uint16_t *out, const struct scale *scale, const uint8_t *pixels,
               uint32_t row_bytes, int y, int x0, int x1);

#endif
