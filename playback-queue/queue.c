/* SPDX-License-Identifier: GPL-3.0-only */

/* The player's playing list: inserting after the current song or at the end,
 * removing, moving, and making the player drop the track it prepared.
 *
 * Insertion adapted from RepriseOS payload/play_next.c and
 * payload/queue_selection.c (c997a51) by lgreitis and brilliant-alien. */

#include "queue.h"
#include "osos-2.0.5.h"
#include <stddef.h>

#define FIELD(base, offset, type) (*(type *)((uint8_t *)(base) + (offset)))

static void *view_of(void *player)
{
    return (uint8_t *)player + MediaPlayer_playback_view;
}

static struct MediaList *list_of(void *player)
{
    return FIELD(view_of(player), MediaListView_list, struct MediaList *);
}

static struct MediaEntry *root_of(struct MediaList *list)
{
    return FIELD(list, MediaList_root, struct MediaEntry *);
}

/* The core's track-count patch keeps a list's track count as a word at
 * MediaEntry_groups, so a library may pass 65,535 tracks. */
static uint32_t tracks_of(struct MediaList *list)
{
    return FIELD(root_of(list), MediaEntry_groups, uint32_t);
}

static uint32_t count_of(void *player)
{
    return tracks_of(list_of(player));
}

static int32_t *current_index(void *player)
{
    return &FIELD(player, MediaPlayer_current_index, int32_t);
}

static int shuffled(struct MediaList *list)
{
    return FIELD(list, MediaList_flags, uint8_t) & MediaListFlags_Shuffled;
}

static int reversed(struct MediaList *list)
{
    return (FIELD(list, MediaList_order_flags, uint8_t) & MediaListOrderFlags_Reversed) != 0;
}

static uint32_t rank_of(struct MediaEntry *entry)
{
    return FIELD(entry, MediaEntry_song_shuffle_rank, uint32_t);
}

static void *lock(void)
{
    void *player = MediaPlayer_GetInstance();
    Mutex_Lock((uint8_t *)player + MediaPlayer_mutex);
    return player;
}

static void unlock(void *player)
{
    Mutex_Unlock((uint8_t *)player + MediaPlayer_mutex);
}

/* The current song when the list is a flat song list the queue can change. */
static struct MediaEntry *current_entry(void *player)
{
    void *view = view_of(player);
    if (!FIELD(player, MediaPlayer_current_track, void *) || !MediaListView_IsValid(view))
        return NULL;
    struct MediaList *list = list_of(player);
    uint32_t count = count_of(player);
    if (!(FIELD(list, MediaList_flags, uint8_t) & MediaListFlags_PlaybackList))
        return NULL;
    if (*current_index(player) < 0 || (uint32_t)*current_index(player) >= count)
        return NULL;
    struct MediaEntry *current = PlaybackList_GetEntryByIndex(view, *current_index(player));
    if (!current || FIELD(current, MediaEntry_parent, struct MediaEntry *) != root_of(list) ||
        (FIELD(current, MediaEntry_flags, uint32_t) & MediaEntryFlags_Group))
        return NULL;
    if (shuffled(list)) {
        void *order = FIELD(list, MediaList_shuffle_index, void *);
        if (!order || FIELD(order, MediaOrder_count, uint32_t) != count)
            return NULL;
    }
    return current;
}

static int editable(void *player)
{
    if (FIELD(player, MediaPlayer_current_track, void *))
        return current_entry(player) != NULL;
    if (!MediaListView_IsValid(view_of(player)))
        return 0;
    struct MediaList *list = list_of(player);
    if (!root_of(list) || !(FIELD(list, MediaList_flags, uint8_t) & MediaListFlags_PlaybackList))
        return 0;
    uint32_t count = count_of(player);
    if (count) {
        struct MediaEntry *entry = PlaybackList_GetEntryByIndex(view_of(player), 0);
        if (!entry || FIELD(entry, MediaEntry_parent, struct MediaEntry *) != root_of(list) ||
            (FIELD(entry, MediaEntry_flags, uint32_t) & MediaEntryFlags_Group))
            return 0;
    }
    if (shuffled(list) && count) {
        void *order = FIELD(list, MediaList_shuffle_index, void *);
        if (!order || FIELD(order, MediaOrder_count, uint32_t) != count)
            return 0;
    }
    return 1;
}

static uint32_t first_pending(void *player)
{
    return FIELD(player, MediaPlayer_current_track, void *) ? (uint32_t)*current_index(player) + 1 : 0;
}

