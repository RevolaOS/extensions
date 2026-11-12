/* SPDX-License-Identifier: GPL-3.0-only */

/* The Compressor screen: the output level for every input level, as the six
 * values shape it, over two rows of the values. The wheel picks a value,
 * Select edits it and Menu steps back out. Every change plays at once. */

#include "labels.h"
#include "rv_libc.h"
#include "rv_ui.h"
#include "shared.h"

static const struct rv_api *api;
static rv_extension_handle self;

enum { MODE_PARAMETER, MODE_EDIT };

/* Wheel order is reading order: Threshold | Ratio | Knee, then Makeup | Attack | Release. */
enum {
    PARAM_THRESHOLD,
    PARAM_RATIO,
    PARAM_KNEE,
    PARAM_MAKEUP,
    PARAM_ATTACK,
    PARAM_RELEASE,
    PARAM_COUNT
};
static const char *const titles[PARAM_COUNT] = {"Threshold", "Ratio", "Knee",
                                                "Makeup", "Attack", "Release"};
static const uint8_t keys[PARAM_COUNT] = {KEY_THRESHOLD, KEY_RATIO, KEY_KNEE,
                                          KEY_MAKEUP, KEY_ATTACK, KEY_RELEASE};
static const uint8_t counts[PARAM_COUNT] = {THRESHOLD_LABELS, RATIO_LABELS, KNEE_LABELS,
                                            MAKEUP_LABELS, ATTACK_LABELS, RELEASE_LABELS};
static const char *const *const labels[PARAM_COUNT] = {
    threshold_labels, ratio_labels, knee_labels, makeup_labels, attack_labels, release_labels,
};

enum {
    HEADER_TOP = 3,
    HEADER_HEIGHT = 24,
    PLOT_LEFT = 44,
    PLOT_POINTS = 259, /* -36 dB to 0 dB of input: 86 px per 12 dB */
    PLOT_TOP = 30,     /* 0 dB of output */
    PLOT_BOTTOM = 105, /* -36 dB: 25 px per 12 dB */
    PLOT_RANGE = 360,  /* tenths of a decibel on either axis */
    TICK_TOP = 106,
    TICK_HEIGHT = 15,
    CELL_TOP = 123,
    CELL_HEIGHT = 47,
    COLUMNS = 3,
    SWATCH = 10,
};

static struct {
    int parameter, mode;
    int menu_taken; /* the Menu press left edit mode; its release is not the system's */
    int on;
    /* The curve in tenths of a decibel: input levels, the offset at each and
     * the makeup gain added after, from compressor_points. */
    int in[5], offset[5], makeup;
} editor;

static int clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

static int value(int parameter)
{
    return clamp(api->setting_int(self, keys[parameter]), 0, counts[parameter] - 1);
}

static int tenths(int32_t db)
{
    return (int)((db * 10 + (1 << 15)) >> 16);
}

static void compute_points(void)
{
    struct compressor_set set = {
        .threshold_db = (int8_t)threshold_db(value(PARAM_THRESHOLD)),
        .makeup = (uint8_t)value(PARAM_MAKEUP),
        .ratio = (uint8_t)value(PARAM_RATIO),
        .knee = (uint8_t)value(PARAM_KNEE),
    };
    struct compressor_points points;
    compressor_points(&set, &points);
    for (unsigned int i = 0; i < 5; i++) {
        editor.in[i] = tenths(points.db[i]);
        editor.offset[i] = tenths(points.offset[i]);
    }
    editor.makeup = tenths(points.makeup);
    editor.on = set.threshold_db != 0;
}

/* The output level for an input level, both in tenths of a decibel: straight
 * between the points, flat below the first and past the last. */
static int output_tenths(int in)
{
    const int *at = editor.in, *offset = editor.offset;
    int gain = offset[4];
    if (in <= at[0]) {
        gain = 0;
    } else {
        for (unsigned int k = 1; k < 5; k++) {
            if (in <= at[k]) {
                gain = offset[k - 1] +
                       (offset[k] - offset[k - 1]) * (in - at[k - 1]) / (at[k] - at[k - 1]);
                break;
            }
        }
    }
    return in + gain + editor.makeup;
}

