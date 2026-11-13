/* SPDX-License-Identifier: GPL-3.0-only */

/* The stock screens draw in fixed colours, the light look's. Each draw passes
 * one of the core's colour sites, where the colour says what is being drawn:
 * light greys are surfaces, dark greys text, the stock blues accents, the
 * white palette entry highlighted text. The handlers map each to its role and
 * let the draw go on. The mapping is in functions of the colour and the
 * view's id, which the hooks wrap. */

#include "render.h"
#include "catalog.h"
#include "ext.h"
#include "flat_cover.h"
#include "osos-2.0.5.h"
#include "pixel.h"
#include "rv_sites.h"
#include "rv_ui.h"
#include "style.h"
#include "tint.h"
#include <stddef.h>

/* Set while a view that keeps its light artwork fills its background, so
 * the fill underneath stays stock too. */
static int stock_fill;

enum {
    SURFACE_LIGHT_MIN = 0xb0, /* greys from here up are surfaces */
    TEXT_DARK_MAX = 0x80,     /* greys to here are text */
    TEXT_WHITE_MIN = 0xf0,
    TEXT_LIGHT_REFERENCE = 0x20, /* the light look's text role; native black lies below it */
};

/* Which of a bar view's own label colours a draw passes. */
enum bar_slot { BAR_OTHER, BAR_LABEL, BAR_SELECTED };

static uint32_t view_id(uint32_t view)
{
    return RV_FIELD(view, View_id, uint32_t);
}

static int same_rgb(const uint8_t *color, const uint8_t *other)
{
    return color[0] == other[0] && color[1] == other[1] && color[2] == other[2];
}

static int grey(const uint8_t *color)
{
    return color[0] == color[1] && color[1] == color[2];
}

static const uint8_t *recolor(const uint8_t *color, const uint8_t *target, uint8_t *out)
{
    out[0] = target[0];
    out[1] = target[1];
    out[2] = target[2];
    out[3] = color[3];
    return out;
}

/* The role offset by the stock shade, so the stock gradations stay. */
static const uint8_t *shade(const uint8_t *color, const uint8_t *target, int offset, uint8_t *out)
{
    for (unsigned i = 0; i < 3; i++)
        out[i] = (uint8_t)clamp(target[i] + offset, 0, 255);
    out[3] = color[3];
    return out;
}

/* Views whose labels keep their stock colour: they sit on their own art. */
static int native_text(uint32_t view)
{
    switch (view) {
    case RES_Alarms_PickDate_Preivew_NumericDay:
    case RES_Settings_DateTime_NumericDay:
    case RES_Settings_DateTime_Preivew_NumericDay:
        return 1;
    case RES_Search_Input_Text:
        return !style_from_theme();
    default:
        return 0;
    }
}

/* A label's colour in view `view`. White text on the stock blue-grey pane
 * and on dark art takes the text role under a custom light background. */
static const uint8_t *map_text(const uint8_t *color, uint32_t view, uint8_t *mapped)
{
    if (native_text(view))
        return color;
    const uint8_t *text = style_color(RVT_ROLE_TEXT);
    const uint8_t *field = view == RES_Search_Input_Text ? tint_field_text_color() : NULL;
    const uint8_t *art = tint_contrast_text_color(view);
    if (art)
        return recolor(color, art, mapped);
    if (field && grey(color) && color[0] <= TEXT_DARK_MAX)
        return recolor(color, field, mapped);
    if (color == Palette_White)
        return recolor(color, style_color(RVT_ROLE_ON_ACCENT), mapped);
    if (same_rgb(color, style_stock_color(RVT_ROLE_ACCENT_FILL)))
        return recolor(color, style_color(RVT_ROLE_ACCENT_FILL), mapped);
    if (same_rgb(color, style_stock_color(RVT_ROLE_ACCENT_TEXT)))
        return recolor(color, style_color(RVT_ROLE_ACCENT_TEXT), mapped);
    if (grey(color) && color[0] <= TEXT_DARK_MAX)
        return shade(color, text,
                     color_dark(text) ? color[0] - TEXT_LIGHT_REFERENCE : -(color[0] / 2), mapped);
    if (grey(color) && color[0] >= TEXT_WHITE_MIN && style_from_theme() &&
        !color_dark(style_color(RVT_ROLE_BACKGROUND)))
        return recolor(color, text, mapped);
    return color;
}

