/* SPDX-License-Identifier: GPL-3.0-only */

#include "rv_api.h"
#include "rv_sites.h"
#include "rv_ui.h"

static const struct rv_hook hooks[] = {
    /* Test SysInfo bits 0 and 7 instead of bit 7 alone. */
    RV_HOOK_WORD(RV_SITE_VolumePolicy_RegionTest_word, 0xe3100081),
    /* EU Volume Limit off: ceiling 256 instead of 222. */
    RV_HOOK_WORD(RV_SITE_Volume_EUOffCeiling_word, 0x03a00c01),
    /* Default user limit and volume: 256 instead of 222. */
    RV_HOOK_WORD(RV_SITE_Volume_EUDefaultLimit_word, 0xe3a00c01),
    /* The warning always reads as acknowledged. */
    RV_HOOK_WORD(RV_SITE_VolumePolicy_WarningPolicyTest_word, 0xe3a00000),
    /* No startup volume clamp. */
    RV_HOOK_WORD(RV_SITE_Volume_StartupClampTest_word, 0xe3a00000),
    /* No 18-hour exposure check on track change and stop. */
    RV_HOOK_WORD(RV_SITE_VolumeExposure_TrackFinishTest_word, 0xea00000f),
    /* bx lr: the 20-hour exposure event never lowers the volume. */
    RV_HOOK_WORD(RV_SITE_MediaPlayer_PostVolumeExposureLimitEvent_word, 0xe12fff1e),
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
