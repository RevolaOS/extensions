/* SPDX-License-Identifier: GPL-3.0-only */

/* The tables the two modules call each other through (modules share no
 * symbols). `main` publishes main_service for as long as the extension is
 * loaded; `switch` publishes switch_service between main's module_load and
 * module_unload of it. */

#ifndef SERVICES_H
#define SERVICES_H
#include "catalog.h"
#include "assets.h"
#include "look.h"
#include "rv_api.h"
#include "rvt.h"
#include <stdint.h>

struct main_service {
    struct look_state *state;
    uint32_t (*crc32_update)(uint32_t crc, const void *data, uint32_t size);
    uint32_t (*catalog_count)(void);
    const uint8_t *(*catalog_fingerprint)(void);
    uint8_t *(*catalog_data)(uint32_t index);
    uint32_t (*catalog_size)(uint32_t index);
    uint32_t (*catalog_crc)(uint32_t index);
    uint32_t (*catalog_file_offset)(uint32_t index);
    int (*catalog_layout)(uint32_t index, struct layout *b);
    void (*style_set)(const uint8_t *prefix);
    void (*style_reset)(void);
    void (*style_set_dark)(int dark);
    void (*style_set_accent)(unsigned accent);
    void (*tint_refresh)(void);
    void (*tint_accent_art)(const uint32_t *skip, uint32_t *touched);
    int (*lz_decode)(struct rv_file *file, uint32_t stream_bytes, uint8_t *out, uint32_t bytes,
                     uint32_t *crc);
    struct asset_entry *(*assets_reserve)(uint32_t count);
    void (*assets_commit)(void);
    void (*assets_drop)(void);
};

struct switch_service {
    /* Streams the pack at `pack` out of `file` (a theme file or the classic
     * asset, its header checked by main and summed in look->header_crc; NULL
     * for the stock light look) into the resident bitmaps, restores what the
     * previous look changed and this one leaves alone, applies the colours
     * and tints the stock accent art. A failure after the first write
     * restores everything and returns to the stock light look. */
    enum switch_result (*apply)(struct rv_file *file, const struct look *look,
                                const struct pack_place *pack);
    /* Another accent for the current look: the accent art is restored and
     * tinted again; no pack is read. */
    enum switch_result (*accent)(unsigned accent);
    /* Puts every bitmap back and forgets the look. */
    void (*restore_all)(void);
};

#endif
