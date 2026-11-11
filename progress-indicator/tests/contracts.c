/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../main.c"
#include "../bitmap.c"

static uint8_t images[4][600];
static uintptr_t methods[128], source_methods[256];
static uintptr_t *source = source_methods;
static uint8_t player[MediaPlayer_mutex + 16];
static uint8_t model[MediaListModel_selected_status_icon + Bitmap_bytes];
static uint8_t model2[sizeof model];
static uint32_t bookmark, start, stop, duration = 3600000, live, generation = 7;
static int valid = 1, remembers = 1, video, current, same = 1, have_player = 1;
static int locked, allocations, fail_alloc, calls, item_calls, native_result = 1;
static int marker, other_record;

static uint32_t get_bookmark(struct MediaItem *i) { (void)i; return bookmark; }
static uint32_t get_start(struct MediaItem *i) { (void)i; return start; }
static uint32_t get_stop(struct MediaItem *i) { (void)i; return stop; }
static uint32_t get_duration(struct MediaItem *i) { (void)i; return duration; }
static uint32_t get_remembers(struct MediaItem *i) { (void)i; return remembers; }
struct MediaItem *MediaItem_InitFromRecord(struct MediaItem *i, void *record, uint8_t flags)
{
    (void)flags;
    memset(i, 0, MediaItem_bytes);
    FIELD(i, 0, void *) = methods;
    FIELD(i, MediaItem_record, void *) = record;
    FIELD(i, MediaItem_generation, uint32_t) = generation;
    return i;
}
void MediaItem_Destroy(struct MediaItem *i) { (void)i; }
int MediaItem_IsValid(struct MediaItem *i) { (void)i; return valid; }
uint32_t MediaItem_HasKind8062(struct MediaItem *i) { (void)i; return video; }
void *MediaPlayer_GetInstance(void) { return have_player ? player : NULL; }
int MediaPlayer_GetCurrentItem(void *p, struct MediaItem *i)
{
    assert(p == player && locked);
    MediaItem_InitFromRecord(i, same == 2 ? &other_record : &marker, 0);
    FIELD(i, MediaItem_generation, uint32_t) += !same;
    return 1;
}
uint32_t MediaPlayer_GetPosition(void *p) { assert(p == player && locked); return live; }
int Mutex_Lock(void *p) { assert(p == player + MediaPlayer_mutex && !locked); locked++; return 0; }
int Mutex_Unlock(void *p) { assert(p == player + MediaPlayer_mutex && locked); locked--; return 0; }
void *LibraryModelRef_Get(void *p) { (void)p; return &source; }
static void get_item(struct MediaItem *i, void *p, uint32_t row)
{
    assert(p == &source && locked && row < 10);
    item_calls++;
    MediaItem_InitFromRecord(i, &marker, 0);
}
void *UI_GetResourceService(void) { return images; }
void *ResourceService_GetBitmap(void *service, uint32_t id)
{
    assert(service == images && id >= RES_MediaStatus_VideoSelected_Bitmap);
    unsigned variant = id - RES_MediaStatus_VideoSelected_Bitmap;
    assert(variant < 4);
    return images[variant];
}
void Bitmap_LoadResource(void *b, const void *resource, uint32_t a, uint32_t flags)
{
    assert(resource && !locked && !a && !flags);
    FIELD(b, Bitmap_pixels, const uint8_t *) = (const uint8_t *)resource + BMap_payload;
}
static void *allocate(rv_extension_handle h, uint32_t bytes)
{
    (void)h;
    if (fail_alloc) return NULL;
    allocations++;
    return calloc(1, bytes);
}
static void release(rv_extension_handle h, void *p) { (void)h; allocations--; free(p); }
static uint32_t next_call(const void *next, uintptr_t m, uintptr_t type, uintptr_t prop,
                          uintptr_t row, uintptr_t out, uintptr_t unused)
{
    (void)row; (void)unused;
    assert(!locked);
    calls++;
    if (next == &marker) {
        assert(!borrowers || borrowers->model != (void *)m);
        return 42;
    }
    assert(type == Property_Drawable || !type);
    unsigned selected = prop == Property_PodcastStatusSelected || prop == Property_ITunesUStatusSelected;
    Bitmap_LoadResource(icon_of((void *)m, selected), images[3], 0, 0);
    *(uintptr_t *)out = (uintptr_t)icon_of((void *)m, selected);
    return native_result;
}
static const struct rv_api test_api = {.alloc = allocate, .free = release, .hook_next = next_call};
static void put_word(uint8_t *p, uint32_t value) { memcpy(p, &value, 4); }
static int render_on(void *m, uint32_t prop, uint32_t row)
{
    uintptr_t out;
    FIELD(player, MediaPlayer_current_track, void *) = current ? &marker : NULL;
    assert(property((uintptr_t)m, Property_Drawable, prop, row, NULL, &out) == (uint32_t)native_result);
    return progress_bitmap_owns(FIELD((void *)out, Bitmap_pixels, void *));
}
static int render(void) { return render_on(model, Property_PodcastStatus, 2); }
/* Check decoded pixels against the semicircle geometry. */
static void check_pixels(unsigned variant)
{
    const uint8_t *source = images[variant];
    const uint8_t *result = progress_bitmap(images[variant], variant);
    assert(result);
    unsigned input = 32 + icon_word(source + 28) * 4;
    unsigned output = 32 + icon_word(result + 28) * 4;
    int center_y = variant < 2 ? 67 : 227;
    for (int y = 0; y < 29; y++) {
        for (int x = 0; x < 13; x++) {
            unsigned clear = 0;
            if (x >= 6 && x < 11) {
                for (int sy = 0; sy < 8; sy++) {
                    int dy = y * 16 + sy * 2 - 7 - center_y;
                    for (int sx = 0; sx < 8; sx++) {
                        int dx = x * 16 + sx * 2 - 7 - 96;
                        clear += dx >= -4 && dx * dx + dy * dy < 64 * 64;
                    }
                }
            }
            const uint8_t *before = source + 32 + source[input + y * 13 + x] * 4;
            const uint8_t *after = result + 32 + result[output + y * 13 + x] * 4;
            unsigned alpha = (before[3] * (64 - clear) + 32) / 64;
            assert(after[3] == alpha);
            if (alpha)
                assert(!memcmp(before, after, 3));
        }
    }
}


