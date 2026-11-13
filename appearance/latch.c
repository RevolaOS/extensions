/* SPDX-License-Identifier: GPL-3.0-only */

#include "latch.h"
#include "ext.h"
#include "rv_sites.h"
#include "rv_ui.h"

static int loading, latched; /* a switch runs; presses are held off and the watch is bound */

static uint32_t process_message(uint32_t wait, uint32_t quit, const void *next);
static const struct rv_hook watch = RV_HOOK_FILTER(RV_SITE_UI_ProcessMessage_call, process_message);

/* (wait, quit): one turn of the message loop; the result says a message was
 * handled. The first idle turn after the switch opens the latch and ends the
 * watch: a handler may unbind itself once hook_next has returned. */
static uint32_t process_message(uint32_t wait, uint32_t quit, const void *next)
{
    uint32_t handled = api->hook_next(next, wait, quit, 0, 0, 0, 0);
    if (!handled && !loading) {
        latched = 0;
        api->hook_unbind(self, &watch);
    }
    return handled;
}

int latch_begin(void)
{
    if (latched)
        return 0;
    /* Without memory for the watch the switch runs unlatched. */
    latched = api->hook_bind(self, &watch) == RV_OK;
    loading = 1;
    return 1;
}

void latch_finish(void)
{
    loading = 0;
}

void latch_unload(void)
{
    if (latched)
        api->hook_unbind(self, &watch);
    loading = latched = 0;
}
