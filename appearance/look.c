/* SPDX-License-Identifier: GPL-3.0-only */

#include "look.h"
#include "ext.h"
#include "assets.h"
#include "latch.h"
#include "render.h"
#include "rv_libc.h"
#include "selection_bar.h"
#include "services.h"
#include "style.h"
#include "theme.h"
#include <stddef.h>

struct look_state look_state;
static int switcher_loaded;       /* the switch module is held until finish releases it */
static int busy;                 /* a switch is writing settings */
static int theming_off;          /* a restore failed: no theming until the next boot */
static const char *problem;      /* what the last switch could not do */
static enum look_pane problem_at; /* the pane that shows it */
static int standing_in;          /* Classic is on screen for a stored theme whose file is missing */

/* The canvas roles: the look's, with the secondary shades mixed from text
 * toward the background as the stock greys are. */
static rv_color canvas_roles[RV_COLOR_ROLE_COUNT];

const struct look *look_active(void)
{
    return &look_state.active;
}

/* Whether the look has the other appearance to switch to. */
static int look_dark_available(void)
{
    return !look_state.active.id[0] ||
           look_state.active.appearances == (RVT_APPEARANCE_LIGHT | RVT_APPEARANCE_DARK);
}

const char *look_appearance_note(void)
{
    if (look_dark_available())
        return NULL;
    return look_state.active.appearances == RVT_APPEARANCE_DARK ? "Theme only supports dark mode"
                                                                : "Theme only supports light mode";
}

const char *look_problem(enum look_pane pane)
{
    return problem && problem_at == pane ? problem : NULL;
}

int look_busy(void)
{
    return busy;
}

int look_theming_off(void)
{
    return theming_off;
}

void look_volume_changed(void)
{
    const struct look *active = &look_state.active;
    look_state.stock_checked = 0;
    if (active->id[0] && !theme_file_matches(active->id, active->header_crc)) {
        assets_drop();
        assets_bind();
    }
}

static rv_color argb(const uint8_t *color)
{
    return 0xff000000u | (uint32_t)color[0] << 16 | (uint32_t)color[1] << 8 | color[2];
}

static void apply_roles(void)
{
    uint8_t mixed[4];
    const uint8_t *text = style_color(RVT_ROLE_TEXT), *background = style_color(RVT_ROLE_BACKGROUND);
    if (!style_not_stock()) {
        api->ui_set_colors(self, NULL);
        return;
    }
    canvas_roles[RV_COLOR_BACKGROUND] = argb(background);
    canvas_roles[RV_COLOR_TEXT] = argb(text);
    canvas_roles[RV_COLOR_HIGHLIGHT] = argb(style_color(RVT_ROLE_SELECTION));
    canvas_roles[RV_COLOR_HIGHLIGHT_TEXT] = argb(style_color(RVT_ROLE_ON_ACCENT));
    color_mix(mixed, text, background, 107);
    canvas_roles[RV_COLOR_DIM] = argb(mixed);
    color_mix(mixed, text, background, 66);
    canvas_roles[RV_COLOR_PARAGRAPH] = argb(mixed);
    canvas_roles[RV_COLOR_ACCENT_TEXT] = argb(style_color(RVT_ROLE_ACCENT_TEXT));
    api->ui_set_colors(self, canvas_roles);
}

static uint32_t theme_setting(char id[RVT_ID_BYTES])
{
    memset(id, 0, RVT_ID_BYTES);
    return api->setting_read(self, KEY_THEME, id, RVT_ID_BYTES - 1);
}

/* Writes the settings to say what is on the screen. */
static void settings_write_back(void)
{
    char id[RVT_ID_BYTES];
    busy = 1;
    theme_setting(id);
    if (!standing_in && memcmp(id, look_state.active.id, RVT_ID_BYTES))
        api->setting_write(self, KEY_THEME, RV_SETTING_TYPE_STRING, look_state.active.id,
                           (uint32_t)strlen(look_state.active.id));
    if (api->setting_int(self, KEY_DARK) != look_state.active.dark)
        api->setting_set_int(self, KEY_DARK, look_state.active.dark);
    if ((unsigned)api->setting_int(self, KEY_ACCENT) != look_state.active.accent)
        api->setting_set_int(self, KEY_ACCENT, (int32_t)look_state.active.accent);
    busy = 0;
}

static const struct switch_service *switcher_load(void)
{
    switcher_loaded = api->module_load(self, SWITCH_MODULE) == RV_OK;
    return switcher_loaded ? api->module_service(self, SWITCH_MODULE) : NULL;
}

