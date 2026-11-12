/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../chapters.c"

/* The host formats; the device's rv_libc does the same for these conversions. */
uint32_t rv_format(char *out, uint32_t capacity, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = vsnprintf(out, capacity, format, args);
    va_end(args);
    return (uint32_t)n;
}
uint32_t rv_format_duration(char *out, uint32_t capacity, uint32_t seconds)
{
    uint32_t minutes = seconds / 60;
    if (minutes < 60)
        return rv_format(out, capacity, "%u:%02u", minutes, seconds % 60);
    return rv_format(out, capacity, "%u:%02u:%02u", minutes / 60, minutes % 60, seconds % 60);
}

/* A native string holds one pointer; init and destroy are counted. */
static int strings_live;
struct NativeString *NativeString_InitEmpty(struct NativeString *s)
{
    FIELD(s, 0, const char *) = "";
    strings_live++;
    return s;
}
struct NativeString *NativeString_Destroy(struct NativeString *s)
{
    assert(strings_live > 0);
    strings_live--;
    FIELD(s, 0, const char *) = NULL;
    return s;
}
char *NativeString_GetUtf8(struct NativeString *s)
{
    return (char *)FIELD(s, 0, const char *);
}

/* The library: one record whose chapters, duration, validity and kind the
 * tests set. Items are views of it: a vtable for the duration getter, the
 * record and its generation. */
static struct {
    uint32_t count, duration, generation;
    uint32_t starts[4];
    const char *titles[4];
    int valid, video;
} fake;
static _Alignas(8) uint8_t track_record[16];
static uintptr_t methods[MediaItem_duration_method / sizeof(void *) + 1];
static _Alignas(8) uint8_t item[MediaItem_bytes];

static uint32_t get_duration(const void *i)
{
    (void)i;
    return fake.duration;
}

static void view(void *i)
{
    memset(i, 0, MediaItem_bytes);
    FIELD(i, 0, void *) = methods;
    FIELD(i, MediaItem_record, void *) = track_record;
    FIELD(i, MediaItem_generation, uint32_t) = fake.generation;
}

int MediaItem_IsValid(struct MediaItem *i)
{
    (void)i;
    return fake.valid;
}
uint32_t MediaItem_HasKind8062(struct MediaItem *i)
{
    (void)i;
    return (uint32_t)fake.video;
}
uint32_t MediaItem_GetChapterCount(struct MediaItem *i)
{
    (void)i;
    return fake.count;
}
uint32_t MediaItem_GetChapterStartMs(struct MediaItem *i, uint32_t index)
{
    (void)i;
    return index < fake.count ? fake.starts[index] : 0;
}
/* As Apple's: the last chapter starting at or before the time. */
int MediaItem_GetChapterTitleAt(struct MediaItem *i, int position_ms, struct NativeString *out)
{
    (void)i;
    for (uint32_t n = fake.count; n-- > 0;)
        if (fake.starts[n] <= (uint32_t)position_ms) {
            if (!fake.titles[n])
                return 0;
            FIELD(out, 0, const char *) = fake.titles[n];
            return 1;
        }
    return 0;
}
int MediaItem_GetChapterIndexAt(struct MediaItem *i, uint32_t position_ms)
{
    (void)i;
    int index = -1;
    for (uint32_t n = 0; n < fake.count; n++)
        if (fake.starts[n] <= position_ms)
            index = (int)n;
    return index;
}

static int items_live;
struct MediaItem *MediaItem_InitFromRecord(struct MediaItem *i, void *record, uint8_t flags)
{
    (void)flags;
    assert(!record);
    memset(i, 0, MediaItem_bytes);
    items_live++;
    return i;
}
void MediaItem_Destroy(struct MediaItem *i)
{
    (void)i;
    assert(items_live > 0);
    items_live--;
}

/* The player plays the record (or nothing) at `position`; the mutex is
 * counted so a seek can be checked to run under it. */
static _Alignas(8) uint8_t player[MediaPlayer_mutex + 4];
static int locked, playing;
static uint32_t position, seeks, seek_to;
void *MediaPlayer_GetInstance(void)
{
    return player;
}
int Mutex_Lock(void *mutex)
{
    assert(mutex == player + MediaPlayer_mutex);
    locked++;
    return 0;
}
int Mutex_Unlock(void *mutex)
{
    assert(mutex == player + MediaPlayer_mutex && locked > 0);
    locked--;
    return 0;
}
int MediaPlayer_GetCurrentItem(void *p, struct MediaItem *i)
{
    assert(p == player && locked);
    if (playing)
        view(i);
    return playing;
}
uint32_t MediaPlayer_GetPosition(void *p)
{
    assert(p == player);
    return position;
}
int MediaPlayer_Seek(void *p, uint32_t ms)
{
    assert(p == player && locked);
    seeks++;
    seek_to = ms;
    return 1;
}

/* The episode list: its model hands out a view of the record for any row;
 * the play path records what it was asked. */
