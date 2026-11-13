/* SPDX-License-Identifier: GPL-3.0-only */

#include "tint.h"
#include "catalog.h"
#include "osos-2.0.5.h"
#include "pixel.h"
#include "rv_api.h"
#include "rv_libc.h"
#include "style.h"
#include <stddef.h>

/* One past the index of the bitmap whose palette holds the on-accent colour
 * between the two halves of one blit; 0 when none. */
static uint32_t highlighted;

/* The label colours that contrast with the artwork, picked once per switch:
 * the wells, the Search field, the contrast texts, then the calendar's
 * active cell, inactive cell and page; alpha 0 where the stock colour stays. */
enum {
    CONTRAST_WELL,
    CONTRAST_FIELD = WELLS,
    CONTRAST_TEXT,
    CONTRAST_CALENDAR = CONTRAST_TEXT + CATALOG_CONTRAST_TEXTS,
    CONTRASTS = CONTRAST_CALENDAR + 3
};
static uint8_t contrast[CONTRASTS][4];

typedef void (*pixel_fn)(uint8_t *bgra, const uint8_t *color);

/* Palette entries and ARGB pixels are BGRA; coverage stays in the alpha byte.
 * Every palette entry is visited; clear ARGB pixels keep their bytes so stock
 * art still compares exactly. */
static void each_entry(uint8_t *entry, uint32_t colors, pixel_fn fn, const uint8_t *color)
{
    for (; colors--; entry += 4)
        fn(entry, color);
}

static void each_pixel(uint32_t index, pixel_fn fn, const uint8_t *color)
{
    uint32_t colors, width, height, stride;
    uint8_t *entry = catalog_palette(index, &colors);
    if (entry) {
        each_entry(entry, colors, fn, color);
        return;
    }
    uint8_t *row = catalog_argb(index, &width, &height, &stride);
    if (!row)
        return;
    for (; height--; row += stride)
        for (uint8_t *p = row, *end = row + width * 4; p < end; p += 4)
            if (p[3])
                fn(p, color);
}

static void paint(uint8_t *bgra, const uint8_t *color)
{
    bgra[0] = color[2];
    bgra[1] = color[1];
    bgra[2] = color[0];
}

enum {
    GRAY_CHROMA = 24, /* below this (max minus min channel) a pixel is grey: letters, gloss, shadows */
    FULL_CHROMA = 96, /* from here on a pixel is fully recoloured; between, blended */
    BRIGHTNESS_MAX = 255 * 256,
    /* Stock accent art is shaded around the stock accent fill, (35, 108, 197). */
    STOCK_ACCENT_BRIGHTNESS = 54 * 35 + 183 * 108 + 19 * 197,
};

/* Moves `color` toward white or black by how much brighter or darker the stock
 * pixel is than the stock blue, so that blue becomes `color` exactly and the
 * art keeps its gradients. */
static void colorize(uint8_t *bgra, const uint8_t *color)
{
    static const uint8_t white[4] = {255, 255, 255, 255}, black[4] = {0, 0, 0, 255};
    uint8_t stock[4] = {bgra[2], bgra[1], bgra[0], 255}, shaded[4];
    int high = stock[0], low = stock[0];
    for (unsigned i = 1; i < 3; i++) {
        high = stock[i] > high ? stock[i] : high;
        low = stock[i] < low ? stock[i] : low;
    }
    int chroma = high - low;
    if (chroma < GRAY_CHROMA)
        return;
    int brightness = color_brightness(stock);
    if (brightness >= STOCK_ACCENT_BRIGHTNESS)
        color_mix(shaded, color, white,
                  divide((unsigned)(brightness - STOCK_ACCENT_BRIGHTNESS) * 255,
                         reciprocal(BRIGHTNESS_MAX - STOCK_ACCENT_BRIGHTNESS)));
    else
        color_mix(shaded, color, black,
                  divide((unsigned)(STOCK_ACCENT_BRIGHTNESS - brightness) * 255,
                         reciprocal(STOCK_ACCENT_BRIGHTNESS)));
    unsigned weight = chroma >= FULL_CHROMA ? 255
                                            : divide((unsigned)(chroma - GRAY_CHROMA) * 255,
                                                     reciprocal(FULL_CHROMA - GRAY_CHROMA));
    color_mix(shaded, stock, shaded, weight);
    paint(bgra, shaded);
}

/* The pixel data of a bitmap context's bitmap, or NULL. */
static const uint8_t *context_pixels(const void *context)
{
    const uint8_t *bitmap = context ? RV_FIELD(context, GraphicsContext_bitmap, const uint8_t *) : NULL;
    return bitmap ? RV_FIELD(bitmap, Bitmap_pixels, const uint8_t *) : NULL;
}

const uint8_t *tint_mask_color(const void *view)
{
    uint32_t index;
    const uint8_t *pixels = context_pixels(RV_FIELD(view, BitmapView_context, const void *));
    const uint8_t *color = NULL;
    if (!pixels || !style_from_theme())
        return NULL;
    if (!catalog_from_pixels(pixels, &index))
        color = style_color(RVT_ROLE_ICON_TINT);
    else if (style_tinted(index))
        color = style_tint_color(index);
    if (color && (RV_FIELD(view, View_flags, uint32_t) & ViewFlags_Highlighted))
        return style_color(RVT_ROLE_ON_ACCENT);
    return color;
}