static uint32_t upcoming(void *player)
{
    return count_of(player) - first_pending(player);
}

static int prepare_queue(void *player)
{
    if (editable(player))
        return 1;
    if (FIELD(player, MediaPlayer_current_track, void *))
        return 0;
    /* Native initialization attaches the list and playback source without
     * loading a current track. Repeat 3 preserves the native preference. */
    MediaPlayer_InitializePlaybackList(player, 0, 3);
    return editable(player);
}

/* Artwork is indexed by list position: stopped before a change, rebuilt after. */
static void stop_artwork(void *player)
{
    ArtworkSession_Stop((uint8_t *)player + MediaPlayer_artwork_session);
}

/* After a change the prepared next track may be the wrong one, and the source
 * caches the track order. */
static void changed(void *player)
{
    if (FIELD(player, MediaPlayer_current_track, void *))
        MediaPlayer_RebuildArtwork(player);
    MediaPlayer_ClearPreparedTrack(player);
    Audio_SetPlaybackSource(FIELD(player, MediaPlayer_playback_source, void *));
}

/* A copy of `entry` that ends up at playback index `position`. */
static struct MediaEntry *insert_at(void *player, struct MediaEntry *entry, uint32_t position)
{
    struct MediaList *list = list_of(player);
    uint32_t count = count_of(player);
    int reverse = reversed(list);
    uint32_t rank = reverse ? count - position : position;
    struct MediaEntry *after = reverse && !position ?
        FIELD(root_of(list), MediaEntry_group_last, struct MediaEntry *) : NULL;
    if (position) {
        after = PlaybackList_GetEntryByIndex(view_of(player), position - 1);
        if (reverse)
            after = FIELD(after, MediaEntry_previous, struct MediaEntry *);
    }
    struct MediaEntry *cursor[] = {root_of(list), after};

    void *track_data = FIELD(list, MediaList_track_data, void *);
    TrackData_AcquireStringCaches(track_data);
    if (shuffled(list)) {
        /* The shuffle order sorts by rank; leave a gap at `rank`. */
        void *order = FIELD(list, MediaList_shuffle_index, void *);
        struct MediaEntry **entries = (struct MediaEntry **)((uint8_t *)order + MediaOrder_entries);
        for (uint32_t i = 0; i < count; i++)
            FIELD(entries[i], MediaEntry_song_shuffle_rank, uint32_t) = i + (i >= rank);
    }
    struct MediaEntry *inserted = MediaList_CopyEntryAtPosition(entry, list, cursor, 0);
    if (inserted && shuffled(list)) {
        FIELD(inserted, MediaEntry_song_shuffle_rank, uint32_t) = rank;
        /* Copy notifications may rebuild the order before the rank is set. */
        MediaList_InvalidateOrderCache(list, MediaOrderMode_Shuffle);
    }
    TrackData_ReleaseStringCaches(track_data);
    return inserted;
}

/* Removes the songs strictly between `from` and `to` in playback order, to the
 * end when `to` is NULL. One pass over the list: each removal drops the order
 * caches, so looking songs up by index in between would rebuild them every
 * time. A removed entry is freed at once. */
static void remove_between(struct MediaList *list, struct MediaEntry *from, struct MediaEntry *to)
{
    int reverse = reversed(list);
    if (shuffled(list)) {
        /* Shuffled songs play by rank, descending when reversed. */
        uint32_t start = from ? rank_of(from) : 0, end = to ? rank_of(to) : 0;
        struct MediaEntry *entry = FIELD(root_of(list), MediaEntry_group_first, struct MediaEntry *);
        while (entry) {
            struct MediaEntry *next = FIELD(entry, MediaEntry_next, struct MediaEntry *);
            uint32_t rank = rank_of(entry);
            int between = reverse ? (!from || rank < start) && (!to || rank > end)
                                  : (!from || rank > start) && (!to || rank < end);
            if (between)
                MediaEntry_Remove(entry);
            entry = next;
        }
    } else {
        uint32_t step = reverse ? MediaEntry_previous : MediaEntry_next;
        struct MediaEntry *entry = from ? FIELD(from, step, struct MediaEntry *) :
            FIELD(root_of(list), reverse ? MediaEntry_group_last : MediaEntry_group_first,
                  struct MediaEntry *);
        while (entry && entry != to) {
            struct MediaEntry *next = FIELD(entry, step, struct MediaEntry *);
            MediaEntry_Remove(entry);
            entry = next;
        }
    }
    MediaList_NotifyChanged(list);
}

