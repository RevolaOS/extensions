/* SPDX-License-Identifier: GPL-3.0-only */

/* Equalizer: Apple's EQ presets through a cascade with headroom, and a Custom
 * preset of five bands shaped in the editor screen. */

#include "engine.h"
#include "presets.h"
#include "rv_libc.h"
#include "rv_menus.h"
#include "rv_sites.h"
#include "rv_ui.h"

static const struct rv_api *api;
static rv_extension_handle self;

/* The Custom preset that plays. */
static uint8_t values[EQ_VALUES];

/* A low shelf at 60 Hz, a peak at 1 kHz and a high shelf at 8 kHz, all flat;
 * two bands off at 250 Hz and 4 kHz. */
static const uint8_t defaults[EQ_VALUES] = {
    EQ_PRECUT_AUTO,
    EQ_LOW_SHELF,  5,  4, 24,
    EQ_PEAKING,    18, 4, 24,
    EQ_HIGH_SHELF, 27, 4, 24,
    EQ_OFF,        12, 4, 24,
    EQ_OFF,        24, 4, 24,
};

static int valid(const uint8_t *v)
{
    if (v[EQ_PRECUT] >= eq_choices[EQ_PRECUT])
        return 0;
    for (unsigned int band = 0; band < EQ_BANDS; band++)
        for (unsigned int parameter = EQ_TYPE; parameter < EQ_PARAMETERS; parameter++)
            if (v[eq_slot(band, parameter)] >= eq_choices[parameter])
                return 0;
    return 1;
}

/* Plays what the store holds, the defaults when it holds nothing usable. */
static void load(void)
{
    uint8_t stored[EQ_VALUES];
    const uint8_t *wanted = defaults;
    if (api->setting_read(self, KEY_VALUES, stored, sizeof stored) == sizeof stored &&
        valid(stored))
        wanted = stored;
    if (!memcmp(values, wanted, EQ_VALUES))
        return;
    memcpy(values, wanted, EQ_VALUES);
    if (!eq_publish(values)) {
        memcpy(values, defaults, EQ_VALUES);
        eq_publish(values);
    }
}

static void change(unsigned int slot, unsigned int choice)
{
    uint8_t trial[EQ_VALUES];
    memcpy(trial, values, EQ_VALUES);
    trial[slot] = (uint8_t)choice;
    if (eq_publish(trial))
        memcpy(values, trial, EQ_VALUES);
}

static void save(void)
{
    api->setting_write(self, KEY_VALUES, RV_SETTING_TYPE_BLOB, values, EQ_VALUES);
}

static const struct eq_link link = {values, change, save, eq_custom_response};

static void open_editor(const struct rv_context *ctx)
{
    (void)ctx;
    api->ui_push(self, "editor", (void *)&link);
}

static void setting_changed(uint16_t key)
{
    if (key == KEY_VALUES || key == RV_SETTING_KEY_ALL)
        load();
}

static void unload(void)
{
    /* Apple's code has no row for Custom once the tables are its own again. */
    if (MediaPlayer_GetEQSettingsId(MediaPlayer_GetInstance()) == EQ_CUSTOM_SETTINGS_ID)
        SettingsModel_SelectEQMenuIndex(SettingsModel_GetInstance(), 0);
    eq_engine_stop();
}

/* Bound in this order: the engine before anything can ask for the Custom
 * preset, the tables before the counts that reach into them, the id map last. */
static const struct rv_hook hooks[] = {
    RV_HOOK_REPLACE(RV_SITE_EQ_Process_slot, eq_process),
    RV_HOOK_REPLACE(RV_SITE_EQ_Reset_slot, eq_reset),
    RV_HOOK_REPLACE(RV_SITE_EQ_SetPreset_Load_call, eq_load),
    RV_HOOK_REPLACE(RV_SITE_EQ_SetRate_Load_call, eq_load),
    RV_HOOK_WORD(RV_SITE_EQ_DspCount_word, 0xe3510018), /* cmp r1, #EQ_ROWS */
    RV_HOOK_POINTER(RV_SITE_EQ_MenuIds_literal, eq_preset_ids),
    RV_HOOK_POINTER(RV_SITE_EQ_SettingsIds_literal, eq_preset_ids),
    RV_HOOK_POINTER(RV_SITE_EQ_PreviewTable_literal, eq_preset_rows),
    RV_HOOK_POINTER(RV_SITE_EQ_NameTable_literal, eq_preset_rows),
    RV_HOOK_POINTER(RV_SITE_EQ_ResourceTable_literal, eq_preset_rows),
    RV_HOOK_WORD(RV_SITE_EQ_PreviewCount_word, 0xe3510018),  /* cmp r1, #EQ_ROWS */
    RV_HOOK_WORD(RV_SITE_EQ_NameCount_word, 0xe3510018),     /* cmp r1, #EQ_ROWS */
    RV_HOOK_WORD(RV_SITE_EQ_SettingsCount_word, 0xe3520018), /* cmp r2, #EQ_ROWS */
    RV_HOOK_WORD(RV_SITE_EQ_ProviderCount_word, 0xe3a00018), /* mov r0, #EQ_ROWS */
    RV_HOOK_WORD(RV_SITE_EQ_ResourceCount_word, 0xe3550018), /* cmp r5, #EQ_ROWS */
    RV_HOOK_REPLACE(RV_SITE_EQ_TrackPreset_call, eq_track_preset),
    RV_HOOK_REPLACE(RV_SITE_EQ_SettingsIdToDspPreset_entry, eq_map_preset),
};

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_SETTINGS, RV_SETTINGS_EQ, RV_ROW_ACTION("Custom EQ", open_editor),
                  .caption = "Adjust your sound", .bitmap = RV_BITMAP("settings-eq")),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_HOOKS(hooks),
    .unload = unload,
    .setting_changed = setting_changed,
    RV_ENTRIES(entries),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    api = core;
    self = handle;
    eq_engine_start(core);
    eq_presets_start(api->string_resource(self, "Custom"));
    load();
    return &extension;
}
