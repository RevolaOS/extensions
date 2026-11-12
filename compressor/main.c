/* SPDX-License-Identifier: GPL-3.0-only */

/* Compressor: Settings > Compressor opens the editor screen, which shapes the
 * six values; a Threshold of Off switches the compressor off. The output-stage
 * hook runs what is chosen on every buffer. */

#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_libc.h"
#include "rv_menus.h"
#include "rv_sites.h"
#include "rv_ui.h"
#include "shared.h"

static const struct rv_api *api;
static rv_extension_handle self;

static const struct rv_setting settings[] = {
    RV_SETTING_INT(KEY_THRESHOLD, "Compressor", THRESHOLD_DEFAULT, 0, THRESHOLD_LABELS - 1),
    RV_SETTING_INT(KEY_MAKEUP, NULL, MAKEUP_DEFAULT, 0, MAKEUP_LABELS - 1),
    RV_SETTING_INT(KEY_RATIO, NULL, RATIO_DEFAULT, 0, RATIO_LABELS - 1),
    RV_SETTING_INT(KEY_KNEE, NULL, KNEE_DEFAULT, 0, KNEE_LABELS - 1),
    RV_SETTING_INT(KEY_ATTACK, NULL, ATTACK_DEFAULT, 0, ATTACK_LABELS - 1),
    RV_SETTING_INT(KEY_RELEASE, NULL, RELEASE_DEFAULT, 0, RELEASE_LABELS - 1),
};

/* Leaves what the settings say for the pump task. */
static void publish(void)
{
    struct compressor_set set = {
        .threshold_db = (int8_t)threshold_db(api->setting_int(self, KEY_THRESHOLD)),
        .makeup = (uint8_t)api->setting_int(self, KEY_MAKEUP),
        .ratio = (uint8_t)api->setting_int(self, KEY_RATIO),
        .knee = (uint8_t)api->setting_int(self, KEY_KNEE),
        .attack_ms = (uint16_t)attack_ms(api->setting_int(self, KEY_ATTACK)),
        .release_ms = (uint16_t)release_ms(api->setting_int(self, KEY_RELEASE)),
    };
    compressor_publish(&set);
}

static void setting_changed(uint16_t key)
{
    (void)key;
    publish();
}

/* The processed output before Apple submits it: (samples, frames, context).
 * Only 16-bit stereo at the rates the delay line is sized for is touched. */
static uint32_t on_output(uint32_t samples, uint32_t frames, uint32_t context,
                          const void *next)
{
    const void *effects = (const void *)context;
    uint32_t rate = RV_FIELD(effects, AudioEffectsContext_output_rate, uint32_t);
    uint32_t bits = RV_FIELD(effects, AudioEffectsContext_output_bits, uint32_t);
    uint32_t channels = RV_FIELD(effects, AudioEffectsContext_output_channels, uint32_t);
    if (frames && bits == 16 && channels == 2 && rate >= 8000 && rate <= 48000)
        compressor_process((int16_t *)samples, frames, rate);
    return api->hook_next(next, samples, frames, context, 0, 0, 0);
}

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER_AT(RV_SITE_AudioOutputQueue_Get_call, on_output, RV_AUDIO_STAGE_COMPRESSOR),
};

/* The Settings pane says whether it plays, as Apple's Sound Check row does. */
static const char *state_caption(const struct rv_context *ctx)
{
    (void)ctx;
    return api->setting_int(self, KEY_THRESHOLD) ? "On" : "Off";
}

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_SETTINGS, RV_SETTINGS_SOUND_CHECK, RV_ROW_PUSH("Compressor", "editor"),
                  .caption_text = state_caption, .bitmap = RV_BITMAP("settings-compressor")),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_HOOKS(hooks),
    RV_SETTINGS(settings),
    .setting_changed = setting_changed,
    RV_ENTRIES(entries),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    api = core;
    self = handle;
    compressor_start(core);
    return &extension;
}
