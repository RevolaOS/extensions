/* SPDX-License-Identifier: GPL-3.0-only */

/* The same fix as RepriseOS pull request 62 by kayboll, arrived at independently. */

/* The MP4 parser compares a track's 64-bit decode time with its edit list after
 * sign-extending the low word, so past 2^31 media ticks (13:31:35 at 44.1 kHz)
 * every sample counts as outside the edit and the track ends. Each site holds
 * `asr r1, r0, #31` with r0 the low word; the replacement loads the high word
 * from the track instead. */

#include "rv_api.h"
#include "rv_sites.h"
#include "rv_ui.h"

static const struct rv_hook hooks[] = {
    /* ldr r1, [r2, #-20]: r2 is track + edit_media_start, the high word of decode_time
     * sits 20 bytes before it. */
    RV_HOOK_WORD(RV_SITE_MP4Parser_ReadEditStartTest_word, 0xe5121014),
    /* ldr r1, [r2, #-28]: r2 is track + edit_media_end. */
    RV_HOOK_WORD(RV_SITE_MP4Parser_ReadEditEndTest_word, 0xe512101c),
    /* The same compare in the seek walk, r2 again track + edit_media_end. */
    RV_HOOK_WORD(RV_SITE_MP4Parser_SeekEditEndTest_word, 0xe512101c),
};

static const struct rv_descriptor extension = {
    .size = sizeof extension,
    RV_HOOKS(hooks),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    (void)core;
    (void)handle;
    return &extension;
}