enum {
    COLOR_BACKGROUND,
    COLOR_CURVE, /* the accent as text: the curve and its legend swatch */
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

static int plot_x(int in)
{
    return PLOT_LEFT + ((in + PLOT_RANGE) * (PLOT_POINTS - 1) + PLOT_RANGE / 2) / PLOT_RANGE;
}

static int plot_y(int out)
{
    out = clamp(out, -PLOT_RANGE, 0);
    return PLOT_TOP + (-out * (PLOT_BOTTOM - PLOT_TOP) + PLOT_RANGE / 2) / PLOT_RANGE;
}

/* The input level at a plot column, in tenths of a decibel. */
static int column_in(unsigned int i)
{
    return -PLOT_RANGE + (int)((i * PLOT_RANGE + (PLOT_POINTS - 1) / 2) / (PLOT_POINTS - 1));
}

/* A curve of `thickness` pixels: a column per point stretched to meet the one
 * before. `compressed` draws the output level, otherwise the input unchanged. */
static void draw_curve(const struct rv_gfx *g, int compressed, int thickness, int color)
{
    int previous = -1;
    for (unsigned int i = 0; i < PLOT_POINTS; i++) {
        int in = column_in(i);
        int y = plot_y(compressed ? output_tenths(in) : in);
        if (previous < 0)
            previous = y;
        int top = y < previous ? y : previous;
        int bottom = y > previous ? y : previous;
        rect(g, PLOT_LEFT + (int)i, top, 1, bottom - top + thickness, color);
        previous = y;
    }
}

static void draw_plot(const struct rv_gfx *g)
{
    static const char *const scale[] = {"0", "-12", "-24", "-36"};
    static const char *const ticks[] = {"-36", "-24", "-12", "0 dB"};

    for (unsigned int i = 0; i < 4; i++) {
        int x = PLOT_LEFT + (int)i * (PLOT_POINTS - 1) / 3;
        int y = PLOT_TOP + (int)i * (PLOT_BOTTOM - PLOT_TOP) / 3;
        rect(g, x, PLOT_TOP, 1, PLOT_BOTTOM - PLOT_TOP + 1, COLOR_GRID);
        rect(g, PLOT_LEFT, y, PLOT_POINTS, 1, COLOR_GRID);
        text_in(g, 0, y - 8, 38, 16, scale[i], 0, RV_ALIGN_CENTER, COLOR_MUTED);
        if (i == 3)
            text_in(g, x - 46, TICK_TOP, 48, TICK_HEIGHT, ticks[i], 0, RV_ALIGN_RIGHT,
                    COLOR_MUTED);
        else
            text_in(g, x - 21, TICK_TOP, 42, TICK_HEIGHT, ticks[i], 0, RV_ALIGN_CENTER,
                    COLOR_MUTED);
    }

    /* The header names the plot; the legend sits at its right. */
    text_in(g, 8, HEADER_TOP, 140, HEADER_HEIGHT, "Output level", 0, RV_ALIGN_LEFT, COLOR_TEXT);
    int swatch_y = HEADER_TOP + (HEADER_HEIGHT - SWATCH) / 2;
    int compressed_width = g->measure(RV_FONT_TEXT, "Compressed");
    int unchanged_width = g->measure(RV_FONT_TEXT, "Unchanged");
    int x = 312 - compressed_width;
    text_in(g, x, HEADER_TOP, compressed_width, HEADER_HEIGHT, "Compressed", 0, RV_ALIGN_LEFT,
            COLOR_MUTED);
    x -= 4 + SWATCH;
    rect(g, x, swatch_y, SWATCH, SWATCH, editor.on ? COLOR_CURVE : COLOR_INACTIVE);
    x -= 10 + unchanged_width;
    text_in(g, x, HEADER_TOP, unchanged_width, HEADER_HEIGHT, "Unchanged", 0, RV_ALIGN_LEFT,
            COLOR_MUTED);
    x -= 4 + SWATCH;
    rect(g, x, swatch_y, SWATCH, SWATCH, editor.on ? COLOR_INACTIVE : COLOR_CURVE);

    /* Off plays the input unchanged, so that line takes the accent. */
    if (!editor.on) {
        draw_curve(g, 0, 2, COLOR_CURVE);
        return;
    }
    rect(g, plot_x(editor.in[1]), PLOT_TOP, 1, PLOT_BOTTOM - PLOT_TOP + 1, COLOR_BORDER);
    draw_curve(g, 0, 1, COLOR_INACTIVE);
    draw_curve(g, 1, 2, COLOR_CURVE);
}

static void draw_parameters(const struct rv_gfx *g, int width)
{
    rect(g, 0, CELL_TOP - 1, width, 1, COLOR_BORDER);
    rect(g, 0, CELL_TOP + CELL_HEIGHT, width, 1, COLOR_BORDER);
    for (int column = 1; column < COLUMNS; column++)
        rect(g, column * width / COLUMNS, CELL_TOP, 1, 2 * CELL_HEIGHT + 1, COLOR_BORDER);
    for (int parameter = 0; parameter < PARAM_COUNT; parameter++) {
        int column = parameter % COLUMNS;
        int left = column * width / COLUMNS;
        int cell_width = (column + 1) * width / COLUMNS - left;
        int top = CELL_TOP + parameter / COLUMNS * (CELL_HEIGHT + 1);
        int active = parameter == editor.parameter;
        int editing = active && editor.mode == MODE_EDIT;
        if (active) {
            rect(g, left, top, cell_width, CELL_HEIGHT, editing ? COLOR_FILL : COLOR_CURVE);
            if (!editing)
                rect(g, left + 2, top + 2, cell_width - 4, CELL_HEIGHT - 4, COLOR_SELECTION);
        }
        text_in(g, left, top + 5, cell_width, 16, titles[parameter], 0, RV_ALIGN_CENTER,
                editing ? COLOR_ON_FILL : COLOR_MUTED);
        text_in(g, left, top + 21, cell_width, 22, labels[parameter][value(parameter)], 1,
                RV_ALIGN_CENTER, editing ? COLOR_ON_FILL : COLOR_TEXT);
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
    compute_points();
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
    compute_points();
}

static const struct rv_screen screens[] = {
    RV_SCREEN_CANVAS("editor", "Compressor", draw, input,
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
