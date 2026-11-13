/* SPDX-License-Identifier: GPL-3.0-only */

#include "pack.h"
#include "bmap.h"
#include "derive.h"
#include "ext.h"
#include "pixel.h"
#include "rv_libc.h"
#include "stock.h"
#include "switch.h"

static uint32_t checksum(uint32_t sum, const void *data, uint32_t size)
{
    return main_module->crc32_update(sum, data, size);
}

static const uint8_t *tint_bytes(const struct pack *pack)
{
    return (const uint8_t *)pack->style + RVT_STYLE_TINTS;
}

/* Opaque colours, known flags, and tint bytes that name a role on a bitmap
 * the catalog has; checked before anything is written. */
static int style_valid(const uint8_t *style, uint32_t catalog_count)
{
    const uint8_t *p = style + RVT_STYLE_COLORS;
    for (unsigned i = 0; i < RVT_ROLES; i++)
        if (p[i * 4 + 3] != 255)
            return 0;
    if (le32(style + RVT_STYLE_FLAGS) & ~(uint32_t)(RVT_FLAG_FLAT_SELECTION | RVT_FLAG_MENU_COVERS_OFF |
                                               RVT_FLAG_FLAT_NOW_PLAYING | RVT_FLAG_FLAT_ICONS))
        return 0;
    p = style + RVT_STYLE_PALETTES;
    for (unsigned i = 0; i < RVT_ACCENTS * RVT_ACCENT_SLOTS; i++)
        if (p[i * 4 + 3] != 255)
            return 0;
    p = style + RVT_STYLE_TINTS;
    for (unsigned i = 0; i < RVT_CATALOG_MAX; i++) {
        unsigned tint = p[i] & ~RVT_TINT_DARKNESS;
        if (tint >= RVT_TINT_ROLE + RVT_ROLES || (p[i] != tint && tint == RVT_TINT_NONE) ||
            (p[i] && i >= catalog_count))
            return 0;
    }
    return 1;
}

int pack_plain(const struct pack *pack)
{
    const uint8_t *tint = tint_bytes(pack);
    if (le32((const uint8_t *)pack->style + RVT_STYLE_FLAGS))
        return 0;
    for (unsigned i = 0; i < RVT_CATALOG_MAX; i++)
        if (tint[i])
            return 0;
    return 1;
}

int pack_open(struct rv_file *file, uint32_t header_crc, const struct pack_place *place,
              struct pack *pack)
{
    uint8_t pack_header[RVT_PACK_HEADER_BYTES];
    uint32_t crc;
    memset(pack, 0, sizeof *pack);
    if (api->file_seek(self, file, RVT_HEADER_CRC) != RV_OK || !read_exact(file, &crc, sizeof crc) ||
        crc != header_crc || api->file_seek(self, file, place->offset) != RV_OK ||
        !read_exact(file, pack_header, sizeof pack_header))
        return 0;
    pack->body = place->offset + RVT_PACK_HEADER_BYTES;
    pack->body_bytes = le32(pack_header + RVT_PACK_BODY_BYTES);
    pack->count = le32(pack_header + RVT_PACK_RECORDS);
    pack->crc = le32(pack_header + RVT_PACK_CRC);
    pack->extension_bitmaps = le32(pack_header + RVT_PACK_EXTENSION_BITMAPS);
    if (pack->body_bytes != place->bytes - RVT_PACK_HEADER_BYTES ||
        pack->count > main_module->catalog_count() ||
        pack->extension_bitmaps > RVT_EXTENSION_BITMAPS_MAX ||
        !read_exact(file, pack->style, RVT_STYLE_BYTES) ||
        !style_valid((const uint8_t *)pack->style, main_module->catalog_count()))
        return 0;
    pack->file = file;
    return 1;
}

/* The payload bytes a kind allows for a bitmap. */
static int record_valid(uint32_t asset, uint32_t kind, uint32_t size, uint32_t i,
                        uint32_t previous, struct layout *b)
{
    if ((i && asset <= previous) || !main_module->catalog_layout(asset, b))
        return 0;
    switch (kind) {
    case RVT_KIND_PIXELS:
        return size == main_module->catalog_size(asset) - b->start;
    case RVT_KIND_PIXELS_LZ:
        return size > 0;
    case RVT_KIND_RECOLOR:
        return size >= 4 + 8 && (size - 4) % 8 == 0 && (size - 4) / 8 <= RVT_RECOLOR_MAX;
    case RVT_KIND_ALIAS:
        return size == 4;
    case RVT_KIND_INVERT:
        return size == 0;
    case RVT_KIND_RELIGHT:
        return size == RVT_RELIGHT_BYTES;
    }
    return 0;
}

/* Every pixel of a paletted bitmap names a palette entry; `data` starts at
 * the palette. */
static int indices_valid(const struct layout *b, const uint8_t *data)
{
    uint32_t colors = b->colors;
    if (!colors || (b->depth == 8 && colors >= 256))
        return 1;
    const uint8_t *row = data + b->pixels - b->start;
    for (uint32_t y = 0; y < b->height; y++, row += b->stride)
        for (uint32_t x = 0; x < b->width; x++)
            if ((b->depth == 8 ? row[x] : le16(row + x * 2)) >= colors)
                return 0;
    return 1;
}

