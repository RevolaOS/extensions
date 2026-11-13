/* SPDX-License-Identifier: GPL-3.0-only */

/* The theme file, /Revola/Themes/<id>.rvt: a header, then one appearance pack
 * for light and one for dark, either optional. The layout is documented in the
 * themes repository, docs/format.md; this header is its one source of numbers,
 * vendored by the appearance extension byte for byte. All integers are
 * little-endian. A reader accepts exactly RVT_VERSION and places the packs by
 * the header's own header-bytes field, never by RVT_HEADER_BYTES.
 *
 * Invariants the defines cannot say: records are in ascending catalog index
 * and padded to 4 bytes; a pixels record holds the bitmap from its data offset
 * to its end (palette, then rows); an lz record decodes to exactly that length
 * and the stream must end at the record's end; a recolour table is strictly
 * ascending by `from` read as a little-endian u32; an alias names a catalog
 * bitmap of the same size that is neither a mask nor tinted, and aliases are
 * applied after the other records of the pack; every alpha byte in the style
 * prefix is 255. The pack's extension bitmaps follow its last record,
 * ascending by owner then asset (both FNV-1a hashes, as the extension package
 * catalog keys them; the core's own assets are owned by the hash of "revola");
 * a bitmap payload is the whole BMap as the iPod decodes it, its 28-byte header
 * raw and its rows raw or as one lz stream decoding to the header's payload
 * length, the stream at most the payload plus one flag byte per 8. The relight
 * arithmetic and the bounds of its words are in docs/format.md under Relight. */

#ifndef RVT_H
#define RVT_H

#define RVT_MAGIC 0x30545652u /* "RVT0" */
#define RVT_VERSION 2
#define RVT_HEADER_BYTES 128
#define RVT_FINGERPRINT_BYTES 8
#define RVT_ID_BYTES 32
#define RVT_NAME_BYTES 48

/* Header field offsets. The CRC covers the first RVT_HEADER_BYTES with its own
 * word as zero; the bytes after the name up to RVT_HEADER_BYTES are zero. */
#define RVT_HEADER_MAGIC 0         /* u32 */
#define RVT_HEADER_VERSION 4       /* u16 */
#define RVT_HEADER_HEADER_BYTES 6  /* u16, where the packs begin */
#define RVT_HEADER_FINGERPRINT 8   /* RVT_FINGERPRINT_BYTES */
#define RVT_HEADER_APPEARANCES 16  /* u32, RVT_APPEARANCE_* bits */
#define RVT_HEADER_LIGHT_OFFSET 20 /* u32, 0 when absent */
#define RVT_HEADER_LIGHT_BYTES 24  /* u32 */
#define RVT_HEADER_DARK_OFFSET 28  /* u32, 0 when absent */
#define RVT_HEADER_DARK_BYTES 32   /* u32 */
#define RVT_HEADER_CRC 36          /* u32 */
#define RVT_HEADER_ID 40           /* RVT_ID_BYTES, NUL-padded ASCII */
#define RVT_HEADER_NAME 72         /* RVT_NAME_BYTES, NUL-padded UTF-8 */

#define RVT_APPEARANCE_LIGHT 1
#define RVT_APPEARANCE_DARK 2

/* Pack header field offsets; the body follows. */
#define RVT_PACK_BODY_BYTES 0         /* u32, at least RVT_STYLE_BYTES */
#define RVT_PACK_RECORDS 4            /* u32, record count */
#define RVT_PACK_CRC 8                /* u32, CRC-32 of the body */
#define RVT_PACK_EXTENSION_BITMAPS 12 /* u32, at most RVT_EXTENSION_BITMAPS_MAX */
#define RVT_PACK_HEADER_BYTES 16

/* Colour roles, in prefix order. */
enum rvt_role {
    RVT_ROLE_BACKGROUND,
    RVT_ROLE_TEXT,
    RVT_ROLE_ACCENT_FILL,
    RVT_ROLE_ACCENT_TEXT,
    RVT_ROLE_ON_ACCENT,
    RVT_ROLE_SELECTION,
    RVT_ROLE_COVER_BORDER,
    RVT_ROLE_ICON_TINT,
    RVT_ROLES
};

/* Named accents a listener picks, in palette order. */
enum rvt_accent {
    RVT_ACCENT_BLUE,
    RVT_ACCENT_PURPLE,
    RVT_ACCENT_PINK,
    RVT_ACCENT_RED,
    RVT_ACCENT_ORANGE,
    RVT_ACCENT_GREEN,
    RVT_ACCENT_TEAL,
    RVT_ACCENT_YELLOW,
    RVT_ACCENT_GRAPHITE,
    RVT_ACCENTS
};

/* The slots of an accent palette, each replacing the role of its name. */
enum rvt_accent_slot {
    RVT_SLOT_ACCENT_FILL,
    RVT_SLOT_ACCENT_TEXT,
    RVT_SLOT_SELECTION,
    RVT_SLOT_ON_ACCENT,
    RVT_SLOT_ICON_TINT,
    RVT_ACCENT_SLOTS
};