/* A fill's or clear's colour. */
static const uint8_t *map_fill(const uint8_t *color, uint8_t *mapped)
{
    const uint8_t *background = style_color(RVT_ROLE_BACKGROUND);
    if (stock_fill)
        return color;
    if (grey(color) && color[0] >= SURFACE_LIGHT_MIN)
        /* Lighter stock greys become darker surfaces; a dark role inverts that. */
        return shade(color, background,
                     color_dark(background) ? (255 - color[0]) / 4 : color[0] - 255, mapped);
    if (same_rgb(color, style_stock_color(RVT_ROLE_COVER_BORDER)))
        return recolor(color, style_color(RVT_ROLE_COVER_BORDER), mapped);
    if (same_rgb(color, style_stock_color(RVT_ROLE_ACCENT_FILL)))
        return recolor(color, style_color(RVT_ROLE_ACCENT_FILL), mapped);
    return color;
}

/* Views that keep their light artwork in every look, so their background
 * fill must stay stock too. */
static int stock_background(uint32_t view)
{
    switch (view) {
    case RES_NowPlaying_White_Background_View:
    case RES_NowPlaying_Idle_Template:
    case RES_NowPlaying_ChapterArt_BackgroundImage_View:
    case RES_RentalInfo_White_Background_View:
        return 1;
    default:
        return 0;
    }
}

/* Accents alone keep the stock clears and blends. */
static int background_themed(void)
{
    return style_dark() || style_from_theme();
}

/* A bar's label: the highlighted one sits on the selection-tinted thumb, so
 * it takes the on-accent colour; the others follow whatever contrasts with
 * the well art. */
static const uint8_t *map_bar_label(const uint8_t *color, enum bar_slot slot, unsigned well,
                                    uint8_t *mapped)
{
    const uint8_t *derived;
    if (slot == BAR_SELECTED)
        return recolor(color, style_color(RVT_ROLE_ON_ACCENT), mapped);
    if (slot == BAR_LABEL && (derived = tint_well_letter_color(well)))
        return recolor(color, derived, mapped);
    return color;
}

/* A calendar day number: the shown month's in black on the active cell, a
 * neighbouring month's in grey, the selected day's in white. */
static const uint8_t *map_calendar_day(const uint8_t *color, uint8_t *mapped)
{
    const uint8_t *themed;
    uint8_t mixed[4];
    if (color[0] == 255 && grey(color))
        themed = style_from_theme() ? style_color(RVT_ROLE_ON_ACCENT) : NULL;
    else
        themed = tint_calendar_text_color(color[0] ? CALENDAR_INACTIVE : CALENDAR_ACTIVE, mixed);
    return themed ? recolor(color, themed, mapped) : color;
}

/* (context, rgba, view): every label. */
static uint32_t text_color(uint32_t context, uint32_t rgba, uint32_t view, const void *next)
{
    uint8_t mapped[4];
    const uint8_t *color = map_text((const uint8_t *)rgba, view_id(view), mapped);
    return api->hook_next(next, context, (uint32_t)color, view, 0, 0, 0);
}

/* The colour a fill or clear onto `surface` uses. On a coverage mask the
 * colour becomes coverage (255 minus its luminance): the white that clears a
 * text bitmap must stay white, or the whole rectangle reads as covered. */
static const uint8_t *fill_color(const void *surface, const uint8_t *color, uint8_t *mapped)
{
    if ((RV_FIELD(surface, Surface_format, uint16_t) & ~BMapFormat_ByteSwapped) <= BMapFormat_Mask8)
        return color;
    return map_fill(color, mapped);
}

/* (surface, rect, rgba, mode, clip): every fill and clear. */
static uint32_t fill_rect(uint32_t surface, uint32_t rect, uint32_t rgba, uint32_t mode,
                     const void *next, uint32_t clip)
{
    uint8_t mapped[4];
    const uint8_t *color = fill_color((const void *)surface, (const uint8_t *)rgba, mapped);
    return api->hook_next(next, surface, rect, (uint32_t)color, mode, clip, 0);
}

/* The fill rule for the background of view `id`, returning the rule it
 * replaces: a view drawn inside another's background gives it back. */
