/* SPDX-License-Identifier: GPL-3.0-only */

/* The Custom EQ editor: five gain bars over a row of the selected band's
 * parameters. The wheel picks a band; Select moves into the parameters and
 * then into editing one, Menu steps back out. Every change plays at once
 * through the link main passed as the screen's argument. The hold switch
 * shows the response curve of what plays.
 *
 * Adapted from RepriseOS payload/eq_canvas.c (d640115, 93f6a45) by lgreitis. */

#include "rv_libc.h"
#include "rv_ui.h"
#include "shared.h"

static const struct rv_api *api;
static rv_extension_handle self;

enum { MODE_BAND, MODE_PARAMETER, MODE_EDIT };

/* Wheel order follows reading order: Type | Precut, then Gain | Frequency | Q. */
enum { PARAM_TYPE, PARAM_PRECUT, PARAM_GAIN, PARAM_FREQUENCY, PARAM_Q, PARAM_COUNT };
static const uint8_t parameters[PARAM_COUNT] = {EQ_TYPE, EQ_PRECUT, EQ_GAIN, EQ_FREQUENCY, EQ_Q};
static const char *const titles[PARAM_COUNT] = {"Type", "Precut", "Gain", "Frequency", "Q"};
static const char *const types[] = {"Off", "Peaking", "Low shelf", "High shelf"};

enum {
    CANVAS_WIDTH = 320,
    CANVAS_HEIGHT = 217,
    BAND_LEFT = 42,
    BAND_WIDTH = 54,
    BAND_TOP = 18,
    BAND_BOTTOM = 108,
    BAND_ZERO = (BAND_TOP + BAND_BOTTOM) / 2,
    TYPE_TOP = 141,
    TYPE_HEIGHT = 27,
    PARAM_TOP = TYPE_TOP + TYPE_HEIGHT + 1,
    PARAM_HEIGHT = CANVAS_HEIGHT - PARAM_TOP,
    RESPONSE_LEFT = 44,
    RESPONSE_POINTS = 261,
    RESPONSE_TOP = 36,
    RESPONSE_ZERO = 108,
    RESPONSE_BOTTOM = 180,
};

