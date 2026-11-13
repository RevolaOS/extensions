/* SPDX-License-Identifier: GPL-3.0-only */

/* The grid canvas: covers from Cover Flow's artwork session downscaled into a
 * cache of the visible cells, the selected one lifted over its neighbours, a
 * caption strip on the Now Playing status bar's art, the stock scrollbar while
 * the grid scrolls, and Center opening the album's song list. The cache and
 * the session live from the first draw to the close, or until the grid has
 * sat a second under an album's songs. */

#include "geometry.h"
#include "scale.h"
#include "shared.h"
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_canvas.h"
#include "rv_libc.h"
#include "rv_ui.h"
#include <stddef.h>

enum {
    CACHE_SLOTS = GRID_VISIBLE_COVERS,
    PLACEHOLDER = CACHE_SLOTS, /* Cover Flow's no-artwork cover, after the album slots */
    /* Downscales per draw; covers loaded beyond it follow with the next. */
    SCALES_PER_DRAW = 4,
    /* Rows of the lifted cover filtered per blit. */
    LIFT_STRIP = 8,
    /* Pixels the lifted cover's edge and shadow reach beyond it. */
    DECOR = GRID_EDGE + GRID_SHADOW + 1,
    /* The hairline inside each cover's edge: the text colour at 15 %, so a cover
     * stays distinct from a background of its own tone. */
    HAIRLINE_ALPHA = 38,
    /* An empty tile: the background 11 % toward the text. */
    TILE_WEIGHT = 28,
    CAPTION_INSET = 8,
    /* The bar's art is this tall; the grid shows GRID_CAPTION of it and the
     * rest falls below the screen. The caption is centred on the whole of it,
     * as the stock bar's title is. */
    CAPTION_ART_ROWS = 24,
    SCROLLBAR_HOLD_MS = 1500,
    SCROLLBAR_FADE_MS = 500,
    SCROLLBAR_TICK_MS = 50,
    MARQUEE_TICK_MS = 40,
    MARQUEE_HOLD_TICKS = 25,
    MARQUEE_GAP = 40,
    IDLE_MS = 1000,
    /* After the session starts or the grid scrolls, cells still showing the
     * placeholder are checked this often, this many times, for a cover the
     * session holds without having reported it. */
    POLL_MS = 100,
    POLL_TICKS = 30,
};

static const uint8_t shadow_alpha[GRID_SHADOW] = {72, 38, 14};
#define SHADOW 0x000000u
#define CAPTION_INK 0xffffffffu /* the status bar's title on its black art */

/* A downscaled cover as a BMap, keyed by artwork id (0: empty); the slots are
 * replaced least recently drawn first. */
struct cover_slot {
    uint32_t artwork, used;
    uint32_t bmap[RV_CANVAS_BMAP_BYTES(GRID_COVER, GRID_COVER) / 4];
};

static const struct rv_api *api;
static rv_extension_handle self;

static struct {
    struct grid_link *link;
    struct cover_slot *cache;
    void *session; /* Cover Flow's artwork session: its worker reads covers from disk */
    uint32_t clock;
    rv_color hairline; /* the text colour baked into the cache */
    int width, height; /* the canvas, and the covers' area above the caption */
    uint16_t late;     /* visible cells whose cover loaded beyond a draw's budget */
    uint16_t waiting;  /* visible cells drawn with the placeholder for want of a cover */
    uint8_t placeholder_ready, drawn, poll_ticks;
    uint8_t covered, idle_ticks; /* an album's songs are above the grid */
    struct rv_timer *late_timer, *scrollbar_timer, *marquee_timer, *idle_timer, *poll_timer;
    uint32_t scrollbar_ms; /* since the last scroll */
    uint8_t scrollbar_alpha;
    char caption[256];
    int caption_width; /* -1 until measured */
    int marquee_offset, marquee_hold;
} grid;

static void *model(void)
{
    return grid.link->reference ? LibraryModelRef_Get(&grid.link->reference) : NULL;
}

static int count(void)
{
    return grid_album_count(model());
}

static uint32_t artwork_of(int index)
{
    uint8_t *browser = grid_browser(model());
    if (!browser || index < 0 || index >= count())
        return 0;
    const uint8_t *albums = RV_FIELD(browser, MediaBrowser_albums, const uint8_t *);
    return RV_FIELD(albums + index * AlbumGroup_bytes, AlbumGroup_artwork, uint32_t);
}