static _Alignas(8) uint8_t list[MediaListController_model + sizeof(void *)];
static uintptr_t model_methods[SongModel_get_item_method / sizeof(void *) + 1];
static void *model[1] = {model_methods};
static int32_t got_row;
static void get_item(struct MediaItem *i, void *m, int32_t row)
{
    assert(m == model);
    got_row = row;
    view(i);
}
void *LibraryModelRef_Get(void *reference)
{
    assert(reference == list + MediaListController_model);
    return model;
}
uint8_t NavigatorProperties[4];
static int stops, plays, keys_live;
static uint32_t start_time;
static int32_t played_row;
void MediaPlayer_Stop(void *p)
{
    assert(p == player);
    stops++;
}
void *CppString_Init(void *s, const char *text)
{
    assert(!strcmp(text, "NowPlayingStartTime"));
    FIELD(s, 0, const char *) = text;
    keys_live++;
    return s;
}
void CppString_Destroy(void *s)
{
    (void)s;
    keys_live--;
}
void NavigatorProperties_SetInteger(void *properties, void *key, const uint32_t *value)
{
    assert(properties == NavigatorProperties && keys_live == 1 && stops == plays + 1);
    assert(!strcmp(FIELD(key, 0, const char *), "NowPlayingStartTime"));
    start_time = *value;
}
void MediaListController_PlayRow(void *controller, int row, int mode, uint32_t shuffle)
{
    assert(controller == list && mode == 0 && shuffle == UINT32_MAX);
    plays++;
    played_row = row;
}

static void reset(void)
{
    chapters_release();
    memset(&fake, 0, sizeof fake);
    fake.valid = 1;
    fake.count = 3;
    fake.duration = 3723000;
    fake.generation = 7;
    fake.starts[1] = 60000;
    fake.starts[2] = 3600000;
    fake.titles[0] = "Intro";
    fake.titles[2] = "Final Hour";
    methods[MediaItem_duration_method / sizeof(void *)] = (uintptr_t)get_duration;
    model_methods[SongModel_get_item_method / sizeof(void *)] = (uintptr_t)get_item;
    FIELD(list, MediaListController_model, void *) = model;
    view(item);
    playing = 1;
    position = 0;
    seeks = 0;
    stops = plays = 0;
    start_time = 0;
}

static void expect(uint32_t index, const char *label, const char *start)
{
    const char *detail;
    const char *text = chapters_row(item, index, &detail);
    if (strcmp(text, label) || (start ? !detail || strcmp(detail, start) : detail != NULL)) {
        fprintf(stderr, "row %u: %s / %s, want %s / %s\n", index, text, detail ? detail : "-",
                label, start ? start : "-");
        assert(0);
    }
}

int main(void)
{
    /* Rows: the title, or "Chapter N" when it has none; the start beside it. */
    reset();
    assert(chapters_count(item) == 3);
    expect(0, "Intro", "0:00");
    expect(1, "Chapter 2", "1:00");
    expect(2, "Final Hour", "1:00:00");
    expect(3, "", NULL);
    chapters_release();
    assert(strings_live == 0);

    /* No rows for no item, a stale item, a video or an absurd count. */
    assert(chapters_count(NULL) == 0);
    fake.valid = 0;
    assert(chapters_count(item) == 0);
    expect(0, "", NULL);
    reset();
    fake.video = 1;
    assert(chapters_count(item) == 0);
    reset();
    fake.count = 0x80000000u;
    assert(chapters_count(item) == 0);
    reset();
    FIELD(item, MediaItem_record, void *) = NULL;
    assert(chapters_count(item) == 0);

    /* Now Playing: the current chapter is highlighted, a chosen one seeks
     * the same track under the player mutex. */
    reset();
    position = 61000;
    assert(chapters_current(item) == 1);
    position = 0;
    fake.starts[0] = 5000;
    assert(chapters_current(item) == 0); /* before the first chapter */
    reset();
    chapters_seek(item, 2);
    assert(seeks == 1 && seek_to == 3600000 && !locked && items_live == 0);
    chapters_seek(item, 0);
    assert(seeks == 2 && seek_to == 0);

    /* Never seek another track, a reloaded record, a missing chapter or one
     * at or past the end. */
    reset();
    playing = 0;
    chapters_seek(item, 1);
    assert(chapters_current(item) == 0);
    reset();
    fake.generation++;
    chapters_seek(item, 1);
    reset();
    chapters_seek(item, 3);
    reset();
    fake.duration = 3600000;
    chapters_seek(item, 2);
    assert(seeks == 0 && !locked && items_live == 0);

    /* Episode list: stop, store the start, play the row. */
    reset();
    chapters_play(list, 4, item, 1);
    assert(stops == 1 && plays == 1 && played_row == 4 && got_row == 4);
    assert(start_time == 60000 && keys_live == 0 && items_live == 0);

    /* Nothing for a row that changed, a negative row or a missing chapter. */
    reset();
    fake.generation++;
    chapters_play(list, 4, item, 1);
    chapters_play(list, -1, item, 1);
    reset();
    chapters_play(list, 0, item, 3);
    assert(stops == 0 && plays == 0 && items_live == 0);
    return 0;
}