#define RVT_FLAG_FLAT_SELECTION 1
#define RVT_FLAG_MENU_COVERS_OFF 2
#define RVT_FLAG_FLAT_NOW_PLAYING 4
#define RVT_FLAG_FLAT_ICONS 8 /* extension menu icons without their reflection */

/* Tint bytes: RVT_TINT_ROLE plus an rvt_role. A reader packs them two per
 * byte, so they stay below 16. */
#define RVT_TINT_NONE 0
#define RVT_TINT_ROLE 1
#define RVT_TINT_DARKNESS 0x80 /* coverage from darkness, or-ed into the byte */

/* Catalog bitmaps a pack can address: records and tint bytes index them. */
#define RVT_CATALOG_MAX 640

/* Style prefix offsets. */
#define RVT_STYLE_COLORS 0 /* RVT_ROLES x RGBA */
#define RVT_STYLE_FLAGS (RVT_ROLES * 4)
#define RVT_STYLE_PALETTES (RVT_STYLE_FLAGS + 4) /* RVT_ACCENTS x RVT_ACCENT_SLOTS x RGBA */
#define RVT_STYLE_TINTS (RVT_STYLE_PALETTES + RVT_ACCENTS * RVT_ACCENT_SLOTS * 4)
#define RVT_STYLE_BYTES (RVT_STYLE_TINTS + RVT_CATALOG_MAX)
#define RVT_RECORD_BYTES 12

/* Record kinds, the second record word. Every kind but the two pixel kinds is
 * computed on the iPod from the bitmap's stock bytes. */
enum rvt_kind {
    RVT_KIND_PIXELS,    /* the bitmap's bytes from its data offset to its end */
    RVT_KIND_RECOLOR,   /* u32 count, count x {u8 from[4], u8 to[4]} RGBA */
    RVT_KIND_ALIAS,     /* u32 source catalog index: its pixels as the pack leaves them */
    RVT_KIND_INVERT,    /* no payload: RGB inverted, alpha kept */
    RVT_KIND_RELIGHT,   /* RVT_RELIGHT_WORDS x i32 8.8, enum rvt_relight order */
    RVT_KIND_PIXELS_LZ, /* RVT_KIND_PIXELS compressed as RVT_LZ describes */
    RVT_KINDS
};

/* RVT_LZ: groups of a flag byte and up to 8 items, bit 0 first. A clear bit is
 * a literal byte. A set bit is a match of two bytes, LLLLDDDD DDDDDDDD: it
 * copies L + 3 bytes from D + 1 bytes back in the output; L = 15 adds the
 * following bytes to the length while they are 255, and the first that is not.
 * Matches copy from the output itself, so a decoder needs no window. */
#define RVT_LZ_WINDOW 4096
#define RVT_LZ_MIN_MATCH 3

/* Relight words, in payload order. */
enum rvt_relight {
    RVT_RELIGHT_T_ZERO,
    RVT_RELIGHT_T_FULL,
    RVT_RELIGHT_BG_BASE,
    RVT_RELIGHT_BG_REF,
    RVT_RELIGHT_BG_SLOPE,
    RVT_RELIGHT_BG_LO,
    RVT_RELIGHT_BG_HI,
    RVT_RELIGHT_ICON_BASE,
    RVT_RELIGHT_ICON_LUMA,
    RVT_RELIGHT_ICON_ROW,
    RVT_RELIGHT_ICON_LO,
    RVT_RELIGHT_ICON_HI,
    RVT_RELIGHT_WORDS
};
#define RVT_RELIGHT_BYTES (RVT_RELIGHT_WORDS * 4)

/* Limits the reader's one scratch block per switch imposes: a recolour table
 * of RVT_RECOLOR_MAX pairs, or the per-pixel grays of a relight or a
 * re-encoded alias plus a 256-byte gray map. */
#define RVT_SCRATCH_BYTES 4096
#define RVT_RECOLOR_MAX 512
#define RVT_SCRATCH_PIXELS 3840
/* Alias records are applied after the other records of a pack. */
#define RVT_ALIASES_MAX 16

/* Extension bitmaps: {u32 owner, u32 asset, u32 kind, u32 stored, u32 decoded}
 * then the payload, 4-byte padded: the whole BMap, header and rows, raw or as
 * one lz stream of `stored` bytes that decodes to `decoded`. */
#define RVT_EXTENSION_BITMAP_BYTES 20
#define RVT_BMAP_HEADER_BYTES 28
enum rvt_extension_kind {
    RVT_EXTENSION_BITMAP = 1,
    RVT_EXTENSION_BITMAP_LZ,
    RVT_EXTENSION_KINDS
};
#define RVT_EXTENSION_BITMAPS_MAX 64
#define RVT_EXTENSION_BITMAP_MAX_BYTES 65536 /* header and rows, decoded */

_Static_assert(RVT_TINT_ROLE + RVT_ROLES <= 16, "tint bytes are packed in nibbles");

#endif