static const uint8_t *stock_bitmap(uint32_t id)
{
    return ResourceService_GetBitmap(UI_GetResourceService(), id);
}

static int bmap_height(const uint8_t *bmap)
{
    const int32_t *bounds = (const int32_t *)(bmap + BMap_bounds);
    return bounds[2] - bounds[0];
}

static int meets(const struct rv_rect *clip, int x, int y, int width, int height)
{
    return width > 0 && height > 0 && x < clip->x + clip->width && x + width > clip->x &&
           y < clip->y + clip->height && y + height > clip->y;
}

static void invalidate(int x, int y, int width, int height)
{
    struct rv_rect rect = {(int16_t)x, (int16_t)y, (int16_t)width, (int16_t)height};
    api->ui_invalidate_rect(self, &rect);
}

/* Redraws one visible cover, or with `lifted` the area it covers when
 * selected, edge and shadow included. */
static void invalidate_cover(int index, int lifted)
{
    int x, y;
    if (!grid_cell(index, grid.link->top, &x, &y))
        return;
    if (!grid.width) {
        api->ui_invalidate(self);
        return;
    }
    if (!lifted) {
        invalidate(x, y, GRID_COVER, GRID_COVER);
        return;
    }
    grid_lift_origin(x, y, grid.width, grid.height, &x, &y);
    invalidate(x - DECOR, y - DECOR, GRID_LIFT_SIZE + 2 * DECOR, GRID_LIFT_SIZE + 2 * DECOR);
}

static void stop_timer(struct rv_timer **timer)
{
    if (*timer)
        api->timer_stop(self, *timer);
    *timer = NULL;
}

/* Artwork session */

static void late_fired(void *arg);
static void poll_start(void);

/* The session reports each cover its worker finishes through slot 2 of this
 * listener's vtable, on the UI task; the other slots are never called. The
 * redraw waits for the late-cell timer: an invalidation made while a frame
 * renders is lost. */
static void cover_loaded(void *listener, void *item, int index)
{
    (void)listener;
    (void)item;
    int cell = index - grid.link->top * GRID_COLUMNS;
    if (cell < 0 || cell >= GRID_VISIBLE_COVERS)
        return;
    grid.late |= (uint16_t)(1u << cell);
    if (!grid.late_timer)
        grid.late_timer = api->timer_start(self, 1, 0, late_fired, NULL);
}

static const uintptr_t listener_vtable[3] = {0, 0, (uintptr_t)cover_loaded};
static const uintptr_t *const listener = listener_vtable;

/* Cover Flow's activation: listen, choose the 128 px format, follow the
 * current music query, centre on the first visible album (the window reaches
 * a few albums behind its centre and well past the visible rows ahead), then
 * start the worker. */
static void start_session(void)
{
    void *block = api->alloc(self, ArtworkSession_bytes);
    if (!block)
        return;
    grid.session = ArtworkSession_Construct(block, "CoverGrid");
    ArtworkSession_Attach(grid.session, (void *)&listener);
    ArtworkSession_SetFormat(grid.session, ArtworkFormat_Cover128);
    ArtworkSession_BindQuery(grid.session);
    ArtworkSession_Center(grid.session, 1, grid.link->top * GRID_COLUMNS);
    ArtworkSession_Start(grid.session);
    poll_start();
}

static void release_session(void)
{
    if (grid.session) {
        ArtworkSession_Stop(grid.session);
        ArtworkSession_Destroy(grid.session);
        api->free(self, grid.session);
    }
    grid.session = NULL;
}

/* The session's 128 px RGB565 pixels for an album once its worker has loaded
 * them; never waits. */
static const uint8_t *session_cover(int index)
{
    uint32_t image[10];
    uint8_t found = 0;
    if (!grid.session || index < 0)
        return NULL;
    PlaybackArtwork_GetImage(grid.session, image, index, &found, ArtworkFormat_Cover128);
    return found ? (const uint8_t *)image[0] : NULL;
}

/* Cover Flow's placeholder cover's pixels and row bytes, or NULL when the
 * resource is not the 128 px RGB565 bitmap expected. */