/* One bit per tinted bitmap. A pixels record clears its bit; a derived record
 * on a tinted bitmap is invalid; what remains is tinted stock art. */
static void tint_pending(const uint8_t *tint, uint32_t *pending)
{
    memset(pending, 0, BITMAP_WORDS * 4);
    for (uint32_t i = 0; i < RVT_CATALOG_MAX; i++)
        if (tint[i])
            bit_set(pending, i);
}

/* The bitmap holds its stock bytes: fetched when the active look changed it. */
static int stock_ready(uint32_t asset, const struct layout *b, const uint32_t *current)
{
    return !bit_test(current, asset) ||
           stock_fetch(asset, main_module->catalog_data(asset) + b->start);
}

struct alias {
    uint16_t asset, source;
};

/* The stored bytes a kind allows after the header, at least one: the rows,
 * or an lz stream for them, which is at most a flag byte per eight literals
 * longer. */
static int bitmap_size_valid(uint32_t kind, uint32_t size, uint32_t payload)
{
    uint32_t stored = size - RVT_BMAP_HEADER_BYTES;
    if (kind == RVT_EXTENSION_BITMAP)
        return stored == payload;
    return stored <= payload + (payload + 7) / 8;
}

/* Bytes the switch leaves in the file, read for the checksum only. */
static int checksum_skip(struct rv_file *file, uint32_t bytes, uint32_t *sum)
{
    uint8_t chunk[256];
    while (bytes) {
        uint32_t n = bytes < sizeof chunk ? bytes : sizeof chunk;
        if (!read_exact(file, chunk, n))
            return 0;
        *sum = checksum(*sum, chunk, n);
        bytes -= n;
    }
    return 1;
}

/* The extension bitmaps after the records into the index: each entry and the
 * header of its bitmap are checked and the header's place kept; the rows are
 * read into the checksum only and served on demand by the provider. */
static int bitmaps_index(struct pack *pack, uint32_t *consumed, uint32_t *sum)
{
    struct asset_entry *entries = main_module->assets_reserve(pack->extension_bitmaps);
    if (pack->extension_bitmaps && !entries)
        return 0;
    for (uint32_t i = 0; i < pack->extension_bitmaps; i++) {
        uint32_t entry[4], payload, zero = 0;
        uint8_t header[RVT_BMAP_HEADER_BYTES];
        if (pack->body_bytes - *consumed < sizeof entry ||
            !read_exact(pack->file, entry, sizeof entry))
            return 0;
        *consumed += sizeof entry;
        *sum = checksum(*sum, entry, sizeof entry);
        uint32_t owner = entry[0], asset = entry[1], kind = entry[2], size = entry[3];
        uint32_t padding = (0u - size) & 3u;
        if ((i && !asset_before(entries + i - 1, owner, asset)) ||
            (kind != RVT_EXTENSION_BITMAP && kind != RVT_EXTENSION_BITMAP_LZ) ||
            size <= RVT_BMAP_HEADER_BYTES || size > pack->body_bytes - *consumed ||
            padding > pack->body_bytes - *consumed - size ||
            !read_exact(pack->file, header, sizeof header))
            return 0;
        *sum = checksum(*sum, header, sizeof header);
        /* A raw Indexed8 payload starts with its colour count, which the
         * header check needs; an lz one is checked when it is decoded. */
        uint32_t skip = size - RVT_BMAP_HEADER_BYTES, word[1];
        const uint8_t *rows = NULL;
        if (kind == RVT_EXTENSION_BITMAP && skip >= sizeof word) {
            if (!read_exact(pack->file, word, sizeof word))
                return 0;
            *sum = checksum(*sum, word, sizeof word);
            skip -= sizeof word;
            rows = (const uint8_t *)word;
        }
        if (!bmap_header_valid(header, rows, &payload) || !bitmap_size_valid(kind, size, payload))
            return 0;
        entries[i].owner = owner;
        entries[i].asset = asset;
        entries[i].offset = pack->body + *consumed;
        entries[i].bytes = size;
        entries[i].decoded = RVT_BMAP_HEADER_BYTES + payload;
        entries[i].kind = (uint16_t)kind;
        if (!checksum_skip(pack->file, skip, sum) ||
            (padding && (!read_exact(pack->file, &zero, padding) || zero)))
            return 0;
        *sum = checksum(*sum, &zero, padding);
        *consumed += size + padding;
    }
    return 1;
}

