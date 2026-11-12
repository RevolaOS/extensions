/* SPDX-License-Identifier: GPL-3.0-only */

/* Crossfeed: Settings > Crossfeed opens the editor screen, which holds the
 * mode, Off, Simple or Custom, and the five values of the Custom cross path.
 * The output-stage hook runs what is chosen on every buffer. */

#include "modes.h"
#include "rv_api.h"
#include "rv_audio.h"
#include "rv_libc.h"
#include "rv_menus.h"
#include "rv_sites.h"
#include "rv_ui.h"
#include "shared.h"

static const struct rv_api *api;
static rv_extension_handle self;

static const struct rv_setting settings[] = {
    RV_SETTING_INT(KEY_MODE, "Crossfeed", CROSSFEED_OFF, CROSSFEED_OFF, CROSSFEED_CUSTOM),
    RV_SETTING_INT(KEY_DIRECT, NULL, DIRECT_DEFAULT, 0, DIRECT_LABELS - 1),
    RV_SETTING_INT(KEY_CROSS, NULL, CROSS_DEFAULT, 0, CROSS_LABELS - 1),
    RV_SETTING_INT(KEY_HF, NULL, HF_DEFAULT, 0, HF_LABELS - 1),
    RV_SETTING_INT(KEY_CUTOFF, NULL, CUTOFF_DEFAULT, 0, CUTOFF_LABELS - 1),
    RV_SETTING_INT(KEY_PRECUT, NULL, PRECUT_DEFAULT, 0, PRECUT_LABELS - 1),
};

/* Leaves what the settings say for the pump task. */
static void publish(void)
{
    struct crossfeed_set set = {
        .mode = (uint8_t)api->setting_int(self, KEY_MODE),
        .direct_tenths = (int16_t)direct_tenths(api->setting_int(self, KEY_DIRECT)),
        .cross_tenths = (int16_t)cross_tenths(api->setting_int(self, KEY_CROSS)),
        .hf_tenths = (int16_t)hf_tenths(api->setting_int(self, KEY_HF)),
        .cutoff_hz = (uint16_t)cutoff_hz(api->setting_int(self, KEY_CUTOFF)),
        .precut_tenths = (int16_t)precut_tenths(api->setting_int(self, KEY_PRECUT)),
    };
    crossfeed_publish(&set);
}

static void setting_changed(uint16_t key)
{
    (void)key;
    publish();
}

/* The processed output before Apple submits it: (samples, frames, context).
 * Only 16-bit stereo at the rates the filters are designed for is touched. */
static uint32_t on_output(uint32_t samples, uint32_t frames, uint32_t context,
                          const void *next)
{
    uint32_t rate;
    if (frames && rv_audio_stereo16((const void *)context, &rate) && rate >= 8000 &&
        rate <= 48000)
        crossfeed_process((int16_t *)samples, frames, rate);
    return api->hook_next(next, samples, frames, context, 0, 0, 0);
}

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER_AT(RV_SITE_AudioOutputQueue_Get_call, on_output, RV_AUDIO_STAGE_CROSSFEED),
};

/* The Settings pane names the mode, as Apple's EQ row does. */
static const char *mode_caption(const struct rv_context *ctx)
{
    (void)ctx;
    return mode_labels[api->setting_int(self, KEY_MODE)];
}

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_SETTINGS, RV_SETTINGS_SOUND_CHECK, RV_ROW_PUSH("Crossfeed", "editor"),
                  .caption_text = mode_caption, .bitmap = RV_BITMAP("settings-crossfeed")),
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
    crossfeed_start(core);
    return &extension;
}
