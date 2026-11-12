/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../info.c"

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

/* A native string holds one pointer; init, destroy and read are counted. */
static int strings_live;
struct NativeString *NativeString_InitEmpty(struct NativeString *s)
{
    RV_FIELD(s, 0, const char *) = "";
    strings_live++;
    return s;
}
struct NativeString *NativeString_Destroy(struct NativeString *s)
{
    assert(strings_live > 0);
    strings_live--;
    RV_FIELD(s, 0, const char *) = NULL;
    return s;
}
char *NativeString_GetUtf8(struct NativeString *s)
{
    return (char *)RV_FIELD(s, 0, const char *);
}

static int item_valid = 1;
int MediaItem_IsValid(struct MediaItem *i)
{
    (void)i;
    return item_valid;
}

/* The item under test: a vtable of getters over these values and a record. */
static struct {
    const char *title, *artist, *album, *genre, *composer, *grouping, *kind, *comment, *category,
        *description, *show;
    uint32_t year, duration, chapters, track, disc, bitrate, file_size, play_count, skip_count;
    int32_t rating, release;
} fake;
static uint8_t track_record[0x100];
static uintptr_t methods[0x140 / 4];
static uint8_t item[MediaItem_bytes];

#define TEXT_GETTER(name, field)                                                                   \
    static void name(const void *i, void *s)                                                       \
    {                                                                                              \
        assert(i == item);                                                                         \
        RV_FIELD(s, 0, const char *) = fake.field;                                                 \
    }
TEXT_GETTER(get_title, title)
TEXT_GETTER(get_artist, artist)
TEXT_GETTER(get_album, album)
TEXT_GETTER(get_genre, genre)
TEXT_GETTER(get_composer, composer)
TEXT_GETTER(get_grouping, grouping)
TEXT_GETTER(get_kind, kind)
TEXT_GETTER(get_comment, comment)
TEXT_GETTER(get_category, category)
TEXT_GETTER(get_description, description)
TEXT_GETTER(get_show, show)
#define VALUE_GETTER(name, field)                                                             \
    static uint32_t name(const void *i)                                                       \
    {                                                                                         \
        assert(i == item);                                                                    \
        return (uint32_t)fake.field;                                                          \
    }
VALUE_GETTER(get_year, year)
VALUE_GETTER(get_duration, duration)
VALUE_GETTER(get_chapters, chapters)
VALUE_GETTER(get_track, track)
VALUE_GETTER(get_disc, disc)
VALUE_GETTER(get_bitrate, bitrate)
VALUE_GETTER(get_file_size, file_size)
VALUE_GETTER(get_play_count, play_count)
VALUE_GETTER(get_skip_count, skip_count)
VALUE_GETTER(get_rating, rating)
/* The date getters fill a DateTime; each has its own fixed moment. */
static void fill_date(void *out, unsigned year, unsigned month, unsigned day, unsigned hour,
                      unsigned minute)
{
    uint8_t *w = out;
    memset(w, 0, DateTime_bytes);
    RV_FIELD(w, DateTime_year, uint16_t) = (uint16_t)year;
    w[DateTime_month] = (uint8_t)month;
    w[DateTime_day] = (uint8_t)day;
    w[DateTime_hour] = (uint8_t)hour;
    w[DateTime_minute] = (uint8_t)minute;
}
#define DATE_GETTER(name, y, mo, d, h, mi)                                                    \
    static int name(void *out, const void *i)                                                 \
    {                                                                                         \
        assert(i == item);                                                                    \
        fill_date(out, y, mo, d, h, mi);                                                      \
        return 0;                                                                             \
    }
DATE_GETTER(get_added, 2024, 3, 5, 14, 7)
DATE_GETTER(get_modified, 2024, 4, 6, 9, 30)
DATE_GETTER(get_played, 2025, 1, 2, 23, 59)
DATE_GETTER(get_skipped, 2025, 2, 3, 0, 1)
static int get_release(const void *i, void *out)
{
    assert(i == item);
    if (fake.release)
        fill_date(out, 2019, 12, 24, 18, 0);
    return fake.release;
}

