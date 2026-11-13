/* SPDX-License-Identifier: GPL-3.0-only */

/* Chapter rows read from Apple's MediaItem, and the two ways a chosen chapter
 * plays: a seek of the playing track, or Apple's audiobook chapter start for
 * an episode list row. */

#include "chapters.h"
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_libc.h"
#include "rv_media.h"

/* The native string the last title lives in, and the texts of a row without
 * one: "Chapter 4294967295" and a start of at most "1193:02:47". */
static struct {
    uint32_t title[NativeString_bytes / 4];
    uint8_t title_live;
    char label[20];
    char start[12];
} chapters;

static struct MediaItem *media(const void *item)
{
    return (struct MediaItem *)item;
}

uint32_t chapters_count(const void *item)
{
    if (!rv_item_valid(item) || MediaItem_HasKind8062(media(item)))
        return 0;
    uint32_t count = MediaItem_GetChapterCount(media(item));
    return count <= INT32_MAX ? count : 0;
}

void chapters_release(void)
{
    if (chapters.title_live)
        NativeString_Destroy((struct NativeString *)chapters.title);
    chapters.title_live = 0;
}

const char *chapters_row(const void *item, uint32_t index, const char **start)
{
    chapters_release();
    *start = NULL;
    if (index >= chapters_count(item))
        return "";
    uint32_t ms = MediaItem_GetChapterStartMs(media(item), index);
    rv_format_duration(chapters.start, sizeof chapters.start, ms / 1000);
    *start = chapters.start;
    struct NativeString *title = (struct NativeString *)chapters.title;
    NativeString_InitEmpty(title);
    chapters.title_live = 1;
    /* The title lookup takes a time and finds the chapter starting at or before it. */
    const char *text = MediaItem_GetChapterTitleAt(media(item), (int)ms, title)
                           ? NativeString_GetUtf8(title)
                           : NULL;
    if (text && *text)
        return text;
    rv_format(chapters.label, sizeof chapters.label, "Chapter %lu", (unsigned long)index + 1);
    return chapters.label;
}

/* Chapter `index`'s start in `item` into *ms; 0 when the chapter is gone or
 * starts at or past the end. */
static int start_of(const void *item, uint32_t index, uint32_t *ms)
{
    if (index >= chapters_count(item))
        return 0;
    typedef uint32_t (*getter)(const void *);
    *ms = MediaItem_GetChapterStartMs(media(item), index);
    return *ms < RV_FIELD(RV_FIELD(item, 0, void *), MediaItem_duration_method, getter)(item);
}

uint32_t chapters_current(const void *item)
{
    uint32_t current[MediaItem_bytes / 4];
    int index = 0;
    void *player = rv_player_lock();
    rv_player_current(player, media(current));
    if (chapters_count(current) && rv_item_same(current, item))
        index = MediaItem_GetChapterIndexAt(media(current), MediaPlayer_GetPosition(player));
    rv_player_unlock(player);
    MediaItem_Destroy(media(current));
    return index > 0 ? (uint32_t)index : 0;
}

void chapters_seek(const void *item, uint32_t index)
{
    uint32_t current[MediaItem_bytes / 4];
    uint32_t ms;
    void *player = rv_player_lock();
    rv_player_current(player, media(current));
    if (rv_item_same(current, item) && start_of(current, index, &ms))
        MediaPlayer_Seek(player, ms);
    rv_player_unlock(player);
    MediaItem_Destroy(media(current));
}

void chapters_play(void *list, int32_t row, const void *item, uint32_t index)
{
    void *model = LibraryModelRef_Get((uint8_t *)list + MediaListController_model);
    if (row < 0 || !model)
        return;
    uint32_t selected[MediaItem_bytes / 4];
    rv_model_item(model, row, media(selected));
    uint32_t ms;
    int ok = rv_item_same(selected, item) && start_of(selected, index, &ms);
    MediaItem_Destroy(media(selected));
    if (!ok)
        return;
    /* As AudiobookChapters_SelectRow: Now Playing reads and removes the start
     * time when it activates after PlayRow. */
    MediaPlayer_Stop(MediaPlayer_GetInstance());
    uint32_t key[CppString_bytes / 4];
    CppString_Init(key, "NowPlayingStartTime");
    NavigatorProperties_SetInteger(NavigatorProperties, key, &ms);
    CppString_Destroy(key);
    MediaListController_PlayRow(list, row, 0, UINT32_MAX);
}
