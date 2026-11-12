/* SPDX-License-Identifier: GPL-3.0-only */

/* Apple keeps the EQ presets in two tables of EQ_PresetCount rows: the settings
 * ids and the name and preview resource ids. Six count words and five literals
 * in the image give their length and address; the extension points them at
 * copies one row longer, and maps the Custom settings id to the DSP preset
 * after Apple's last, which the engine plays. */

#include "presets.h"
#include "engine.h"
#include "rv_api.h"
#include "rv_libc.h"

uint32_t eq_preset_ids[EQ_ROWS];
uint32_t eq_preset_rows[EQ_ROWS * EQPreset_bytes / sizeof(uint32_t)];

void eq_presets_start(uint32_t name)
{
    uint8_t *custom = (uint8_t *)eq_preset_rows + EQ_PresetCount * EQPreset_bytes;
    memcpy(eq_preset_ids, EQ_PresetIds, EQ_PresetCount * sizeof(uint32_t));
    memcpy(eq_preset_rows, EQ_Presets, EQ_PresetCount * EQPreset_bytes);
    eq_preset_ids[EQ_PresetCount] = EQ_CUSTOM_SETTINGS_ID;
    /* Off and Flat share a preview: the flat line. */
    RV_FIELD(custom, EQPreset_preview, uint32_t) = RV_FIELD(EQ_Presets, EQPreset_preview, uint32_t);
    RV_FIELD(custom, EQPreset_name, uint32_t) = name;
}

uint32_t eq_map_preset(uint32_t settings_id, const void *next)
{
    (void)next;
    /* Apple's ids and Custom's are consecutive, as the presets they map to. */
    uint32_t n = settings_id - EQ_SettingsId_First;
    return n < EQ_PresetCount ? n + 1 : 0;
}

uint32_t eq_track_preset(struct MediaItem *item, struct MediaTrack *track, const void *next)
{
    (void)track;
    (void)next;
    /* A track's own preset replaces the player's unless that is Flat; Custom
     * holds as well. */
    if (MediaPlayer_GetEQSettingsId(MediaPlayer_GetInstance()) == EQ_CUSTOM_SETTINGS_ID)
        return 0;
    return MediaItem_GetEQSettingsId(item);
}
