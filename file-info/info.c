/* SPDX-License-Identifier: GPL-3.0-only */

/* The rows of File Info: which fields a media kind shows, in which order, and
 * each value formatted from the MediaItem's getters and its iTunesDB record. */

#include "info.h"
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_libc.h"
#include "rv_media.h"

enum field {
    TITLE,
    ARTIST,
    AUTHOR,
    ALBUM,
    PODCAST,
    BOOK,
    SHOW,
    YEAR,
    GENRE,
    CATEGORY,
    COMPOSER,
    GROUPING,
    COMMENT,
    DESCRIPTION,
    RELEASE_DATE,
    DURATION,
    CHAPTERS,
    TRACK,
    DISC,
    KIND,
    SAMPLE_RATE,
    BITRATE,
    FILE_SIZE,
    VOLUME,
    RATING,
    PLAY_COUNT,
    LAST_PLAYED,
    SKIP_COUNT,
    LAST_SKIPPED,
    DATE_ADDED,
    DATE_MODIFIED,
    FIELDS
};

/* Labels in field order, each found by its offset into one string. */
static const char labels[] =
    "Title\0Artist\0Author\0Album\0Podcast\0Book\0Show\0Year\0Genre\0Category\0"
    "Composer\0Grouping\0Comment\0Description\0Release Date\0Duration\0Chapters\0"
    "Track\0Disc\0Kind\0Sample Rate\0Bitrate\0File Size\0Volume Adjustment\0"
    "Rating\0Play Count\0Last Played\0Skip Count\0Last Skipped\0Date Added\0"
    "Date Modified";
static const uint16_t label_offsets[FIELDS] = {
    0,   6,   13,  20,  26,  34,  39,  44,  49,  55,  64,  73,  82,  90,  102, 115,
    124, 133, 139, 144, 149, 161, 169, 179, 197, 204, 215, 227, 238, 251, 262,
};
_Static_assert(sizeof labels == 262 + sizeof "Date Modified", "label offsets drifted");

/* Rows per media kind, in display order. Fields without a value are left out. */
static const uint8_t song_rows[] = {
    TITLE,     ARTIST,     ALBUM,       YEAR,       GENRE,        COMPOSER,   GROUPING,
    COMMENT,   DURATION,   TRACK,       DISC,       KIND,         SAMPLE_RATE, BITRATE,
    FILE_SIZE, VOLUME,     RATING,      PLAY_COUNT, LAST_PLAYED,  SKIP_COUNT, LAST_SKIPPED,
    DATE_ADDED, DATE_MODIFIED, FIELDS,
};
static const uint8_t podcast_rows[] = {
    TITLE,     PODCAST,     ARTIST,  CATEGORY,  GENRE,      DESCRIPTION, RELEASE_DATE,
    DURATION,  CHAPTERS,    KIND,    SAMPLE_RATE, BITRATE,  FILE_SIZE,   VOLUME,
    PLAY_COUNT, LAST_PLAYED, DATE_ADDED, FIELDS,
};
static const uint8_t lecture_rows[] = {
    TITLE,     ALBUM,       ARTIST,  CATEGORY,  GENRE,      DESCRIPTION, RELEASE_DATE,
    DURATION,  CHAPTERS,    KIND,    SAMPLE_RATE, BITRATE,  FILE_SIZE,   VOLUME,
    PLAY_COUNT, LAST_PLAYED, DATE_ADDED, FIELDS,
};
static const uint8_t audiobook_rows[] = {
    TITLE,     AUTHOR,  BOOK,      GENRE,      DESCRIPTION, DURATION,   CHAPTERS, KIND,
    SAMPLE_RATE, BITRATE, FILE_SIZE, VOLUME,   PLAY_COUNT,  LAST_PLAYED, DATE_ADDED, FIELDS,
};
static const uint8_t music_video_rows[] = {
    TITLE,     ARTIST,  ALBUM,     YEAR,       GENRE,       COMMENT,    DURATION, KIND,
    BITRATE,   FILE_SIZE, VOLUME,  RATING,     PLAY_COUNT,  LAST_PLAYED, DATE_ADDED, FIELDS,
};
static const uint8_t video_rows[] = {
    TITLE,     SHOW,      YEAR,     GENRE,      DESCRIPTION, RELEASE_DATE, DURATION, KIND,
    BITRATE,   FILE_SIZE, VOLUME,   RATING,     PLAY_COUNT,  LAST_PLAYED,  DATE_ADDED, FIELDS,
};

/* The item the rows were worked out for, the native string the last text
 * value lives in, and the formatted number of the last other value. */
