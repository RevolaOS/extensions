/* SPDX-License-Identifier: GPL-3.0-only */

/* Derived art: a bitmap computed in place from the stock bytes it holds,
 * which pack_apply has made sure of first. Every function takes the bitmap's
 * layout from catalog_layout and returns 0 when the record does not fit the
 * bitmap, before writing anything. */

#ifndef DERIVE_H
#define DERIVE_H
#include "catalog.h"
#include <stdint.h>

/* `table` holds `count` pairs {from RGBA, to RGBA} ascending by `from` as a
 * little-endian word; matching palette entries, ARGB pixels or RGB565 words
 * take their `to`. */
int derive_recolor(uint32_t asset, const struct layout *b, const uint8_t *table, uint32_t count);
/* RGB inverted, alpha kept; not for masks. */
int derive_invert(uint32_t asset, const struct layout *b);
/* The relight rule of rvt.h over a paletted bitmap; `scratch` is
 * RVT_SCRATCH_BYTES. */
int derive_relight(uint32_t asset, const struct layout *b, const int32_t *rule, uint8_t *scratch);
/* The source's pixels as it holds them now: a byte copy for an equal layout,
 * else re-encoded, for which the bitmap is at most RVT_SCRATCH_PIXELS; the
 * same size, neither a mask, and a paletted bitmap needs room for the
 * source's colours. */
int derive_alias(uint32_t asset, const struct layout *b, uint32_t source);
/* A tinted paletted or ARGB bitmap without a record: its alpha becomes the
 * coverage tint_refresh paints with, from the stock alpha or, with
 * `darkness`, from alpha x (255 - luma) / 255. */
int derive_coverage(uint32_t asset, const struct layout *b, int darkness);

#endif