static int enter_background(uint32_t id)
{
    int outer = stock_fill;
    stock_fill = stock_background(id);
    return outer;
}

/* (context, rect, view): a view's background. */
static uint32_t background_fill(uint32_t context, uint32_t rect, uint32_t view, const void *next)
{
    int outer = enter_background(view_id(view));
    uint32_t result = api->hook_next(next, context, rect, view, 0, 0, 0);
    stock_fill = outer;
    return result;
}

/* A channel as a 16.16 fraction of 255: c * 65536 / 255 is c * 257 plus one
 * for c = 255. */
static uint32_t fraction(uint8_t channel)
{
    return channel * 257u + ((channel + 1u) >> 8);
}

/* (red, green, blue, alpha) in 16.16: the GLES clear behind Cover Flow and album art. */
static uint32_t clear_color(uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha,
                            const void *next)
{
    if (background_themed()) {
        const uint8_t *background = style_color(RVT_ROLE_BACKGROUND);
        red = fraction(background[0]);
        green = fraction(background[1]);
        blue = fraction(background[2]);
    }
    return api->hook_next(next, red, green, blue, alpha, 0, 0);
}

/* (pixel, background, weight): the album art reflection fading to the background. */
static uint32_t reflection_blend(uint32_t pixel, uint32_t background, uint32_t weight,
                                 const void *next)
{
    if (background_themed())
        background = rgb565_pack(style_color(RVT_ROLE_BACKGROUND));
    return api->hook_next(next, pixel, background, weight, 0, 0, 0);
}

/* (context, rgba, view): a bitmap view's mask colour. Tinted masks take their
 * tint, or the on-accent colour while highlighted. */
static uint32_t bitmap_color(uint32_t context, uint32_t rgba, uint32_t view, const void *next)
{
    const uint8_t *tinted = tint_mask_color((const void *)view);
    return api->hook_next(next, context, tinted ? (uint32_t)tinted : rgba, view, 0, 0, 0);
}

/* Which of the view's two label colour fields `color` is, by address. */
static enum bar_slot bar_slot(uint32_t rgba, uint32_t view, uint32_t label, uint32_t selected)
{
    return rgba == view + selected ? BAR_SELECTED : rgba == view + label ? BAR_LABEL : BAR_OTHER;
}

/* (context, rgba, view): the keyboard's key letters, on black well slices. */
static uint32_t keyboard_color(uint32_t context, uint32_t rgba, uint32_t view, const void *next)
{
    uint8_t mapped[4];
    enum bar_slot slot =
        bar_slot(rgba, view, KeyboardView_letter_color, KeyboardView_selected_letter_color);
    const uint8_t *color = map_bar_label((const uint8_t *)rgba, slot, WELL_BLACK, mapped);
    return api->hook_next(next, context, (uint32_t)color, view, 0, 0, 0);
}

/* (context, rgba, view): an option bar's segment labels. */
static uint32_t option_bar_color(uint32_t context, uint32_t rgba, uint32_t view, const void *next)
{
    uint8_t mapped[4];
    const uint8_t *color = (const uint8_t *)rgba;
    unsigned well = RV_FIELD(view, OptionBarView_style, uint8_t);
    if (well < WELL_NOW_PLAYING || (well == WELL_NOW_PLAYING && style_from_theme()))
        color = map_bar_label(color,
                              bar_slot(rgba, view, OptionBarView_label_color,
                                       OptionBarView_selected_label_color),
                              well, mapped);
    return api->hook_next(next, context, (uint32_t)color, view, 0, 0, 0);
}

/* (context, rgba): a calendar day number. */
static uint32_t calendar_day_color(uint32_t context, uint32_t rgba, const void *next)
{
    uint8_t mapped[4];
    const uint8_t *color = map_calendar_day((const uint8_t *)rgba, mapped);
    return api->hook_next(next, context, (uint32_t)color, 0, 0, 0, 0);
}

/* (context, rgba): the calendar's weekday names. */
static uint32_t calendar_header_color(uint32_t context, uint32_t rgba, const void *next)
{
    uint8_t mapped[4], mixed[4];
    const uint8_t *color = (const uint8_t *)rgba;
    const uint8_t *themed = tint_calendar_text_color(CALENDAR_HEADER, mixed);
    if (themed)
        color = recolor(color, themed, mapped);
    return api->hook_next(next, context, (uint32_t)color, 0, 0, 0, 0);
}