int pack_apply(struct pack *pack, uint32_t *written, const uint32_t *current)
{
    struct rv_file *file = pack->file;
    uint32_t pending[BITMAP_WORDS], previous = 0;
    uint32_t sum = checksum(0xffffffffu, pack->style, RVT_STYLE_BYTES);
    uint32_t consumed = RVT_STYLE_BYTES;
    struct alias aliases[RVT_ALIASES_MAX];
    uint32_t naliases = 0;
    uint8_t *scratch = NULL;
    int ok = 0;
    tint_pending(tint_bytes(pack), pending);
    for (uint32_t i = 0; i < pack->count; i++) {
        uint32_t record[3], payload[RVT_RELIGHT_WORDS];
        struct layout b;
        if (pack->body_bytes - consumed < sizeof record || !read_exact(file, record, sizeof record))
            goto done;
        consumed += sizeof record;
        sum = checksum(sum, record, sizeof record);
        uint32_t asset = record[0], kind = record[1], size = record[2];
        uint32_t padding = (0u - size) & 3u;
        if (kind >= RVT_KINDS || size > pack->body_bytes - consumed ||
            padding > pack->body_bytes - consumed - size ||
            !record_valid(asset, kind, size, i, previous, &b))
            goto done;
        previous = asset;
        bit_set(written, asset);
        uint8_t *data = main_module->catalog_data(asset) + b.start;
        uint32_t bytes = main_module->catalog_size(asset) - b.start;
        if (kind == RVT_KIND_PIXELS || kind == RVT_KIND_PIXELS_LZ) {
            /* A resident bitmap must never index past its palette. */
            int whole;
            if (kind == RVT_KIND_PIXELS) {
                whole = read_exact(file, data, size);
                sum = checksum(sum, data, size);
            } else {
                whole = main_module->lz_decode(file, size, data, bytes, &sum);
            }
            if (!whole || !indices_valid(&b, data)) {
                memset(data, 0, bytes);
                goto done;
            }
            pending[asset / 32] &= ~(1u << (asset % 32));
        } else {
            if (bit_test(pending, asset) ||
                (!scratch && !(scratch = api->alloc(self, RVT_SCRATCH_BYTES))))
                goto done;
            uint8_t *table = (uint8_t *)payload;
            uint32_t pairs = 0;
            if (kind == RVT_KIND_RECOLOR) {
                /* The count, then the pairs into the scratch block, which holds
                 * RVT_RECOLOR_MAX of them and nothing else. */
                if (!read_exact(file, &pairs, 4) || pairs != (size - 4) / 8)
                    goto done;
                sum = checksum(sum, &pairs, 4);
                table = scratch;
                size -= 4;
            }
            if (!read_exact(file, table, size))
                goto done;
            sum = checksum(sum, table, size);
            if (kind == RVT_KIND_RECOLOR)
                size += 4;
            if (kind == RVT_KIND_ALIAS) {
                uint32_t source = payload[0];
                struct layout s;
                if (naliases == RVT_ALIASES_MAX || source == asset ||
                    !main_module->catalog_layout(source, &s) || tint_bytes(pack)[source] ||
                    b.mask || s.mask || b.width != s.width || b.height != s.height)
                    goto done;
                aliases[naliases].asset = (uint16_t)asset;
                aliases[naliases++].source = (uint16_t)source;
            } else {
                if (!stock_ready(asset, &b, current))
                    goto done;
                int derived = kind == RVT_KIND_RECOLOR
                                  ? derive_recolor(asset, &b, scratch, pairs)
                              : kind == RVT_KIND_INVERT
                                  ? derive_invert(asset, &b)
                                  : derive_relight(asset, &b, (const int32_t *)payload, scratch);
                if (!derived)
                    goto done;
            }
        }
        uint32_t zero = 0;
        if (padding && (!read_exact(file, &zero, padding) || zero))
            goto done;
        sum = checksum(sum, &zero, padding);
        consumed += size + padding;
    }
    if (!bitmaps_index(pack, &consumed, &sum) || consumed != pack->body_bytes || ~sum != pack->crc)
        goto done;
    /* Aliases last, so every source is final; a source that is itself listed
     * is a chain. A source the pack left alone must be stock, and the target
     * starts from stock too, so its row padding is the same whatever the
     * previous look left there. */
    for (uint32_t a = 0; a < naliases; a++) {
        struct layout b, s;
        for (uint32_t c = 0; c < naliases; c++)
            if (aliases[c].asset == aliases[a].source)
                goto done;
        if (!main_module->catalog_layout(aliases[a].asset, &b) ||
            !main_module->catalog_layout(aliases[a].source, &s))
            goto done;
        if (!bit_test(written, aliases[a].source) && !stock_ready(aliases[a].source, &s, current))
            goto done;
        if (!stock_ready(aliases[a].asset, &b, current) ||
            !derive_alias(aliases[a].asset, &b, aliases[a].source))
            goto done;
    }
    /* Tinted paletted and ARGB bitmaps without a record are tinted stock art:
     * their coverage is prepared here; masks are coloured while drawing. */
    for (uint32_t i = 0; i < RVT_CATALOG_MAX; i++) {
        struct layout b;
        if (!bit_test(pending, i))
            continue;
        if (!main_module->catalog_layout(i, &b))
            goto done;
        if (b.mask)
            continue;
        bit_set(written, i);
        if (!stock_ready(i, &b, current) ||
            !derive_coverage(i, &b, (tint_bytes(pack)[i] & RVT_TINT_DARKNESS) != 0))
            goto done;
    }
    ok = 1;
done:
    if (scratch)
        api->free(self, scratch);
    return ok;
}
