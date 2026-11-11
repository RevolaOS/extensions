/* SPDX-License-Identifier: GPL-3.0-only */

#include "bitmap.h"
#include "osos-2.0.5.h"
#include <stddef.h>

enum {
    icon_width = 13,
    icon_height = 29,
    palette_capacity = 70,
    output_pixels = BMap_payload + 4 + palette_capacity * 4,
    output_size = output_pixels + icon_width * icon_height,
};

/* Drawables borrow module-lifetime, word-aligned BMaps for each icon variant. */
static uint32_t half_icons[4][(output_size + 3) / 4];
static const uint16_t icon_sizes[] = {537, 597, 533, 593};
_Static_assert(sizeof half_icons[0] >= output_size, "Half-circle bitmap capacity");

/* Inner semicircle coverage, out of 64 samples per pixel. Rows start at y=0
 * for video and y=10 for audio; columns start at x=6. The 23 partial pixels
 * require at most 23 additional palette entries. */
static const uint8_t inner_coverage[9][5] = {
    {12, 11, 0, 0, 0},    {48, 64, 49, 6, 0},   {48, 64, 64, 52, 1},
    {48, 64, 64, 64, 19}, {48, 64, 64, 64, 32}, {48, 64, 64, 64, 26},
    {48, 64, 64, 61, 6},  {48, 64, 63, 21, 0},  {30, 35, 10, 0, 0},
};

static uint32_t icon_word(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Derive from the current themed pixels. Only the interior of the right half
 * changes; the native antialiased outline and the video glyph stay intact. */
const void *progress_bitmap(const uint8_t *source, unsigned variant)
{
    if (variant >= 4)
        return NULL;
    unsigned size = icon_sizes[variant];
    if (!source || source[BMap_format] != BMapFormat_Indexed8 || source[BMap_format + 1] ||
        source[BMap_stride] != icon_width || source[BMap_stride + 1] ||
        source[BMap_depth] != 8 || source[BMap_depth + 1] ||
        icon_word(source + BMap_bounds) || icon_word(source + BMap_bounds + 4) ||
        icon_word(source + BMap_bounds + 8) != icon_height ||
        icon_word(source + BMap_bounds + 12) != icon_width ||
        icon_word(source + BMap_payload_bytes) != size - BMap_payload)
        return NULL;
    unsigned colors = icon_word(source + BMap_payload);
    unsigned pixels = BMap_payload + 4 + colors * 4;
    if (colors > 256 || pixels + 13 * 29 != size)
        return NULL;
    unsigned transparent = 0;
    while (transparent < colors && source[BMap_payload + 4 + transparent * 4 + 3])
        transparent++;
    if (transparent == colors)
        return NULL;

    /* Validate before modifying storage that an earlier drawable may borrow.
     * Each partially covered pixel can add at most one palette entry; fully
     * cleared pixels use the existing transparent entry. */
    enum { extra = 23 };
    if (colors + extra > palette_capacity)
        return NULL;
    for (unsigned i = 0; i < icon_width * icon_height; i++)
        if (source[pixels + i] >= colors)
            return NULL;

    uint8_t *image = (void *)half_icons[variant];
    for (unsigned i = 0; i < output_pixels; i++)
        image[i] = i < pixels ? source[i] : 0;
    for (unsigned i = 0; i < icon_width * icon_height; i++)
        image[output_pixels + i] = source[pixels + i];
    half_icons[variant][BMap_payload_bytes / 4] = output_size - BMap_payload;
    half_icons[variant][BMap_payload / 4] = palette_capacity;

    unsigned top = variant < 2 ? 0 : 10;
    unsigned used = colors;
    for (unsigned y = 0; y < 9; y++) {
        for (unsigned x = 0; x < 5; x++) {
            unsigned clear = inner_coverage[y][x];
            if (!clear)
                continue;
            unsigned pixel = output_pixels + (top + y) * icon_width + x + 6;
            const uint8_t *color = source + BMap_payload + 4 + image[pixel] * 4;
            unsigned alpha = (color[3] * (64 - clear) + 32) / 64;
            if (!alpha) {
                image[pixel] = transparent;
                continue;
            }
            unsigned match = 0;
            for (; match < used; match++) {
                const uint8_t *candidate = image + BMap_payload + 4 + match * 4;
                if (candidate[0] == color[0] && candidate[1] == color[1] &&
                    candidate[2] == color[2] && candidate[3] == alpha)
                    break;
            }
            if (match == used) {
                uint8_t *added = image + BMap_payload + 4 + used++ * 4;
                for (unsigned i = 0; i < 3; i++)
                    added[i] = color[i];
                added[3] = alpha;
            }
            image[pixel] = match;
        }
    }
    return image;
}

int progress_bitmap_owns(const void *payload)
{
    for (unsigned i = 0; i < 4; i++)
        if (payload == (const uint8_t *)half_icons[i] + BMap_payload)
            return 1;
    return 0;
}
