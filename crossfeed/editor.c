/* SPDX-License-Identifier: GPL-3.0-only */

/* The Crossfeed screen: what each ear hears, as two curves over the mode and
 * the five values of the Custom cross path. One curve is the ear's own
 * channel, the other the channel fed across; in Custom the first is flat at
 * the direct gain and the second a shelf at the cross gain in the bass and the
 * attenuation above the cutoff, both lower by the precut. The wheel picks a
 * value, Select edits it, holding Select puts it back to its default and Menu
 * steps back out. Every change plays at once. */

#include "labels.h"
#include "rv_canvas.h"
#include "rv_libc.h"
#include "rv_ui.h"
#include "shared.h"

static const struct rv_api *api;
static rv_extension_handle self;

enum { DEPTH_PARAMETER, DEPTH_EDIT, DEPTHS };

static const char *const titles[PARAM_COUNT] = {"Mode",  "Precut", "Direct",
                                                "Cross", "Highs",  "Cutoff"};
static const uint8_t keys[PARAM_COUNT] = {KEY_MODE,  KEY_PRECUT, KEY_DIRECT,
                                          KEY_CROSS, KEY_HF,     KEY_CUTOFF};
static const uint8_t counts[PARAM_COUNT] = {MODE_LABELS,  PRECUT_LABELS, DIRECT_LABELS,
                                            CROSS_LABELS, HF_LABELS,     CUTOFF_LABELS};
static const uint8_t defaults[PARAM_COUNT] = {MODE_DEFAULT,  PRECUT_DEFAULT, DIRECT_DEFAULT,
                                              CROSS_DEFAULT, HF_DEFAULT,     CUTOFF_DEFAULT};

enum {
    PLOT_POINTS = 261,
    TICK_TOP = 114,
    TICK_HEIGHT = 22,
    INLINE_TOP = 141, /* Mode | Precut, title left and value right */
    INLINE_HEIGHT = 27,
    INLINE_COUNT = PARAM_DIRECT,
    CELL_TOP = INLINE_TOP + INLINE_HEIGHT + 1, /* the cells run to the bottom of the canvas */
    CELL_COUNT = PARAM_COUNT - INLINE_COUNT,
};

/* 0 dB at the top, -24 dB at the bottom: 3 px per decibel. */
static const struct rv_plot plot = {
    .left = 44, .top = 36, .bottom = 108, .points = PLOT_POINTS, .bottom_value = -240,
};

static struct {
    struct rv_editor input;
    int parameter;
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

static void compute_curve(void)
{
    struct crossfeed_set set = {
        .mode = (uint8_t)value(PARAM_MODE),
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

/* The curve the highlighted value shapes draws in the accent, over the other:
 * the ear's own channel for Direct and while Off, the one fed across otherwise.
 * Off has no cross path, so that curve and its swatch stay inactive. */
static void draw_plot(const struct rv_canvas *c)
{
    static const char *const ticks[] = {"20", "200", "2k", "20k Hz"};
    static const char *const scale[] = {"0", "-12", "-24"};
    int off = value(PARAM_MODE) == CROSSFEED_OFF;
    int same_side = off || editor.parameter == PARAM_DIRECT;
    uint8_t same_color = same_side ? RV_CANVAS_ACCENT : RV_CANVAS_INACTIVE;
    uint8_t other_color = same_side ? RV_CANVAS_INACTIVE : RV_CANVAS_ACCENT;
    const struct rv_canvas_legend legend[] = {{"Same side", same_color},
                                              {"Other side", other_color}};

    rv_plot_ticks(c, &plot, ticks, 4, TICK_TOP, TICK_HEIGHT);
    rv_plot_grid(c, &plot, scale, 3);
    rv_canvas_header(c, "Each ear hears", legend, 2);
    if (same_side) {
        if (!off)
            rv_plot_curve(c, &plot, editor.other, 2, other_color);
        rv_plot_curve(c, &plot, editor.same, 2, same_color);
    } else {
        rv_plot_curve(c, &plot, editor.same, 2, same_color);
        rv_plot_curve(c, &plot, editor.other, 2, other_color);
    }
}

/* The Custom values read inactive while another mode plays; they still edit. */
static void draw_parameters(const struct rv_canvas *c)
{
    char text[PARAM_COUNT][LABEL_BYTES];
    const char *values[PARAM_COUNT];
    uint8_t states[PARAM_COUNT];
    int custom = value(PARAM_MODE) == CROSSFEED_CUSTOM;
    for (int parameter = 0; parameter < PARAM_COUNT; parameter++) {
        values[parameter] = crossfeed_label(parameter, value(parameter), text[parameter]);
        states[parameter] = parameter != editor.parameter            ? RV_CELL_IDLE
                            : editor.input.depth == DEPTH_EDIT ? RV_CELL_EDITING
                                                               : RV_CELL_SELECTED;
        if (!custom && parameter != PARAM_MODE)
            states[parameter] |= RV_CELL_MUTED;
    }
    rv_canvas_fields(c, INLINE_TOP, INLINE_HEIGHT, titles, values, states, INLINE_COUNT);
    rv_canvas_cells(c, CELL_TOP, c->height - CELL_TOP, titles + INLINE_COUNT, values + INLINE_COUNT,
                    states + INLINE_COUNT, CELL_COUNT);
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
    compute_curve();
}

/* A held Select puts the highlighted value back to its default. */
static void reset(void)
{
    if (value(editor.parameter) == defaults[editor.parameter])
        return;
    /* Main republishes from setting_changed, so the change plays at once. */
    api->setting_set_int(self, keys[editor.parameter], defaults[editor.parameter]);
    compute_curve();
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
    case RV_EDITOR_RESET:
        reset();
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