/* (context, source, source_rect, rect, alpha, flags, view): a bitmap view's or
 * a menu row icon's blit, with the view the site pushes. */
static uint32_t view_blit(uint32_t context, uint32_t source, uint32_t source_rect, uint32_t rect,
                          const void *next, uint32_t alpha, uint32_t flags, uint32_t view)
{
    tint_highlight_begin((const void *)view, (const void *)source);
    uint32_t result = api->hook_next7(next, context, source, source_rect, rect, alpha, flags, view);
    tint_highlight_end();
    return result;
}

/* The same for a calendar day cell's marker, with the cell pushed. */
static uint32_t marker_blit(uint32_t context, uint32_t source, uint32_t source_rect, uint32_t rect,
                            const void *next, uint32_t alpha, uint32_t flags, uint32_t cell)
{
    tint_marker_highlight_begin((const void *)cell, (const void *)source);
    uint32_t result = api->hook_next7(next, context, source, source_rect, rect, alpha, flags, cell);
    tint_highlight_end();
    return result;
}

static const struct rv_hook themed_hooks[] = {
    RV_HOOK_FILTER(RV_SITE_TextView_DrawColor_call, text_color),
    RV_HOOK_FILTER(RV_SITE_MarqueeTextView_DrawColor_call, text_color),
    RV_HOOK_FILTER(RV_SITE_GraphicsContext_FillRect_call, fill_rect),
    RV_HOOK_FILTER(RV_SITE_GraphicsContext_ClearRect_call, fill_rect),
    RV_HOOK_FILTER(RV_SITE_View_BackgroundFill_call, background_fill),
    RV_HOOK_FILTER(RV_SITE_CoverFlow_ClearColor_call, clear_color),
    RV_HOOK_FILTER(RV_SITE_AlbumArt_ClearColor_call, clear_color),
    RV_HOOK_FILTER(RV_SITE_ArtworkReflection_Blend_call, reflection_blend),
    RV_HOOK_FILTER(RV_SITE_BitmapView_DrawColor_call, bitmap_color),
    RV_HOOK_FILTER(RV_SITE_KeyboardView_DrawColor_call, keyboard_color),
    RV_HOOK_FILTER(RV_SITE_OptionBarView_DrawColor_call, option_bar_color),
    RV_HOOK_FILTER(RV_SITE_CalendarDayCell_DrawColor_call, calendar_day_color),
    RV_HOOK_FILTER(RV_SITE_CalendarView_DrawColor_call, calendar_header_color),
    RV_HOOK_FILTER(RV_SITE_BitmapView_Blit_call, view_blit),
    RV_HOOK_FILTER(RV_SITE_BitmapView_TileBlit_call, view_blit),
    RV_HOOK_FILTER(RV_SITE_MenuRowBitmapView_Blit_call, view_blit),
    RV_HOOK_FILTER(RV_SITE_MenuRowBitmapView_TileBlit_call, view_blit),
    RV_HOOK_FILTER(RV_SITE_CalendarDayCell_TopMarkerBlit_call, marker_blit),
    RV_HOOK_FILTER(RV_SITE_CalendarDayCell_BottomMarkerBlit_call, marker_blit),
    RV_HOOK_FILTER(RV_SITE_GLAlbumArtView_Draw_slot, flat_cover_view_draw),
    RV_HOOK_FILTER(RV_SITE_AlbumArt_RendererDraw_call, flat_cover_renderer_draw),
    RV_HOOK_FILTER(RV_SITE_AlbumArt_EdgeLines_call, flat_cover_edge_lines),
};
#define THEMED_HOOKS (sizeof themed_hooks / sizeof *themed_hooks)

static unsigned bound; /* how many of themed_hooks are bound, from the first */

int render_bind(int on)
{
    if (on) {
        while (bound < THEMED_HOOKS && api->hook_bind(self, &themed_hooks[bound]) == RV_OK)
            bound++;
        if (bound == THEMED_HOOKS)
            return 1;
    }
    while (bound)
        api->hook_unbind(self, &themed_hooks[--bound]);
    return !on;
}
