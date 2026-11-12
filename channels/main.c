/* SPDX-License-Identifier: GPL-3.0-only */

/* Channels: Settings > Channels opens the editor screen, which chooses
 * the mode and shapes the Custom width. The output-stage hook runs what is
 * chosen on every buffer, after Crossfeed and before the Compressor. */

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
    RV_SETTING_INT(KEY_MODE, "Channels", CHANNELS_STEREO, CHANNELS_STEREO, CHANNELS_CUSTOM),
    RV_SETTING_INT(KEY_WIDTH, NULL, WIDTH_DEFAULT, 0, WIDTH_LABELS - 1),
};

/* Leaves what the settings say for the pump task. */
static void publish(void)
{
    struct channels_set set = {
        .mode = (uint8_t)api->setting_int(self, KEY_MODE),
        .width = (uint8_t)api->setting_int(self, KEY_WIDTH),
    };
    channels_publish(&set);
}

static void setting_changed(uint16_t key)
{
    (void)key;
    publish();
}

/* The processed output before Apple submits it: (samples, frames, context).
 * Only 16-bit stereo is touched; the mixes do not depend on the rate. */
static uint32_t on_output(uint32_t samples, uint32_t frames, uint32_t context,
                          const void *next)
{
    uint32_t rate;
    if (frames && rv_audio_stereo16((const void *)context, &rate))
        channels_process((int16_t *)samples, frames);
    return api->hook_next(next, samples, frames, context, 0, 0, 0);
}

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER_AT(RV_SITE_AudioOutputQueue_Get_call, on_output, RV_AUDIO_STAGE_CHANNELS),
};

/* The Settings pane names the mode, or the width under Custom, as Apple's EQ
 * row names its preset. */
static const char *caption(const struct rv_context *ctx)
{
    static char text[8];
    (void)ctx;
    int mode = api->setting_int(self, KEY_MODE);
    if (mode != CHANNELS_CUSTOM)
        return mode_labels[mode];
    rv_format(text, sizeof text, "%u %%", width_percent(api->setting_int(self, KEY_WIDTH)));
    return text;
}

static const struct rv_menu_entry entries[] = {
    RV_MENU_ENTRY(RV_MENU_SETTINGS, RV_SETTINGS_SOUND_CHECK, RV_ROW_PUSH("Channels", "editor"),
                  .caption_text = caption, .bitmap = RV_BITMAP("settings-channels")),
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
    channels_start(core);
    return &extension;
}