/* Removes one song at playback index `index`, never the current one. */
static void remove_one(void *player, uint32_t index)
{
    struct MediaEntry *entry = PlaybackList_GetEntryByIndex(view_of(player), index);
    if (!entry)
        return;
    MediaEntry_Remove(entry);
    MediaList_NotifyChanged(list_of(player));
    if ((int32_t)index < *current_index(player))
        --*current_index(player);
}

static struct MediaEntry *item_entry(struct MediaItem *item)
{
    struct MediaEntry *entry = FIELD(item, MediaItem_entry, struct MediaEntry *);
    void *record = FIELD(item, MediaItem_record, void *);
    if (!entry && record)
        entry = FIELD(record, MediaRecord_entries, struct MediaEntry *);
    return entry;
}

static int is_queueable(struct MediaItem *item)
{
    struct MediaEntry *entry = item_entry(item);
    return entry && !(FIELD(entry, MediaEntry_flags, uint32_t) & MediaEntryFlags_Group) &&
           !MediaItem_HasKind200000(item) &&
           !MediaItem_HasKind8(item) && !MediaItem_HasKind8062(item);
}

int queue_available(void)
{
    void *player = lock();
    int available = editable(player);
    unlock(player);
    return available;
}

int queue_can_add(void)
{
    void *player = lock();
    int allowed = !FIELD(player, MediaPlayer_current_track, void *) || editable(player);
    unlock(player);
    return allowed;
}

int queue_can_add_item(const void *item)
{
    return item && is_queueable((struct MediaItem *)item) && queue_can_add();
}

int queue_start_idle(void)
{
    void *player = lock();
    int start = MediaPlayer_IsStopped(player) && editable(player) && upcoming(player);
    if (start)
        MediaPlayer_PlayIndex(player, first_pending(player), 0, 0, 0);
    unlock(player);
    return start;
}

void queue_add_item(const void *item, int last)
{
    struct MediaItem *song = (struct MediaItem *)item;
    void *player = lock();
    if (song && is_queueable(song) && prepare_queue(player)) {
        uint32_t position = last ? count_of(player) : first_pending(player);
        stop_artwork(player);
        if (insert_at(player, item_entry(song), position))
            changed(player);
        else
            MediaPlayer_RebuildArtwork(player);
    }
    unlock(player);
}

void queue_add_browser(void *browser, int last)
{
    void *player = lock();
    struct MediaList *source = browser ? MediaBrowser_GetCurrentList(browser) : NULL;
    if (!source || !root_of(source) || !prepare_queue(player)) {
        unlock(player);
        return;
    }
    /* The songs and order Add to On-The-Go would take for this selection. */
    MediaBrowser_BuildSongs(browser);
    uint32_t mode = shuffled(source) ? MediaOrderMode_Shuffle : MediaBrowser_GetOrderMode(browser);
    uint32_t grouped = FIELD(browser, MediaBrowser_order_flag, uint8_t) != 0;
    uint32_t count = tracks_of(source);

    stop_artwork(player);
    int inserted = 0;
    for (uint32_t i = 0; i < count; i++) {
        /* Play Next puts each song right after the current one: walk backwards. */
        uint32_t index = last ? i : count - 1 - i;
        struct MediaEntry *entry = MediaList_GetEntryByOrderIndex(source, mode, grouped, index);
        if (!entry || !FIELD(entry, MediaEntry_song_record, void *) ||
            !MediaBrowser_EvaluateEntryFilters(browser, entry, source))
            continue;
        if (!editable(player))
            break;
        uint32_t position = last ? count_of(player) : first_pending(player);
        if (insert_at(player, entry, position))
            inserted = 1;
    }
    if (inserted)
        changed(player);
    else
        MediaPlayer_RebuildArtwork(player);
    unlock(player);
}

uint32_t queue_rows(void)
{
    void *player = lock();
    uint32_t rows = editable(player) ? upcoming(player) : 0;
    unlock(player);
    return rows;
}

int queue_changed(void)
{
    static void *last_list, *last_track;
    static uint32_t last_count;
    static int32_t last_index;
    static uint8_t last_order;
    void *player = lock();
    int valid = editable(player);
    void *list = valid ? list_of(player) : NULL;
    void *track = valid ? FIELD(player, MediaPlayer_current_track, void *) : NULL;
    uint32_t count = valid ? count_of(player) : 0;
    int32_t index = valid ? *current_index(player) : -1;
    uint8_t order = valid ? (shuffled(list) != 0) | (reversed(list) << 1) : 0;
    int changed = list != last_list || track != last_track || count != last_count ||
                  index != last_index || order != last_order;
    last_list = list;
    last_track = track;
    last_count = count;
    last_index = index;
    last_order = order;
    unlock(player);
    return changed;
}

