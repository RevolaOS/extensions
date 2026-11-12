/* SPDX-License-Identifier: GPL-3.0-only */

/* The Custom EQ editor: five gain bars over a row of the selected band's
 * parameters. The wheel picks a band; Select moves into the parameters and
 * then into editing one, Menu steps back out. Every change plays at once
 * through the link main passed as the screen's argument. The hold switch
 * shows the response curve of what plays.
 *
 * Adapted from RepriseOS payload/eq_canvas.c (d640115, 93f6a45) by lgreitis. */

#include "rv_canvas.h"
#include "rv_libc.h"
#include "rv_ui.h"
#include "shared.h"

static const struct rv_api *api;
static rv_extension_handle self;

enum { DEPTH_BAND, DEPTH_PARAMETER, DEPTH_EDIT, DEPTHS };

/* Wheel order follows reading order: Type | Precut, then Gain | Frequency | Q. */
enum { PARAM_TYPE, PARAM_PRECUT, PARAM_GAIN, PARAM_FREQUENCY, PARAM_Q, PARAM_COUNT };
static const uint8_t parameters[PARAM_COUNT] = {EQ_TYPE, EQ_PRECUT, EQ_GAIN, EQ_FREQUENCY, EQ_Q};
static const char *const titles[PARAM_COUNT] = {"Type", "Precut", "Gain", "Frequency", "Q"};
static const char *const types[] = {"Off", "Peaking", "Low shelf", "High shelf"};

enum {
    BAND_LEFT = 42,
    BAND_WIDTH = 54,
    BAND_TOP = 18,
    BAND_BOTTOM = 108,
    BAND_ZERO = (BAND_TOP + BAND_BOTTOM) / 2,
    INLINE_TOP = 141, /* Type | Precut, title left and value right */
    INLINE_HEIGHT = 27,
    INLINE_COUNT = PARAM_GAIN,
    CELL_TOP = INLINE_TOP + INLINE_HEIGHT + 1, /* the cells run to the bottom of the canvas */
    RESPONSE_POINTS = 261,
    RESPONSE_TOP = 36,
    RESPONSE_BOTTOM = 180,
    RESPONSE_ZERO = (RESPONSE_TOP + RESPONSE_BOTTOM) / 2,
    TICK_TOP = 192,
    TICK_HEIGHT = 22,
};

static struct {
    struct rv_editor input;
    int band, parameter, response, dirty;
    int16_t curve[RESPONSE_POINTS];
    /* The response from +extent at the top to -extent at the bottom, tenths
     * of a decibel, doubled until the curve fits. */
    struct rv_plot plot;
    unsigned int precut;
} editor;

static const struct eq_link *link(const struct rv_context *ctx)
{
    return ctx->arg;
}

static unsigned int slot(int band, int parameter)
{
    return eq_slot((unsigned int)band, parameters[parameter]);
}

static unsigned int choice(const struct rv_context *ctx, int band, int parameter)
{
    return link(ctx)->values[slot(band, parameter)];
}

static int clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

static void save(const struct rv_context *ctx)
{
    if (editor.dirty)
        link(ctx)->save();
    editor.dirty = 0;
}

enum { LABEL_BYTES = 12 };

static const char *label(int parameter, unsigned int choice, char out[LABEL_BYTES])
{
    switch (parameter) {
    case PARAM_TYPE:
        return types[choice];
    case PARAM_PRECUT:
        if (choice == EQ_PRECUT_AUTO)
            return "Auto";
        rv_format_tenths(out, LABEL_BYTES, eq_precut_tenths(choice), 0, " dB");
        return out;
    case PARAM_GAIN:
        rv_format_tenths(out, LABEL_BYTES, eq_gain_tenths(choice), 1, " dB");
        return out;
    case PARAM_FREQUENCY:
        rv_format_hz(out, LABEL_BYTES, eq_frequencies[choice]);
        return out;
    default:
        rv_format_tenths(out, LABEL_BYTES, eq_q_tenths(choice), 0, "");
        return out;
    }
}

