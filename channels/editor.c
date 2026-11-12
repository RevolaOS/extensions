/* SPDX-License-Identifier: GPL-3.0-only */

/* The Channels screen: the stereo image as a half disc, Left and Right at
 * its arms, with a lobe as wide as the chosen mode and width leave it. The
 * wheel picks Mode or Width, Select edits it and Menu steps back out. Every
 * change plays at once. */

#include "labels.h"
#include "rv_libc.h"
#include "rv_ui.h"
#include "shared.h"

static const struct rv_api *api;
static rv_extension_handle self;

enum { MODE_PARAMETER, MODE_EDIT };

enum { PARAM_MODE, PARAM_WIDTH, PARAM_COUNT };
static const char *const titles[PARAM_COUNT] = {"Mode", "Width"};
static const uint8_t keys[PARAM_COUNT] = {KEY_MODE, KEY_WIDTH};
static const uint8_t counts[PARAM_COUNT] = {CHANNELS_MODES, WIDTH_LABELS};
static const char *const *const labels[PARAM_COUNT] = {mode_labels, width_labels};

enum {
    CANVAS_WIDTH = 320,
    CANVAS_HEIGHT = 217,
    HEADER_TOP = 3,
    HEADER_HEIGHT = 24,
    CENTRE_X = 160,
    CENTRE_Y = 154, /* the listener; the baseline of the half disc */
    RADIUS = 110,
    TAG_RADIUS = RADIUS + 14, /* the L and R tags sit just outside the arc */
    TAG_SIZE = 18,
    PARAM_TOP = 169,
    PARAM_HEIGHT = CANVAS_HEIGHT - PARAM_TOP,
    CELL_WIDTH = CANVAS_WIDTH / PARAM_COUNT,
};

static struct {
    int parameter, mode;
    int menu_taken; /* the Menu press left edit mode; its release is not the system's */
} editor;

static int clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

static int value(int parameter)
{
    return clamp(api->setting_int(self, keys[parameter]), 0, counts[parameter] - 1);
}

enum {
    COLOR_BACKGROUND,
    COLOR_CURVE, /* the accent as text: the lobe's edges and the tags */
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

static int isqrt(int n)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= n)
        r++;
    return r;
}

/* The image the mode draws: a lobe of this width in percent, or none, and the
 * channel each arm of it plays. Mono, Left Only and Right Only are a width of
 * 0; Karaoke is pure side, drawn along the baseline instead. */
struct image {
    int lobe;      /* a lobe is drawn */
    int width;     /* percent */
    int baseline;  /* Karaoke's line along the baseline */
    const char *left_tag, *right_tag; /* at the lobe's arms, NULL for none */
    const char *top_tag;              /* above a centre line, NULL for none */
};

static struct image image_of(int mode, int width_index)
{
    struct image image = {.lobe = 1, .width = 100, .left_tag = "L", .right_tag = "R"};
    switch (mode) {
    case CHANNELS_MONO:
        image.width = 0;
        image.left_tag = image.right_tag = NULL;
        break;
    case CHANNELS_LEFT:
    case CHANNELS_RIGHT:
        image.width = 0;
        image.left_tag = image.right_tag = NULL;
        image.top_tag = mode == CHANNELS_LEFT ? "L" : "R";
        break;
    case CHANNELS_KARAOKE:
        image.lobe = 0;
        image.baseline = 1;
        image.left_tag = image.right_tag = NULL;
        break;
    case CHANNELS_SWAP:
        image.left_tag = "R";
        image.right_tag = "L";
        break;
    case CHANNELS_CUSTOM:
        image.width = (int)width_percent(width_index);
        break;
    default:
        break;
    }
    return image;
}

/* A tag centred on (x, y). */
static void tag(const struct rv_gfx *g, int x, int y, const char *text)
{
    text_in(g, x - TAG_SIZE / 2, y - TAG_SIZE / 2, TAG_SIZE, TAG_SIZE, text, 0, RV_ALIGN_CENTER,
            COLOR_CURVE);
}

/* A curve's two sides on one row, each a run from where it was on the row
 * below to where it is now, so a flat stretch stays joined. */
static void sides(const struct rv_gfx *g, int y, int from, int to, int thickness, int color)
{
    int near = from < to ? from : to;
    int far = from > to ? from : to;
    rect(g, CENTRE_X - far - thickness + 1, y, far - near + thickness, 1, color);
    rect(g, CENTRE_X + near, y, far - near + thickness, 1, color);
}