static struct {
    uint32_t item[MediaItem_bytes / 4];
    uint32_t text[NativeString_bytes / 4];
    uint8_t text_live;
    uint8_t unknown; /* the requested field has no value */
    uint8_t rows[FIELDS];
    uint8_t count;
    char value[24]; /* the longest is "4294967295 kbps" */
} info;

static const uint8_t *record(const void *item)
{
    return RV_FIELD(item, MediaItem_record, const uint8_t *);
}

static void release_text(void)
{
    if (info.text_live)
        NativeString_Destroy((struct NativeString *)info.text);
    info.text_live = 0;
}

static void number(uint32_t value, const char *unit)
{
    if (!value) {
        info.unknown = 1;
        return;
    }
    rv_format(info.value, sizeof info.value, "%lu%s", (unsigned long)value, unit);
}

static const char *text(const void *item, enum field field)
{
    static const uint16_t slots[FIELDS] = {
        [TITLE] = MediaItem_title_method,         [ARTIST] = MediaItem_artist_method,
        [AUTHOR] = MediaItem_artist_method,       [ALBUM] = MediaItem_album_method,
        [PODCAST] = MediaItem_album_method,       [BOOK] = MediaItem_album_method,
        [SHOW] = MediaItem_show_name_method,      [GENRE] = MediaItem_genre_method,
        [CATEGORY] = MediaItem_category_method,   [COMPOSER] = MediaItem_composer_method,
        [GROUPING] = MediaItem_grouping_method,   [COMMENT] = MediaItem_comment_method,
        [DESCRIPTION] = MediaItem_description_method, [KIND] = MediaItem_file_type_method,
    };
    release_text();
    const char *data = rv_item_text(item, slots[field], (struct NativeString *)info.text);
    info.text_live = 1;
    if (data)
        return data;
    info.unknown = 1;
    return "";
}

static void ordinal(uint32_t value, uint32_t total)
{
    number(value, "");
    if (value && total >= value)
        rv_format(info.value, sizeof info.value, "%lu of %lu", (unsigned long)value,
                  (unsigned long)total);
}

/* A local date and time from one of the DateTime getters: "2024-03-05 14:07".
 * The record's own word is checked first, since a getter turns an unset
 * date into 1904. Release dates have no word; their getter says whether
 * the record has one. */
static void date(const void *item, uint32_t slot, int present)
{
    typedef int (*getter)(void *, const void *);
    typedef int (*release_getter)(const void *, void *);
    uint16_t when[DateTime_bytes / 2];
    const uint8_t *w = (const uint8_t *)when;
    if (present && slot == MediaItem_release_date_method)
        present = RV_FIELD(RV_FIELD(item, 0, void *), slot, release_getter)(item, when);
    else if (present)
        RV_FIELD(RV_FIELD(item, 0, void *), slot, getter)(when, item);
    if (!present) {
        info.unknown = 1;
        return;
    }
    rv_format(info.value, sizeof info.value, "%u-%02u-%02u %02u:%02u",
              RV_FIELD(w, DateTime_year, uint16_t), w[DateTime_month], w[DateTime_day],
              w[DateTime_hour], w[DateTime_minute]);
}

/* Sound Check's per-track adjustment, -255..255 for -100%..+100%. */
static void volume(int32_t adjustment)
{
    if (!adjustment) {
        info.unknown = 1;
        return;
    }
    rv_format(info.value, sizeof info.value, "%s%d%%", adjustment > 0 ? "+" : "",
              (int)(adjustment * 100 / 255));
}

/* The record keeps the sample rate as an IEEE single. */
static void sample_rate(uint32_t bits)
{
    uint32_t exponent = bits >> 23 & 0xff, hertz = 0;
    if (!(bits >> 31) && exponent >= 127 && exponent < 150)
        hertz = ((bits & 0x7fffff) | 0x800000) >> (150 - exponent);
    if (!hertz) {
        info.unknown = 1;
        return;
    }
    if (hertz % 1000 >= 100)
        rv_format(info.value, sizeof info.value, "%lu.%lu kHz", (unsigned long)(hertz / 1000),
                  (unsigned long)(hertz % 1000 / 100));
    else
        rv_format(info.value, sizeof info.value, "%lu kHz", (unsigned long)(hertz / 1000));
}