static void switcher_unload(void)
{
    if (switcher_loaded)
        api->module_unload(self, SWITCH_MODULE);
    switcher_loaded = 0;
}

/* After a switch or an accent change, `accent` saying which: the settings,
 * the canvas colours, the draw hooks, the cached drawings and the screens
 * follow what is there; the switch module is let go. */
static void finish(enum switch_result result, int accent, enum look_pane pane)
{
    if (result != SWITCH_OK && !problem)
        problem = result == SWITCH_RESTORE_FAILED ? "Could not restore artwork"
                  : accent                        ? "Could not change accent"
                  : result == SWITCH_RECOVERED    ? "Could not apply theme"
                                                  : "Could not load theme";
    if (problem)
        problem_at = pane;
    if (result == SWITCH_RESTORE_FAILED) {
        theming_off = 1;
        busy = 1;
        api->setting_set_int(self, KEY_AVAILABLE, 0);
        busy = 0;
    }
    switcher_unload();
    settings_write_back();
    apply_roles();
    /* Binding again after an accent change makes the core decode the
     * pictures on screen anew, so tinted extension bitmaps take the colour. */
    if (!assets_bind() && !accent && !problem) {
        problem = "Artwork held by another extension";
        problem_at = PANE_THEME;
    }
    if (!render_bind(style_not_stock()) && !problem) {
        problem = "Not enough memory";
        problem_at = pane;
    }
    selection_bar_apply();
    api->ui_refresh(self, "theme");
    api->ui_redraw(self);
    latch_finish();
}

void look_switch(const char *id, int dark, unsigned accent, enum look_pane pane)
{
    struct look look;
    struct pack_place pack = {0, 0};
    struct rv_file *file = NULL;
    const struct switch_service *switcher;
    enum switch_result result = SWITCH_LOAD_FAILED;
    enum look_pane at = pane;
    memset(&look, 0, sizeof look);
    if (!latch_begin()) {
        /* Presses queued behind a switch change nothing; the settings follow the screen. */
        settings_write_back();
        return;
    }
    problem = NULL;
    standing_in = 0;
    if (id[0]) {
        enum theme_result opened = theme_open(id, dark, &file, &look, &pack);
        if (opened == THEME_UNAVAILABLE) {
            /* Classic in the same appearance and accent, the stored theme kept. */
            problem = "Theme unavailable";
            standing_in = 1;
            id = "";
        } else {
            if (opened == THEME_NOT_IN_APPEARANCE &&
                !memcmp(look.id, look_state.active.id, RVT_ID_BYTES) &&
                look.dark == look_state.active.dark && accent == look_state.active.accent) {
                /* The theme's one appearance is on the screen already: only
                 * the settings have to say so. */
                api->file_close(self, file);
                finish(SWITCH_OK, 0, pane);
                return;
            }
            dark = look.dark;
        }
    }
    look.dark = dark;
    look.accent = accent;
    if (!id[0] && dark && (!look_file_open(&look, &file) || !look_pack_place(file, &look, &pack))) {
        problem = "Could not load dark artwork";
        at = PANE_DARK;
    } else if (!(switcher = switcher_load())) {
        problem = "Not enough memory";
    } else {
        result = switcher->apply(file, &look, &pack);
    }
    if (file)
        api->file_close(self, file);
    finish(result, 0, at);
}

void look_select(const char *id)
{
    look_switch(id, api->setting_int(self, KEY_DARK), (unsigned)api->setting_int(self, KEY_ACCENT),
                PANE_THEME);
}

void look_accent(unsigned accent)
{
    const struct switch_service *switcher;
    if (accent == look_state.active.accent)
        return;
    if (!latch_begin()) {
        settings_write_back();
        return;
    }
    problem = NULL;
    if (!(switcher = switcher_load())) {
        problem = "Not enough memory";
        finish(SWITCH_LOAD_FAILED, 1, PANE_ACCENT);
        return;
    }
    finish(switcher->accent(accent), 1, PANE_ACCENT);
}

void look_unload(void)
{
    const struct switch_service *switcher = switcher_load();
    if (switcher)
        switcher->restore_all();
    switcher_unload();
    assets_release();
    assets_drop();
    latch_unload();
    render_bind(0);
    selection_bar_restore();
    api->ui_set_colors(self, NULL);
    api->ui_redraw(self);
}
