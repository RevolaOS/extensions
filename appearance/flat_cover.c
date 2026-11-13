/* SPDX-License-Identifier: GPL-3.0-only */

/* Now Playing draws its cover with GLES into a 320 by 240 RGB565 buffer: the
 * tilted, reflected artwork, then smoothed lines along its edges. A flat look
 * copies the artwork's pixels into the buffer instead, face on and unscaled
 * where the stock cover starts, and skips the edge lines. */

#include "flat_cover.h"
#include "ext.h"
#include "osos-2.0.5.h"
#include "rvt.h"
#include "style.h"
#include <stddef.h>

enum { TARGET_WIDTH = 320, ART_SIZE = 128, ART_STRIDE = ART_SIZE * 2, FLAT_X = 18, FLAT_Y = 38 };

static void *drawing_view; /* the album art view inside its draw, where the renderer is called */

static int flat(void)
{
    return style_flags() & RVT_FLAG_FLAT_NOW_PLAYING;
}

uint32_t flat_cover_view_draw(uint32_t view, uint32_t argument, uint32_t clip, const void *next)
{
    drawing_view = (void *)view;
    uint32_t result = api->hook_next(next, view, argument, clip, 0, 0, 0);
    drawing_view = NULL;
    return result;
}

/* The stock cover is a 128 by 128 RGB565 bitmap with packed rows; any other
 * image keeps the stock drawing. */
static int copyable(const uint8_t *image)
{
    const int32_t *bounds = (const int32_t *)(image + Bitmap_bounds);
    return RV_FIELD(image, Bitmap_format, uint16_t) == BMapFormat_RGB565 &&
           RV_FIELD(image, Bitmap_stride, uint32_t) == ART_STRIDE &&
           bounds[2] - bounds[0] == ART_SIZE && bounds[3] - bounds[1] == ART_SIZE;
}

uint32_t flat_cover_renderer_draw(uint32_t renderer, const void *next)
{
    if (!flat() || !drawing_view)
        return api->hook_next(next, renderer, 0, 0, 0, 0, 0);
    const uint8_t *image = GLAlbumArtView_GetImage(drawing_view);
    void *buffer = NULL;
    if (image)
        GameGraphics_GetCurrentPixelBuffer(&buffer);
    if (!image || !buffer || !copyable(image))
        return api->hook_next(next, renderer, 0, 0, 0, 0, 0);
    const uint16_t *pixels = RV_FIELD(image, Bitmap_pixels, const uint16_t *);
    uint16_t *target = (uint16_t *)buffer + FLAT_Y * TARGET_WIDTH + FLAT_X;
    for (int row = 0; row < ART_SIZE; row++, target += TARGET_WIDTH)
        for (int column = 0; column < ART_SIZE; column++)
            target[column] = *pixels++;
    return 0;
}

uint32_t flat_cover_edge_lines(uint32_t buffer, const void *next)
{
    return flat() ? 0 : api->hook_next(next, buffer, 0, 0, 0, 0, 0);
}
