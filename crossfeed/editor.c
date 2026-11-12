/* SPDX-License-Identifier: GPL-3.0-only */

/* The Crossfeed screen: what each ear hears, as two curves over the mode and
 * the five values of the Custom cross path. One curve is the ear's own
 * channel, the other the channel fed across; in Custom the first is flat at
 * the direct gain and the second a shelf at the cross gain in the bass and the
 * attenuation above the cutoff, both lower by the precut. The wheel picks a
 * value, Select edits it and Menu steps back out. Every change plays at once. */

#include "labels.h"
#include "rv_libc.h"
#include "rv_ui.h"
#include "shared.h"

static const struct rv_api *api;
static rv_extension_handle self;

enum { MODE_PARAMETER, MODE_EDIT };

/* Wheel order is reading order: Mode | Precut, then Direct | Cross | Highs | Cutoff. */
enum { PARAM_MODE, PARAM_PRECUT, PARAM_DIRECT, PARAM_CROSS, PARAM_HF, PARAM_CUTOFF, PARAM_COUNT };
static const char *const titles[PARAM_COUNT] = {"Mode",  "Precut", "Direct",
                                                "Cross", "Highs",  "Cutoff"};
static const uint8_t keys[PARAM_COUNT] = {KEY_MODE,  KEY_PRECUT, KEY_DIRECT,
                                          KEY_CROSS, KEY_HF,     KEY_CUTOFF};
static const uint8_t counts[PARAM_COUNT] = {MODE_LABELS,  PRECUT_LABELS, DIRECT_LABELS,
                                            CROSS_LABELS, HF_LABELS,     CUTOFF_LABELS};
static const char *const *const labels[PARAM_COUNT] = {mode_labels,  precut_labels, direct_labels,
                                                       cross_labels, hf_labels,     cutoff_labels};

enum {
    CANVAS_WIDTH = 320,
    CANVAS_HEIGHT = 217,
    HEADER_TOP = 3,
    HEADER_HEIGHT = 24,
    PLOT_LEFT = 44,
    PLOT_POINTS = 261,
    PLOT_TOP = 36,     /* 0 dB */
    PLOT_BOTTOM = 108, /* -24 dB: 3 px per decibel */
    PLOT_RANGE = 240,  /* tenths of a decibel from top to bottom */
    TICK_TOP = 114,
    INLINE_TOP = 141, /* Mode | Precut, title left and value right */
    INLINE_HEIGHT = 27,
    INLINE_COUNT = PARAM_DIRECT,
    PARAM_TOP = INLINE_TOP + INLINE_HEIGHT + 1,
    PARAM_HEIGHT = CANVAS_HEIGHT - PARAM_TOP,
    CELL_COUNT = PARAM_COUNT - INLINE_COUNT,
    CELL_WIDTH = CANVAS_WIDTH / CELL_COUNT,
    SWATCH = 10,
};

static struct {
    int parameter, mode;
    int menu_taken; /* the Menu press left edit mode; its release is not the system's */
    int16_t same[PLOT_POINTS], other[PLOT_POINTS];
} editor;

static int clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

static int value(int parameter)
{
    return clamp(api->setting_int(self, keys[parameter]), 0, counts[parameter] - 1);
}

static int crossfeed_mode(void)
{
    return value(PARAM_MODE);
}

static void compute_curve(void)
{
    struct crossfeed_set set = {
        .mode = (uint8_t)crossfeed_mode(),
        .direct_tenths = (int16_t)direct_tenths(value(PARAM_DIRECT)),
        .cross_tenths = (int16_t)cross_tenths(value(PARAM_CROSS)),
        .hf_tenths = (int16_t)hf_tenths(value(PARAM_HF)),
        .cutoff_hz = (uint16_t)cutoff_hz(value(PARAM_CUTOFF)),
        .precut_tenths = (int16_t)precut_tenths(value(PARAM_PRECUT)),
    };
    /* Drawn for 44.1 kHz, the rate nearly everything plays at; the shelf
     * barely moves at the others. */
    crossfeed_response(&set, 44100, editor.same, editor.other, PLOT_POINTS);
}