static const uint8_t *placeholder_source(uint32_t *row_bytes)
{
    const uint8_t *bmap = stock_bitmap(RES_CoverFlow_Proxy_Image);
    if (!bmap || *(const uint16_t *)(bmap + BMap_format) != BMapFormat_RGB565)
        return NULL;
    const int32_t *bounds = (const int32_t *)(bmap + BMap_bounds);
    *row_bytes = *(const uint16_t *)(bmap + BMap_stride);
    if (bounds[2] - bounds[0] != SCALE_SOURCE || bounds[3] - bounds[1] != SCALE_SOURCE ||
        *row_bytes < SCALE_SOURCE * 2)
        return NULL;
    return bmap + BMap_payload;
}

/* Cache */

static void release_cache(void)
{
    api->free(self, grid.cache);
    grid.cache = NULL;
    grid.late = 0;
    grid.waiting = 0;
}

static void empty_cache(void)
{
    for (unsigned int i = 0; i <= CACHE_SLOTS; i++)
        grid.cache[i].artwork = 0;
    grid.placeholder_ready = 0;
    grid.clock = 0;
}

/* `color` at alpha/255 over an RGB565 pixel. */
static uint16_t blend565(uint16_t pixel, rv_color color, uint32_t alpha)
{
    uint32_t r = (pixel >> 11) << 3 | pixel >> 13;
    uint32_t g = (pixel >> 5 & 0x3f) << 2 | (pixel >> 9 & 3);
    uint32_t b = (pixel & 0x1f) << 3 | (pixel >> 2 & 7);
    uint32_t over = 255 - alpha;
    r = (r * over + (color >> 16 & 0xff) * alpha + 127) / 255;
    g = (g * over + (color >> 8 & 0xff) * alpha + 127) / 255;
    b = (b * over + (color & 0xff) * alpha + 127) / 255;
    return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
}

/* A whole cover at grid size into a slot, with the hairline inside its edge. */
static void downscale(struct cover_slot *slot, const uint8_t *pixels, uint32_t row_bytes)
{
    enum { PITCH = RV_CANVAS_BMAP_PITCH(GRID_COVER), LAST = GRID_COVER - 1 };
    uint16_t *out = rv_canvas_bmap(slot->bmap, GRID_COVER, GRID_COVER);
    struct scale scale;
    scale_init(&scale, GRID_COVER);
    for (int y = 0; y < GRID_COVER; y++)
        scale_row(out + y * PITCH, &scale, pixels, row_bytes, y, 0, GRID_COVER);
    for (int i = 0; i < GRID_COVER; i++) {
        out[i] = blend565(out[i], grid.hairline, HAIRLINE_ALPHA);
        out[LAST * PITCH + i] = blend565(out[LAST * PITCH + i], grid.hairline, HAIRLINE_ALPHA);
    }
    for (int y = 1; y < LAST; y++) {
        out[y * PITCH] = blend565(out[y * PITCH], grid.hairline, HAIRLINE_ALPHA);
        out[y * PITCH + LAST] = blend565(out[y * PITCH + LAST], grid.hairline, HAIRLINE_ALPHA);
    }
}

static struct cover_slot *placeholder(void)
{
    if (!grid.cache)
        return NULL;
    struct cover_slot *slot = &grid.cache[PLACEHOLDER];
    if (!grid.placeholder_ready) {
        uint32_t row_bytes;
        const uint8_t *pixels = placeholder_source(&row_bytes);
        if (!pixels)
            return NULL;
        downscale(slot, pixels, row_bytes);
        grid.placeholder_ready = 1;
    }
    return slot;
}

/* The album's slot, downscaling its cover while `budget` remains once the
 * session has it; NULL draws the placeholder, with `late` set when the cover
 * is there but the budget was spent. */
static struct cover_slot *cover(int index, int *budget, int *late)
{
    uint32_t artwork = artwork_of(index);
    if (!grid.cache || !artwork)
        return NULL;
    struct cover_slot *victim = &grid.cache[0];
    for (unsigned int i = 0; i < CACHE_SLOTS; i++) {
        struct cover_slot *slot = &grid.cache[i];
        if (slot->artwork == artwork) {
            slot->used = ++grid.clock;
            return slot;
        }
        if (!slot->artwork || (victim->artwork && slot->used < victim->used))
            victim = slot;
    }
    const uint8_t *pixels = session_cover(index);
    if (!pixels)
        return NULL;
    if (*budget <= 0) {
        *late = 1;
        return NULL;
    }
    (*budget)--;
    downscale(victim, pixels, SCALE_SOURCE * 2);
    victim->artwork = artwork;
    victim->used = ++grid.clock;
    return victim;
}