static const char *value(const void *item, enum field field)
{
    const uint8_t *r = record(item);
    switch (field) {
    case YEAR:
        number(rv_item_value(item, MediaItem_year_method) & 0xffff, "");
        break;
    case DURATION: {
        uint32_t ms = rv_item_value(item, MediaItem_duration_method);
        if (ms)
            rv_format_duration(info.value, sizeof info.value, ms / 1000);
        else
            info.unknown = 1;
        break;
    }
    case CHAPTERS:
        number(rv_item_value(item, MediaItem_chapter_count_method), "");
        break;
    /* The record keeps the track and disc totals beside the ordinals. */
    case TRACK:
        ordinal(rv_item_value(item, MediaItem_track_number_method) & 0xffff,
                RV_FIELD(r, MediaRecord_track_total, uint16_t));
        break;
    case DISC:
        ordinal(rv_item_value(item, MediaItem_disc_number_method) & 0xffff,
                RV_FIELD(r, MediaRecord_disc_total, uint16_t));
        break;
    case SAMPLE_RATE:
        sample_rate(RV_FIELD(r, MediaRecord_sample_rate, uint32_t));
        break;
    case BITRATE:
        number(rv_item_value(item, MediaItem_bitrate_method) & 0xffff, " kbps");
        break;
    case FILE_SIZE: {
        uint32_t bytes = rv_item_value(item, MediaItem_file_size_method);
        if (bytes)
            rv_format(info.value, sizeof info.value, "%lu.%lu MiB", (unsigned long)(bytes >> 20),
                      (unsigned long)((bytes & 0xfffff) * 10 >> 20));
        else
            info.unknown = 1;
        break;
    }
    case RATING: {
        int32_t rating = (int32_t)rv_item_value(item, MediaItem_rating_method);
        if (rating > 0 && rating <= 100)
            rv_format(info.value, sizeof info.value, "%d/5", (int)(rating / 20));
        else
            rv_format(info.value, sizeof info.value, "Unrated");
        break;
    }
    case VOLUME:
        volume(RV_FIELD(r, MediaRecord_volume, int16_t));
        break;
    case RELEASE_DATE:
        date(item, MediaItem_release_date_method, 1);
        break;
    case LAST_PLAYED:
        date(item, MediaItem_last_played_method,
             RV_FIELD(r, MediaRecord_last_played, uint32_t) != 0);
        break;
    case LAST_SKIPPED:
        date(item, MediaItem_last_skipped_method,
             RV_FIELD(r, MediaRecord_last_skipped, uint32_t) != 0);
        break;
    case DATE_ADDED:
        date(item, MediaItem_date_added_method, RV_FIELD(r, MediaRecord_date_added, uint32_t) != 0);
        break;
    case DATE_MODIFIED:
        date(item, MediaItem_date_modified_method,
             RV_FIELD(r, MediaRecord_date_modified, uint32_t) != 0);
        break;
    case PLAY_COUNT:
        rv_format(info.value, sizeof info.value, "%lu",
                  (unsigned long)rv_item_value(item, MediaItem_play_count_method));
        break;
    case SKIP_COUNT:
        rv_format(info.value, sizeof info.value, "%lu",
                  (unsigned long)rv_item_value(item, MediaItem_skip_count_method));
        break;
    default:
        return text(item, field);
    }
    return info.value;
}

static const uint8_t *rows_for(uint32_t kind)
{
    if (kind & MediaKind_Audiobook)
        return audiobook_rows;
    if (kind & MediaKind_Podcast)
        return podcast_rows;
    if (kind & MediaKind_ITunesU)
        return lecture_rows;
    if (kind & MediaKind_MusicVideo)
        return music_video_rows;
    if (kind & (MediaKind_Movie | MediaKind_TVShow | MediaKind_Rental))
        return video_rows;
    return song_rows;
}

uint32_t info_rows(const void *item)
{
    if (!item)
        return 0;
    if (!memcmp(item, info.item, MediaItem_bytes))
        return info.count;
    memcpy(info.item, item, MediaItem_bytes);
    info.count = 0;
    if (!rv_item_valid(item))
        return 0;
    uint32_t kind = RV_FIELD(record(item), MediaRecord_media_kind, uint32_t);
    for (const uint8_t *row = rows_for(kind); *row != FIELDS; row++) {
        info.unknown = 0;
        value(item, (enum field)*row);
        if (!info.unknown)
            info.rows[info.count++] = *row;
    }
    release_text();
    return info.count;
}

const char *info_row(const void *item, uint32_t index, const char **out)
{
    if (info_rows(item) <= index) {
        *out = NULL;
        return "";
    }
    enum field field = (enum field)info.rows[index];
    info.unknown = 0;
    info.value[0] = 0;
    /* The list can outlive the item when the library reloads. */
    *out = rv_item_valid(item) ? value(item, field) : info.value;
    return labels + label_offsets[field];
}

void info_release(void)
{
    release_text();
    memset(info.item, 0, sizeof info.item);
    info.count = 0;
}
