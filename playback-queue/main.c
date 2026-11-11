/* SPDX-License-Identifier: GPL-3.0-only */

/* Play Next and Play Last in music and audio podcast episode context menus,
 * and Queue: pending items in Music, the Main menu and the
 * Now Playing context menu. */

#include "queue.h"
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_menus.h"
#include "rv_sites.h"
#include "rv_ui.h"
#include <stddef.h>

#define KEY_IN_MUSIC 1 /* Settings > Music Menu and Main Menu flip these two */
#define KEY_IN_MAIN 2

static const struct rv_api *api;
static rv_extension_handle self;

/* Play Last flag of the Add to On-The-Go in progress, -1 when none is. */
static int32_t adding = -1;

/* Album and artist rows reuse Add to On-The-Go: it filters the library to the
 * selection and hands it to the On-The-Go playlist here. */
static uint32_t on_the_go_add(uint32_t otg, uint32_t row, uint32_t browser, uint32_t save,
                              const void *next)
{
    if (adding < 0)
        return api->hook_next(next, otg, row, browser, save, 0, 0);
    if ((int32_t)row == -1)
        queue_add_browser((void *)browser, adding);
    return 1;
}

static void add_selection(const struct rv_context *ctx, int last)
{
    adding = last;
    MediaListController_AddSelectionToOnTheGo(ctx->controller);
    adding = -1;
}

static void song_next(const struct rv_context *ctx) { queue_add_item(ctx->item, 0); }
static void song_last(const struct rv_context *ctx) { queue_add_item(ctx->item, 1); }
static void selection_next(const struct rv_context *ctx) { add_selection(ctx, 0); }
static void selection_last(const struct rv_context *ctx) { add_selection(ctx, 1); }

static int available(const struct rv_context *ctx)
{
    (void)ctx;
    return queue_available();
}

static int can_add(const struct rv_context *ctx)
{
    (void)ctx;
    return queue_can_add();
}

static int can_add_item(const struct rv_context *ctx)
{
    return queue_can_add_item(ctx->item);
}

static uint32_t toggle_stopped(uint32_t player, const void *next)
{
    if (queue_start_idle())
        return 1;
    return api->hook_next(next, player, 0, 0, 0, 0, 0);
}

static int upcoming(const struct rv_context *ctx)
{
    (void)ctx;
    return queue_rows() != 0;
}

/* Queue */

static char title[128], artist[128];
static struct rv_timer *refresh_timer;
static uint32_t queue_views;

static void refresh_queue(void *arg)
{
    (void)arg;
    if (queue_changed())
        api->ui_refresh(self, "queue");
}

static void queue_opened(const struct rv_context *ctx)
{
    (void)ctx;
    if (queue_views++)
        return;
    queue_changed();
    refresh_timer = api->timer_start(self, 500, 1, refresh_queue, NULL);
}

static void queue_closed(const struct rv_context *ctx)
{
    (void)ctx;
    if (--queue_views)
        return;
    if (refresh_timer)
        api->timer_stop(self, refresh_timer);
    refresh_timer = NULL;
}
static uint32_t song_count(const struct rv_context *ctx)
{
    (void)ctx;
    return queue_rows();
}

static const char *song_row(const struct rv_context *ctx, uint32_t index, const char **detail)
{
    (void)ctx;
    queue_text(index, 0, title, sizeof title);
    queue_text(index, 1, artist, sizeof artist);
    *detail = artist;
    return title;
}

static uint32_t song_first(const struct rv_context *ctx)
{
    (void)ctx;
    return 0;
}

static int song_select(const struct rv_context *ctx, uint32_t index)
{
    queue_play(index);
    void *created = ControllerRegistry_InvokeControllerFactory(
        UI_GetControllerRegistry(), "TMediaNowPlayingCntlr", "TMediaNowPlayingCntlr");
    if (created) {
        void (*push)(void *, void *) = *(void **)((uint8_t *)*(void **)ctx->controller +
                                                 ScreenController_push_method);
        push(ctx->controller, created);
    }
    api->ui_select(self, "queue", 0);
    return 0;
}

static int can_remove(const struct rv_context *ctx)
{
    return ctx->row >= 0 && queue_capture(ctx->row);
}

static int can_move_up(const struct rv_context *ctx)
{
    return ctx->row > 0 && can_remove(ctx);
}

static int can_move_down(const struct rv_context *ctx)
{
    return can_remove(ctx) && (uint32_t)ctx->row + 1 < queue_rows();
}

static void move_top(const struct rv_context *ctx)
{
    queue_move(ctx->row, 0);
    api->ui_refresh(self, "queue");
}

static void move_up(const struct rv_context *ctx)
{
    if (ctx->row > 0)
        queue_move(ctx->row, ctx->row - 1);
    api->ui_refresh(self, "queue");
}

static void move_down(const struct rv_context *ctx)
{
    if (ctx->row >= 0)
        queue_move(ctx->row, ctx->row + 1);
    api->ui_refresh(self, "queue");
}