/* Timers */

/* Covers loaded beyond a draw's budget, and covers the session reported,
 * are redrawn from here, outside any frame. */
static void late_fired(void *arg)
{
    (void)arg;
    grid.late_timer = NULL;
    uint16_t late = grid.late;
    grid.late = 0;
    for (int cell = 0; late; cell++, late >>= 1)
        if (late & 1) {
            int index = grid.link->top * GRID_COLUMNS + cell;
            invalidate_cover(index, index == grid.link->selected);
        }
}

/* The session reports covers its worker reads from disk; cells still on the
 * placeholder are checked for a while after a start or a scroll in case a
 * cover turned up without a report. */
static void poll_tick(void *arg)
{
    (void)arg;
    uint16_t waiting = grid.waiting;
    for (int cell = 0; waiting; cell++, waiting >>= 1) {
        int index = grid.link->top * GRID_COLUMNS + cell;
        if ((waiting & 1) && session_cover(index)) {
            grid.waiting &= (uint16_t)~(1u << cell);
            invalidate_cover(index, index == grid.link->selected);
        }
    }
    if (!grid.waiting || !--grid.poll_ticks)
        stop_timer(&grid.poll_timer);
}

static void poll_start(void)
{
    grid.poll_ticks = POLL_TICKS;
    if (!grid.poll_timer)
        grid.poll_timer = api->timer_start(self, POLL_MS, 1, poll_tick, NULL);
}

/* Like the stock lists, the scrollbar shows when the grid scrolls, holds for
 * 1.5 s after the last scroll, then fades out over 0.5 s. */
static uint32_t scrollbar_alpha(void)
{
    if (!grid.scrollbar_timer)
        return 0;
    if (grid.scrollbar_ms < SCROLLBAR_HOLD_MS)
        return 255;
    if (grid.scrollbar_ms >= SCROLLBAR_HOLD_MS + SCROLLBAR_FADE_MS)
        return 0;
    return 255 - (grid.scrollbar_ms - SCROLLBAR_HOLD_MS) * 255 / SCROLLBAR_FADE_MS;
}

static void scrollbar_tick(void *arg)
{
    (void)arg;
    grid.scrollbar_ms += SCROLLBAR_TICK_MS;
    uint32_t alpha = scrollbar_alpha();
    if (alpha == grid.scrollbar_alpha)
        return;
    const uint8_t *art = stock_bitmap(RES_System_ScrollbarCenter_Image);
    int width = art ? ((const int32_t *)(art + BMap_bounds))[3] : 0;
    invalidate(grid.width - width, 0, width, grid.height);
    if (!alpha)
        stop_timer(&grid.scrollbar_timer);
}

static void scrollbar_show(void)
{
    grid.scrollbar_ms = 0;
    if (!grid.scrollbar_timer)
        grid.scrollbar_timer = api->timer_start(self, SCROLLBAR_TICK_MS, 1, scrollbar_tick, NULL);
}

/* A caption too long for the strip scrolls across it after a pause and
 * starts over after a gap; one that fits stands still. */
static void marquee_tick(void *arg)
{
    (void)arg;
    if (grid.marquee_hold) {
        grid.marquee_hold--;
        return;
    }
    if (++grid.marquee_offset >= grid.caption_width + MARQUEE_GAP) {
        grid.marquee_offset = 0;
        grid.marquee_hold = MARQUEE_HOLD_TICKS;
    }
    invalidate(0, grid.height, grid.width, GRID_CAPTION);
}

/* While an album's songs cover the grid, its covers are worth keeping for a
 * moment in case the list is left at once, and not worth keeping after that.
 * The core tells a screen when it is pushed and when it closed, but not when
 * another screen covers or uncovers it, so a draw stands in for the latter:
 * the first tick absorbs the frames of the push transition, and a draw in any
 * tick after it means the grid is on top again. Input means the same and is
 * the surer signal, since a grid nobody touches never draws. */