int main(void)
{
    methods[MediaItem_bookmark_method / sizeof(uintptr_t)] = (uintptr_t)get_bookmark;
    methods[MediaItem_start_time_method / sizeof(uintptr_t)] = (uintptr_t)get_start;
    methods[MediaItem_stop_time_method / sizeof(uintptr_t)] = (uintptr_t)get_stop;
    methods[MediaItem_duration_method / sizeof(uintptr_t)] = (uintptr_t)get_duration;
    methods[MediaItem_remembers_position_method / sizeof(uintptr_t)] = (uintptr_t)get_remembers;
    source_methods[SongModel_get_item_method / sizeof(uintptr_t)] = (uintptr_t)get_item;
    for (unsigned v = 0; v < 4; v++) {
        uint8_t *b = images[v];
        b[BMap_format] = BMapFormat_Indexed8;
        b[BMap_stride] = 13; b[BMap_depth] = 8;
        put_word(b + BMap_bounds + 8, 29); put_word(b + BMap_bounds + 12, 13);
        put_word(b + BMap_payload_bytes, icon_sizes[v] - BMap_payload);
        unsigned colors = (icon_sizes[v] - 32 - 13 * 29) / 4;
        put_word(b + BMap_payload, colors);
        for (unsigned j = 1; j < colors; j++) {
            b[32 + j * 4] = j * 5;
            b[33 + j * 4] = 255 - j * 3;
            b[34 + j * 4] = j * 7;
            b[35 + j * 4] = j * 5;
        }
        for (unsigned j = 0; j < 13 * 29; j++) b[32 + colors * 4 + j] = j % colors;
        check_pixels(v);
    }
    rv_module_entry(&test_api, NULL);
    assert(!render()); bookmark = 60000; assert(render()); assert(allocations == 1);
    for (int j = 0; j < 50; j++) assert(render());
    assert(allocations == 1);
    assert(render_on(model, Property_PodcastStatusSelected, 2));
    assert(render_on(model, Property_ITunesUStatus, 2));
    assert(render_on(model, Property_ITunesUStatusSelected, 2));
    video = 1; assert(render()); video = 0;
    bookmark = duration; assert(!render()); bookmark = UINT32_MAX; assert(!render());
    start = 30000; stop = 120000; bookmark = start; assert(!render());
    bookmark++; assert(render()); bookmark = stop; assert(!render());
    stop = start; assert(!render()); start = stop = 0;
    duration = 0; assert(!render()); duration = 3600000; bookmark = 60000;
    current = 1; live = 0; assert(!render()); live = 90000; assert(render());
    live = duration; assert(!render()); same = 0; assert(render()); same = 2; assert(render()); same = 1; current = 0;
    valid = 0; assert(!render()); valid = 1;
    remembers = 0; assert(!render()); remembers = 1;
    have_player = 0; assert(!render()); have_player = 1;
    int before = item_calls;
    assert(!render_on(model, Property_ItemId, 2)); assert(item_calls == before);
    FIELD(model, MediaListModel_all_row, uint8_t) = 1;
    assert(!render_on(model, Property_PodcastStatus, 0)); assert(item_calls == before);
    assert(render_on(model, Property_PodcastStatus, 3));
    FIELD(model, MediaListModel_all_row, uint8_t) = 0;
    native_result = 0; assert(!render()); native_result = 1;
    assert(!render_on(model, Property_PodcastStatus, UINT32_MAX));
    uint8_t saved[sizeof half_icons[3]]; assert(render());
    memcpy(saved, half_icons[3], sizeof saved);
    unsigned colors = icon_word(images[3] + BMap_payload), pixels = 32 + colors * 4;
    uint8_t last = images[3][pixels + 376]; images[3][pixels + 376] = colors;
    assert(!render()); assert(!memcmp(saved, half_icons[3], sizeof saved));
    images[3][pixels + 376] = last;
    images[3][35] = 255; assert(!render());
    assert(!memcmp(saved, half_icons[3], sizeof saved)); images[3][35] = 0;
    put_word(images[3] + BMap_payload, 0x40000000u + colors);
    assert(!render()); assert(!memcmp(saved, half_icons[3], sizeof saved));
    put_word(images[3] + BMap_payload, colors);
    put_word(images[3] + BMap_bounds, 1); assert(!render());
    put_word(images[3] + BMap_bounds, 0);
    images[3][0] = 0; assert(!render()); images[3][0] = BMapFormat_Indexed8;
    images[3][36] = 17; assert(render()); assert(((uint8_t *)half_icons[3])[36] == 17);
    fail_alloc = 1; assert(!render_on(model2, Property_PodcastStatus, 2));
    fail_alloc = 0; assert(render_on(model2, Property_PodcastStatus, 2)); assert(allocations == 2);
    assert(destroy((uintptr_t)model2, &marker) == 42); assert(allocations == 1);
    memset(model2, 0xa5, sizeof model2); /* Dead model must never be touched on unload. */
    unload(); assert(!allocations && !borrowers);
    for (unsigned selected = 0; selected < 2; selected++)
        assert(!progress_bitmap_owns(FIELD(icon_of(model, selected), Bitmap_pixels, void *)));
    assert(!render());
    stopping = 0;
    for (int j = 0; j < 20; j++) {
        assert(render()); unload(); assert(!allocations); stopping = 0;
    }
    return 0;
}
