/* SPDX-License-Identifier: GPL-3.0-only */

/* The Custom preset's row in Apple's EQ tables. */

#ifndef EQUALIZER_PRESETS_H
#define EQUALIZER_PRESETS_H
#include "osos-2.0.5.h"

/* The settings id of the Custom preset, after Apple's last. */
#define EQ_CUSTOM_SETTINGS_ID (EQ_SettingsId_Last + 1)
/* The rows of the EQ list with Custom. */
#define EQ_ROWS (EQ_PresetCount + 1)

/* Copies of Apple's two tables with the Custom row appended, for the literal
 * sites. eq_presets_start fills them, naming the row by the string resource
 * `name`; before that they are zero. */
extern uint32_t eq_preset_ids[EQ_ROWS];
extern uint32_t eq_preset_rows[EQ_ROWS * EQPreset_bytes / sizeof(uint32_t)];
void eq_presets_start(uint32_t name);

/* Hook handlers. */
uint32_t eq_map_preset(uint32_t settings_id, const void *next);
uint32_t eq_track_preset(struct MediaItem *item, struct MediaTrack *track, const void *next);

#endif