static void idle_tick(void *arg)
{
    (void)arg;
    if (!grid.covered || !grid.idle_ticks++) {
        grid.drawn = 0;
        return;
    }
    if (grid.drawn) {
        grid.covered = 0;
        stop_timer(&grid.idle_timer);
        return;
    }
    release_cache();
    release_session();
    stop_timer(&grid.poll_timer);
    stop_timer(&grid.idle_timer);
}

/* Caption */

/* The album's name, as the stock Albums list shows it. */
static void album_name(void *browser, int index, struct NativeString *out)
{
    struct {
        uint16_t units;
        uint16_t text[255];
    } name;
    name.units = 0;
    MediaBrowser_ReadAlbumName(browser, index, &name.units);
    NativeString_AssignUtf16(out, name.text, name.units < 255 ? name.units : 255);
}

static uint32_t append(uint32_t length, const char *text)
{
    if (!text || length + 1 >= sizeof grid.caption)
        return length;
    return length + rv_copy_utf8(grid.caption + length, sizeof grid.caption - length, text);
}

/* Album and artist read like the stock Albums list: the name, then its
 * subtitle, joined by a dash (the title font has no middle dot). */
static void update_caption(void)
{
    _Alignas(uint32_t) uint8_t storage[NativeString_bytes];
    struct NativeString *string = (struct NativeString *)storage;
    void *library = model();
    int selected = grid.link->selected;
    uint32_t length = 0;
    grid.caption[0] = 0;
    if (library && selected < count()) {
        NativeString_InitEmpty(string);
        album_name(grid_browser(library), selected, string);
        length = append(length, NativeString_GetUtf8(string));
        NativeString_Destroy(string);
        NativeString_InitEmpty(string);
        LibraryBrowser_CopyAlbumArtistSubtitle(library, selected, string);
        const char *artist = NativeString_GetUtf8(string);
        if (artist && *artist) {
            length = append(length, " - ");
            append(length, artist);
        }
        NativeString_Destroy(string);
    }
    grid.caption_width = -1;
    grid.marquee_offset = 0;
    grid.marquee_hold = MARQUEE_HOLD_TICKS;
    stop_timer(&grid.marquee_timer);
    if (grid.width)
        invalidate(0, grid.height, grid.width, GRID_CAPTION);
}

/* Drawing */

static void fill(const struct rv_gfx *g, int x, int y, int width, int height, rv_color color)
{
    struct rv_rect rect = {(int16_t)x, (int16_t)y, (int16_t)width, (int16_t)height};
    if (width > 0 && height > 0)
        g->fill(&rect, color);
}

/* A rectangle's outline `thickness` px wide, inside it. */
static void frame(const struct rv_gfx *g, int x, int y, int width, int height, int thickness,
                  rv_color color)
{
    fill(g, x, y, width, thickness, color);
    fill(g, x, y + height - thickness, width, thickness, color);
    fill(g, x, y + thickness, thickness, height - 2 * thickness, color);
    fill(g, x + width - thickness, y + thickness, thickness, height - 2 * thickness, color);
}

/* Background in the margins and gaps around the covers; the covers and the
 * caption paint the rest. */
static void draw_gaps(const struct rv_gfx *g, const struct rv_rect *clip, rv_color background)
{
    int width = grid.width, height = grid.height;
    if (meets(clip, 0, 0, width, GRID_MARGIN))
        fill(g, 0, 0, width, GRID_MARGIN, background);
    for (int column = 0; column <= GRID_COLUMNS; column++) {
        int x = column ? GRID_MARGIN + column * GRID_PITCH - GRID_GAP : 0;
        int gap = !column ? GRID_MARGIN : column == GRID_COLUMNS ? width - x : GRID_GAP;
        if (meets(clip, x, GRID_MARGIN, gap, height - GRID_MARGIN))
            fill(g, x, GRID_MARGIN, gap, height - GRID_MARGIN, background);
    }
    for (int row = 0; row < GRID_VISIBLE_ROWS; row++) {
        int y = GRID_MARGIN + row * GRID_PITCH + GRID_COVER;
        int gap = height - y < GRID_GAP ? height - y : GRID_GAP;
        if (meets(clip, 0, y, width, gap))
            fill(g, 0, y, width, gap, background);
    }
}

