/* SPDX-License-Identifier: GPL-3.0-only */

#include "derive.h"
#include "pixel.h"
#include "rv_libc.h"
#include "switch.h"

enum { ONE = 256, FULL = 255, GRAY_MAP = 256 };

static int luma(const uint8_t *bgra)
{
    return (77 * bgra[2] + 150 * bgra[1] + 29 * bgra[0]) >> 8;
}

static int is_565(const struct layout *b)
{
    return b->depth == 16 && !b->colors && !b->mask;
}

/* A BGRA entry or pixel as the RGBA word the tables are keyed by. */
static uint32_t key_bgra(const uint8_t *p)
{
    return p[2] | (uint32_t)p[1] << 8 | (uint32_t)p[0] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t key_565(uint32_t w)
{
    uint8_t rgb[3];
    rgb565_unpack(w, rgb);
    return rgb[0] | (uint32_t)rgb[1] << 8 | (uint32_t)rgb[2] << 16 | 0xff000000u;
}

static uint32_t index_at(const struct layout *b, const uint8_t *rows, uint32_t x, uint32_t y)
{
    const uint8_t *row = rows + y * b->stride;
    return b->depth == 8 ? row[x] : le16(row + x * 2);
}

static void set_index(const struct layout *b, uint8_t *rows, uint32_t x, uint32_t y, uint32_t v)
{
    uint8_t *row = rows + y * b->stride;
    if (b->depth == 8) {
        row[x] = (uint8_t)v;
    } else {
        row[x * 2] = (uint8_t)v;
        row[x * 2 + 1] = (uint8_t)(v >> 8);
    }
}

/* The pair whose `from` is `k`, or NULL. */
static const uint8_t *lookup(const uint8_t *table, uint32_t count, uint32_t k)
{
    uint32_t low = 0, high = count;
    while (low < high) {
        uint32_t mid = (low + high) / 2, at = le32(table + mid * 8);
        if (at == k)
            return table + mid * 8 + 4;
        if (at < k)
            low = mid + 1;
        else
            high = mid;
    }
    return NULL;
}

static int exact_565(const uint8_t *rgba)
{
    return !(rgba[0] & 7) && !(rgba[1] & 3) && !(rgba[2] & 7) && rgba[3] == 255;
}

int derive_recolor(uint32_t asset, const struct layout *b, const uint8_t *table, uint32_t count)
{
    uint8_t *data = main_module->catalog_data(asset);
    if (b->mask || !count || count > RVT_RECOLOR_MAX)
        return 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *pair = table + i * 8;
        if ((i && le32(pair) <= le32(pair - 8)) || le32(pair) == le32(pair + 4) ||
            (is_565(b) && !(exact_565(pair) && exact_565(pair + 4))))
            return 0;
    }
    if (b->colors) {
        uint8_t *entry = data + b->start;
        for (uint32_t i = 0; i < b->colors; i++, entry += 4) {
            const uint8_t *to = lookup(table, count, key_bgra(entry));
            if (to)
                entry[0] = to[2], entry[1] = to[1], entry[2] = to[0], entry[3] = to[3];
        }
        return 1;
    }
    uint8_t *row = data + b->pixels;
    for (uint32_t y = 0; y < b->height; y++, row += b->stride)
        for (uint32_t x = 0; x < b->width; x++) {
            if (is_565(b)) {
                uint8_t *p = row + x * 2;
                const uint8_t *to = lookup(table, count, key_565(le16(p)));
                if (to) {
                    uint32_t w = rgb565_pack(to);
                    p[0] = (uint8_t)w, p[1] = (uint8_t)(w >> 8);
                }
            } else {
                uint8_t *p = row + x * 4;
                const uint8_t *to = lookup(table, count, key_bgra(p));
                if (to)
                    p[0] = to[2], p[1] = to[1], p[2] = to[0], p[3] = to[3];
            }
        }
    return 1;
}

int derive_invert(uint32_t asset, const struct layout *b)
{
    uint8_t *data = main_module->catalog_data(asset);
    if (b->mask)
        return 0;
    if (b->colors) {
        uint8_t *entry = data + b->start;
        for (uint32_t i = 0; i < b->colors; i++, entry += 4)
            entry[0] ^= 255, entry[1] ^= 255, entry[2] ^= 255;
        return 1;
    }
    uint8_t *row = data + b->pixels;
    for (uint32_t y = 0; y < b->height; y++, row += b->stride)
        for (uint32_t x = 0; x < b->width; x++) {
            uint8_t *p = row + x * (b->depth / 8);
            if (is_565(b))
                p[0] ^= 255, p[1] ^= 255;
            else if (p[3])
                p[0] ^= 255, p[1] ^= 255, p[2] ^= 255;
        }
    return 1;
}