static struct {
    int band, parameter, mode, response, dirty;
    int16_t curve[RESPONSE_POINTS];
    int extent; /* tenths of a decibel at the top and bottom of the curve */
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

/* "-0.5 dB", "+12.0 dB" with `plus`, "0.7" with an empty unit. */
static void tenths_label(char *out, int tenths, int plus, const char *unit)
{
    const char *sign = tenths < 0 ? "-" : plus && tenths ? "+" : "";
    unsigned int magnitude = (unsigned int)(tenths < 0 ? -tenths : tenths);
    rv_format(out, LABEL_BYTES, "%s%u.%u%s", sign, magnitude / 10, magnitude % 10, unit);
}

/* "60 Hz", "1k Hz", "3.15k Hz". */
static void frequency_label(char *out, unsigned int hz)
{
    unsigned int fraction = hz % 1000;
    if (hz < 1000) {
        rv_format(out, LABEL_BYTES, "%u Hz", hz);
        return;
    }
    while (fraction && fraction % 10 == 0)
        fraction /= 10;
    if (fraction)
        rv_format(out, LABEL_BYTES, "%u.%uk Hz", hz / 1000, fraction);
    else
        rv_format(out, LABEL_BYTES, "%uk Hz", hz / 1000);
}

static const char *label(int parameter, unsigned int choice, char out[LABEL_BYTES])
{
    switch (parameter) {
    case PARAM_TYPE:
        return types[choice];
    case PARAM_PRECUT:
        if (choice == EQ_PRECUT_AUTO)
            return "Auto";
        tenths_label(out, eq_precut_tenths(choice), 0, " dB");
        return out;
    case PARAM_GAIN:
        tenths_label(out, eq_gain_tenths(choice), 1, " dB");
        return out;
    case PARAM_FREQUENCY:
        frequency_label(out, eq_frequencies[choice]);
        return out;
    default:
        tenths_label(out, eq_q_tenths(choice), 0, "");
        return out;
    }
}

enum {
    COLOR_BACKGROUND,
    COLOR_CURVE, /* the accent as text: the curve, the bars, the selected band's label */
    COLOR_FILL,  /* the accent as a surface: the parameter being edited */
    COLOR_TEXT,
    COLOR_ON_FILL,
    COLOR_ROLES,
    /* Derived from the roles above. */
    COLOR_MUTED = COLOR_ROLES,
    COLOR_TRACK,
    COLOR_INACTIVE,
    COLOR_GRID,
    COLOR_ZERO,
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
    {COLOR_TRACK, 22}, {COLOR_GRID, 28},      {COLOR_BORDER, 46},
    {COLOR_ZERO, 89},  {COLOR_INACTIVE, 130}, {COLOR_MUTED, 185},
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

static void compute_response(const struct rv_context *ctx)
{
    editor.precut = link(ctx)->response(editor.curve, RESPONSE_POINTS);
    editor.extent = 60;
    for (unsigned int i = 0; i < RESPONSE_POINTS; i++)
        while (editor.curve[i] < -editor.extent || editor.curve[i] > editor.extent)
            editor.extent *= 2;
}

static int response_y(int value)
{
    int half = editor.extent / 2;
    return RESPONSE_ZERO - (value * (RESPONSE_ZERO - RESPONSE_TOP) + (value < 0 ? -half : half)) /
                               editor.extent;
}

static void draw_response(const struct rv_context *ctx, const struct rv_gfx *g)
{
    static const int ticks[] = {44, 131, 217, 304};
    static const char *const tick_labels[] = {"20", "200", "2k", "20k Hz"};
    char value[LABEL_BYTES], scale[8];
    unsigned int precut = choice(ctx, 0, PARAM_PRECUT);
    const char *title = "Precut";
    text_in(g, 8, 3, 140, 24, "EQ response", 0, RV_ALIGN_LEFT, COLOR_TEXT);
    /* With Auto, the attenuation the engine found. */
    if (precut == EQ_PRECUT_AUTO) {
        title = "Auto precut";
        precut = editor.precut + 1;
    }
    label(PARAM_PRECUT, precut, value);
    int value_width = g->measure(RV_FONT_TEXT, value);
    int title_width = g->measure(RV_FONT_TEXT, title);
    text_in(g, 312 - value_width - 6 - title_width, 3, title_width, 24, title, 0, RV_ALIGN_LEFT,
            COLOR_MUTED);
    text_in(g, 312 - value_width, 3, value_width, 24, value, 0, RV_ALIGN_RIGHT, COLOR_TEXT);
    for (unsigned int i = 0; i < 4; i++) {
        rect(g, ticks[i], RESPONSE_TOP, 1, RESPONSE_BOTTOM - RESPONSE_TOP + 1, COLOR_GRID);
        text_in(g, i == 3 ? 271 : ticks[i] - 21, 192, i == 3 ? 49 : 42, 22, tick_labels[i], 0,
                RV_ALIGN_CENTER, COLOR_MUTED);
    }
    rect(g, RESPONSE_LEFT, RESPONSE_TOP, RESPONSE_POINTS, 1, COLOR_GRID);
    rect(g, RESPONSE_LEFT, RESPONSE_BOTTOM, RESPONSE_POINTS, 1, COLOR_GRID);
    rect(g, RESPONSE_LEFT, RESPONSE_ZERO, RESPONSE_POINTS, 1, COLOR_ZERO);
    rv_format(scale, sizeof scale, "+%u", (unsigned int)editor.extent / 10);
    text_in(g, 0, RESPONSE_TOP - 9, 38, 18, scale, 0, RV_ALIGN_CENTER, COLOR_MUTED);
    text_in(g, 0, RESPONSE_ZERO - 9, 38, 18, "0", 0, RV_ALIGN_CENTER, COLOR_MUTED);
    scale[0] = '-';
    text_in(g, 0, RESPONSE_BOTTOM - 9, 38, 18, scale, 0, RV_ALIGN_CENTER, COLOR_MUTED);
    int previous = response_y(editor.curve[0]);
    for (unsigned int i = 0; i < RESPONSE_POINTS; i++) {
        int y = response_y(editor.curve[i]);
        int top = y < previous ? y : previous;
        int bottom = y > previous ? y : previous;
        rect(g, RESPONSE_LEFT + (int)i, top, 1, bottom - top + 1, COLOR_CURVE);
        previous = y;
    }
}

static void draw_bands(const struct rv_context *ctx, const struct rv_gfx *g)
{
    rect(g, BAND_LEFT, BAND_TOP, BAND_WIDTH * EQ_BANDS, 1, COLOR_GRID);
    rect(g, BAND_LEFT, BAND_BOTTOM, BAND_WIDTH * EQ_BANDS, 1, COLOR_GRID);
    rect(g, BAND_LEFT + 2 + editor.band * BAND_WIDTH, 4, BAND_WIDTH - 4, TYPE_TOP - 7,
         COLOR_SELECTION);
    rect(g, BAND_LEFT, BAND_ZERO, BAND_WIDTH * EQ_BANDS, 1, COLOR_ZERO);
    text_in(g, 2, BAND_TOP - 9, 34, 18, "+12", 0, RV_ALIGN_CENTER, COLOR_MUTED);
    text_in(g, 2, BAND_ZERO - 9, 34, 18, "0", 0, RV_ALIGN_CENTER, COLOR_MUTED);
    text_in(g, 2, BAND_BOTTOM - 9, 34, 18, "-12", 0, RV_ALIGN_CENTER, COLOR_MUTED);
    for (int band = 0; band < EQ_BANDS; band++) {
        char frequency[LABEL_BYTES];
        int x = BAND_LEFT + BAND_WIDTH / 2 + band * BAND_WIDTH;
        int y = BAND_ZERO -
                eq_gain_tenths(choice(ctx, band, PARAM_GAIN)) * (BAND_BOTTOM - BAND_ZERO) / 120;
        int selected = band == editor.band;
        int off = choice(ctx, band, PARAM_TYPE) == EQ_OFF;
        int color = off ? COLOR_BORDER : selected ? COLOR_CURVE : COLOR_INACTIVE;
        rect(g, x - 8, BAND_TOP, 16, BAND_BOTTOM - BAND_TOP, COLOR_TRACK);
        rect(g, x - 16, BAND_ZERO, 32, 1, COLOR_ZERO);
        int top = y < BAND_ZERO ? y : BAND_ZERO;
        int height = y < BAND_ZERO ? BAND_ZERO - y : y - BAND_ZERO;
        if (height)
            rect(g, x - 8, top, 16, height, color);
        rect(g, x - 12, y - 3, 24, 6, color);
        text_in(g, x - 25, TYPE_TOP - 25, 50, 22,
                off ? "Off" : label(PARAM_FREQUENCY, choice(ctx, band, PARAM_FREQUENCY), frequency),
                0, RV_ALIGN_CENTER, selected ? COLOR_CURVE : COLOR_MUTED);
    }
}

static void draw_parameters(const struct rv_context *ctx, const struct rv_gfx *g, int width)
{
    rect(g, 0, TYPE_TOP - 1, width, 1, COLOR_BORDER);
    rect(g, 0, PARAM_TOP - 1, width, 1, COLOR_BORDER);
    rect(g, CANVAS_WIDTH / 2, TYPE_TOP, 1, TYPE_HEIGHT, COLOR_BORDER);
    rect(g, CANVAS_WIDTH / 3, PARAM_TOP, 1, PARAM_HEIGHT, COLOR_BORDER);
    rect(g, 2 * CANVAS_WIDTH / 3, PARAM_TOP, 1, PARAM_HEIGHT, COLOR_BORDER);
    for (int parameter = 0; parameter < PARAM_COUNT; parameter++) {
        char value[LABEL_BYTES];
        int inline_row = parameter < PARAM_GAIN;
        int left = inline_row ? parameter * CANVAS_WIDTH / 2
                              : (parameter - PARAM_GAIN) * CANVAS_WIDTH / 3;
        int width = inline_row ? CANVAS_WIDTH / 2
                               : (parameter - PARAM_GAIN + 1) * CANVAS_WIDTH / 3 - left;
        int top = inline_row ? TYPE_TOP : PARAM_TOP;
        int height = inline_row ? TYPE_HEIGHT : PARAM_HEIGHT;
        int active = editor.mode != MODE_BAND && parameter == editor.parameter;
        int editing = active && editor.mode == MODE_EDIT;
        if (active) {
            rect(g, left, top, width, height, editing ? COLOR_FILL : COLOR_CURVE);
            if (!editing)
                rect(g, left + 2, top + 2, width - 4, height - 4, COLOR_SELECTION);
        }
        int title_color = editing ? COLOR_ON_FILL : COLOR_MUTED;
        int value_color = editing ? COLOR_ON_FILL : COLOR_TEXT;
        const char *text = label(parameter, choice(ctx, editor.band, parameter), value);
        if (inline_row) {
            text_in(g, left + 12, TYPE_TOP, width - 24, TYPE_HEIGHT, titles[parameter], 0,
                    RV_ALIGN_LEFT, title_color);
            text_in(g, left + 12, TYPE_TOP, width - 24, TYPE_HEIGHT, text, 0, RV_ALIGN_RIGHT,
                    value_color);
        } else {
            text_in(g, left, PARAM_TOP + 5, width, 16, titles[parameter], 0, RV_ALIGN_CENTER,
                    title_color);
            text_in(g, left, PARAM_TOP + 21, width, 22, text, 1, RV_ALIGN_CENTER, value_color);
        }
    }
}

static void draw(const struct rv_context *ctx, const struct rv_gfx *g, int width, int height)
{
    load_colors(g);
    rect(g, 0, 0, width, height, COLOR_BACKGROUND);
    if (editor.response) {
        draw_response(ctx, g);
        return;
    }
    draw_bands(ctx, g);
    draw_parameters(ctx, g, width);
}

static void wheel(const struct rv_context *ctx, int delta)
{
    if (editor.mode == MODE_BAND) {
        editor.band = clamp(editor.band + delta, 0, EQ_BANDS - 1);
    } else if (editor.mode == MODE_PARAMETER) {
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
    } else if (input->kind == RV_INPUT_WHEEL) {
        wheel(ctx, input->delta);
    } else if (input->button == RV_BUTTON_CENTER) {
        if (input->kind == RV_INPUT_RELEASE)
            return 1;
        save(ctx);
        editor.mode = editor.mode == MODE_PARAMETER ? MODE_EDIT : MODE_PARAMETER;
    } else if (input->button == RV_BUTTON_MENU) {
        if (editor.mode == MODE_BAND)
            return 0; /* the system pops the screen */
        if (input->kind == RV_INPUT_RELEASE)
            return 1;
        save(ctx);
        editor.mode--;
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