/* The selected cover, lifted over its neighbours: its shadow, its edge in the
 * selection colour, then its art box-filtered strip by strip from the
 * session's 128 px cover or Cover Flow's placeholder. */
static void draw_lifted(const struct rv_gfx *g, const struct rv_rect *clip, int x, int y,
                        rv_color tile)
{
    int edge = GRID_LIFT_SIZE + 2 * GRID_EDGE;
    grid_lift_origin(x, y, grid.width, grid.height, &x, &y);
    if (!meets(clip, x - DECOR, y - DECOR, GRID_LIFT_SIZE + 2 * DECOR,
               GRID_LIFT_SIZE + 2 * DECOR))
        return;
    for (int d = GRID_SHADOW; d >= 1; d--)
        frame(g, x - GRID_EDGE - d, y - GRID_EDGE - d + 1, edge + 2 * d, edge + 2 * d, 1,
              (rv_color)shadow_alpha[d - 1] << 24 | SHADOW);
    frame(g, x - GRID_EDGE, y - GRID_EDGE, edge, edge, GRID_EDGE, g->color(RV_COLOR_HIGHLIGHT));
    uint32_t row_bytes = SCALE_SOURCE * 2;
    const uint8_t *source = session_cover(grid.link->selected);
    if (!source)
        source = placeholder_source(&row_bytes);
    if (!source) {
        fill(g, x, y, GRID_LIFT_SIZE, GRID_LIFT_SIZE, tile);
        return;
    }
    enum { PITCH = RV_CANVAS_BMAP_PITCH(GRID_LIFT_SIZE) };
    uint32_t strip[RV_CANVAS_BMAP_BYTES(GRID_LIFT_SIZE, LIFT_STRIP) / 4];
    uint16_t *pixels = rv_canvas_bmap(strip, GRID_LIFT_SIZE, LIFT_STRIP);
    struct scale scale;
    scale_init(&scale, GRID_LIFT_SIZE);
    for (int first = 0; first < GRID_LIFT_SIZE; first += LIFT_STRIP) {
        int rows = GRID_LIFT_SIZE - first < LIFT_STRIP ? GRID_LIFT_SIZE - first : LIFT_STRIP;
        if (!meets(clip, x, y + first, GRID_LIFT_SIZE, rows))
            continue;
        for (int row = 0; row < rows; row++)
            scale_row(pixels + row * PITCH, &scale, source, row_bytes, first + row, 0,
                      GRID_LIFT_SIZE);
        g->bmap_part(x, y + first, strip, rows, 0);
    }
}

/* Caps and a tiled centre, flush with the right edge over the covers. */
static void draw_scrollbar(const struct rv_gfx *g, const struct rv_rect *clip, int albums)
{
    uint32_t alpha = scrollbar_alpha();
    grid.scrollbar_alpha = (uint8_t)alpha;
    if (!alpha)
        return;
    const uint8_t *top = stock_bitmap(RES_System_ScrollbarTop_Image);
    const uint8_t *centre = stock_bitmap(RES_System_ScrollbarCenter_Image);
    const uint8_t *bottom = stock_bitmap(RES_System_ScrollbarBottom_Image);
    if (!top || !centre || !bottom)
        return;
    int width = ((const int32_t *)(centre + BMap_bounds))[3];
    int top_rows = bmap_height(top), centre_rows = bmap_height(centre),
        bottom_rows = bmap_height(bottom);
    int y0, y1;
    if (!grid_scrollbar_span(albums, grid.link->top, grid.height, top_rows + bottom_rows - 3, &y0,
                             &y1))
        return;
    int x = grid.width - width;
    if (!meets(clip, x, y0, width, y1 - y0))
        return;
    int end = y1 - bottom_rows;
    g->bmap_blend(x, y0, top, top_rows, (uint8_t)alpha);
    for (int y = y0 + top_rows; y < end; y += centre_rows)
        g->bmap_blend(x, y, centre, end - y < centre_rows ? end - y : centre_rows, (uint8_t)alpha);
    g->bmap_blend(x, end, bottom, bottom_rows, (uint8_t)alpha);
}