/* A tinted paletted bitmap: its palette takes the on-accent colour until the
 * end call. ARGB art would need every pixel rewritten twice per blit. A blit
 * inside another's two halves leaves the first one's bitmap the highlighted
 * one. */
static void highlight(const void *context)
{
    uint32_t index, colors;
    uint8_t *entry;
    const uint8_t *pixels = context_pixels(context);
    if (highlighted || !pixels || !catalog_from_pixels(pixels, &index) || !style_tinted(index) ||
        !(entry = catalog_palette(index, &colors)))
        return;
    each_entry(entry, colors, paint, style_color(RVT_ROLE_ON_ACCENT));
    highlighted = index + 1;
}

void tint_highlight_begin(const void *view, const void *context)
{
    if (style_from_theme() && (RV_FIELD(view, View_flags, uint32_t) & ViewFlags_Highlighted))
        highlight(context);
}

void tint_marker_highlight_begin(const void *cell, const void *context)
{
    uint32_t index;
    const uint8_t *art = context_pixels(RV_FIELD(cell, CalendarDayCell_art, const void *));
    if (style_from_theme() && art && catalog_from_pixels(art, &index) &&
        index == catalog_calendar_cell(CALENDAR_HILITE))
        highlight(context);
}

void tint_highlight_end(void)
{
    if (!highlighted)
        return;
    each_pixel(highlighted - 1, paint, style_tint_color(highlighted - 1));
    highlighted = 0;
}

/* The text or background role, whichever differs more in brightness from `art`. */
static const uint8_t *contrast_with(const uint8_t *art)
{
    const uint8_t *text = style_color(RVT_ROLE_TEXT), *background = style_color(RVT_ROLE_BACKGROUND);
    int reference = color_brightness(art);
    int text_gap = color_brightness(text) - reference;
    int background_gap = color_brightness(background) - reference;
    if (text_gap < 0)
        text_gap = -text_gap;
    if (background_gap < 0)
        background_gap = -background_gap;
    return text_gap >= background_gap ? text : background;
}

/* Entry `slot` of `contrast`: the role against the centre pixel of `index`,
 * or nothing when the art is clear. */
static void pick_contrast(unsigned slot, uint32_t index)
{
    uint8_t art[4];
    if (catalog_center_pixel(index, art))
        memcpy(contrast[slot], contrast_with(art), 4);
    else
        memset(contrast[slot], 0, 4);
}

static void pick_contrasts(void)
{
    uint8_t art[4];
    const uint8_t *background = style_color(RVT_ROLE_BACKGROUND);
    for (unsigned well = 0; well < WELLS; well++)
        pick_contrast(CONTRAST_WELL + well, catalog_well_center(well));
    pick_contrast(CONTRAST_FIELD, catalog_field_center());
    for (unsigned entry = 0; entry < CATALOG_CONTRAST_TEXTS; entry++)
        pick_contrast(CONTRAST_TEXT + entry, catalog_contrast_art(entry));
    /* A clear cell counts as the page: the role that contrasts with the
     * background, mixed halfway toward it for days of other months. */
    for (unsigned place = CALENDAR_ACTIVE; place <= CALENDAR_INACTIVE; place++) {
        const uint8_t *behind =
            catalog_center_pixel(catalog_calendar_cell(place), art) ? art : background;
        const uint8_t *color = contrast_with(behind);
        if (place == CALENDAR_ACTIVE)
            memcpy(contrast[CONTRAST_CALENDAR + place], color, 4);
        else
            color_mix(contrast[CONTRAST_CALENDAR + place], color, behind, 128);
    }
    color_mix(contrast[CONTRAST_CALENDAR + 2], contrast_with(background), background, 128);
}

void tint_refresh(void)
{
    if (!style_from_theme())
        return;
    for (uint32_t i = 0; i < RVT_CATALOG_MAX; i++)
        if (style_tinted(i))
            each_pixel(i, paint, style_tint_color(i));
    pick_contrasts();
}

void tint_accent_art(const uint32_t *skip, uint32_t *touched)
{
    uint32_t index;
    uint8_t role;
    if (!style_accent_art_active())
        return;
    for (uint32_t i = 0; catalog_accent_art(i, &index, &role); i++) {
        if (index >= RVT_CATALOG_MAX || bit_test(skip, index))
            continue;
        each_pixel(index, colorize, style_color((enum rvt_role)role));
        bit_set(touched, index);
    }
}

static const uint8_t *picked(unsigned slot)
{
    return style_from_theme() && contrast[slot][3] ? contrast[slot] : NULL;
}

const uint8_t *tint_well_letter_color(unsigned well)
{
    return well < WELLS ? picked(CONTRAST_WELL + well) : NULL;
}

const uint8_t *tint_field_text_color(void)
{
    return picked(CONTRAST_FIELD);
}

const uint8_t *tint_contrast_text_color(uint32_t view)
{
    int entry = style_from_theme() ? catalog_contrast_text(view) : -1;
    return entry < 0 ? NULL : picked(CONTRAST_TEXT + (unsigned)entry);
}

const uint8_t *tint_calendar_text_color(unsigned place, uint8_t out[4])
{
    unsigned slot = place == CALENDAR_HEADER ? 2 : place;
    if (place == CALENDAR_HILITE || place > CALENDAR_HEADER || !picked(CONTRAST_CALENDAR + slot))
        return NULL;
    if (place == CALENDAR_ACTIVE)
        return contrast[CONTRAST_CALENDAR];
    memcpy(out, contrast[CONTRAST_CALENDAR + slot], 4);
    return out;
}