static void set(uint32_t slot, void *fn)
{
    methods[slot / sizeof(void *)] = (uintptr_t)fn;
}

static void reset(uint32_t kind, float sample_rate, uint16_t tracks, uint16_t discs)
{
    info_release();
    memset(&fake, 0, sizeof fake);
    fake.title = "Song A";
    fake.artist = "Artist A";
    fake.album = "Album A";
    fake.genre = "Rock";
    fake.composer = "Composer A";
    fake.grouping = "Set 1";
    fake.kind = "AAC audio file";
    fake.comment = "A note";
    fake.category = "Technology";
    fake.description = "About the episode";
    fake.show = "The Show";
    fake.release = 1;
    fake.year = 1999;
    fake.duration = 3723000;
    fake.chapters = 5;
    fake.track = 3;
    fake.disc = 1;
    fake.bitrate = 256;
    fake.file_size = 5767168;
    fake.rating = 80;
    fake.play_count = 0;
    fake.skip_count = 2;
    memset(track_record, 0, sizeof track_record);
    RV_FIELD(track_record, MediaRecord_media_kind, uint32_t) = kind;
    memcpy(track_record + MediaRecord_sample_rate, &sample_rate, 4);
    RV_FIELD(track_record, MediaRecord_track_total, uint16_t) = tracks;
    RV_FIELD(track_record, MediaRecord_disc_total, uint16_t) = discs;
    RV_FIELD(track_record, MediaRecord_volume, int16_t) = -51;
    RV_FIELD(track_record, MediaRecord_date_added, uint32_t) = 1;
    RV_FIELD(track_record, MediaRecord_date_modified, uint32_t) = 1;
    RV_FIELD(track_record, MediaRecord_last_played, uint32_t) = 1;
    RV_FIELD(track_record, MediaRecord_last_skipped, uint32_t) = 1;
    memset(item, 0, sizeof item);
    RV_FIELD(item, 0, void *) = methods;
    RV_FIELD(item, MediaItem_record, void *) = track_record;
    RV_FIELD(item, MediaItem_generation, uint32_t)++;
    item_valid = 1;
}

/* The label and value of row `index`. */
static const char *label_of(uint32_t index, const char **value)
{
    return info_row(item, index, value);
}

static void expect(uint32_t index, const char *label, const char *value)
{
    const char *got;
    const char *l = label_of(index, &got);
    if (strcmp(l, label) || strcmp(got, value))
        fprintf(stderr, "row %u: got %s = %s, want %s = %s\n", index, l, got, label, value);
    assert(!strcmp(l, label) && !strcmp(got, value));
}

static void expect_labels(const char *const *labels, uint32_t count)
{
    assert(info_rows(item) == count);
    for (uint32_t i = 0; i < count; i++) {
        const char *value;
        assert(!strcmp(label_of(i, &value), labels[i]));
        assert(value && *value);
    }
}