/* The status bar's art with the caption on it, centred when it fits. */
static void draw_caption(const struct rv_gfx *g, const struct rv_rect *clip, rv_color background)
{
    int y = grid.height, width = grid.width;
    if (!meets(clip, 0, y, width, GRID_CAPTION))
        return;
    /* The bar's art is translucent at its edges, as it is over the Now Playing
     * background, so the strip is painted before it or the covers show
     * through. */
    fill(g, 0, y, width, GRID_CAPTION, background);
    const uint8_t *bar = stock_bitmap(RES_StatusBarBlack_Background_Image);
    if (bar)
        g->bmap_part(0, y, bar, GRID_CAPTION, 0);
    if (!grid.caption[0])
        return;
    if (grid.caption_width < 0)
        grid.caption_width = g->measure(RV_FONT_TITLE, grid.caption);
    int text_y = y + (CAPTION_ART_ROWS - g->line_height(RV_FONT_TITLE)) / 2;
    if (grid.caption_width <= width - 2 * CAPTION_INSET) {
        stop_timer(&grid.marquee_timer);
        g->text((width - grid.caption_width) / 2, text_y, RV_FONT_TITLE, CAPTION_INK, grid.caption);
        return;
    }
    int x = CAPTION_INSET - grid.marquee_offset;
    g->text(x, text_y, RV_FONT_TITLE, CAPTION_INK, grid.caption);
    if (x + grid.caption_width + MARQUEE_GAP < width)
        g->text(x + grid.caption_width + MARQUEE_GAP, text_y, RV_FONT_TITLE, CAPTION_INK,
                grid.caption);
    if (!grid.marquee_timer)
        grid.marquee_timer = api->timer_start(self, MARQUEE_TICK_MS, 1, marquee_tick, NULL);
}

static void draw(const struct rv_context *ctx, const struct rv_gfx *g, int width, int height)
{
    (void)ctx;
    const struct rv_rect *clip = g->clip();
    rv_color background = g->color(RV_COLOR_BACKGROUND), text = g->color(RV_COLOR_TEXT);
    rv_color tile = g->mix(background, text, TILE_WEIGHT);
    grid.width = width;
    grid.height = height - GRID_CAPTION;
    grid.drawn = 1;
    /* A theme that changed the text colour changes the baked hairlines. */
    if (grid.cache && grid.hairline != text)
        empty_cache();
    grid.hairline = text;
    if (!grid.cache && (grid.cache = api->alloc(self, (CACHE_SLOTS + 1) * sizeof *grid.cache)))
        empty_cache();
    if (!grid.session)
        start_session();

    draw_gaps(g, clip, background);
    int albums = count(), budget = SCALES_PER_DRAW;
    int lifted_x = -1, lifted_y = 0;
    uint16_t late = 0;
    for (int row = 0; row < GRID_VISIBLE_ROWS; row++) {
        int y = GRID_MARGIN + row * GRID_PITCH;
        int below = grid.height - y;
        if (below <= 0)
            break;
        int side = below < GRID_COVER ? below : GRID_COVER;
        for (int column = 0; column < GRID_COLUMNS; column++) {
            int index = (grid.link->top + row) * GRID_COLUMNS + column;
            int x = GRID_MARGIN + column * GRID_PITCH;
            if (index == grid.link->selected && index < albums) {
                lifted_x = x;
                lifted_y = y;
                continue;
            }
            if (!meets(clip, x, y, GRID_COVER, side))
                continue;
            if (index >= albums) {
                fill(g, x, y, GRID_COVER, side, background);
                continue;
            }
            int pending = 0;
            uint16_t bit = (uint16_t)(1u << (row * GRID_COLUMNS + column));
            struct cover_slot *slot = cover(index, &budget, &pending);
            if (pending)
                late |= bit;
            if (slot || pending || !artwork_of(index))
                grid.waiting &= (uint16_t)~bit;
            else
                grid.waiting |= bit;
            if (!slot)
                slot = placeholder();
            if (slot)
                g->bmap(x, y, slot->bmap);
            else
                fill(g, x, y, GRID_COVER, side, tile);
        }
    }
    if (lifted_x >= 0)
        draw_lifted(g, clip, lifted_x, lifted_y, tile);
    draw_scrollbar(g, clip, albums);
    draw_caption(g, clip, background);
    grid.late |= late;
    if (grid.late && !grid.late_timer)
        grid.late_timer = api->timer_start(self, 1, 0, late_fired, NULL);
}

