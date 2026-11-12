/* SPDX-License-Identifier: GPL-3.0-only */

/* Chapter marks on the Now Playing bar. Apple draws one mark per chapter after
 * the fill: the first hook measures the pass, the second leaves every mark out
 * when they are too crowded to read. */

#include "marks.h"
#include "osos-2.0.5.h"
#include "rv_api.h"
#include "rv_sites.h"
#include "rv_ui.h"

static const struct rv_api *api;
static int hidden;

static uint32_t draw_marks(uintptr_t view, uintptr_t context, uintptr_t bar, const void *next)
{
    const int32_t *rect = (const int32_t *)bar;
    hidden = marks_hidden(rect[3] - rect[1], RV_FIELD(view, ProgressBar_marker_count, uint32_t));
    return api->hook_next(next, view, context, bar, 0, 0, 0);
}

static uint32_t blit_mark(uintptr_t cache, uintptr_t context, uintptr_t source, uintptr_t rect,
                          const void *next, uint32_t alpha)
{
    return hidden ? 0 : api->hook_next(next, cache, context, source, rect, alpha, 0);
}

static const struct rv_hook hooks[] = {
    RV_HOOK_FILTER(RV_SITE_ProgressBar_DrawMarkers_slot, draw_marks),
    RV_HOOK_FILTER(RV_SITE_ProgressBarStyle8_DrawMarkers_slot, draw_marks),
    RV_HOOK_FILTER(RV_SITE_ProgressBar_MarkerBlit_call, blit_mark),
};
static const struct rv_descriptor descriptor = {
    .size = sizeof descriptor,
    RV_HOOKS(hooks),
};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    (void)handle;
    api = core;
    return &descriptor;
}
