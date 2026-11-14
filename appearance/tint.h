/* SPDX-License-Identifier: GPL-3.0-only */

/* Colouring bitmaps in place and picking label colours against artwork. */

#ifndef TINT_H
#define TINT_H
#include <stdint.h>

/* The colour a bitmap view draws its mask in: its tint colour when the bitmap
 * is a tinted catalog bitmap, the icon tint role when it is no catalog bitmap
 * at all (an extension's menu icon) under a theme, the on-accent colour while
 * the view is highlighted, NULL to keep the stock colour. */
const uint8_t *tint_mask_color(const void *view);
/* Around one blit of a bitmap context by a view, or of a calendar day cell's
 * marker: a tinted paletted asset in a highlighted view, or on the selected
 * day, draws with the on-accent colour, put back by the end call. Tinted ARGB
 * art keeps its tint while highlighted. */
void tint_highlight_begin(const void *view, const void *context);
void tint_marker_highlight_begin(const void *cell, const void *context);
void tint_highlight_end(void);
/* Paints a decoded extension picture, a whole BMap of `bytes`, in `color`:
 * the entries of an Indexed8 palette or the covered pixels of BGRA8888 rows,
 * keeping their alpha. 0 for any other format. */
int tint_bmap(void *bmap, uint32_t bytes, const uint8_t *color);
/* Rewrites the palettes of tinted paletted bitmaps and the pixels of tinted
 * ARGB bitmaps with their tint colour, and picks the label colours that
 * contrast with the artwork; after a switch or accent change, once the pack's
 * records are applied. */
void tint_refresh(void);
/* Tints the stock accent artwork with its role, skipping bitmaps set in
 * `skip` (replaced by the pack), and sets each one it touched in `touched`.
 * Expects stock pixels. */
void tint_accent_art(const uint32_t *skip, uint32_t *touched);
/* The colour for labels on an option bar well (catalog.h WELL_*): the text or
 * background role, whichever contrasts more with the well's centre art; NULL
 * keeps the stock colour (no pack, or clear art). */
const uint8_t *tint_well_letter_color(unsigned well);
/* The same for the text typed into the Search field. */
const uint8_t *tint_field_text_color(void);
/* The same for a stock label drawn in a fixed colour over art, by view id. */
const uint8_t *tint_contrast_text_color(uint32_t view);
/* Calendar labels (catalog.h calendar_place): a day of the shown month on the
 * active cell, of another month on the inactive cell (mixed halfway toward
 * it), a weekday name on the page; `out` holds a mixed result. NULL keeps the
 * stock colour. */
const uint8_t *tint_calendar_text_color(unsigned place, uint8_t out[4]);

#endif