/* Input */

/* Like the stock Albums list: select the album on the shared browser, title
 * the song list with its name, then push it in its plain layout on the grid's
 * model. The list's saved row is cleared first so it opens at the top. */
static void open_album(const struct rv_context *ctx)
{
    struct grid_link *link = grid.link;
    void *browser = grid_browser(model());
    if (!browser || link->selected >= count())
        return;
    if (MediaBrowser_SelectAlbumIndex(browser, link->selected))
        return;
    _Alignas(uint32_t) uint8_t storage[NativeString_bytes];
    struct NativeString *title = (struct NativeString *)storage;
    NativeString_InitEmpty(title);
    album_name(browser, link->selected, title);
    MediaListSelection_SetTitle(MediaListSelection_GetInstance(), title);
    NativeString_Destroy(title);
    UI_SetScreenState(UI_GetLayoutManager(), RES_MediaLists_Songs_Screen, 0, 0, 0, 0);
    void *screen = UI_CreateScreenController(UI_GetControllerRegistry(), RES_MediaLists_Songs_Screen,
                                             RES_MediaLists_Songs_Plain_Layout,
                                             Transition_ForegroundPush);
    if (!screen)
        return;
    LibraryModelRef_Assign((uint8_t *)screen + MediaListController_model, &link->reference);
    stop_timer(&grid.scrollbar_timer);
    stop_timer(&grid.marquee_timer);
    grid.drawn = 0;
    grid.covered = 1;
    grid.idle_ticks = 0;
    if (!grid.idle_timer)
        grid.idle_timer = api->timer_start(self, IDLE_MS, 1, idle_tick, NULL);
    void (*push)(void *, void *) =
        *(void **)((uint8_t *)*(void **)ctx->controller + ScreenController_push_method);
    push(ctx->controller, screen);
}

static int input(const struct rv_context *ctx, const struct rv_input *in)
{
    struct grid_link *link = grid.link;
    /* Only the top screen is given input, so the grid is not covered. */
    if (grid.covered) {
        grid.covered = 0;
        stop_timer(&grid.idle_timer);
    }
    if (in->kind == RV_INPUT_WHEEL) {
        int albums = count();
        if (!albums)
            return 1;
        int next = grid_clamp(link->selected + in->delta, 0, albums - 1);
        if (next == link->selected)
            return 1;
        int previous = link->selected;
        link->selected = next;
        int top = grid_top_row(link->top, next);
        if (top != link->top) {
            if (grid.session)
                ArtworkSession_Center(grid.session, 0, (top - link->top) * GRID_COLUMNS);
            link->top = top;
            grid.waiting = 0;
            scrollbar_show();
            poll_start();
            api->ui_invalidate(self);
        } else {
            /* Only the two lifted areas change. */
            invalidate_cover(previous, 1);
            invalidate_cover(next, 1);
        }
        update_caption();
        return 1;
    }
    if (in->kind == RV_INPUT_PRESS)
        return in->button == RV_BUTTON_CENTER;
    if (in->kind == RV_INPUT_RELEASE && in->button == RV_BUTTON_CENTER) {
        open_album(ctx);
        return 1;
    }
    return in->kind == RV_INPUT_HOLD;
}

static void opened(const struct rv_context *ctx)
{
    memset(&grid, 0, sizeof grid);
    grid.link = ctx->arg;
    grid.caption_width = -1;
    update_caption();
}

/* After the last draw of the slide-out. */
static void closed(const struct rv_context *ctx)
{
    (void)ctx;
    stop_timer(&grid.late_timer);
    stop_timer(&grid.scrollbar_timer);
    stop_timer(&grid.marquee_timer);
    stop_timer(&grid.idle_timer);
    stop_timer(&grid.poll_timer);
    release_session();
    release_cache();
    if (grid.link->reference)
        LibraryModelRef_Release(&grid.link->reference);
    grid.link->reference = NULL;
}

static const struct rv_screen screens[] = {
    RV_SCREEN_CANVAS("grid", "Cover Grid", draw, input, .opened = opened, .closed = closed),
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
