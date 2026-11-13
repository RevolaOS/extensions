/* SPDX-License-Identifier: GPL-3.0-only */

/* The firmware's bitmap bank: Apple's BMaps lie one after another in RAM,
 * word aligned, in catalog order. The catalog (catalog.inc, generated from
 * catalog.json) gives each bitmap's offset in the bank and its stock checksum,
 * and names the ones the extension treats specially. */

#ifndef CATALOG_H
#define CATALOG_H
#include "rvt.h"
#include <stdint.h>

#define BITMAP_WORDS (RVT_CATALOG_MAX / 32)

/* Where a bitmap's bytes are: the palette or pixels start at `start`, the
 * pixels at `pixels`; `colors` is the palette size, 0 without one; `mask` for
 * the coverage formats the system colours while drawing. */
struct layout {
    uint32_t start, pixels, colors, stride, width, height, depth, mask;
};

static inline int same_layout(const struct layout *a, const struct layout *b)
{
    return a->start == b->start && a->pixels == b->pixels && a->colors == b->colors &&
           a->stride == b->stride && a->width == b->width && a->height == b->height &&
           a->depth == b->depth && a->mask == b->mask;
}

uint32_t catalog_count(void);
const uint8_t *catalog_fingerprint(void);
uint8_t *catalog_data(uint32_t index);
uint32_t catalog_size(uint32_t index);
/* The low 16 bits of the stock bytes' CRC-32. */
uint32_t catalog_crc(uint32_t index);
/* The offset of a bitmap's bytes in the firmware file. */
uint32_t catalog_file_offset(uint32_t index);
/* 0 for an index past the catalog or a header that does not describe the
 * bitmap's bytes. */
int catalog_layout(uint32_t index, struct layout *b);
/* The catalog index of the bitmap whose pixel data starts at `pixels`. */
int catalog_from_pixels(const uint8_t *pixels, uint32_t *index);
/* The resident BGRA palette of a paletted bitmap, or NULL. */
uint8_t *catalog_palette(uint32_t index, uint32_t *colors);
/* The resident BGRA rows of an ARGB bitmap, or NULL. */
uint8_t *catalog_argb(uint32_t index, uint32_t *width, uint32_t *height, uint32_t *stride);
/* The opaque RGBA of the centre pixel of a paletted or ARGB bitmap; 0 for
 * masks, RGB565, a bad layout or a clear pixel. */
int catalog_center_pixel(uint32_t index, uint8_t out[4]);

/* Option bar wells, which the keyboard and the option bars draw their labels
 * on: white and black take the look, Now Playing's shuffle selector has its
 * own colours and follows a theme only. */
enum { WELL_WHITE, WELL_BLACK, WELL_NOW_PLAYING, WELLS };
uint32_t catalog_well_center(unsigned well);
/* The Search input field's middle slice. */
uint32_t catalog_field_center(void);
/* Calendar day cells by what they show, then the page the weekday names sit
 * on, which is no cell. */
enum calendar_place {
    CALENDAR_ACTIVE,
    CALENDAR_INACTIVE,
    CALENDAR_HILITE,
    CALENDAR_CELLS,
    CALENDAR_HEADER = CALENDAR_CELLS
};
/* The bitmap of a cell place, below CALENDAR_CELLS. */
uint32_t catalog_calendar_cell(unsigned place);
/* Stock blue art that follows the accent: entry i's index and role; 0 past the end. */
int catalog_accent_art(uint32_t i, uint32_t *index, uint8_t *role);
/* Labels drawn in a fixed colour over art: the entry (below
 * CATALOG_CONTRAST_TEXTS) a view id belongs to, -1 for other views, and the
 * art behind an entry. */
#define CATALOG_CONTRAST_TEXTS 5
int catalog_contrast_text(uint32_t view);
uint32_t catalog_contrast_art(unsigned entry);

static inline int bit_test(const uint32_t *bits, uint32_t index)
{
    return (bits[index / 32] >> (index % 32)) & 1;
}
static inline void bit_set(uint32_t *bits, uint32_t index)
{
    bits[index / 32] |= 1u << (index % 32);
}

#endif
