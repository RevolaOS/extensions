/* SPDX-License-Identifier: GPL-3.0-only */

/* Cover Grid: a row in Music and on the main menu that opens the grid on
 * Cover Flow's library query, music-only album groups sorted by artist. */

#include "geometry.h"
#include "shared.h"
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_menus.h"
#include "rv_ui.h"
#include <stddef.h>

static const struct rv_api *api;
static rv_extension_handle self;

/* The grid reopens on the last album while it exists; the screen module
 * unloads after the grid closes, so the selection lives here. */
static struct grid_link link;

static void release(void)
{
    if (link.reference)
        LibraryModelRef_Release(&link.reference);
    link.reference = NULL;
}

static void open_grid(const struct rv_context *ctx)
{
    void *menu = ctx->controller;
    /* Cover Flow's handler raises the artwork preview first; the query returns
     * the album count, 0 for a library without music. */
    RV_FIELD(menu, MainMediaMenu_preview_kind, uint8_t) = 1;
    if (!MusicMenu_ConfigureLibraryBrowser(menu, MediaKind_Music, 0, 1, LibraryGrouping_Albums,
                                           LibrarySort_Artist, 0))
        return;
    release();
    void *model = LibraryBrowser_Create(0);
    if (!model)
        return;
    LibraryModelRef_Init(&link.reference, model, 0);
    if (link.selected >= grid_album_count(model))
        link.selected = link.top = 0;
    link.top = grid_top_row(link.top, link.selected);
    if (api->ui_push(self, "grid", &link) != RV_OK)
        release();
}

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_MUSIC, RV_MUSIC_COVER_FLOW, RV_ROW_ACTION("Cover Grid", open_grid),
                  .shown_by = KEY_IN_MUSIC),
    RV_MENU_ENTRY(RV_MENU_MAIN, RV_MAIN_COVER_FLOW, RV_ROW_ACTION("Cover Grid", open_grid),
                  .shown_by = KEY_IN_MAIN, .preview = RV_PREVIEW_MUSIC),
};

static const struct rv_setting settings[] = {
    RV_SETTING_BOOL(KEY_IN_MUSIC, "Cover Grid in Music", 1),
    RV_SETTING_BOOL(KEY_IN_MAIN, "Cover Grid in Main Menu", 0),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    .unload = release,
    RV_SETTINGS(settings),
    RV_ENTRIES(entries),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    api = core;
    self = handle;
    return &extension;
}
