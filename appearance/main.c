/* SPDX-License-Identifier: GPL-3.0-only */

/* Appearance: a Settings row after Brightness opening a menu of Theme, Dark
 * Mode, Accent Color and Menu Covers. A look is a theme file's pack or the
 * Classic dark art from the `classic` asset streamed into the resident
 * bitmaps, plus the colour roles the draw hooks map the stock screens to; the
 * core's canvases follow through ui_set_colors. The settings hold the theme id,
 * the appearance and the accent; whatever cannot be put on the screen is
 * written back as what is. */

#include "catalog.h"
#include "crc.h"
#include "ext.h"
#include "assets.h"
#include "look.h"
#include "lz.h"
#include "menu_covers.h"
#include "rv_api.h"
#include "rv_libc.h"
#include "rv_menus.h"
#include "rv_sites.h"
#include "rv_ui.h"
#include "services.h"
#include "style.h"
#include "theme_picker.h"
#include "tint.h"
#include <stddef.h>

const struct rv_api *api;
rv_extension_handle self;

static const struct rv_setting settings[] = {
    RV_SETTING_BOOL(KEY_DARK, "Dark Mode", 0),
    RV_SETTING_CHOICE(KEY_ACCENT, "Accent Color", ACCENT_DEFAULT, accent_labels),
    RV_SETTING_CHOICE(KEY_MENU_COVERS, "Menu Covers", COVERS_THEME, menu_covers_labels),
    RV_SETTING_BOOL(KEY_AVAILABLE, "Appearance", 1),
};

static void setting_changed(uint16_t key)
{
    char id[RVT_ID_BYTES];
    if (look_busy())
        return;
    int dark = api->setting_int(self, KEY_DARK);
    unsigned accent = (unsigned)api->setting_int(self, KEY_ACCENT);
    const struct look *active = look_active();
    switch (key) {
    case RV_SETTING_KEY_ALL:
        /* At load, and after Reset All Settings emptied the store. */
        if (!api->setting_int(self, KEY_AVAILABLE) && !look_theming_off())
            api->setting_set_int(self, KEY_AVAILABLE, 1);
        menu_covers_set(api->setting_int(self, KEY_MENU_COVERS));
        memset(id, 0, sizeof id);
        api->setting_read(self, KEY_THEME, id, sizeof id - 1);
        if (memcmp(id, active->id, RVT_ID_BYTES) || dark != active->dark || accent != active->accent)
            look_switch(id, dark, accent, PANE_THEME);
        break;
    case KEY_DARK:
        /* The stored theme, not the active one: a theme whose file was
         * missing is tried again. */
        memset(id, 0, sizeof id);
        api->setting_read(self, KEY_THEME, id, sizeof id - 1);
        look_switch(id, dark, accent, PANE_DARK);
        break;
    case KEY_ACCENT:
        look_accent(accent);
        break;
    case KEY_MENU_COVERS:
        menu_covers_set(api->setting_int(self, KEY_MENU_COVERS));
        break;
    default:
        break;
    }
}

static const char *theme_caption(const struct rv_context *ctx)
{
    (void)ctx;
    const struct look *active = look_active();
    const char *problem = look_problem(PANE_THEME);
    return problem ? problem : active->id[0] ? active->name : "Classic";
}

static const char *dark_caption(const struct rv_context *ctx)
{
    (void)ctx;
    const char *problem = look_problem(PANE_DARK), *note = look_appearance_note();
    if (problem)
        return problem;
    return note ? note : style_dark() ? "On" : "Off";
}

/* The row says Theme; the pane has room for the full phrase. */
static const char *accent_caption(const struct rv_context *ctx)
{
    (void)ctx;
    const char *problem = look_problem(PANE_ACCENT);
    if (problem)
        return problem;
    return style_accent() == ACCENT_DEFAULT ? "Theme Default" : accent_labels[style_accent()];
}

static const char *covers_caption(const struct rv_context *ctx)
{
    (void)ctx;
    return menu_covers_enabled() ? "On" : "Off";
}

/* The Appearance menu: each row with its own pane caption and icon. */
static const struct rv_row rows[] = {
    RV_ROW_PUSH("Theme", "theme"),
    RV_ROW_SETTING("Dark Mode", KEY_DARK),
    RV_ROW_SETTING("Accent Color", KEY_ACCENT),
    RV_ROW_SETTING("Menu Covers", KEY_MENU_COVERS),
};
static const struct rv_pane panes[] = {
    {.caption_text = theme_caption, .bitmap = RV_BITMAP("settings-theme")},
    {.caption_text = dark_caption, .bitmap = RV_BITMAP("settings-moon")},
    {.caption_text = accent_caption, .bitmap = RV_BITMAP("settings-accent")},
    {.caption_text = covers_caption, .bitmap = RV_BITMAP("settings-covers")},
};
static const struct rv_screen screens[] = {
    RV_SCREEN_LIST_PANES("appearance", "Appearance", rows, panes),
    THEME_PICKER_SCREEN,
};

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_SETTINGS, RV_SETTINGS_BRIGHTNESS, RV_ROW_PUSH("Appearance", "appearance"),
                  .caption = "Make it your own", .bitmap = RV_BITMAP("settings-appearance"),
                  .shown_by = KEY_AVAILABLE),
};

/* No arguments: disk mode has ended and the volume, firmware file included,
 * is readable again. */
static uint32_t disk_mode_exit(const void *next)
{
    look_volume_changed();
    return api->hook_next(next, 0, 0, 0, 0, 0, 0);
}

/* The hooks that answer in every look; the draw hooks are bound while one is on. */
static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_MediaArtworkProvider_GetSlotArtwork_entry, menu_covers_query),
    RV_HOOK_FILTER(RV_SITE_DiskModeExit_PopToMainScreen_call, disk_mode_exit),
};

static void unload(void)
{
    look_unload();
}

static const struct main_service service = {
    .state = &look_state,
    .crc32_update = crc32_update,
    .catalog_count = catalog_count,
    .catalog_fingerprint = catalog_fingerprint,
    .catalog_data = catalog_data,
    .catalog_size = catalog_size,
    .catalog_crc = catalog_crc,
    .catalog_file_offset = catalog_file_offset,
    .catalog_layout = catalog_layout,
    .style_set = style_set,
    .style_reset = style_reset,
    .style_set_dark = style_set_dark,
    .style_set_accent = style_set_accent,
    .tint_refresh = tint_refresh,
    .tint_accent_art = tint_accent_art,
    .lz_decode = lz_decode,
    .assets_reserve = assets_reserve,
    .assets_commit = assets_commit,
    .assets_drop = assets_drop,
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_HOOKS(hooks),
    RV_SETTINGS(settings),
    .unload = unload,
    .setting_changed = setting_changed,
    RV_SCREENS(screens),
    RV_ENTRIES(entries),
    .service = &service,
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    api = core;
    self = handle;
    return &extension;
}
