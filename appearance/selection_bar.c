/* SPDX-License-Identifier: GPL-3.0-only */

/* The selection bar's drawer has two renderers for its cached bitmap: the
 * stock one blends fixed colours, the other a highlight line and a gradient
 * from the RAM colour words, behind a branch the stock firmware never takes.
 * The look's colours go into the RAM words, the branch is made unconditional
 * and the cache's bounds cleared, so the next draw builds the bar from them;
 * restore puts the stock words back and lets the branch fall through again. */

#include "selection_bar.h"
#include "ext.h"
#include "osos-2.0.5.h"
#include "rv_libc.h"
#include "rv_sites.h"
#include "rv_ui.h"
#include "style.h"

static uint8_t stock_gradient[2][4], stock_highlight[4]; /* the RAM words as the firmware had them */
static int saved, custom; /* the stock words are kept; the branch is bound */
/* `b` to the RAM-colour renderer, in place of the stock `beq` there. */
static const struct rv_hook ram_colors = RV_HOOK_WORD(RV_SITE_SelectionBar_StyleBranch_word, 0xea000138);

static void redraw(void)
{
    int32_t *bounds = (int32_t *)(SelectionGradient_Bitmap + Bitmap_bounds);
    for (unsigned i = 0; i < 4; i++)
        bounds[i] = 0;
}

/* The Now Playing status icons are drawn once and kept; the model redraws
 * them when told their resources changed. */
static void redraw_play_state(void)
{
    void *service = UI_FindServiceById(Service_NowPlaying);
    if (!service)
        return;
    void *(*interface)(void *, uint32_t) =
        RV_FIELD(RV_FIELD(service, 0, void **), Service_interface_method, void *);
    void *model = interface(service, Interface_Model);
    if (!model)
        return;
    Model_Notify(model, Resource_Draw, Property_PlayStateWhite);
    Model_Notify(model, Resource_Draw, Property_PlayStateBlack);
}

void selection_bar_apply(void)
{
    uint8_t colors[3][4];
    if (!saved) {
        memcpy(stock_gradient, SelectionBar_RamGradient, sizeof stock_gradient);
        memcpy(stock_highlight, SelectionBar_RamHighlight, sizeof stock_highlight);
        saved = 1;
    }
    if (style_selection_bar(colors)) {
        memcpy(SelectionBar_RamGradient[0], colors[0], 4);
        memcpy(SelectionBar_RamGradient[1], colors[1], 4);
        memcpy(SelectionBar_RamHighlight, colors[2], 4);
        if (!custom)
            custom = api->hook_bind(self, &ram_colors) == RV_OK;
        redraw();
    } else {
        selection_bar_restore();
    }
    redraw_play_state();
}

void selection_bar_restore(void)
{
    if (saved) {
        memcpy(SelectionBar_RamGradient, stock_gradient, sizeof stock_gradient);
        memcpy(SelectionBar_RamHighlight, stock_highlight, sizeof stock_highlight);
    }
    if (custom)
        api->hook_unbind(self, &ram_colors);
    custom = 0;
    redraw();
}