enum {
    COLOR_BACKGROUND,
    COLOR_CURVE, /* the accent as text: the curve being shaped and its legend swatch */
    COLOR_FILL,  /* the accent as a surface: the value being edited */
    COLOR_TEXT,
    COLOR_ON_FILL,
    COLOR_ROLES,
    /* Derived from the roles above. */
    COLOR_MUTED = COLOR_ROLES,
    COLOR_INACTIVE,
    COLOR_GRID,
    COLOR_BORDER,
    COLOR_SELECTION,
    COLOR_COUNT,
};
static rv_color colors[COLOR_COUNT];

static const uint8_t roles[COLOR_ROLES] = {RV_COLOR_BACKGROUND, RV_COLOR_ACCENT_TEXT,
                                           RV_COLOR_HIGHLIGHT, RV_COLOR_TEXT,
                                           RV_COLOR_HIGHLIGHT_TEXT};

/* Greys are the background mixed toward the text by weight/255. */
static const uint8_t greys[][2] = {
    {COLOR_GRID, 28}, {COLOR_BORDER, 46}, {COLOR_INACTIVE, 130}, {COLOR_MUTED, 185},
};

/* Weighted RGB brightness, scaled by 256. */
static int brightness(rv_color color)
{
    return 54 * (int)(color >> 16 & 0xff) + 183 * (int)(color >> 8 & 0xff) +
           19 * (int)(color & 0xff);
}

/* The selection tints the background toward the accent, further on a dark one. */
static void load_colors(const struct rv_gfx *g)
{
    for (unsigned int i = 0; i < COLOR_ROLES; i++)
        colors[i] = g->color(roles[i]);
    for (unsigned int i = 0; i < sizeof greys / sizeof greys[0]; i++)
        colors[greys[i][0]] = g->mix(colors[COLOR_BACKGROUND], colors[COLOR_TEXT], greys[i][1]);
    int dark = brightness(colors[COLOR_BACKGROUND]) < 128 * 256;
    colors[COLOR_SELECTION] = g->mix(colors[COLOR_BACKGROUND], colors[COLOR_CURVE], dark ? 43 : 20);
}

static void rect(const struct rv_gfx *g, int x, int y, int width, int height, int color)
{
    struct rv_rect r = {(int16_t)x, (int16_t)y, (int16_t)width, (int16_t)height};
    g->fill(&r, colors[color]);
}

/* `text` in the box; a large text that does not fit falls back to the text font. */
static void text_in(const struct rv_gfx *g, int x, int y, int width, int height, const char *text,
                    int large, int alignment, int color)
{
    struct rv_rect r = {(int16_t)x, (int16_t)y, (int16_t)width, (int16_t)height};
    uint8_t font = large && g->measure(RV_FONT_LARGE, text) <= width - 8 ? RV_FONT_LARGE
                                                                          : RV_FONT_TEXT;
    g->text_in(&r, font, colors[color], text, (int8_t)alignment);
}

static int plot_y(int tenths)
{
    tenths = clamp(tenths, -PLOT_RANGE, 0);
    return PLOT_TOP + (-tenths * (PLOT_BOTTOM - PLOT_TOP) + PLOT_RANGE / 2) / PLOT_RANGE;
}

/* Two pixels thick: a column per point stretched to meet the one before. */
static void draw_curve(const struct rv_gfx *g, const int16_t *tenths, int color)
{
    int previous = plot_y(tenths[0]);
    for (unsigned int i = 0; i < PLOT_POINTS; i++) {
        int y = plot_y(tenths[i]);
        int top = y < previous ? y : previous;
        int bottom = y > previous ? y : previous;
        rect(g, PLOT_LEFT + (int)i, top, 1, bottom - top + 2, color);
        previous = y;
    }
}

