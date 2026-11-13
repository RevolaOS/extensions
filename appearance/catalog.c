/* SPDX-License-Identifier: GPL-3.0-only */

#include "catalog.h"
#include "osos-2.0.5.h"
#include "pixel.h"

struct contrast_text {
    uint32_t first;
    uint16_t count, index;
};
struct accent_art {
    uint16_t index;
    uint8_t role;
};
/* catalog_offsets[i] is bitmap i's offset from the bank base, word aligned,
 * with the padding that follows it in the low two bits; the last entry ends
 * the bank. */
#include "catalog.inc"
_Static_assert(sizeof contrast_text / sizeof *contrast_text == CATALOG_CONTRAST_TEXTS,
               "catalog.h names the contrast text count");

static uint32_t offset_of(uint32_t index)
{
    return catalog_offsets[index] & ~3u;
}

uint32_t catalog_count(void)
{
    return CATALOG_COUNT;
}

const uint8_t *catalog_fingerprint(void)
{
    return fingerprint;
}

uint8_t *catalog_data(uint32_t index)
{
    return (uint8_t *)CATALOG_BASE + offset_of(index);
}

uint32_t catalog_size(uint32_t index)
{
    return offset_of(index + 1) - offset_of(index) - (catalog_offsets[index] & 3u);
}

uint32_t catalog_crc(uint32_t index)
{
    return catalog_crcs[index];
}

uint32_t catalog_file_offset(uint32_t index)
{
    return CATALOG_FILE_OFFSET + offset_of(index);
}

/* A BMap: the header's format, stride, depth and bounds, then its data length
 * word right before the data, which a bank bitmap holds at BMap_payload_bytes
 * or one word later; then for paletted formats the colour count and the BGRA
 * palette, then the rows. The bounds start at the origin. */
int catalog_layout(uint32_t index, struct layout *b)
{
    if (index >= CATALOG_COUNT || index >= RVT_CATALOG_MAX)
        return 0;
    uint32_t size = catalog_size(index);
    const uint8_t *p = catalog_data(index);
    if (size < RVT_BMAP_HEADER_BYTES)
        return 0;
    b->start = le32(p + BMap_payload_bytes) == size - BMap_payload ? BMap_payload : BMap_payload + 4;
    if (size < b->start || le32(p + b->start - 4) != size - b->start)
        return 0;
    uint32_t format = le16(p + BMap_format);
    b->mask = format == BMapFormat_Mask4 || format == BMapFormat_Mask8;
    b->depth = format == BMapFormat_Mask4                                       ? 4
               : format == BMapFormat_Mask8 || format == BMapFormat_Indexed8     ? 8
               : format == BMapFormat_Indexed16 || format == BMapFormat_RGB565   ? 16
               : format == BMapFormat_BGRA8888                                   ? 32
                                                                                 : 0;
    b->stride = le16(p + BMap_stride);
    b->height = le32(p + BMap_bounds + 8);
    b->width = le32(p + BMap_bounds + 12);
    b->colors = 0;
    if (le32(p + BMap_bounds) || le32(p + BMap_bounds + 4))
        return 0;
    if (!b->depth || le16(p + BMap_depth) != b->depth || !b->width || !b->height || !b->stride ||
        b->width > 0xffffu || b->height > 0xffffu || b->width * b->depth > b->stride * 8)
        return 0;
    if (format == BMapFormat_Indexed8 || format == BMapFormat_Indexed16) {
        if (size - b->start < 4)
            return 0;
        b->colors = le32(p + b->start);
        b->start += 4;
        if (!b->colors || b->colors > (1u << b->depth) || b->colors > (size - b->start) / 4)
            return 0;
    }
    b->pixels = b->start + b->colors * 4;
    return b->pixels <= size && b->height * b->stride == size - b->pixels;
}

/* The last answers: a screen draws the same few bitmaps again and again. */
enum { RECENT = 8 };
static struct {
    const uint8_t *pixels;
    uint32_t index;
} recent[RECENT];
static unsigned recent_next;