static void compute_response(const struct rv_context *ctx)
{
    int extent = 60;
    editor.precut = link(ctx)->response(editor.curve, RESPONSE_POINTS);
    for (unsigned int i = 0; i < RESPONSE_POINTS; i++)
        while (editor.curve[i] < -extent || editor.curve[i] > extent)
            extent *= 2;
    editor.plot = (struct rv_plot){.left = 44,
                                   .top = RESPONSE_TOP,
                                   .bottom = RESPONSE_BOTTOM,
                                   .points = RESPONSE_POINTS,
                                   .top_value = (int16_t)extent,
                                   .bottom_value = (int16_t)-extent};
}

static void draw_response(const struct rv_context *ctx, const struct rv_canvas *c)
{
    static const char *const tick_labels[] = {"20", "200", "2k", "20k Hz"};
    char value[LABEL_BYTES], high[8], low[8];
    const char *scale[] = {high, "0", low};
    unsigned int precut = choice(ctx, 0, PARAM_PRECUT);
    const char *title = "Precut";
    rv_canvas_header(c, "EQ response", NULL, 0);
    /* With Auto, the attenuation the engine found. */
    if (precut == EQ_PRECUT_AUTO) {
        title = "Auto precut";
        precut = editor.precut + 1;
    }
    label(PARAM_PRECUT, precut, value);
    int right = c->width - 8;
    int value_width = c->gfx->measure(RV_FONT_TEXT, value);
    int title_width = c->gfx->measure(RV_FONT_TEXT, title);
    rv_canvas_text(c, right - value_width - 6 - title_width, 3, title_width, 24, title, 0,
                   RV_ALIGN_LEFT, RV_CANVAS_MUTED);
    rv_canvas_text(c, right - value_width, 3, value_width, 24, value, 0, RV_ALIGN_RIGHT,
                   RV_CANVAS_TEXT);
    rv_format(high, sizeof high, "+%u", (unsigned int)editor.plot.top_value / 10);
    rv_format(low, sizeof low, "-%u", (unsigned int)editor.plot.top_value / 10);
    rv_plot_ticks(c, &editor.plot, tick_labels, 4, TICK_TOP, TICK_HEIGHT);
    rv_plot_grid(c, &editor.plot, scale, 3);
    rv_canvas_rect(c, editor.plot.left, RESPONSE_ZERO, RESPONSE_POINTS, 1, RV_CANVAS_ZERO);
    rv_plot_curve(c, &editor.plot, editor.curve, 1, RV_CANVAS_ACCENT);
}

static void draw_bands(const struct rv_context *ctx, const struct rv_canvas *c)
{
    rv_canvas_rect(c, BAND_LEFT, BAND_TOP, BAND_WIDTH * EQ_BANDS, 1, RV_CANVAS_GRID);
    rv_canvas_rect(c, BAND_LEFT, BAND_BOTTOM, BAND_WIDTH * EQ_BANDS, 1, RV_CANVAS_GRID);
    rv_canvas_rect(c, BAND_LEFT + 2 + editor.band * BAND_WIDTH, 4, BAND_WIDTH - 4,
                   INLINE_TOP - 7, RV_CANVAS_SELECTION);
    rv_canvas_rect(c, BAND_LEFT, BAND_ZERO, BAND_WIDTH * EQ_BANDS, 1, RV_CANVAS_ZERO);
    rv_canvas_text(c, 2, BAND_TOP - 9, 34, 18, "+12", 0, RV_ALIGN_CENTER, RV_CANVAS_MUTED);
    rv_canvas_text(c, 2, BAND_ZERO - 9, 34, 18, "0", 0, RV_ALIGN_CENTER, RV_CANVAS_MUTED);
    rv_canvas_text(c, 2, BAND_BOTTOM - 9, 34, 18, "-12", 0, RV_ALIGN_CENTER, RV_CANVAS_MUTED);
    for (int band = 0; band < EQ_BANDS; band++) {
        char frequency[LABEL_BYTES];
        int x = BAND_LEFT + BAND_WIDTH / 2 + band * BAND_WIDTH;
        int y = BAND_ZERO -
                eq_gain_tenths(choice(ctx, band, PARAM_GAIN)) * (BAND_BOTTOM - BAND_ZERO) / 120;
        int selected = band == editor.band;
        int off = choice(ctx, band, PARAM_TYPE) == EQ_OFF;
        uint8_t color = off        ? RV_CANVAS_BORDER
                        : selected ? RV_CANVAS_ACCENT
                                   : RV_CANVAS_INACTIVE;
        rv_canvas_rect(c, x - 8, BAND_TOP, 16, BAND_BOTTOM - BAND_TOP, RV_CANVAS_TRACK);
        rv_canvas_rect(c, x - 16, BAND_ZERO, 32, 1, RV_CANVAS_ZERO);
        int top = y < BAND_ZERO ? y : BAND_ZERO;
        int height = y < BAND_ZERO ? BAND_ZERO - y : y - BAND_ZERO;
        if (height)
            rv_canvas_rect(c, x - 8, top, 16, height, color);
        rv_canvas_rect(c, x - 12, y - 3, 24, 6, color);
        rv_canvas_text(c, x - 25, INLINE_TOP - 25, 50, 22,
                       off ? "Off"
                           : label(PARAM_FREQUENCY, choice(ctx, band, PARAM_FREQUENCY), frequency),
                       0, RV_ALIGN_CENTER, selected ? RV_CANVAS_ACCENT : RV_CANVAS_MUTED);
    }
}