/* Relight words: see enum rvt_relight. */
enum {
    DEAD = RVT_RELIGHT_T_ZERO,
    K = RVT_RELIGHT_T_FULL,
    BG_BASE = RVT_RELIGHT_BG_BASE,
    BG_REF = RVT_RELIGHT_BG_REF,
    BG_SLOPE = RVT_RELIGHT_BG_SLOPE,
    BG_LO = RVT_RELIGHT_BG_LO,
    BG_HI = RVT_RELIGHT_BG_HI,
    ICON_BASE = RVT_RELIGHT_ICON_BASE,
    ICON_LUMA = RVT_RELIGHT_ICON_LUMA,
    ICON_ROW = RVT_RELIGHT_ICON_ROW,
    ICON_LO = RVT_RELIGHT_ICON_LO,
    ICON_HI = RVT_RELIGHT_ICON_HI,
};

/* The words a product can take without overflowing 32 bits: offsets and
 * lumas are at most 255 in 8.8, rows below RVT_SCRATCH_PIXELS. */
enum {
    LEVEL_MAX = 255 * ONE,
    BASE_MAX = 1024 * ONE,
    SLOPE_MAX = 16 * ONE,
};

static int within(int32_t value, int32_t bound)
{
    return value >= -bound && value <= bound;
}

static int relight_valid(const struct layout *b, const int32_t *p)
{
    int32_t levels = ((p[ICON_HI] + ONE / 2) >> 8) - (p[BG_LO] >> 8) + 1;
    return b->colors && b->width * b->height <= RVT_SCRATCH_PIXELS && 0 <= p[DEAD] &&
           p[DEAD] < p[K] && p[K] <= LEVEL_MAX && within(p[BG_BASE], BASE_MAX) &&
           within(p[BG_REF], LEVEL_MAX) && within(p[BG_SLOPE], SLOPE_MAX) &&
           within(p[ICON_BASE], BASE_MAX) && within(p[ICON_LUMA], LEVEL_MAX) &&
           within(p[ICON_ROW], LEVEL_MAX) && 0 <= p[BG_LO] && p[BG_LO] <= p[BG_HI] &&
           p[BG_HI] <= p[ICON_LO] && p[ICON_LO] <= p[ICON_HI] && p[ICON_HI] <= LEVEL_MAX &&
           levels <= (int32_t)b->colors;
}

static int32_t offset_from(const uint8_t *c, const uint8_t *bg)
{
    int32_t o = 0;
    for (unsigned i = 0; i < 3; i++) {
        int32_t d = c[i] > bg[i] ? c[i] - bg[i] : bg[i] - c[i];
        o = d > o ? d : o;
    }
    return o * ONE;
}

int derive_relight(uint32_t asset, const struct layout *b, const int32_t *p, uint8_t *scratch)
{
    uint8_t *data = main_module->catalog_data(asset), *palette = data + b->start;
    uint8_t *rows = data + b->pixels;
    uint32_t w = b->width, h = b->height;
    uint8_t *t = scratch, *map = scratch + RVT_SCRATCH_PIXELS;
    if (!relight_valid(b, p))
        return 0;
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
            if (index_at(b, rows, x, y) >= b->colors)
                return 0;
    uint64_t range = reciprocal((uint32_t)(p[K] - p[DEAD]));
    /* Coverage, then shadows: a partly covered pixel with no fully covered
     * neighbour. Pruning in place is safe because only full pixels count as
     * neighbours and pruning never creates one. */
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *bg = palette + index_at(b, rows, 0, y) * 4;
        for (uint32_t x = 0; x < w; x++) {
            int32_t o = offset_from(palette + index_at(b, rows, x, y) * 4, bg);
            t[y * w + x] = (uint8_t)(o <= p[DEAD] ? 0
                                     : o >= p[K]  ? FULL
                                                  : divide((uint32_t)(o - p[DEAD]) * FULL, range));
        }
    }
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            if (!t[y * w + x] || t[y * w + x] == FULL)
                continue;
            int solid = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int32_t nx = (int32_t)x + dx, ny = (int32_t)y + dy;
                    if ((dx || dy) && nx >= 0 && ny >= 0 && nx < (int32_t)w && ny < (int32_t)h &&
                        t[ny * w + nx] == FULL)
                        solid = 1;
                }
            if (!solid)
                t[y * w + x] = 0;
        }
    /* The grays overwrite the coverage in place; the palette is read here and
     * rewritten only after this pass. */
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *bg = palette + index_at(b, rows, 0, y) * 4;
        int32_t bgd = p[BG_BASE] + (luma(bg) * ONE - p[BG_REF]) * p[BG_SLOPE] / ONE;
        bgd = clamp(bgd, p[BG_LO], p[BG_HI]);
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t *c = palette + index_at(b, rows, x, y) * 4;
            int32_t icon = p[ICON_BASE] + p[ICON_LUMA] * luma(c) + p[ICON_ROW] * (int32_t)y;
            icon = clamp(icon, p[ICON_LO], p[ICON_HI]);
            int32_t mixed =
                bgd + (int32_t)divide((uint32_t)(icon - bgd) * t[y * w + x] + FULL / 2, reciprocal(FULL));
            t[y * w + x] = (uint8_t)((mixed + ONE / 2) >> 8);
        }
    }
    memset(map, 0, GRAY_MAP);
    for (uint32_t i = 0; i < w * h; i++)
        map[t[i]] = 1;
    uint32_t n = 0;
    for (uint32_t g = 0; g < GRAY_MAP; g++)
        if (map[g]) {
            if (n == b->colors) {
                /* More grays than entries: a half-written bitmap, zeroed for the caller. */
                memset(data + b->start, 0, main_module->catalog_size(asset) - b->start);
                return 0;
            }
            palette[n * 4] = palette[n * 4 + 1] = palette[n * 4 + 2] = (uint8_t)g;
            palette[n * 4 + 3] = 255;
            map[g] = (uint8_t)n++;
        }
    memset(palette + n * 4, 0, (b->colors - n) * 4);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
            set_index(b, rows, x, y, map[t[y * w + x]]);
    return 1;
}