/* The half disc row by row: the arc and the 45-degree arms of the Stereo frame
 * in the grid colour, then the lobe, whose edge a hard-panned source sits on,
 * at tan(angle) = width / 100 from the centre line, held inside the arc. */
static void draw_image(const struct rv_gfx *g, const struct image *image)
{
    text_in(g, 8, HEADER_TOP, 200, HEADER_HEIGHT, "Stereo image", 0, RV_ALIGN_LEFT, COLOR_TEXT);
    int d_below = RADIUS, e_below = 0;
    for (int h = 1; h <= RADIUS; h++) {
        int y = CENTRE_Y - h;
        int d = isqrt(RADIUS * RADIUS - h * h);
        sides(g, y, d_below, d, 1, COLOR_GRID);
        if (image->lobe) {
            int e = h * image->width / 100;
            if (e > d)
                e = d;
            rect(g, CENTRE_X - e, y, 2 * e + 1, 1, COLOR_SELECTION);
            if (h <= d)
                sides(g, y, h, h, 1, COLOR_GRID);
            sides(g, y, h == 1 ? e : e_below, e, 2, COLOR_CURVE);
            e_below = e;
        } else if (h <= d) {
            sides(g, y, h, h, 1, COLOR_GRID);
        }
        d_below = d;
    }
    rect(g, CENTRE_X - RADIUS, CENTRE_Y, 2 * RADIUS + 1, 1, COLOR_GRID);
    if (image->baseline)
        rect(g, CENTRE_X - RADIUS, CENTRE_Y - 1, 2 * RADIUS + 1, 2, COLOR_CURVE);

    /* The arm tags follow the lobe's edge out past the arc; at 0 % they would
     * meet, so they keep a little apart. */
    if (image->left_tag) {
        int w = image->width;
        int h = isqrt(TAG_RADIUS * TAG_RADIUS * 10000 / (10000 + w * w));
        int x = h * w / 100;
        if (x < TAG_SIZE / 2)
            x = TAG_SIZE / 2;
        tag(g, CENTRE_X - x, CENTRE_Y - h, image->left_tag);
        tag(g, CENTRE_X + x, CENTRE_Y - h, image->right_tag);
    }
    if (image->top_tag)
        tag(g, CENTRE_X, CENTRE_Y - TAG_RADIUS, image->top_tag);
}

/* The Width cell is muted while another mode than Custom plays it no part. */
static void draw_parameters(const struct rv_gfx *g, int width, int custom)
{
    rect(g, 0, PARAM_TOP - 1, width, 1, COLOR_BORDER);
    for (int parameter = 1; parameter < PARAM_COUNT; parameter++)
        rect(g, parameter * CELL_WIDTH, PARAM_TOP, 1, PARAM_HEIGHT, COLOR_BORDER);
    for (int parameter = 0; parameter < PARAM_COUNT; parameter++) {
        int left = parameter * CELL_WIDTH;
        int active = parameter == editor.parameter;
        int editing = active && editor.mode == MODE_EDIT;
        int muted = parameter == PARAM_WIDTH && !custom;
        if (active) {
            rect(g, left, PARAM_TOP, CELL_WIDTH, PARAM_HEIGHT, editing ? COLOR_FILL : COLOR_CURVE);
            if (!editing)
                rect(g, left + 2, PARAM_TOP + 2, CELL_WIDTH - 4, PARAM_HEIGHT - 4,
                     COLOR_SELECTION);
        }
        text_in(g, left, PARAM_TOP + 5, CELL_WIDTH, 16, titles[parameter], 0, RV_ALIGN_CENTER,
                editing ? COLOR_ON_FILL : muted ? COLOR_INACTIVE : COLOR_MUTED);
        text_in(g, left, PARAM_TOP + 21, CELL_WIDTH, 22, labels[parameter][value(parameter)], 1,
                RV_ALIGN_CENTER, editing ? COLOR_ON_FILL : muted ? COLOR_INACTIVE : COLOR_TEXT);
    }
}

static void draw(const struct rv_context *ctx, const struct rv_gfx *g, int width, int height)
{
    (void)ctx;
    load_colors(g);
    rect(g, 0, 0, width, height, COLOR_BACKGROUND);
    int mode = value(PARAM_MODE);
    struct image image = image_of(mode, value(PARAM_WIDTH));
    draw_image(g, &image);
    draw_parameters(g, width, mode == CHANNELS_CUSTOM);
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
}

static const struct rv_screen screens[] = {
    RV_SCREEN_CANVAS("editor", "Channels", draw, input,
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