static int catalog_contains(const uint8_t *pixels)
{
    const uint8_t *base = (const uint8_t *)CATALOG_BASE;
    return pixels >= base && pixels < base + CATALOG_BANK_BYTES;
}

int catalog_from_pixels(const uint8_t *pixels, uint32_t *index)
{
    const uint8_t *base = (const uint8_t *)CATALOG_BASE;
    if (!catalog_contains(pixels) || pixels < base + RVT_BMAP_HEADER_BYTES)
        return 0;
    for (unsigned i = 0; i < RECENT; i++)
        if (recent[i].pixels == pixels) {
            *index = recent[i].index;
            return 1;
        }
    /* The header the pixels follow, found by bisection over the offsets. */
    uint32_t at = (uint32_t)(pixels - base) - RVT_BMAP_HEADER_BYTES, low = 0, high = CATALOG_COUNT;
    while (high - low > 1) {
        uint32_t mid = (low + high) / 2;
        if (offset_of(mid) <= at)
            low = mid;
        else
            high = mid;
    }
    struct layout b;
    if (offset_of(low) != at || !catalog_layout(low, &b))
        return 0;
    recent[recent_next].pixels = pixels;
    recent[recent_next].index = low;
    recent_next = (recent_next + 1) % RECENT;
    *index = low;
    return 1;
}

uint8_t *catalog_palette(uint32_t index, uint32_t *colors)
{
    struct layout b;
    if (!catalog_layout(index, &b) || !b.colors)
        return 0;
    *colors = b.colors;
    return catalog_data(index) + b.start;
}

uint8_t *catalog_argb(uint32_t index, uint32_t *width, uint32_t *height, uint32_t *stride)
{
    struct layout b;
    if (!catalog_layout(index, &b) || b.depth != 32)
        return 0;
    *width = b.width;
    *height = b.height;
    *stride = b.stride;
    return catalog_data(index) + b.pixels;
}

int catalog_center_pixel(uint32_t index, uint8_t out[4])
{
    struct layout b;
    if (!catalog_layout(index, &b) || b.mask || (b.depth == 16 && !b.colors))
        return 0;
    const uint8_t *data = catalog_data(index);
    const uint8_t *row = data + b.pixels + ((b.height - 1) / 2) * b.stride;
    const uint8_t *pixel;
    if (b.colors) {
        uint32_t column = (b.width - 1) / 2;
        uint32_t entry = b.depth == 16 ? le16(row + column * 2) : row[column];
        if (entry >= b.colors)
            return 0;
        pixel = data + b.start + entry * 4;
    } else {
        pixel = row + ((b.width - 1) / 2) * 4;
    }
    if (!pixel[3])
        return 0;
    out[0] = pixel[2];
    out[1] = pixel[1];
    out[2] = pixel[0];
    out[3] = 255;
    return 1;
}

uint32_t catalog_well_center(unsigned well)
{
    return well_centers[well < WELLS ? well : 0];
}

uint32_t catalog_field_center(void)
{
    return field_center;
}

uint32_t catalog_calendar_cell(unsigned place)
{
    return calendar_cells[place < CALENDAR_CELLS ? place : 0];
}

int catalog_accent_art(uint32_t i, uint32_t *index, uint8_t *role)
{
    if (i >= sizeof accent_art / sizeof *accent_art)
        return 0;
    *index = accent_art[i].index;
    *role = accent_art[i].role;
    return 1;
}

int catalog_contrast_text(uint32_t view)
{
    for (uint32_t i = 0; i < CATALOG_CONTRAST_TEXTS; i++)
        if (view - contrast_text[i].first < contrast_text[i].count)
            return (int)i;
    return -1;
}

uint32_t catalog_contrast_art(unsigned entry)
{
    return contrast_text[entry < CATALOG_CONTRAST_TEXTS ? entry : 0].index;
}