/* The RGBA of a source pixel, whatever its format. */
static void source_pixel(const struct layout *s, const uint8_t *data, uint32_t x, uint32_t y,
                         uint8_t rgba[4])
{
    const uint8_t *p;
    if (s->colors) {
        p = data + s->start + index_at(s, data + s->pixels, x, y) * 4;
    } else if (is_565(s)) {
        rgb565_unpack(le16(data + s->pixels + y * s->stride + x * 2), rgba);
        rgba[3] = 255;
        return;
    } else {
        p = data + s->pixels + y * s->stride + x * 4;
    }
    rgba[0] = p[2], rgba[1] = p[1], rgba[2] = p[0], rgba[3] = p[3];
}

int derive_alias(uint32_t asset, const struct layout *b, uint32_t source)
{
    struct layout s;
    uint8_t *data = main_module->catalog_data(asset);
    if (source == asset || !main_module->catalog_layout(source, &s) || b->mask || s.mask ||
        b->width != s.width || b->height != s.height)
        return 0;
    const uint8_t *from = main_module->catalog_data(source);
    if (same_layout(b, &s) && main_module->catalog_size(asset) == main_module->catalog_size(source)) {
        memcpy(data + b->start, from + s.start, main_module->catalog_size(asset) - b->start);
        return 1;
    }
    if (b->width * b->height > RVT_SCRATCH_PIXELS)
        return 0;
    if (s.colors)
        for (uint32_t y = 0; y < s.height; y++)
            for (uint32_t x = 0; x < s.width; x++)
                if (index_at(&s, from + s.pixels, x, y) >= s.colors)
                    return 0;
    uint8_t *palette = data + b->start, *rows = data + b->pixels;
    uint32_t n = 0;
    for (uint32_t y = 0; y < b->height; y++)
        for (uint32_t x = 0; x < b->width; x++) {
            uint8_t rgba[4];
            source_pixel(&s, from, x, y, rgba);
            if (b->colors) {
                uint8_t bgra[4] = {rgba[2], rgba[1], rgba[0], rgba[3]};
                uint32_t i = 0;
                while (i < n && memcmp(palette + i * 4, bgra, 4))
                    i++;
                if (i == n) {
                    if (n == b->colors) {
                        /* Out of palette entries: a half-written bitmap, zeroed for the caller. */
                        memset(data + b->start, 0, main_module->catalog_size(asset) - b->start);
                        return 0;
                    }
                    memcpy(palette + n++ * 4, bgra, 4);
                }
                set_index(b, rows, x, y, i);
            } else if (is_565(b)) {
                uint32_t w = rgb565_pack(rgba);
                uint8_t *p = rows + y * b->stride + x * 2;
                p[0] = (uint8_t)w, p[1] = (uint8_t)(w >> 8);
            } else {
                uint8_t *p = rows + y * b->stride + x * 4;
                p[0] = rgba[2], p[1] = rgba[1], p[2] = rgba[0], p[3] = rgba[3];
            }
        }
    if (b->colors)
        memset(palette + n * 4, 0, (b->colors - n) * 4);
    return 1;
}

/* One BGRA colour's alpha as tint coverage: as it is, or scaled by how dark
 * the colour is when the tint reads coverage from darkness. */
static void coverage_of(uint8_t *bgra, int darkness)
{
    if (darkness)
        bgra[3] = (uint8_t)div255((unsigned)bgra[3] * (unsigned)(255 - luma(bgra)));
}

int derive_coverage(uint32_t asset, const struct layout *b, int darkness)
{
    uint8_t *data = main_module->catalog_data(asset);
    if (b->mask || is_565(b))
        return 0;
    if (b->colors) {
        for (uint32_t i = 0; i < b->colors; i++)
            coverage_of(data + b->start + i * 4, darkness);
        return 1;
    }
    uint8_t *row = data + b->pixels;
    for (uint32_t y = 0; y < b->height; y++, row += b->stride)
        for (uint32_t x = 0; x < b->width; x++)
            coverage_of(row + x * 4, darkness);
    return 1;
}
