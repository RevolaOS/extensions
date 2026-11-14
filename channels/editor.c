/* SPDX-License-Identifier: GPL-3.0-only */

/* The Channels screen: the stereo image as a half disc, Left and Right at
 * its arms, with a lobe as wide as the chosen mode and width leave it. The
 * wheel picks Mode or Width, Select edits it, holding Select puts it back to
 * its default and Menu steps back out. Every change plays at once. */

#include "labels.h"
#include "rv_canvas.h"
#include "rv_libc.h"
#include "rv_ui.h"
#include "shared.h"

static const struct rv_api *api;
static rv_extension_handle self;

enum { DEPTH_PARAMETER, DEPTH_EDIT, DEPTHS };

static const char *const titles[PARAM_COUNT] = {"Mode", "Width"};
static const uint8_t keys[PARAM_COUNT] = {KEY_MODE, KEY_WIDTH};
static const uint8_t counts[PARAM_COUNT] = {CHANNELS_MODES, WIDTH_LABELS};
static const uint8_t defaults[PARAM_COUNT] = {MODE_DEFAULT, WIDTH_DEFAULT};

enum {
    CENTRE_Y = 154, /* the listener; the baseline of the half disc */
    RADIUS = 110,
    TAG_RADIUS = RADIUS + 14, /* the L and R tags sit just outside the arc */
    TAG_SIZE = 18,
    CELL_TOP = 169, /* the cells run to the bottom of the canvas */
};

static struct {
    struct rv_editor input;
    int parameter;
} editor;

static int clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

static int value(int parameter)
{
    return clamp(api->setting_int(self, keys[parameter]), 0, counts[parameter] - 1);
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
static void tag(const struct rv_canvas *c, int x, int y, const char *text)
{
    rv_canvas_text(c, x - TAG_SIZE / 2, y - TAG_SIZE / 2, TAG_SIZE, TAG_SIZE, text, 0,
                   RV_ALIGN_CENTER, RV_CANVAS_ACCENT);
}

/* A curve's two sides on one row, each a run from where it was on the row
 * below to where it is now, so a flat stretch stays joined. */
static void sides(const struct rv_canvas *c, int y, int from, int to, int thickness,
                  uint8_t color)
{
    int centre = c->width / 2;
    int near = from < to ? from : to;
    int far = from > to ? from : to;
    rv_canvas_rect(c, centre - far - thickness + 1, y, far - near + thickness, 1, color);
    rv_canvas_rect(c, centre + near, y, far - near + thickness, 1, color);
}

/* The half disc row by row: the arc and the 45-degree arms of the Stereo frame
 * in the grid colour, then the lobe, whose edge a hard-panned source sits on,
 * at tan(angle) = width / 100 from the centre line, held inside the arc. */
static void draw_image(const struct rv_canvas *c, const struct image *image)
{
    int centre = c->width / 2;
    rv_canvas_header(c, "Stereo image", NULL, 0);
    int d_below = RADIUS, e_below = 0;
    for (int h = 1; h <= RADIUS; h++) {
        int y = CENTRE_Y - h;
        int d = isqrt(RADIUS * RADIUS - h * h);
        sides(c, y, d_below, d, 1, RV_CANVAS_GRID);
        if (image->lobe) {
            int e = h * image->width / 100;
            if (e > d)
                e = d;
            rv_canvas_rect(c, centre - e, y, 2 * e + 1, 1, RV_CANVAS_SELECTION);
            if (h <= d)
                sides(c, y, h, h, 1, RV_CANVAS_GRID);
            sides(c, y, h == 1 ? e : e_below, e, 2, RV_CANVAS_ACCENT);
            e_below = e;
        } else if (h <= d) {
            sides(c, y, h, h, 1, RV_CANVAS_GRID);
        }
        d_below = d;
    }
    rv_canvas_rect(c, centre - RADIUS, CENTRE_Y, 2 * RADIUS + 1, 1, RV_CANVAS_GRID);
    if (image->baseline)
        rv_canvas_rect(c, centre - RADIUS, CENTRE_Y - 1, 2 * RADIUS + 1, 2, RV_CANVAS_ACCENT);

    /* The arm tags follow the lobe's edge out past the arc; at 0 % they would
     * meet, so they keep a little apart. */
    if (image->left_tag) {
        int w = image->width;
        int h = isqrt(TAG_RADIUS * TAG_RADIUS * 10000 / (10000 + w * w));
        int x = h * w / 100;
        if (x < TAG_SIZE / 2)
            x = TAG_SIZE / 2;
        tag(c, centre - x, CENTRE_Y - h, image->left_tag);
        tag(c, centre + x, CENTRE_Y - h, image->right_tag);
    }
    if (image->top_tag)
        tag(c, centre, CENTRE_Y - TAG_RADIUS, image->top_tag);
}

/* The Width cell reads inactive while another mode than Custom plays it no part. */
static void draw_parameters(const struct rv_canvas *c, int custom)
{
    char text[PARAM_COUNT][LABEL_BYTES];
    const char *values[PARAM_COUNT];
    uint8_t states[PARAM_COUNT];
    for (int parameter = 0; parameter < PARAM_COUNT; parameter++) {
        values[parameter] = channels_label(parameter, value(parameter), text[parameter]);
        states[parameter] = parameter != editor.parameter            ? RV_CELL_IDLE
                            : editor.input.depth == DEPTH_EDIT ? RV_CELL_EDITING
                                                               : RV_CELL_SELECTED;
        if (!custom && parameter != PARAM_MODE)
            states[parameter] |= RV_CELL_MUTED;
    }
    rv_canvas_cells(c, CELL_TOP, c->height - CELL_TOP, titles, values, states, PARAM_COUNT);
}

static void draw(const struct rv_context *ctx, const struct rv_gfx *g, int width, int height)
{
    struct rv_canvas c;
    (void)ctx;
    rv_canvas_begin(&c, g, width, height);
    int mode = value(PARAM_MODE);
    struct image image = image_of(mode, value(PARAM_WIDTH));
    draw_image(&c, &image);
    draw_parameters(&c, mode == CHANNELS_CUSTOM);
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
}

/* A held Select puts the highlighted value back to its default. */
static void reset(void)
{
    if (value(editor.parameter) == defaults[editor.parameter])
        return;
    /* Main republishes from setting_changed, so the change plays at once. */
    api->setting_set_int(self, keys[editor.parameter], defaults[editor.parameter]);
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