/* The curve the highlighted value shapes draws in the accent, over the other:
 * the ear's own channel for Direct and while Off, the one fed across otherwise.
 * Off has no cross path, so that curve and its swatch stay inactive. */
static void draw_plot(const struct rv_gfx *g)
{
    static const int ticks[] = {44, 131, 217, 304};
    static const char *const tick_labels[] = {"20", "200", "2k", "20k Hz"};
    static const char *const scale[] = {"0", "-12", "-24"};
    int off = crossfeed_mode() == CROSSFEED_OFF;
    int same_side = off || editor.parameter == PARAM_DIRECT;
    int same_color = same_side ? COLOR_CURVE : COLOR_INACTIVE;
    int other_color = same_side ? COLOR_INACTIVE : COLOR_CURVE;

    for (unsigned int i = 0; i < 4; i++) {
        rect(g, ticks[i], PLOT_TOP, 1, PLOT_BOTTOM - PLOT_TOP + 1, COLOR_GRID);
        text_in(g, i == 3 ? 271 : ticks[i] - 21, TICK_TOP, i == 3 ? 49 : 42, 22, tick_labels[i],
                0, RV_ALIGN_CENTER, COLOR_MUTED);
    }
    for (unsigned int i = 0; i < 3; i++) {
        int y = PLOT_TOP + (int)i * (PLOT_BOTTOM - PLOT_TOP) / 2;
        rect(g, PLOT_LEFT, y, PLOT_POINTS, 1, COLOR_GRID);
        text_in(g, 0, y - 9, 38, 18, scale[i], 0, RV_ALIGN_CENTER, COLOR_MUTED);
    }

    /* The header names the plot; the legend sits at its right. */
    text_in(g, 8, HEADER_TOP, 140, HEADER_HEIGHT, "Each ear hears", 0, RV_ALIGN_LEFT,
            COLOR_TEXT);
    int swatch_y = HEADER_TOP + (HEADER_HEIGHT - SWATCH) / 2;
    int other_width = g->measure(RV_FONT_TEXT, "Other side");
    int same_width = g->measure(RV_FONT_TEXT, "Same side");
    int x = 312 - other_width;
    text_in(g, x, HEADER_TOP, other_width, HEADER_HEIGHT, "Other side", 0, RV_ALIGN_LEFT,
            COLOR_MUTED);
    x -= 4 + SWATCH;
    rect(g, x, swatch_y, SWATCH, SWATCH, other_color);
    x -= 10 + same_width;
    text_in(g, x, HEADER_TOP, same_width, HEADER_HEIGHT, "Same side", 0, RV_ALIGN_LEFT,
            COLOR_MUTED);
    x -= 4 + SWATCH;
    rect(g, x, swatch_y, SWATCH, SWATCH, same_color);

    if (same_side) {
        if (!off)
            draw_curve(g, editor.other, other_color);
        draw_curve(g, editor.same, same_color);
    } else {
        draw_curve(g, editor.same, same_color);
        draw_curve(g, editor.other, other_color);
    }
}

