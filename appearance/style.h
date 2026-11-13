/* SPDX-License-Identifier: GPL-3.0-only */

/* The colours in force: the roles of the stock look in light or dark,
 * of a theme pack's style prefix, and of the accent chosen on top of either.
 * Every colour is RGBA bytes, alpha 255. */

#ifndef STYLE_H
#define STYLE_H
#include "rvt.h"
#include <stdint.h>

/* The accent setting, in the order the list shows: Theme keeps the look's own
 * accent colours, then the named accents. */
enum accent { ACCENT_DEFAULT, ACCENT_FIRST, ACCENTS = ACCENT_FIRST + RVT_ACCENTS };
extern const char *const accent_labels[ACCENTS];

/* A role's colour under the current appearance and accent. */
const uint8_t *style_color(enum rvt_role role);
/* A role's colour in the stock light look, which is what the stock screens
 * draw in and the draw hooks recognise. */
const uint8_t *style_stock_color(enum rvt_role role);
/* The style prefix of a theme pack (RVT_STYLE_BYTES) replaces the stock
 * colours; reset returns to them. */
void style_set(const uint8_t *prefix);
void style_reset(void);
int style_from_theme(void);
void style_set_dark(int dark);
int style_dark(void);
void style_set_accent(unsigned accent);
unsigned style_accent(void);
/* Anything but the stock light look with its own accent. */
int style_not_stock(void);
/* RVT_FLAG_* of the pack, 0 for the stock look. */
unsigned style_flags(void);
/* Whether a catalog bitmap is tinted, and the colour of the role it is
 * tinted with; ask only for tinted ones. */
int style_tinted(unsigned asset);
const uint8_t *style_tint_color(unsigned asset);
/* Whether stock accent artwork and the selection bar follow the colours:
 * under a pack always, under the stock look for accents other than Blue. */
int style_accent_art_active(void);
/* Gradient start, gradient end and top line of the selection bar; 0 keeps
 * the stock bar. */
int style_selection_bar(uint8_t out[3][4]);

/* Weighted brightness scaled by 256 (black 0, white 255 * 256), and whether a
 * colour reads as a dark surface. */
static inline int color_brightness(const uint8_t *color)
{
    return 54 * color[0] + 183 * color[1] + 19 * color[2];
}
static inline int color_dark(const uint8_t *color)
{
    return color_brightness(color) < 128 * 256;
}
/* Each channel of `from` moved toward `to` by weight / 255; `out` is opaque. */
void color_mix(uint8_t out[4], const uint8_t *from, const uint8_t *to, unsigned weight);

#endif
