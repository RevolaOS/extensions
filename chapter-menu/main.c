/* SPDX-License-Identifier: GPL-3.0-only */

/* Chapters in the Now Playing, podcast and iTunes U episode context menus: the item's
 * chapter titles and start times. Choosing one seeks the playing track there,
 * or plays the episode from it. */

#include "chapters.h"
#include "rv_api.h"
#include "rv_ui.h"
#include <stddef.h>

static int has_chapters(const struct rv_context *ctx)
{
    return chapters_count(ctx->item) != 0;
}

static uint32_t count(const struct rv_context *ctx)
{
    return chapters_count(ctx->item);
}

static const char *row(const struct rv_context *ctx, uint32_t index, const char **detail)
{
    return chapters_row(ctx->item, index, detail);
}

static int select(const struct rv_context *ctx, uint32_t index)
{
    if (ctx->list)
        chapters_play(ctx->list, ctx->row, ctx->item, index);
    else
        chapters_seek(ctx->item, index);
    return 1;
}

static uint32_t initial(const struct rv_context *ctx)
{
    return ctx->list ? 0 : chapters_current(ctx->item);
}

static void closed(const struct rv_context *ctx)
{
    (void)ctx;
    chapters_release();
}

static const struct rv_screen screens[] = {
    RV_SCREEN_DYNAMIC_LIST("chapters", "Chapters", count, row, select, .layout = RV_LAYOUT_FULL,
                           .dynamic.detail = RV_DETAIL_BESIDE, .dynamic.initial = initial,
                           .closed = closed),
};

#define CHAPTERS(menu_)                                                                        \
    RV_MENU_ENTRY(menu_, 0, RV_ROW_PUSH("Chapters", "chapters"), .available = has_chapters)

static const struct rv_menu_entry entries[] = {
    CHAPTERS(RV_MENU_NOW_PLAYING_CONTEXT),
    CHAPTERS(RV_MENU_PODCAST_EPISODE_CONTEXT),
    CHAPTERS(RV_MENU_ITUNES_U_EPISODE_CONTEXT),
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
