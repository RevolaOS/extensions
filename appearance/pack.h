/* SPDX-License-Identifier: GPL-3.0-only */

/* Reading one appearance pack of a theme file (rvt.h), streamed straight into
 * the resident bitmaps. The file is an rv_file from content_open or
 * asset_open whose header main has checked and placed the pack by. */

#ifndef PACK_H
#define PACK_H
#include "catalog.h"
#include "look.h"
#include "rv_api.h"
#include <stdint.h>

/* An open pack positioned at its first record, with its style prefix read;
 * `body` is the file offset of its body, `extension_bitmaps` the count of
 * the bitmaps for extensions after its records. */
struct pack {
    struct rv_file *file;
    uint32_t body, body_bytes, count, crc, extension_bitmaps;
    uint32_t style[RVT_STYLE_BYTES / 4];
};

/* Opens the pack at `place`: the header's checksum word must still be
 * `header_crc` (the file may have changed since main read it), then the pack
 * header and the style prefix, which must be opaque and tint catalog bitmaps
 * only. Nothing is written yet, so a failure leaves the look unchanged. */
int pack_open(struct rv_file *file, uint32_t header_crc, const struct pack_place *place,
              struct pack *pack);
/* Whether the pack's style asks for nothing but colours: no tints, no flags. */
int pack_plain(const struct pack *pack);
/* Streams the records into the resident bitmaps: pixel records are read or
 * decoded in place, derived records are computed from the stock bytes
 * (fetched from the firmware file first when `current` says the active look
 * changed the bitmap), aliases after every other record, and a tinted
 * paletted or ARGB bitmap without a record gets its coverage prepared for
 * tint_refresh. Each record's layout, payload and padding are checked, then
 * the extension bitmaps go into the index main reserves for them (committed
 * or dropped by the caller), then the body checksum. Every catalog bitmap it
 * wrote is set in `written`, also on failure, when a half-written bitmap
 * holds zeros or stock bytes. The caller has stock_open and the bitmaps are
 * live, so this runs on the UI task and the caller redraws. */
int pack_apply(struct pack *pack, uint32_t *written, const uint32_t *current);

#endif
