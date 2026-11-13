/* SPDX-License-Identifier: GPL-3.0-only */

#include "switch.h"
#include "ext.h"
#include "pack.h"
#include "rv_libc.h"
#include "stock.h"
#include "style.h"

const struct rv_api *api;
rv_extension_handle self;
const struct main_service *main_module;

static int empty(const uint32_t *bits)
{
    for (unsigned i = 0; i < BITMAP_WORDS; i++)
        if (bits[i])
            return 0;
    return 1;
}

static void accent_art_tint(struct look_state *s)
{
    memset(s->accented, 0, sizeof s->accented);
    main_module->tint_accent_art(s->assets, s->accented);
    for (unsigned i = 0; i < BITMAP_WORDS; i++)
        s->assets[i] |= s->accented[i];
}

/* The firmware file is checked against the resident bank once per session. */
static int stock_usable(struct look_state *s)
{
    if (!s->stock_checked)
        s->stock_checked = stock_file_matches();
    return s->stock_checked;
}

/* Artwork is half changed: restore every bitmap either look touched and go
 * back to the stock light look. When that fails every bitmap of the attempt
 * stays marked, restored or not, so the next switch reads them all again,
 * and the firmware file is checked again before it. */
static enum switch_result recover(struct look_state *s, const uint32_t *touched)
{
    uint32_t restore[BITMAP_WORDS];
    for (unsigned i = 0; i < BITMAP_WORDS; i++)
        restore[i] = s->assets[i] | touched[i];
    int restored = stock_restore(restore);
    memset(&s->active, 0, sizeof s->active);
    if (restored) {
        memset(s->assets, 0, sizeof s->assets);
        memset(s->accented, 0, sizeof s->accented);
    } else {
        memcpy(s->assets, restore, sizeof s->assets);
        s->stock_checked = 0;
    }
    main_module->assets_drop();
    main_module->style_reset();
    main_module->style_set_dark(0);
    main_module->style_set_accent(ACCENT_DEFAULT);
    main_module->tint_refresh();
    return restored ? SWITCH_RECOVERED : SWITCH_RESTORE_FAILED;
}

static enum switch_result switch_apply(struct rv_file *file, const struct look *look,
                                       const struct pack_place *place)
{
    struct look_state *s = main_module->state;
    uint32_t target[BITMAP_WORDS], restore[BITMAP_WORDS];
    struct pack pack;
    enum switch_result result;
    memset(target, 0, sizeof target);
    if (!stock_usable(s) || (file && !pack_open(file, look->header_crc, place, &pack)))
        return SWITCH_LOAD_FAILED;
    /* The Classic dark art carries colours the stock look already has. */
    if (file && !look->id[0] && !pack_plain(&pack))
        return SWITCH_LOAD_FAILED;
    if (!stock_open())
        return SWITCH_LOAD_FAILED;
    int applied = !file || pack_apply(&pack, target, s->assets);
    if (!applied && empty(target)) {
        /* Nothing changed: the look on the screen keeps its bitmaps. */
        main_module->assets_reserve(0);
        stock_close();
        return SWITCH_LOAD_FAILED;
    }
    for (unsigned i = 0; i < BITMAP_WORDS; i++)
        restore[i] = s->assets[i] & ~target[i];
    if (!applied || !stock_restore(restore)) {
        result = recover(s, target);
        stock_close();
        return result;
    }
    s->active = *look;
    memcpy(s->assets, target, sizeof s->assets);
    if (file)
        main_module->assets_commit();
    else
        main_module->assets_drop();
    if (look->id[0])
        main_module->style_set((const uint8_t *)pack.style);
    else
        main_module->style_reset();
    main_module->style_set_dark(look->dark);
    main_module->style_set_accent(look->accent);
    main_module->tint_refresh();
    accent_art_tint(s);
    stock_close();
    return SWITCH_OK;
}

static enum switch_result switch_accent(unsigned accent)
{
    struct look_state *s = main_module->state;
    if (!stock_usable(s))
        return SWITCH_LOAD_FAILED;
    s->active.accent = accent;
    main_module->style_set_accent(accent);
    main_module->tint_refresh();
    if (!stock_restore(s->accented))
        return recover(s, s->assets);
    for (unsigned i = 0; i < BITMAP_WORDS; i++)
        s->assets[i] &= ~s->accented[i];
    accent_art_tint(s);
    return SWITCH_OK;
}

static void switch_restore_all(void)
{
    struct look_state *s = main_module->state;
    if (!empty(s->assets))
        stock_restore(s->assets);
    memset(s->assets, 0, sizeof s->assets);
    memset(s->accented, 0, sizeof s->accented);
    memset(&s->active, 0, sizeof s->active);
    main_module->assets_drop();
    main_module->style_reset();
}

static const struct switch_service service = {switch_apply, switch_accent, switch_restore_all};

static const struct rv_descriptor module = {.size = sizeof module, .service = &service};

const struct rv_descriptor *rv_module_entry(const struct rv_api *core, rv_extension_handle handle)
{
    api = core;
    self = handle;
    main_module = api->module_service(self, 0);
    return &module;
}
