/* SPDX-License-Identifier: GPL-3.0-only */

/* A theme's bitmaps for extensions: the pack's extension bitmap table as an
 * index into the active look's file, and the asset provider that serves the
 * core from it when it decodes an extension's bitmap, for a Settings pane or
 * a canvas alike. The index is main's, since the provider answers while the
 * switch module is unloaded; the switch module fills it through main_service. */

#ifndef ASSETS_H
#define ASSETS_H
#include "rv_api.h"
#include <stdint.h>

/* One bitmap of the look: where it is in the look's file, the bytes stored
 * there (the whole BMap raw or as one lz stream) and the bytes it decodes to. */
struct asset_entry {
    uint32_t owner, asset, offset, bytes, decoded;
    uint16_t kind;
};

/* The index order: by owner, then asset. */
static inline int asset_before(const struct asset_entry *a, uint32_t owner, uint32_t asset)
{
    return a->owner < owner || (a->owner == owner && a->asset < asset);
}

/* A switch builds the next index beside the active one, which answers until
 * the switch succeeds: assets_reserve frees any index under construction and,
 * for a count above 0, allocates one of `count` entries to fill in (owner,
 * asset) order, NULL for no memory; assets_commit makes it the active one;
 * assets_drop frees both. */
struct asset_entry *assets_reserve(uint32_t count);
void assets_commit(void);
void assets_drop(void);
/* Holds the core's asset provider while the active index has entries or the
 * look asks for flat icons, which also makes the core decode the bitmaps on
 * screen again; releases it otherwise. 0 when the core refused. */
int assets_bind(void);
void assets_release(void);

#endif