/* The Custom values read inactive while another mode plays; they still edit. */
static void draw_parameters(const struct rv_gfx *g, int width)
{
    int custom = crossfeed_mode() == CROSSFEED_CUSTOM;
    rect(g, 0, INLINE_TOP - 1, width, 1, COLOR_BORDER);
    rect(g, 0, PARAM_TOP - 1, width, 1, COLOR_BORDER);
    rect(g, CANVAS_WIDTH / 2, INLINE_TOP, 1, INLINE_HEIGHT, COLOR_BORDER);
    for (int cell = 1; cell < CELL_COUNT; cell++)
        rect(g, cell * CELL_WIDTH, PARAM_TOP, 1, PARAM_HEIGHT, COLOR_BORDER);
    for (int parameter = 0; parameter < PARAM_COUNT; parameter++) {
        int inline_row = parameter < INLINE_COUNT;
        int left = inline_row ? parameter * CANVAS_WIDTH / 2
                              : (parameter - INLINE_COUNT) * CELL_WIDTH;
        int cell_width = inline_row ? CANVAS_WIDTH / 2 : CELL_WIDTH;
        int top = inline_row ? INLINE_TOP : PARAM_TOP;
        int height = inline_row ? INLINE_HEIGHT : PARAM_HEIGHT;
        int active = parameter == editor.parameter;
        int editing = active && editor.mode == MODE_EDIT;
        if (active) {
            rect(g, left, top, cell_width, height, editing ? COLOR_FILL : COLOR_CURVE);
            if (!editing)
                rect(g, left + 2, top + 2, cell_width - 4, height - 4, COLOR_SELECTION);
        }
        int title_color = editing ? COLOR_ON_FILL : COLOR_MUTED;
        int value_color = editing ? COLOR_ON_FILL
                          : custom || parameter == PARAM_MODE ? COLOR_TEXT
                                                                : COLOR_INACTIVE;
        const char *text = labels[parameter][value(parameter)];
        if (inline_row) {
            text_in(g, left + 12, top, cell_width - 24, height, titles[parameter], 0,
                    RV_ALIGN_LEFT, title_color);
            text_in(g, left + 12, top, cell_width - 24, height, text, 0, RV_ALIGN_RIGHT,
                    value_color);
        } else {
            text_in(g, left, top + 5, cell_width, 16, titles[parameter], 0, RV_ALIGN_CENTER,
                    title_color);
            text_in(g, left, top + 21, cell_width, 22, text, 1, RV_ALIGN_CENTER, value_color);
        }
    }
}

static void draw(const struct rv_context *ctx, const struct rv_gfx *g, int width, int height)
{
    (void)ctx;
    load_colors(g);
    rect(g, 0, 0, width, height, COLOR_BACKGROUND);
    draw_plot(g);
    draw_parameters(g, width);
}

static void wheel(int delta)
{
    if (editor.mode == MODE_PARAMETER) {
        editor.parameter = clamp(editor.parameter + delta, 0, PARAM_COUNT - 1);
        return;
    }
    int previous = value(editor.parameter);
    int next = clamp(previous + delta, 0, counts[editor.parameter] - 1);
    if (next == previous)
        return;
    /* Main republishes from setting_changed, so the change plays at once. */
    api->setting_set_int(self, keys[editor.parameter], next);
    compute_curve();
}

static int input(const struct rv_context *ctx, const struct rv_input *input)
{
    (void)ctx;
    if (input->kind == RV_INPUT_WHEEL) {
        wheel(input->delta);
    } else if (input->kind == RV_INPUT_HOLD) {
        return 1;
    } else if (input->button == RV_BUTTON_CENTER) {
        if (input->kind == RV_INPUT_RELEASE)
            return 1;
        editor.mode = editor.mode == MODE_PARAMETER ? MODE_EDIT : MODE_PARAMETER;
    } else if (input->button == RV_BUTTON_MENU) {
        if (input->kind == RV_INPUT_RELEASE) {
            int taken = editor.menu_taken;
            editor.menu_taken = 0;
            return taken;
        }
        if (editor.mode == MODE_PARAMETER)
            return 0; /* the system pops the screen */
        editor.mode = MODE_PARAMETER;
        editor.menu_taken = 1;
    } else {
        return 0; /* playback keys */
    }
    api->ui_invalidate(self);
    return 1;
}

static void opened(const struct rv_context *ctx)
{
    (void)ctx;
    memset(&editor, 0, sizeof editor);
    compute_curve();
}

static const struct rv_screen screens[] = {
    RV_SCREEN_CANVAS("editor", "Crossfeed", draw, input,
                     .transition = RV_TRANSITION_FROM_PANE, .opened = opened),
};

static const struct rv_descriptor module = {
    .size = sizeof module,
    RV_SCREENS(screens),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    api = core;
    self = handle;
    return &module;
}
