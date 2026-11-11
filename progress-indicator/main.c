/* SPDX-License-Identifier: GPL-3.0-only */

#include "bitmap.h"
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_sites.h"
#include "rv_ui.h"
#include <stddef.h>

#define FIELD(base, offset, type) (*(type *)((uint8_t *)(base) + (offset)))

static const struct rv_api *api;
static rv_extension_handle self;
static int stopping;

/* The UI task owns models and this list. Both native destructor routes remove
 * their model before its embedded bitmaps are destroyed. */
struct borrower {
    struct borrower *next;
    void *model;
    const void *native[2];
};
static struct borrower *borrowers;

static void *icon_of(void *model, unsigned selected)
{
    return (uint8_t *)model + (selected ? MediaListModel_selected_status_icon
                                      : MediaListModel_status_icon);
}

static uint32_t item_value(struct MediaItem *item, uint32_t method)
{
    typedef uint32_t (*getter)(struct MediaItem *);
    return FIELD(FIELD(item, 0, void *), method, getter)(item);
}

static int in_progress(struct MediaItem *item, void *player)
{
    if (!FIELD(item, MediaItem_record, void *) || !MediaItem_IsValid(item) ||
        !item_value(item, MediaItem_remembers_position_method))
        return 0;
    uint32_t start = item_value(item, MediaItem_start_time_method);
    uint32_t end = item_value(item, MediaItem_stop_time_method);
    if (!end)
        end = item_value(item, MediaItem_duration_method);
    if (end <= start)
        return 0;
    uint32_t position = item_value(item, MediaItem_bookmark_method);
    if (FIELD(player, MediaPlayer_current_track, void *)) {
        uint32_t storage[MediaItem_bytes / 4];
        struct MediaItem *current = (void *)storage;
        MediaItem_InitFromRecord(current, NULL, 0);
        MediaPlayer_GetCurrentItem(player, current);
        if (MediaItem_IsValid(current) &&
            FIELD(current, MediaItem_record, void *) == FIELD(item, MediaItem_record, void *) &&
            FIELD(current, MediaItem_generation, uint32_t) ==
                FIELD(item, MediaItem_generation, uint32_t))
            position = MediaPlayer_GetPosition(player);
        MediaItem_Destroy(current);
    }
    return position > start && position < end;
}

static struct borrower *remember(void *model)
{
    for (struct borrower *b = borrowers; b; b = b->next)
        if (b->model == model)
            return b;
    struct borrower *b = api->alloc(self, sizeof *b);
    if (b) {
        *b = (struct borrower){.next = borrowers, .model = model};
        borrowers = b;
    }
    return b;
}

static uint32_t destroy(uintptr_t model, const void *next)
{
    for (struct borrower **link = &borrowers; *link; link = &(*link)->next) {
        struct borrower *b = *link;
        if (b->model == (void *)model) {
            *link = b->next;
            api->free(self, b);
            break;
        }
    }
    return api->hook_next(next, model, 0, 0, 0, 0, 0);
}

static uint32_t property(uintptr_t model_address, uint32_t type, uint32_t property,
                         uint32_t row, const void *next, uintptr_t *out)
{
    uint32_t handled = api->hook_next(next, model_address, type, property, row,
                                    (uintptr_t)out, 0);
    if (!handled || stopping || type != Property_Drawable || (int32_t)row < 0 ||
        (property != Property_PodcastStatus && property != Property_PodcastStatusSelected &&
         property != Property_ITunesUStatus && property != Property_ITunesUStatusSelected))
        return handled;
    void *model = (void *)model_address;
    if (FIELD(model, MediaListModel_all_row, uint8_t)) {
        if (!row)
            return handled;
        row--;
    }
    void *player = MediaPlayer_GetInstance();
    void *source = LibraryModelRef_Get((uint8_t *)model + MediaListModel_source);
    if (!player || !source)
        return handled;
    uint32_t storage[MediaItem_bytes / 4];
    struct MediaItem *item = (void *)storage;
    MediaItem_InitFromRecord(item, NULL, 0);
    Mutex_Lock((uint8_t *)player + MediaPlayer_mutex);
    /* Native struct return places the output pointer before this. */
    typedef void (*get_item)(struct MediaItem *, void *, uint32_t);
    FIELD(FIELD(source, 0, void *), SongModel_get_item_method, get_item)(item, source, row);
    int partial = in_progress(item, player);
    int video = partial && MediaItem_HasKind8062(item);
    MediaItem_Destroy(item);
    Mutex_Unlock((uint8_t *)player + MediaPlayer_mutex);
    if (!partial)
        return handled;
    unsigned selected = property == Property_PodcastStatusSelected ||
                        property == Property_ITunesUStatusSelected;
    unsigned variant = (video ? 0 : 2) + (selected ? 0 : 1);
    const void *native = ResourceService_GetBitmap(UI_GetResourceService(),
                                                 RES_MediaStatus_VideoSelected_Bitmap + variant);
    const void *bitmap = progress_bitmap(native, variant);
    if (!bitmap)
        return handled;
    void *icon = icon_of(model, selected);
    const uint8_t *payload = FIELD(icon, Bitmap_pixels, const uint8_t *);
    if (!payload || progress_bitmap_owns(payload) || FIELD(icon, Bitmap_owns_pixels, uint8_t))
        return handled;
    struct borrower *b = remember(model);
    if (!b)
        return handled;
    /* Preserve the native glyph, including its empty state during a replay. */
    b->native[selected] = payload - BMap_payload;
    Bitmap_LoadResource(icon, bitmap, 0, 0);
    *out = (uintptr_t)icon;
    return handled;
}

static void unload(void)
{
    stopping = 1;
    while (borrowers) {
        struct borrower *b = borrowers;
        borrowers = b->next;
        for (unsigned selected = 0; selected < 2; selected++) {
            void *icon = icon_of(b->model, selected);
            if (progress_bitmap_owns(FIELD(icon, Bitmap_pixels, const void *)))
                Bitmap_LoadResource(icon, b->native[selected], 0, 0);
        }
        api->free(self, b);
    }
}

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_MediaListModel_IndexedProperty_slot, property),
    RV_HOOK_FILTER(RV_SITE_MediaListModel_Destroy_slot, destroy),
    RV_HOOK_FILTER(RV_SITE_MediaListModel_Destroy_call, destroy),
};
static const struct rv_descriptor descriptor = {
    .size = sizeof descriptor,
    RV_HOOKS(hooks),
    .unload = unload,
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    api = core;
    self = handle;
    return &descriptor;
}
