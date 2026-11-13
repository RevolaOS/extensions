/* SPDX-License-Identifier: GPL-3.0-only */

/* The BMap header of a bitmap a theme holds for an extension, checked as the
 * core checks one before it draws it, so the index and the provider never
 * hand over a bitmap the core would refuse. */

#ifndef BMAP_H
#define BMAP_H
#include "osos-2.0.5.h"
#include "pixel.h"
#include "rvt.h"
#include <stdint.h>

/* Whether the 28-byte header `h` names a known format whose rows, and for
 * Indexed8 whose palette, fit its payload word under the format's cap. `rows`
 * is the payload when it is at hand, for the palette's colour count, NULL
 * when only the header is known. *payload is the header's payload word. */
static inline int bmap_header_valid(const uint8_t *h, const uint8_t *rows, uint32_t *payload)
{
    uint32_t format = le16(h + BMap_format);
    int32_t top = (int32_t)le32(h + BMap_bounds), left = (int32_t)le32(h + BMap_bounds + 4);
    int32_t bottom = (int32_t)le32(h + BMap_bounds + 8), right = (int32_t)le32(h + BMap_bounds + 12);
    uint32_t stride = le16(h + BMap_stride), depth = le16(h + BMap_depth), palette = 0;
    *payload = le32(h + BMap_payload_bytes);
    if (format != BMapFormat_Mask4 && format != BMapFormat_Mask8 && format != BMapFormat_Indexed8 &&
        format != BMapFormat_Indexed16 && format != BMapFormat_RGB565 &&
        format != BMapFormat_BGRA8888)
        return 0;
    if (top < 0 || left < 0 || bottom < top || right < left)
        return 0;
    if (format == BMapFormat_Indexed8) {
        if (*payload < 4)
            return 0;
        uint32_t colors = rows ? le32(rows) : 0;
        if (colors > 256)
            return 0;
        palette = 4 + 4 * colors;
    }
    uint32_t height = (uint32_t)(bottom - top), width = (uint32_t)(right - left);
    return height <= 0xffff && width <= 0xffff && depth <= 32 && width * depth <= stride * 8 &&
           *payload <= RVT_EXTENSION_BITMAP_MAX_BYTES - RVT_BMAP_HEADER_BYTES &&
           palette <= *payload && height * stride <= *payload - palette;
}

#endif