static void move_bottom(const struct rv_context *ctx)
{
    uint32_t count = queue_rows();
    if (count)
        queue_move(ctx->row, count - 1);
    api->ui_refresh(self, "queue");
}

static void remove_song(const struct rv_context *ctx)
{
    queue_remove(ctx->row);
    api->ui_refresh(self, "queue");
    api->ui_select(self, "queue", ctx->row > 0 ? (uint32_t)ctx->row - 1 : 0);
}

static const struct rv_context_row song_menu[] = {
    RV_CONTEXT_ROW(RV_ROW_ACTION("Move to Top", move_top), .available = can_move_up),
    RV_CONTEXT_ROW(RV_ROW_ACTION("Move Up", move_up), .available = can_move_up),
    RV_CONTEXT_ROW(RV_ROW_ACTION("Move Down", move_down), .available = can_move_down),
    RV_CONTEXT_ROW(RV_ROW_ACTION("Move to Bottom", move_bottom), .available = can_move_down),
    RV_CONTEXT_ROW(RV_ROW_ACTION("Remove", remove_song), .available = can_remove),
    RV_CONTEXT_ROW(RV_ROW_PUSH("Clear Queue", "clear"), .available = upcoming),
};

static void clear_confirmed(const struct rv_context *ctx)
{
    (void)ctx;
    queue_clear();
    api->ui_pop(self);
    api->ui_refresh(self, "queue");
}

static void clear_cancelled(const struct rv_context *ctx)
{
    (void)ctx;
    api->ui_pop(self);
}

static const struct rv_row clear_rows[] = {
    RV_ROW_ACTION("Clear Queue", clear_confirmed),
    RV_ROW_ACTION("Cancel", clear_cancelled),
};

static const struct rv_screen screens[] = {
    RV_SCREEN_DYNAMIC_LIST("queue", "Queue", song_count, song_row, song_select,
                           .layout = RV_LAYOUT_FULL, .dynamic.detail = RV_DETAIL_BELOW,
                           .dynamic.initial = song_first, .dynamic.empty = "No Items in Queue",
                           RV_SCREEN_CONTEXT(song_menu),
                           .opened = queue_opened, .closed = queue_closed),
    RV_SCREEN_LIST("clear", "Clear Queue", clear_rows, .layout = RV_LAYOUT_FULL),
};

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_SONG_CONTEXT, 0, RV_ROW_ACTION("Play Next", song_next),
                  .available = can_add_item),
    RV_MENU_ENTRY(RV_MENU_SONG_CONTEXT, 0, RV_ROW_ACTION("Play Last", song_last),
                  .available = can_add_item),
    RV_MENU_ENTRY(RV_MENU_ALBUM_CONTEXT, 0, RV_ROW_ACTION("Play Next", selection_next),
                  .available = can_add),
    RV_MENU_ENTRY(RV_MENU_ALBUM_CONTEXT, 0, RV_ROW_ACTION("Play Last", selection_last),
                  .available = can_add),
    RV_MENU_ENTRY(RV_MENU_ARTIST_CONTEXT, 0, RV_ROW_ACTION("Play Next", selection_next),
                  .available = can_add),
    RV_MENU_ENTRY(RV_MENU_ARTIST_CONTEXT, 0, RV_ROW_ACTION("Play Last", selection_last),
                  .available = can_add),
    RV_MENU_ENTRY(RV_MENU_PODCAST_EPISODE_CONTEXT, 0, RV_ROW_ACTION("Play Next", song_next),
                  .available = can_add_item),
    RV_MENU_ENTRY(RV_MENU_PODCAST_EPISODE_CONTEXT, 0, RV_ROW_ACTION("Play Last", song_last),
                  .available = can_add_item),
    RV_MENU_ENTRY(RV_MENU_NOW_PLAYING_CONTEXT, 0, RV_ROW_PUSH("Queue", "queue"),
                  .available = available),
    RV_MENU_ENTRY(RV_MENU_NOW_PLAYING_CONTEXT, 0, RV_ROW_PUSH("Clear Queue", "clear"),
                  .available = upcoming),
    RV_MENU_ENTRY(RV_MENU_MUSIC, RV_MUSIC_AUDIOBOOKS, RV_ROW_PUSH("Queue", "queue"),
                  .shown_by = KEY_IN_MUSIC),
    RV_MENU_ENTRY(RV_MENU_MAIN, RV_MAIN_AUDIOBOOKS, RV_ROW_PUSH("Queue", "queue"),
                  .shown_by = KEY_IN_MAIN),
};

static const struct rv_setting settings[] = {
    RV_SETTING_BOOL(KEY_IN_MUSIC, "Queue in Music", 1),
    RV_SETTING_BOOL(KEY_IN_MAIN, "Queue in Main Menu", 0),
};

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_MediaPlayer_ToggleStopped_call, toggle_stopped),
    RV_HOOK_FILTER(RV_SITE_MediaList_OnTheGoAdd_call, on_the_go_add),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_HOOKS(hooks),
    RV_SETTINGS(settings),
    RV_SCREENS(screens),
    RV_ENTRIES(entries),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    api = core;
    self = handle;
    return &extension;
}
