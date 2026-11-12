/* SPDX-License-Identifier: GPL-3.0-only */

/* File Info in the context menus of songs, podcast and iTunes U episodes,
 * audiobooks, movies, TV episodes, video playlists and Now Playing: the item's
 * tags and file properties as a list of label and value rows.
 *
 * Rewrite of RepriseOS payload/song_info.c (beef0e9) by lgreitis. */

#include "info.h"
#include "rv_api.h"
#include "rv_ui.h"
#include <stddef.h>

static int has_item(const struct rv_context *ctx)
{
    return ctx->item != NULL;
}

static uint32_t count(const struct rv_context *ctx)
{
    return info_rows(ctx->item);
}

static const char *row(const struct rv_context *ctx, uint32_t index, const char **detail)
{
    return info_row(ctx->item, index, detail);
}

static int stay(const struct rv_context *ctx, uint32_t index)
{
    (void)ctx;
    (void)index;
    return 0;
}

static uint32_t first(const struct rv_context *ctx)
{
    (void)ctx;
    return 0;
}

static void closed(const struct rv_context *ctx)
{
    (void)ctx;
    info_release();
}

static const struct rv_screen screens[] = {
    RV_SCREEN_DYNAMIC_LIST("info", "File Info", count, row, stay, .layout = RV_LAYOUT_FULL,
                           .dynamic.detail = RV_DETAIL_BELOW, .dynamic.initial = first,
                           .closed = closed),
};

#define FILE_INFO(menu_) \
    RV_MENU_ENTRY(menu_, 0, RV_ROW_PUSH("File Info", "info"), .available = has_item)

static const struct rv_menu_entry entries[] = {
    FILE_INFO(RV_MENU_SONG_CONTEXT),
    FILE_INFO(RV_MENU_NOW_PLAYING_CONTEXT),
    FILE_INFO(RV_MENU_PODCAST_EPISODE_CONTEXT),
    FILE_INFO(RV_MENU_ITUNES_U_EPISODE_CONTEXT),
    FILE_INFO(RV_MENU_AUDIOBOOK_CONTEXT),
    FILE_INFO(RV_MENU_MOVIE_CONTEXT),
    FILE_INFO(RV_MENU_TV_EPISODE_CONTEXT),
    FILE_INFO(RV_MENU_VIDEO_PLAYLIST_CONTEXT),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_SCREENS(screens),
    RV_ENTRIES(entries),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *api, rv_extension_handle self)
{
    (void)api;
    (void)self;
    return &extension;
}
