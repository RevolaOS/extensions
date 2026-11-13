/* SPDX-License-Identifier: GPL-3.0-only */

#include "assets.h"
#include "bmap.h"
#include "ext.h"
#include "look.h"
#include "lz.h"
#include "osos-2.0.5.h"
#include "pixel.h"
#include "rv_libc.h"
#include "rvt.h"
#include "style.h"
#include "theme.h"
#include <stddef.h>

static struct asset_entry *live;    /* the active look's index, which the provider answers from */
static struct asset_entry *pending; /* the next look's, under construction by a switch */
static uint32_t live_count, pending_count;
static int providing; /* the core's provider is ours */

struct asset_entry *assets_reserve(uint32_t count)
{
    if (pending)
        api->free(self, pending);
    pending = count ? api->alloc(self, count * sizeof *pending) : NULL;
    pending_count = pending ? count : 0;
    return pending;
}

void assets_commit(void)
{
    if (live)
        api->free(self, live);
    live = pending;
    live_count = pending_count;
    pending = NULL;
    pending_count = 0;
}

void assets_drop(void)
{
    assets_reserve(0);
    assets_commit();
}

static const struct asset_entry *find(uint32_t owner, uint32_t asset)
{
    uint32_t low = 0, high = live_count;
    while (low < high) {
        uint32_t mid = low + (high - low) / 2;
        const struct asset_entry *entry = live + mid;
        if (entry->owner == owner && entry->asset == asset)
            return entry;
        if (asset_before(entry, owner, asset))
            low = mid + 1;
        else
            high = mid;
    }
    return NULL;
}

/* The core's probe: the size of the look's bitmap for the asset, from the index. */
static uint32_t replacement_bytes(uint32_t owner, uint32_t asset)
{
    const struct asset_entry *entry = find(owner, asset);
    return entry ? entry->decoded : 0;
}

/* The core's fill: the bitmap from the look's file, its header read and its
 * rows read or decoded behind it, and the header checked again over the
 * rows, since the file may not be the one that was indexed. A fill that
 * fails returns 0 and the core decodes the extension's own bitmap instead.
 * Nothing here binds or releases: the core is in the middle of a decode. */
static int replace(uint32_t owner, uint32_t asset, void *bmap, uint32_t bytes)
{
    struct rv_file *file;
    const struct asset_entry *entry = find(owner, asset);
    uint32_t crc = 0, payload;
    if (!entry || bytes != entry->decoded || !look_file_open(look_active(), &file))
        return 0;
    uint8_t *rows = (uint8_t *)bmap + RVT_BMAP_HEADER_BYTES;
    uint32_t row_bytes = bytes - RVT_BMAP_HEADER_BYTES;
    int whole = api->file_seek(self, file, entry->offset) == RV_OK &&
                read_exact(file, bmap, RVT_BMAP_HEADER_BYTES) &&
                (entry->kind == RVT_EXTENSION_BITMAP_LZ
                     ? lz_decode(file, entry->bytes - RVT_BMAP_HEADER_BYTES, rows, row_bytes, &crc)
                     : read_exact(file, rows, row_bytes));
    api->file_close(self, file);
    return whole && bmap_header_valid(bmap, rows, &payload) && payload == row_bytes;
}

/* An extension's Settings icon as the core decodes it: the art in the upper
 * rows of a 78 by 121 Mask8, its mirrored reflection below ICON_ART_ROWS. */
enum { ICON_WIDTH = 78, ICON_HEIGHT = 121, ICON_STRIDE = 80, ICON_ART_ROWS = 79 };

static int menu_bitmap(const uint8_t *b, uint32_t bytes)
{
    return bytes == RVT_BMAP_HEADER_BYTES + ICON_STRIDE * ICON_HEIGHT &&
           le16(b + BMap_format) == BMapFormat_Mask8 && le16(b + BMap_stride) == ICON_STRIDE &&
           le16(b + BMap_depth) == 8 && !le32(b + BMap_bounds) && !le32(b + BMap_bounds + 4) &&
           le32(b + BMap_bounds + 8) == ICON_HEIGHT && le32(b + BMap_bounds + 12) == ICON_WIDTH &&
           le32(b + BMap_payload_bytes) == ICON_STRIDE * ICON_HEIGHT;
}

/* The extension's own bitmap, decoded: under a flat-icon look a Settings
 * icon loses its reflection. */
static int adjust(uint32_t owner, uint32_t asset, void *bmap, uint32_t bytes)
{
    (void)owner;
    (void)asset;
    if (!(style_flags() & RVT_FLAG_FLAT_ICONS) || !menu_bitmap(bmap, bytes))
        return 0;
    memset((uint8_t *)bmap + RVT_BMAP_HEADER_BYTES + ICON_ART_ROWS * ICON_STRIDE, 0,
           (ICON_HEIGHT - ICON_ART_ROWS) * ICON_STRIDE);
    return 1;
}

static const struct rv_asset_provider provider = {replacement_bytes, replace, adjust};

int assets_bind(void)
{
    if (!live_count && !(style_flags() & RVT_FLAG_FLAT_ICONS)) {
        assets_release();
        return 1;
    }
    providing = api->asset_set_provider(self, &provider) == RV_OK;
    return providing;
}

void assets_release(void)
{
    if (providing)
        api->asset_set_provider(self, NULL);
    providing = 0;
}