void queue_text(uint32_t row, int artist, char *out, uint32_t capacity)
{
    if (!capacity)
        return;
    out[0] = 0;
    void *player = lock();
    if (editable(player) && row < upcoming(player)) {
        _Alignas(uint32_t) uint8_t item[MediaItem_bytes];
        _Alignas(uint32_t) uint8_t text[NativeString_bytes];
        MediaItem_InitFromRecord((struct MediaItem *)item, NULL, 0);
        if (MediaPlayer_GetItemAtIndex(player, first_pending(player) + row,
                                       (struct MediaItem *)item)) {
            void (*get)(void *, struct NativeString *) =
                FIELD(FIELD(item, 0, void **),
                      artist ? MediaItem_artist_method : MediaItem_title_method, void *);
            NativeString_InitEmpty((struct NativeString *)text);
            get(item, (struct NativeString *)text);
            const char *utf8 = NativeString_GetUtf8((struct NativeString *)text);
            uint32_t i = 0;
            for (; utf8 && utf8[i] && i + 1 < capacity; i++)
                out[i] = utf8[i];
            /* A byte limit must not leave part of a UTF-8 character. */
            while (i && ((uint8_t)utf8[i] & 0xc0) == 0x80)
                i--;
            out[i] = 0;
            NativeString_Destroy((struct NativeString *)text);
        }
        MediaItem_Destroy((struct MediaItem *)item);
    }
    unlock(player);
}

void queue_play(uint32_t row)
{
    void *player = lock();
    if (editable(player) && row < upcoming(player)) {
        struct MediaEntry *current = current_entry(player);
        uint32_t first = first_pending(player);
        struct MediaEntry *selected = PlaybackList_GetEntryByIndex(view_of(player), first + row);
        if (selected) {
            stop_artwork(player);
            if (row)
                remove_between(list_of(player), current, selected);
            /* Keep the old current entry alive until Apple has saved its
             * position and installed the selected track. */
            if (MediaPlayer_PlayIndex(player, first, 0, 0, 0) && current)
                remove_one(player, first - 1);
            changed(player);
        }
    }
    unlock(player);
}

/* Playback can advance while a context menu is open. Never edit a different
 * song that has since taken the captured row's place. */
static struct MediaEntry *menu_entry;
static struct MediaList *menu_list;

int queue_capture(uint32_t row)
{
    void *player = lock();
    menu_entry = NULL;
    menu_list = NULL;
    if (editable(player) && row < upcoming(player)) {
        menu_list = list_of(player);
        menu_entry = PlaybackList_GetEntryByIndex(view_of(player), first_pending(player) + row);
    }
    unlock(player);
    return menu_entry != NULL;
}

void queue_move(uint32_t from, uint32_t to)
{
    void *player = lock();
    if (editable(player) && from != to && from < upcoming(player) &&
        to < upcoming(player)) {
        uint32_t first = first_pending(player);
        struct MediaEntry *entry = PlaybackList_GetEntryByIndex(view_of(player), first + from);
        if (list_of(player) != menu_list || entry != menu_entry) {
            unlock(player);
            return;
        }
        stop_artwork(player);
        /* A copy at the new place, then the original goes. */
        if (insert_at(player, entry, first + to + (to > from))) {
            MediaEntry_Remove(entry);
            MediaList_NotifyChanged(list_of(player));
        }
        changed(player);
    }
    unlock(player);
}

void queue_remove(uint32_t row)
{
    void *player = lock();
    if (editable(player) && row < upcoming(player) &&
        list_of(player) == menu_list &&
        PlaybackList_GetEntryByIndex(view_of(player), first_pending(player) + row) == menu_entry) {
        stop_artwork(player);
        remove_one(player, first_pending(player) + row);
        changed(player);
    }
    unlock(player);
}

void queue_clear(void)
{
    void *player = lock();
    struct MediaEntry *current = current_entry(player);
    if (editable(player) && upcoming(player)) {
        stop_artwork(player);
        remove_between(list_of(player), current, NULL);
        changed(player);
    }
    unlock(player);
}