static void draw_parameters(const struct rv_context *ctx, const struct rv_canvas *c)
{
    char text[PARAM_COUNT][LABEL_BYTES];
    const char *values[PARAM_COUNT];
    uint8_t states[PARAM_COUNT];
    for (int parameter = 0; parameter < PARAM_COUNT; parameter++) {
        int active = editor.input.depth != DEPTH_BAND && parameter == editor.parameter;
        values[parameter] = label(parameter, choice(ctx, editor.band, parameter), text[parameter]);
        states[parameter] = !active                              ? RV_CELL_IDLE
                            : editor.input.depth == DEPTH_EDIT ? RV_CELL_EDITING
                                                               : RV_CELL_SELECTED;
    }
    rv_canvas_fields(c, INLINE_TOP, INLINE_HEIGHT, titles, values, states, INLINE_COUNT);
    rv_canvas_cells(c, CELL_TOP, c->height - CELL_TOP, titles + INLINE_COUNT, values + INLINE_COUNT,
                    states + INLINE_COUNT, PARAM_COUNT - INLINE_COUNT);
}

static void draw(const struct rv_context *ctx, const struct rv_gfx *g, int width, int height)
{
    struct rv_canvas c;
    rv_canvas_begin(&c, g, width, height);
    if (editor.response) {
        draw_response(ctx, &c);
        return;
    }
    draw_bands(ctx, &c);
    draw_parameters(ctx, &c);
}

static void wheel(const struct rv_context *ctx, int delta)
{
    if (editor.input.depth == DEPTH_BAND) {
        editor.band = clamp(editor.band + delta, 0, EQ_BANDS - 1);
    } else if (editor.input.depth == DEPTH_PARAMETER) {
        editor.parameter = clamp(editor.parameter + delta, 0, PARAM_COUNT - 1);
    } else {
        unsigned int at = slot(editor.band, editor.parameter);
        int previous = link(ctx)->values[at];
        int value = clamp(previous + delta, 0, eq_choices[parameters[editor.parameter]] - 1);
        if (value != previous) {
            link(ctx)->change(at, (unsigned int)value);
            editor.dirty = 1;
        }
    }
}

static int input(const struct rv_context *ctx, const struct rv_input *input)
{
    if (input->kind == RV_INPUT_HOLD) {
        editor.response = input->delta;
        if (editor.response) {
            save(ctx);
            compute_response(ctx);
        }
    } else if (editor.response) {
        return 1;
    } else {
        switch (rv_editor_input(&editor.input, input)) {
        case RV_EDITOR_IGNORED:
            return 0;
        case RV_EDITOR_CONSUMED:
            return 1;
        case RV_EDITOR_WHEEL:
            wheel(ctx, input->delta);
            break;
        default:
            save(ctx);
            break;
        }
    }
    api->ui_invalidate(self);
    return 1;
}

static void opened(const struct rv_context *ctx)
{
    (void)ctx;
    memset(&editor, 0, sizeof editor);
    editor.input.depths = DEPTHS;
}

static void closed(const struct rv_context *ctx)
{
    save(ctx);
}

static const struct rv_screen screens[] = {
    RV_SCREEN_CANVAS("editor", "Custom EQ", draw, input, .transition = RV_TRANSITION_FROM_PANE,
                     .opened = opened, .closed = closed),
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