int main(void)
{
    set(MediaItem_title_method, get_title);
    set(MediaItem_artist_method, get_artist);
    set(MediaItem_album_method, get_album);
    set(MediaItem_genre_method, get_genre);
    set(MediaItem_composer_method, get_composer);
    set(MediaItem_grouping_method, get_grouping);
    set(MediaItem_file_type_method, get_kind);
    set(MediaItem_comment_method, get_comment);
    set(MediaItem_category_method, get_category);
    set(MediaItem_description_method, get_description);
    set(MediaItem_show_name_method, get_show);
    set(MediaItem_date_added_method, get_added);
    set(MediaItem_date_modified_method, get_modified);
    set(MediaItem_last_played_method, get_played);
    set(MediaItem_last_skipped_method, get_skipped);
    set(MediaItem_release_date_method, get_release);
    set(MediaItem_year_method, get_year);
    set(MediaItem_duration_method, get_duration);
    set(MediaItem_chapter_count_method, get_chapters);
    set(MediaItem_track_number_method, get_track);
    set(MediaItem_disc_number_method, get_disc);
    set(MediaItem_bitrate_method, get_bitrate);
    set(MediaItem_file_size_method, get_file_size);
    set(MediaItem_play_count_method, get_play_count);
    set(MediaItem_skip_count_method, get_skip_count);
    set(MediaItem_rating_method, get_rating);

    /* A song with every field. */
    reset(0, 44100.0f, 12, 0);
    assert(info_rows(item) == 23);
    expect(0, "Title", "Song A");
    expect(1, "Artist", "Artist A");
    expect(2, "Album", "Album A");
    expect(3, "Year", "1999");
    expect(4, "Genre", "Rock");
    expect(5, "Composer", "Composer A");
    expect(6, "Grouping", "Set 1");
    expect(7, "Comment", "A note");
    expect(8, "Duration", "1:02:03");
    expect(9, "Track", "3 of 12");
    expect(10, "Disc", "1");
    expect(11, "Kind", "AAC audio file");
    expect(12, "Sample Rate", "44.1 kHz");
    expect(13, "Bitrate", "256 kbps");
    expect(14, "File Size", "5.5 MiB");
    expect(15, "Volume Adjustment", "-20%");
    expect(16, "Rating", "4/5");
    expect(17, "Play Count", "0");
    expect(18, "Last Played", "2025-01-02 23:59");
    expect(19, "Skip Count", "2");
    expect(20, "Last Skipped", "2025-02-03 00:01");
    expect(21, "Date Added", "2024-03-05 14:07");
    expect(22, "Date Modified", "2024-04-06 09:30");
    /* The rows are kept for the same item; the last text value's string stays
     * until the next text value, a new item or release. */
    assert(info_rows(item) == 23 && strings_live == 1);
    expect(0, "Title", "Song A");
    assert(strings_live == 1);
    info_release();
    assert(strings_live == 0);

    /* Fields without a value are left out; a disc total below the ordinal is
     * ignored; an unset date word skips its getter. */
    reset(0, 48000.0f, 0, 1);
    fake.year = 0;
    fake.composer = "";
    fake.grouping = NULL;
    fake.comment = "";
    fake.kind = "";
    fake.disc = 2;
    fake.rating = 0;
    RV_FIELD(track_record, MediaRecord_volume, int16_t) = 0;
    RV_FIELD(track_record, MediaRecord_last_played, uint32_t) = 0;
    RV_FIELD(track_record, MediaRecord_last_skipped, uint32_t) = 0;
    RV_FIELD(track_record, MediaRecord_date_modified, uint32_t) = 0;
    static const char *const song_short[] = {
        "Title", "Artist", "Album", "Genre", "Duration", "Track", "Disc", "Sample Rate",
        "Bitrate", "File Size", "Rating", "Play Count", "Skip Count", "Date Added",
    };
    expect_labels(song_short, 14);
    expect(5, "Track", "3");
    expect(6, "Disc", "2");
    expect(7, "Sample Rate", "48 kHz");
    expect(10, "Rating", "Unrated");

    /* Short durations, odd sample rates, large files, full ratings, loud tracks. */
    reset(0, 22050.0f, 0, 0);
    fake.duration = 187000;
    fake.rating = 100;
    fake.file_size = 1048576 * 1000 + 943718;
    RV_FIELD(track_record, MediaRecord_volume, int16_t) = 255;
    expect(8, "Duration", "3:07");
    expect(10, "Disc", "1");
    expect(12, "Sample Rate", "22 kHz");
    expect(14, "File Size", "1000.8 MiB");
    expect(15, "Volume Adjustment", "+100%");
    expect(16, "Rating", "5/5");
    reset(0, -44100.0f, 0, 0);
    assert(info_rows(item) == 22); /* a negative rate is no rate */

    /* Each media kind has its own rows; audiobooks relabel artist and album,
     * videos show their series and release date. */
    reset(MediaKind_Podcast, 44100.0f, 0, 0);
    static const char *const podcast[] = {
        "Title", "Podcast", "Artist", "Category", "Genre", "Description", "Release Date",
        "Duration", "Chapters", "Kind", "Sample Rate", "Bitrate", "File Size",
        "Volume Adjustment", "Play Count", "Last Played", "Date Added",
    };
    expect_labels(podcast, 17);
    expect(1, "Podcast", "Album A");
    expect(6, "Release Date", "2019-12-24 18:00");
    expect(8, "Chapters", "5");
    fake.release = 0;
    RV_FIELD(item, MediaItem_generation, uint32_t)++;
    assert(info_rows(item) == 16);
    reset(MediaKind_ITunesU, 44100.0f, 0, 0);
    static const char *const lecture[] = {
        "Title", "Album", "Artist", "Category", "Genre", "Description", "Release Date",
        "Duration", "Chapters", "Kind", "Sample Rate", "Bitrate", "File Size",
        "Volume Adjustment", "Play Count", "Last Played", "Date Added",
    };
    expect_labels(lecture, 17);
    reset(MediaKind_Audiobook, 44100.0f, 0, 0);
    static const char *const audiobook[] = {
        "Title", "Author", "Book", "Genre", "Description", "Duration", "Chapters", "Kind",
        "Sample Rate", "Bitrate", "File Size", "Volume Adjustment", "Play Count",
        "Last Played", "Date Added",
    };
    expect_labels(audiobook, 15);
    expect(1, "Author", "Artist A");
    expect(2, "Book", "Album A");
    reset(MediaKind_MusicVideo, 44100.0f, 0, 0);
    static const char *const music_video[] = {
        "Title", "Artist", "Album", "Year", "Genre", "Comment", "Duration", "Kind", "Bitrate",
        "File Size", "Volume Adjustment", "Rating", "Play Count", "Last Played", "Date Added",
    };
    expect_labels(music_video, 15);
    static const char *const video[] = {
        "Title", "Show", "Year", "Genre", "Description", "Release Date", "Duration", "Kind",
        "Bitrate", "File Size", "Volume Adjustment", "Rating", "Play Count", "Last Played",
        "Date Added",
    };
    reset(MediaKind_Movie, 44100.0f, 0, 0);
    expect_labels(video, 15);
    expect(1, "Show", "The Show");
    reset(MediaKind_TVShow, 44100.0f, 0, 0);
    expect_labels(video, 15);
    reset(MediaKind_Rental | MediaKind_Movie, 44100.0f, 0, 0);
    fake.show = "";
    assert(info_rows(item) == 14);
    /* Audiobook wins over the other bits, as the Music menu files it. */
    reset(MediaKind_Audiobook | MediaKind_Podcast, 44100.0f, 0, 0);
    expect_labels(audiobook, 15);

    /* An item that is no longer valid keeps its rows with blank values; one
     * that was never valid has none. */
    reset(0, 44100.0f, 0, 0);
    assert(info_rows(item) == 23);
    item_valid = 0;
    expect(0, "Title", "");
    expect(13, "Bitrate", "");
    assert(strings_live == 0);
    reset(0, 44100.0f, 0, 0);
    item_valid = 0;
    assert(info_rows(item) == 0);
    assert(info_rows(NULL) == 0);
    const char *none;
    assert(!strcmp(info_row(item, 0, &none), "") && none == NULL);

    /* A different item replaces the rows; release forgets them. */
    reset(0, 44100.0f, 0, 0);
    assert(info_rows(item) == 23);
    RV_FIELD(item, MediaItem_generation, uint32_t)++;
    fake.year = 0;
    assert(info_rows(item) == 22);
    info_release();
    assert(strings_live == 0);
    fake.year = 2001;
    assert(info_rows(item) == 23);
    info_release();
    assert(strings_live == 0);
    return 0;
}
