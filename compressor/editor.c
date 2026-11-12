/* SPDX-License-Identifier: GPL-3.0-only */

/* The Compressor screen: the output level for every input level, as the six
 * values shape it, over two rows of the values. The wheel picks a value,
 * Select edits it and Menu steps back out. Every change plays at once. */

#include "labels.h"
#include "rv_canvas.h"
#include "rv_libc.h"
#include "rv_ui.h"
#include "shared.h"

static const struct rv_api *api;
static rv_extension_handle self;

enum { DEPTH_PARAMETER, DEPTH_EDIT, DEPTHS };

static const char *const titles[PARAM_COUNT] = {"Threshold", "Ratio", "Knee",
                                                "Makeup", "Attack", "Release"};
static const uint8_t keys[PARAM_COUNT] = {KEY_THRESHOLD, KEY_RATIO, KEY_KNEE,
                                          KEY_MAKEUP, KEY_ATTACK, KEY_RELEASE};
static const uint8_t counts[PARAM_COUNT] = {THRESHOLD_LABELS, RATIO_LABELS, KNEE_LABELS,
                                            MAKEUP_LABELS, ATTACK_LABELS, RELEASE_LABELS};

enum {
    PLOT_POINTS = 259, /* -36 dB to 0 dB of input: 86 px per 12 dB */
    PLOT_RANGE = 360,  /* tenths of a decibel on either axis */
    TICK_TOP = 106,
    TICK_HEIGHT = 15,
    CELL_TOP = 123, /* two rows of cells share the rest of the canvas */
    COLUMNS = 3,
    ROWS = PARAM_COUNT / COLUMNS,
};

/* The output level, 0 dB at the top and -36 dB at the bottom: 25 px per 12 dB. */
static const struct rv_plot plot = {
    .left = 44, .top = 30, .bottom = 105, .points = PLOT_POINTS, .bottom_value = -PLOT_RANGE,
};

static struct {
    struct rv_editor input;
    int parameter;
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

static int plot_x(int in)
{
    return plot.left + ((in + PLOT_RANGE) * (PLOT_POINTS - 1) + PLOT_RANGE / 2) / PLOT_RANGE;
}

/* The input level at a plot column, in tenths of a decibel. */
static int column_in(unsigned int i)
{
    return -PLOT_RANGE + (int)((i * PLOT_RANGE + (PLOT_POINTS - 1) / 2) / (PLOT_POINTS - 1));
}

/* A curve of `thickness` pixels, a column per point stretched to meet the one
 * before, as rv_plot_curve draws, but from the level of each column as it
 * goes: `compressed` draws the output level, otherwise the input unchanged. */
static void draw_curve(const struct rv_canvas *c, int compressed, int thickness, uint8_t color)
{
    int previous = -1;
    for (unsigned int i = 0; i < PLOT_POINTS; i++) {
        int in = column_in(i);
        int y = rv_plot_y(&plot, compressed ? output_tenths(in) : in);
        if (previous < 0)
            previous = y;
        int top = y < previous ? y : previous;
        int bottom = y > previous ? y : previous;
        rv_canvas_rect(c, plot.left + (int)i, top, 1, bottom - top + thickness, color);
        previous = y;
    }
}

static void draw_plot(const struct rv_canvas *c)
{
    static const char *const scale[] = {"0", "-12", "-24", "-36"};
    static const char *const ticks[] = {"-36", "-24", "-12", "0 dB"};
    /* Off plays the input unchanged, so that line takes the accent. */
    const struct rv_canvas_legend legend[] = {
        {"Unchanged", editor.on ? RV_CANVAS_INACTIVE : RV_CANVAS_ACCENT},
        {"Compressed", editor.on ? RV_CANVAS_ACCENT : RV_CANVAS_INACTIVE},
    };
    rv_plot_ticks(c, &plot, ticks, 4, TICK_TOP, TICK_HEIGHT);
    rv_plot_grid(c, &plot, scale, 4);
    rv_canvas_header(c, "Output level", legend, 2);
    if (!editor.on) {
        draw_curve(c, 0, 2, RV_CANVAS_ACCENT);
        return;
    }
    rv_canvas_rect(c, plot_x(editor.in[1]), plot.top, 1, plot.bottom - plot.top + 1,
                   RV_CANVAS_BORDER);
    draw_curve(c, 0, 1, RV_CANVAS_INACTIVE);
    draw_curve(c, 1, 2, RV_CANVAS_ACCENT);
}

static void draw_parameters(const struct rv_canvas *c)
{
    int row_height = (c->height - CELL_TOP - (ROWS - 1)) / ROWS; /* a border between rows */
    for (int row = 0; row < ROWS; row++) {
        int top = CELL_TOP + row * (row_height + 1);
        int height = row == ROWS - 1 ? c->height - top : row_height;
        char text[COLUMNS][LABEL_BYTES];
        const char *values[COLUMNS];
        uint8_t states[COLUMNS];
        for (int column = 0; column < COLUMNS; column++) {
            int parameter = row * COLUMNS + column;
            values[column] = compressor_label(parameter, value(parameter), text[column]);
            states[column] = parameter != editor.parameter      ? RV_CELL_IDLE
                             : editor.input.depth == DEPTH_EDIT ? RV_CELL_EDITING
                                                                : RV_CELL_SELECTED;
        }
        rv_canvas_cells(c, top, height, titles + row * COLUMNS, values, states, COLUMNS);
    }
}

static void draw(const struct rv_context *ctx, const struct rv_gfx *g, int width, int height)
{
    struct rv_canvas c;
    (void)ctx;
    rv_canvas_begin(&c, g, width, height);
    draw_plot(&c);
    draw_parameters(&c);
}

static void wheel(int delta)
{
    if (editor.input.depth == DEPTH_PARAMETER) {
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
    switch (rv_editor_input(&editor.input, input)) {
    case RV_EDITOR_IGNORED:
        return 0;
    case RV_EDITOR_CONSUMED:
        return 1;
    case RV_EDITOR_WHEEL:
        wheel(input->delta);
        break;
    default:
        break;
    }
    api->ui_invalidate(self);
    return 1;
}

static void opened(const struct rv_context *ctx)
{
    (void)ctx;
    memset(&editor, 0, sizeof editor);
    editor.input.depths = DEPTHS;
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
