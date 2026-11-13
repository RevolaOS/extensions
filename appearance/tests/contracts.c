/* SPDX-License-Identifier: GPL-3.0-only */

/* The extension's logic against a bitmap bank and a firmware file in host
 * memory: what a theme file must look like to be accepted, that no record
 * writes outside the bitmap it names, what each derived record kind computes,
 * how a switch recovers, how the folder is listed, and which colour each
 * stock draw is mapped to. */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A BMap: format, stride, depth half-words, zeros, height and width words, the
 * data length, then the bytes. */
#define HEADER 28
#define MASK_BYTES (HEADER + 4 * 2)                 /* 4 wide, 2 high, 8-bit mask, stride 4 */
#define PALETTED_BYTES (HEADER + 4 + 2 * 4 + 4 * 2) /* 2 colours, 4 by 2 at 8 bits */
#define RGB_BYTES (HEADER + 2 * 3)                  /* 1 by 3 RGB565, stride 2: padded in the bank */
#define ARGB_BYTES (HEADER + 16 * 2)                /* 4 by 2 BGRA, stride 16 */
#define WIDE_BYTES (HEADER + 4 + 4 * 4 + 4 * 2)     /* 4 colours, 4 by 2 at 8 bits */
#define ALIGNED(n) (((n) + 3) & ~3u)
#define PAD(n) (ALIGNED(n) - (n))
enum {
    AT_MASK = 0,
    AT_PALETTED = AT_MASK + ALIGNED(MASK_BYTES),
    AT_RGB = AT_PALETTED + ALIGNED(PALETTED_BYTES),
    AT_SECOND = AT_RGB + ALIGNED(RGB_BYTES),
    AT_ARGB = AT_SECOND + ALIGNED(PALETTED_BYTES),
    AT_WIDE = AT_ARGB + ALIGNED(ARGB_BYTES),
    BANK_END = AT_WIDE + WIDE_BYTES,
};
enum { MASK = 0, PALETTED = 1, RGB = 2, SECOND = 3, ARGB = 4, WIDE = 5 };
static uint8_t host_bank[BANK_END];
static uint8_t stock_bank[sizeof host_bank];

uint8_t Palette_White[4] = {255, 255, 255, 255};

/* The main module's sources, then the switch module's; switch.c defines
 * `api`, `self` and `main_module`. */
#include "../catalog.c"
#include "../crc.c"
#include "../flat_cover.c"
#include "../assets.c"
#include "../latch.c"
#include "../look.c"
#include "../lz.c"
#include "../render.c"
#include "../selection_bar.c"
#include "../style.c"
#include "../theme.c"
#include "../theme_picker.c"
#include "../tint.c"
#include "../derive.c"
#include "../pack.c"
#include "../stock.c"
#include "../switch.c"

/* The file under test: an rv_file is a cursor into a byte buffer. */
struct rv_file {
    const uint8_t *data;
    uint32_t size, at;
};

/* The firmware file: the bank as built, at CATALOG_FILE_OFFSET, copied when
 * it is opened. */
static uint8_t firmware_image[CATALOG_FILE_OFFSET + BANK_END];
static struct rv_file firmware_file;
static int firmware_open_now, firmware_missing, opens, closes;
static int corrupt_asset = -1; /* this asset's last byte reads back wrong */

static int firmware_open(rv_extension_handle s, struct rv_file **file)
{
    (void)s;
    if (firmware_missing)
        return RV_NOT_FOUND;
    assert(!firmware_open_now);
    memcpy(firmware_image + CATALOG_FILE_OFFSET, stock_bank, sizeof stock_bank);
    if (corrupt_asset >= 0)
        firmware_image[CATALOG_FILE_OFFSET + offset_of((uint32_t)corrupt_asset) +
                       catalog_size((uint32_t)corrupt_asset) - 1] ^= 1;
    firmware_file = (struct rv_file){firmware_image, sizeof firmware_image, 0};
    firmware_open_now = 1;
    opens++;
    *file = &firmware_file;
    return RV_OK;
}

/* The selection bar's RAM words and cached bitmap, and the Now Playing
 * service lookup, which finds nothing here. */
uint8_t SelectionBar_RamGradient[2][4], SelectionBar_RamHighlight[4], SelectionGradient_Bitmap[0xa8];
void *UI_FindServiceById(uint32_t service_id)
{
    (void)service_id;
    return NULL;
}
void Model_Notify(void *model, uint32_t type, uint32_t property)
{
    (void)model;
    (void)type;
    (void)property;
    assert(0);
}

/* The album art view and the GLES buffer behind the flat cover. */
static uint8_t cover_image[0x100];
static uint16_t render_target[320 * 240];
static int cover_view;

void *GLAlbumArtView_GetImage(void *view)
{
    assert(view == &cover_view);
    return cover_image;
}

void GameGraphics_GetCurrentPixelBuffer(void **out_pixels)
{
    *out_pixels = render_target;
}

static int file_read(rv_extension_handle s, struct rv_file *f, void *out, uint32_t bytes, uint32_t *got)
{
    (void)s;
    uint32_t n = f->at >= f->size ? 0 : f->at + bytes > f->size ? f->size - f->at : bytes;
    memcpy(out, f->data + f->at, n);
    f->at += n;
    *got = n;
    return RV_OK;
}
static int file_seek(rv_extension_handle s, struct rv_file *f, uint32_t offset)
{
    (void)s;
    f->at = offset;
    return RV_OK;
}
static int allocations, alloc_fails; /* blocks out; the next alloc returns NULL */
static void *alloc(rv_extension_handle s, uint32_t bytes)
{
    (void)s;
    if (alloc_fails) {
        alloc_fails = 0;
        return NULL;
    }
    allocations++;
    return calloc(1, bytes);
}
static void release(rv_extension_handle s, void *block)
{
    (void)s;
    allocations--;
    free(block);
}

/* hook_next records what a handler passed on and returns `next_result`. */
static uint32_t passed[7], next_result = 0x600d;
static uint32_t hook_next(const void *next, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                          uint32_t e, uint32_t f)
{
    (void)next;
    passed[0] = a, passed[1] = b, passed[2] = c, passed[3] = d, passed[4] = e, passed[5] = f;
    return next_result;
}
static int binds, bind_failures_at; /* hooks bound now; the bind that fails (1-based), 0 for none */
static int hook_bind(rv_extension_handle s, const struct rv_hook *hook)
{
    (void)s;
    (void)hook;
    if (bind_failures_at && binds + 1 == bind_failures_at)
        return RV_NO_MEMORY;
    binds++;
    return RV_OK;
}
static int hook_unbind(rv_extension_handle s, const struct rv_hook *hook)
{
    (void)s;
    (void)hook;
    assert(binds > 0);
    binds--;
    return RV_OK;
}

/* A record to build into a pack: `payload` bytes of `kind`. */
struct rec {
    uint32_t asset, kind, size;
    const uint8_t *payload;
};

/* The Themes folder: names the listing returns, and files opened by id. */
static const char *const *folder;
static unsigned folder_count;
static const char *unreadable; /* an id whose file does not open */
struct built {
    uint8_t bytes[32768];
    uint32_t size;
};
static struct built opened_theme, classic_asset;
static struct rv_file opened_file, classic_file;
static int open_files;
static const struct built *provided; /* the file content_open returns for PROVIDED_ID */
#define PROVIDED_ID "test"
static void build_theme(struct built *b, const char *id, const char *name);
static void build_file_at(struct built *b, const char *id, const char *name, uint32_t header_bytes,
                          const struct rec *light, uint32_t nlight, const struct rec *dark,
                          uint32_t ndark, const uint8_t *tint, const uint8_t *prefix);
static int content_open(rv_extension_handle s, const char *name, struct rv_file **file)
{
    (void)s;
    char id[RVT_ID_BYTES] = {0};
    assert(!strncmp(name, "Themes/", 7));
    size_t n = strlen(name + 7);
    assert(n > 4 && n - 4 < sizeof id && !strcmp(name + 7 + n - 4, ".rvt"));
    memcpy(id, name + 7, n - 4);
    if (unreadable && !strcmp(id, unreadable))
        return RV_NOT_FOUND;
    assert(!open_files);
    if (provided && !strcmp(id, PROVIDED_ID)) {
        opened_file = (struct rv_file){provided->bytes, provided->size, 0};
    } else {
        build_theme(&opened_theme, id, "Named");
        opened_file = (struct rv_file){opened_theme.bytes, opened_theme.size, 0};
    }
    *file = &opened_file;
    open_files++;
    return RV_OK;
}
static void file_close(rv_extension_handle s, struct rv_file *file)
{
    (void)s;
    if (file == &firmware_file) {
        assert(firmware_open_now);
        firmware_open_now = 0;
        closes++;
        return;
    }
    assert((file == &opened_file || file == &classic_file) && open_files == 1);
    open_files--;
}
static int content_list(rv_extension_handle s, const char *name,
                        void (*each)(const char *entry, int is_folder, void *arg), void *arg)
{
    (void)s;
    assert(!strcmp(name, "Themes"));
    each("Sub", 1, arg);
    for (unsigned i = 0; i < folder_count; i++)
        each(folder[i], 0, arg);
    return RV_OK;
}

/* The settings store: ints by key, and the theme id string. */
static int32_t setting_ints[8];
static char theme_id_setting[RVT_ID_BYTES];
static int32_t setting_int(rv_extension_handle s, uint16_t key)
{
    (void)s;
    return setting_ints[key];
}
static int setting_set_int(rv_extension_handle s, uint16_t key, int32_t value)
{
    (void)s;
    setting_ints[key] = value;
    return RV_OK;
}
static uint32_t setting_read(rv_extension_handle s, uint16_t key, void *out, uint32_t capacity)
{
    (void)s;
    assert(key == KEY_THEME);
    uint32_t n = (uint32_t)strlen(theme_id_setting);
    memcpy(out, theme_id_setting, n < capacity ? n : capacity);
    return n;
}
static int setting_write(rv_extension_handle s, uint16_t key, uint8_t type, const void *data,
                         uint32_t bytes)
{
    (void)s;
    assert(key == KEY_THEME && type == RV_SETTING_TYPE_STRING && bytes < sizeof theme_id_setting);
    memset(theme_id_setting, 0, sizeof theme_id_setting);
    memcpy(theme_id_setting, data, bytes);
    return RV_OK;
}
/* The switch module loads on request; the classic asset is a plain dark pack. */
static int module_loaded, module_load_fails;
static int module_load(rv_extension_handle s, uint32_t module)
{
    (void)s;
    assert(module == SWITCH_MODULE);
    if (module_load_fails)
        return RV_NO_MEMORY;
    module_loaded = 1;
    return RV_OK;
}
static void module_unload(rv_extension_handle s, uint32_t module)
{
    (void)s;
    assert(module == SWITCH_MODULE);
    module_loaded = 0;
}
static int asset_open(rv_extension_handle s, const char *name, struct rv_file **file)
{
    (void)s;
    assert(!strcmp(name, "classic"));
    build_file_at(&classic_asset, "classic", "Classic", RVT_HEADER_BYTES, NULL, 0, NULL, 0, NULL, NULL);
    classic_file = (struct rv_file){classic_asset.bytes, classic_asset.size, 0};
    *file = &classic_file;
    open_files++;
    return RV_OK;
}
static const rv_color *canvas_colors;
static int redraws;
static int ui_set_colors(rv_extension_handle s, const rv_color *colors)
{
    (void)s;
    canvas_colors = colors;
    return RV_OK;
}
static void ui_refresh(rv_extension_handle s, const char *screen)
{
    (void)s;
    assert(!strcmp(screen, "theme"));
}
static void ui_redraw(rv_extension_handle s)
{
    (void)s;
    redraws++;
}
static const struct rv_asset_provider *core_provider;
static int provider_held; /* another extension has it */
static int asset_set_provider(rv_extension_handle s, const struct rv_asset_provider *p)
{
    (void)s;
    if (provider_held)
        return RV_ALREADY_BOUND;
    core_provider = p;
    return RV_OK;
}

static const struct main_service host_service = {
    .state = &look_state,
    .crc32_update = crc32_update,
    .catalog_count = catalog_count,
    .catalog_fingerprint = catalog_fingerprint,
    .catalog_data = catalog_data,
    .catalog_size = catalog_size,
    .catalog_crc = catalog_crc,
    .catalog_file_offset = catalog_file_offset,
    .catalog_layout = catalog_layout,
    .style_set = style_set,
    .style_reset = style_reset,
    .style_set_dark = style_set_dark,
    .style_set_accent = style_set_accent,
    .tint_refresh = tint_refresh,
    .tint_accent_art = tint_accent_art,
    .lz_decode = lz_decode,
    .assets_reserve = assets_reserve,
    .assets_commit = assets_commit,
    .assets_drop = assets_drop,
};
static const void *module_service(rv_extension_handle s, uint32_t module)
{
    (void)s;
    if (module == SWITCH_MODULE)
        return module_loaded ? (const void *)&service : NULL;
    assert(module == 0);
    return &host_service;
}
static struct rv_api fake = {
    .hook_bind = hook_bind,
    .hook_unbind = hook_unbind,
    .hook_next = hook_next,
    .file_read = file_read,
    .file_seek = file_seek,
    .file_close = file_close,
    .alloc = alloc,
    .free = release,
    .content_open = content_open,
    .content_list = content_list,
    .module_service = module_service,
    .setting_int = setting_int,
    .setting_set_int = setting_set_int,
    .setting_read = setting_read,
    .setting_write = setting_write,
    .module_load = module_load,
    .module_unload = module_unload,
    .asset_open = asset_open,
    .ui_set_colors = ui_set_colors,
    .ui_refresh = ui_refresh,
    .ui_redraw = ui_redraw,
    .asset_set_provider = asset_set_provider,
    .firmware_open = firmware_open,
};

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

static void bmap(uint8_t *p, uint32_t format, uint32_t stride, uint32_t depth, uint32_t width,
                 uint32_t height, uint32_t total)
{
    memset(p, 0, total);
    put16(p, format);
    put16(p + 4, stride);
    put16(p + 6, depth);
    put32(p + 16, height);
    put32(p + 20, width);
    put32(p + 24, total - HEADER);
}

/* Palette entries are BGRA. Bitmap 1 is a light square with a darker icon
 * pixel, as a relight expects; bitmap 3 is black and white; bitmap 5 holds
 * the stock accent blue. */
static const uint8_t LIGHT[4] = {247, 244, 240, 255}, ICON[4] = {216, 208, 200, 255};
static const uint8_t BLACK[4] = {0, 0, 0, 255}, WHITE[4] = {255, 255, 255, 255};
static const uint8_t BLUE[4] = {197, 108, 35, 255}, GREY[4] = {128, 128, 128, 255};

static void build_bank(void)
{
    uint8_t *p = host_bank;
    bmap(p, 8, 4, 8, 4, 2, MASK_BYTES);
    p = host_bank + AT_PALETTED;
    bmap(p, 0x64, 4, 8, 4, 2, PALETTED_BYTES);
    put32(p + HEADER, 2);
    memcpy(p + HEADER + 4, LIGHT, 4);
    memcpy(p + HEADER + 8, ICON, 4);
    memcpy(p + HEADER + 12, (uint8_t[]){0, 0, 1, 0, 0, 1, 1, 0}, 8);
    p = host_bank + AT_RGB;
    bmap(p, 0x565, 2, 16, 1, 3, RGB_BYTES);
    p = host_bank + AT_SECOND;
    bmap(p, 0x64, 4, 8, 4, 2, PALETTED_BYTES);
    put32(p + HEADER, 2);
    memcpy(p + HEADER + 4, BLACK, 4);
    memcpy(p + HEADER + 8, WHITE, 4);
    memcpy(p + HEADER + 12, (uint8_t[]){0, 1, 0, 1, 1, 0, 1, 0}, 8);
    p = host_bank + AT_ARGB;
    bmap(p, 0x1888, 16, 32, 4, 2, ARGB_BYTES);
    for (unsigned i = 0; i < 8; i++)
        memcpy(p + HEADER + i * 4, i % 3 ? WHITE : BLUE, 4);
    p[HEADER + 7 * 4 + 3] = 0; /* one clear pixel */
    p = host_bank + AT_WIDE;
    bmap(p, 0x64, 4, 8, 4, 2, WIDE_BYTES);
    put32(p + HEADER, 4);
    memcpy(p + HEADER + 4, BLUE, 4);
    memcpy(p + HEADER + 8, GREY, 4);
    memcpy(p + HEADER + 12, BLACK, 4);
    memcpy(p + HEADER + 16, WHITE, 4);
    memcpy(p + HEADER + 20, (uint8_t[]){0, 1, 2, 3, 3, 2, 1, 0}, 8);
    memcpy(stock_bank, host_bank, sizeof host_bank);
    for (uint32_t i = 0; i < CATALOG_COUNT; i++)
        catalog_crcs[i] = (uint16_t)~crc32_update(0xffffffffu, catalog_data(i), catalog_size(i));
}

static void reset_bank(void)
{
    memcpy(host_bank, stock_bank, sizeof host_bank);
}

static int bank_is_stock(void)
{
    return !memcmp(host_bank, stock_bank, sizeof host_bank);
}

static uint32_t crc(const void *data, uint32_t size)
{
    return ~crc32_update(0xffffffffu, data, size);
}

/* An opaque style prefix with the stock light roles and no tints. */
static void prefix_plain(uint8_t *p)
{
    memset(p, 0, RVT_STYLE_BYTES);
    for (unsigned i = 0; i < RVT_ROLES; i++)
        memcpy(p + RVT_STYLE_COLORS + i * 4, style_stock_color((enum rvt_role)i), 4);
    for (unsigned i = 0; i < RVT_ACCENTS * RVT_ACCENT_SLOTS; i++)
        p[RVT_STYLE_PALETTES + i * 4 + 3] = 255;
}

/* Extension bitmaps appended to every pack written while `asset_table` is set. */
struct asset_payload {
    uint32_t owner, asset, kind, size;
    const uint8_t *payload;
};
static const struct asset_payload *asset_table;
static uint32_t asset_table_count;

/* Writes one pack at `pack`; returns the bytes written (header and body). */
static uint32_t write_pack(uint8_t *pack, const struct rec *recs, uint32_t count, const uint8_t *tint,
                           const uint8_t *prefix)
{
    uint8_t *body = pack + RVT_PACK_HEADER_BYTES, *p = body;
    if (prefix)
        memcpy(p, prefix, RVT_STYLE_BYTES);
    else
        prefix_plain(p);
    if (tint)
        memcpy(p + RVT_STYLE_TINTS, tint, RVT_CATALOG_MAX);
    p += RVT_STYLE_BYTES;
    for (uint32_t i = 0; i < count; i++) {
        put32(p, recs[i].asset);
        put32(p + 4, recs[i].kind);
        put32(p + 8, recs[i].size);
        p += 12;
        memcpy(p, recs[i].payload, recs[i].size);
        memset(p + recs[i].size, 0, ALIGNED(recs[i].size) - recs[i].size);
        p += ALIGNED(recs[i].size);
    }
    for (uint32_t i = 0; i < asset_table_count; i++) {
        const struct asset_payload *a = asset_table + i;
        put32(p, a->owner);
        put32(p + 4, a->asset);
        put32(p + 8, a->kind);
        put32(p + 12, a->size);
        p += RVT_EXTENSION_BITMAP_BYTES;
        memcpy(p, a->payload, a->size);
        memset(p + a->size, 0, PAD(a->size));
        p += ALIGNED(a->size);
    }
    uint32_t body_bytes = (uint32_t)(p - body);
    put32(pack + RVT_PACK_BODY_BYTES, body_bytes);
    put32(pack + RVT_PACK_RECORDS, count);
    put32(pack + RVT_PACK_CRC, crc(body, body_bytes));
    put32(pack + RVT_PACK_EXTENSION_BITMAPS, asset_table_count);
    return RVT_PACK_HEADER_BYTES + body_bytes;
}

/* Recomputes the dark pack's body checksum of a file built without a light
 * pack, after a test edited the body. */
static void resign_dark_pack(struct built *b)
{
    uint8_t *pack = b->bytes + RVT_HEADER_BYTES;
    put32(pack + RVT_PACK_CRC, crc(pack + RVT_PACK_HEADER_BYTES, le32(pack + RVT_PACK_BODY_BYTES)));
}

static void sign_header(struct built *b)
{
    put32(b->bytes + RVT_HEADER_CRC, 0);
    put32(b->bytes + RVT_HEADER_CRC, crc(b->bytes, RVT_HEADER_BYTES));
}

/* A theme file: a light pack when `light` is given, then the dark pack, with
 * the packs starting at `header_bytes`. */
static void build_file_at(struct built *b, const char *id, const char *name, uint32_t header_bytes,
                          const struct rec *light, uint32_t nlight, const struct rec *dark,
                          uint32_t ndark, const uint8_t *tint, const uint8_t *prefix)
{
    uint8_t *h = b->bytes;
    memset(b, 0, sizeof *b);
    memcpy(h + RVT_HEADER_MAGIC, "RVT0", 4);
    put16(h + RVT_HEADER_VERSION, RVT_VERSION);
    put16(h + RVT_HEADER_HEADER_BYTES, header_bytes);
    memcpy(h + RVT_HEADER_FINGERPRINT, fingerprint, RVT_FINGERPRINT_BYTES);
    put32(h + RVT_HEADER_APPEARANCES, RVT_APPEARANCE_DARK | (light ? RVT_APPEARANCE_LIGHT : 0));
    strcpy((char *)h + RVT_HEADER_ID, id);
    strcpy((char *)h + RVT_HEADER_NAME, name);
    uint32_t at = header_bytes;
    if (light) {
        uint32_t size = write_pack(h + at, light, nlight, NULL, prefix);
        put32(h + RVT_HEADER_LIGHT_OFFSET, at);
        put32(h + RVT_HEADER_LIGHT_BYTES, size);
        at += size;
    }
    uint32_t size = write_pack(h + at, dark, ndark, tint, prefix);
    put32(h + RVT_HEADER_DARK_OFFSET, at);
    put32(h + RVT_HEADER_DARK_BYTES, size);
    sign_header(b);
    b->size = at + size;
}

/* A theme file with the light pack only. */
static void build_light_only(struct built *b, const char *id, const char *name)
{
    uint8_t *h = b->bytes;
    memset(b, 0, sizeof *b);
    memcpy(h + RVT_HEADER_MAGIC, "RVT0", 4);
    put16(h + RVT_HEADER_VERSION, RVT_VERSION);
    put16(h + RVT_HEADER_HEADER_BYTES, RVT_HEADER_BYTES);
    memcpy(h + RVT_HEADER_FINGERPRINT, fingerprint, RVT_FINGERPRINT_BYTES);
    put32(h + RVT_HEADER_APPEARANCES, RVT_APPEARANCE_LIGHT);
    strcpy((char *)h + RVT_HEADER_ID, id);
    strcpy((char *)h + RVT_HEADER_NAME, name);
    uint32_t size = write_pack(h + RVT_HEADER_BYTES, NULL, 0, NULL, NULL);
    put32(h + RVT_HEADER_LIGHT_OFFSET, RVT_HEADER_BYTES);
    put32(h + RVT_HEADER_LIGHT_BYTES, size);
    sign_header(b);
    b->size = RVT_HEADER_BYTES + size;
}

static void build_file(struct built *b, const struct rec *light, uint32_t nlight,
                       const struct rec *dark, uint32_t ndark, const uint8_t *tint)
{
    build_file_at(b, "test", "Test", RVT_HEADER_BYTES, light, nlight, dark, ndark, tint, NULL);
}

static void build(struct built *b, const struct rec *dark, uint32_t ndark, const uint8_t *tint)
{
    build_file(b, NULL, 0, dark, ndark, tint);
}

static void build_theme(struct built *b, const char *id, const char *name)
{
    build_file_at(b, id, name, RVT_HEADER_BYTES, NULL, 0, NULL, 0, NULL, NULL);
}

static uint32_t record_at(uint32_t i)
{
    return RVT_HEADER_BYTES + RVT_PACK_HEADER_BYTES + RVT_STYLE_BYTES + i;
}

static const uint32_t none[BITMAP_WORDS];
static const struct look TEST_LOOK = {.id = "test", .dark = 1};

/* As main and the switch read a file: the header checked and the dark pack
 * placed by main, opened and applied by the switch. */
static int apply_from(struct built *b, uint32_t *assets, const uint32_t *current)
{
    struct rv_file file = {b->bytes, b->size, 0};
    struct look look = TEST_LOOK;
    struct pack_place place;
    struct pack pack;
    memset(assets, 0, BITMAP_WORDS * 4);
    stock_open();
    int ok = look_pack_place(&file, &look, &place) &&
             pack_open(&file, look.header_crc, &place, &pack) &&
             pack_apply(&pack, assets, current ? current : none);
    stock_close();
    /* As the switch does with the index the pack filled. */
    if (ok)
        assets_commit();
    else
        assets_reserve(0);
    assert(allocations == (live_count ? 1 : 0));
    return ok;
}

/* A switch of `look` to the file `b`, placed as main places it. */
static enum switch_result switch_to(struct built *b, struct look *look)
{
    struct rv_file file = {b->bytes, b->size, 0};
    struct pack_place place;
    if (!look_pack_place(&file, look, &place))
        return SWITCH_LOAD_FAILED;
    return switch_apply(&file, look, &place);
}

static int apply(struct built *b, uint32_t *assets)
{
    return apply_from(b, assets, NULL);
}

static struct rec pixels(uint32_t asset, const uint8_t *payload)
{
    struct layout l;
    assert(catalog_layout(asset, &l));
    return (struct rec){asset, RVT_KIND_PIXELS, catalog_size(asset) - l.start, payload};
}

static uint32_t at_data(uint32_t asset)
{
    struct layout l;
    assert(catalog_layout(asset, &l));
    return l.start;
}
/* Relight vectors from the themes compiler's reference arithmetic
 * (rvt/derive.py), pinned: a bitmap's bytes after the header in the WIDE or
 * PALETTED layout, the rule, and what the iPod must produce. */
static const uint8_t wide_plain_in[] = {4, 0, 0, 0, 230, 236, 240, 255, 70, 80, 90, 255, 150, 160, 170, 255, 255, 255, 255, 255, 0, 1, 2, 1, 0, 2, 3, 0};
static const int32_t wide_plain_rule[RVT_RELIGHT_WORDS] = {768, 15360, 25600, 60416, 256, 25600, 25856, 26112, 0, 128, 26112, 26368};
static const uint8_t wide_plain_out[] = {4, 0, 0, 0, 100, 100, 100, 255, 101, 101, 101, 255, 102, 102, 102, 255, 103, 103, 103, 255, 0, 2, 2, 2, 0, 3, 1, 0};
/* A partly covered pixel without a fully covered neighbour is pruned. */
static const uint8_t wide_shadow_in[] = {4, 0, 0, 0, 250, 250, 250, 255, 0, 0, 0, 255, 242, 242, 242, 255, 200, 200, 200, 255, 0, 2, 0, 0, 0, 0, 0, 1};
static const int32_t wide_shadow_rule[RVT_RELIGHT_WORDS] = {512, 10240, 40960, 64000, 256, 38400, 38656, 35840, 40, -256, 38912, 39168};
static const uint8_t wide_shadow_out[] = {4, 0, 0, 0, 151, 151, 151, 255, 152, 152, 152, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
static const uint8_t paletted_two_in[] = {2, 0, 0, 0, 240, 244, 247, 255, 200, 208, 216, 255, 0, 0, 1, 0, 0, 1, 1, 0};
static const int32_t paletted_two_rule[RVT_RELIGHT_WORDS] = {768, 3584, 6400, 55296, 91, 6144, 6144, 101018, -300, -453, 6400, 6400};
static const uint8_t paletted_two_out[] = {2, 0, 0, 0, 24, 24, 24, 255, 25, 25, 25, 255, 0, 0, 1, 0, 0, 1, 1, 0};

static void expect_relight(uint32_t asset, const uint8_t *in, const int32_t *rule, const uint8_t *out)
{
    struct built b;
    uint32_t assets[BITMAP_WORDS], at = (uint32_t)(catalog_data(asset) - host_bank) + HEADER;
    uint32_t size = catalog_size(asset) - HEADER;
    uint8_t kept[WIDE_BYTES];
    memcpy(kept, stock_bank + at, size);
    memcpy(stock_bank + at, in, size);
    reset_bank();
    catalog_crcs[asset] = (uint16_t)~crc32_update(0xffffffffu, catalog_data(asset), catalog_size(asset));
    struct rec relight = {asset, RVT_KIND_RELIGHT, RVT_RELIGHT_BYTES, (const uint8_t *)rule};
    build(&b, &relight, 1, NULL);
    assert(apply(&b, assets) && !memcmp(catalog_data(asset) + HEADER, out, size));
    memcpy(stock_bank + at, kept, size);
    reset_bank();
    catalog_crcs[asset] = (uint16_t)~crc32_update(0xffffffffu, catalog_data(asset), catalog_size(asset));
}



static void reader_contracts(void)
{
    uint32_t assets[BITMAP_WORDS];
    struct built b;
    struct look look;
    struct pack_place place;
    struct rv_file file, *opened;
    char provided_id[RVT_ID_BYTES] = PROVIDED_ID;

    /* An empty pack is a valid theme, and the header reader sees its name. */
    build(&b, NULL, 0, NULL);
    provided = &b;
    assert(theme_open(provided_id, 1, &opened, &look, &place) == THEME_OK &&
           !strcmp(look.name, "Test") && look.appearances == RVT_APPEARANCE_DARK &&
           place.offset == RVT_HEADER_BYTES && place.bytes == b.size - RVT_HEADER_BYTES);
    file_close(NULL, opened);
    provided = NULL;
    assert(apply(&b, assets));

    /* A record replaces exactly the bytes after the header, and after the
     * colour count of a paletted bitmap; palette indices are checked. */
    uint8_t mask[4 * 2] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t paletted[2 * 4 + 4 * 2] = {9, 9, 9, 255, 8, 8, 8, 255, 0, 1, 0, 1, 1, 0, 1, 0};
    struct rec two[2] = {pixels(MASK, mask), pixels(PALETTED, paletted)};
    build(&b, two, 2, NULL);
    assert(apply(&b, assets) && bit_test(assets, 0) && bit_test(assets, 1) && !bit_test(assets, 2));
    assert(!memcmp(catalog_data(MASK) + HEADER, mask, sizeof mask));
    assert(!memcmp(catalog_data(PALETTED) + HEADER + 4, paletted, sizeof paletted));
    paletted[8] = 2; /* an index past the two colours */
    build(&b, two, 2, NULL);
    assert(!apply(&b, assets));
    assert(catalog_data(PALETTED)[HEADER + 4 + 8] == 0); /* zeroed, never left half written */
    paletted[8] = 0;
    reset_bank();

    /* Records out of order, repeated, naming a size that is not the bitmap's
     * or an unknown kind are refused. */
    struct rec reversed[2] = {pixels(PALETTED, paletted), pixels(MASK, mask)};
    build(&b, reversed, 2, NULL);
    assert(!apply(&b, assets));
    build(&b, two, 2, NULL);
    put32(b.bytes + record_at(8), sizeof mask + 4);
    assert(!apply(&b, assets));
    struct rec unknown = {MASK, RVT_KINDS, sizeof mask, mask};
    build(&b, &unknown, 1, NULL);
    assert(!apply(&b, assets));

    /* The body checksum, the header checksum, the fingerprint and the magic
     * each guard the file. */
    build(&b, two, 2, NULL);
    b.bytes[b.size - 1] ^= 1;
    assert(!apply(&b, assets));
    build(&b, two, 2, NULL);
    b.bytes[RVT_HEADER_NAME] = 'U';
    file = (struct rv_file){b.bytes, b.size, 0};
    look = TEST_LOOK;
    assert(!look_pack_place(&file, &look, &place));
    build(&b, two, 2, NULL);
    b.bytes[RVT_HEADER_FINGERPRINT] ^= 1;
    sign_header(&b);
    assert(!apply(&b, assets));
    build(&b, two, 2, NULL);
    b.bytes[RVT_HEADER_MAGIC] = 'X';
    sign_header(&b);
    assert(!apply(&b, assets));
    /* Exactly the format version; another id than the file name's. */
    build(&b, two, 2, NULL);
    put16(b.bytes + RVT_HEADER_VERSION, RVT_VERSION + 1);
    sign_header(&b);
    assert(!apply(&b, assets));
    put16(b.bytes + RVT_HEADER_VERSION, RVT_VERSION - 1);
    sign_header(&b);
    assert(!apply(&b, assets));
    build_file_at(&b, "other", "Test", RVT_HEADER_BYTES, NULL, 0, two, 2, NULL, NULL);
    assert(!apply(&b, assets));
    reset_bank();

    /* The packs start at the header's own size, a whole number of words: a
     * pack before it is refused, one after a larger header is read; the
     * switch sees the file main checked by its header checksum. */
    build(&b, two, 2, NULL);
    put16(b.bytes + RVT_HEADER_HEADER_BYTES, RVT_HEADER_BYTES + 4);
    sign_header(&b);
    assert(!apply(&b, assets));
    build_file_at(&b, "test", "Test", RVT_HEADER_BYTES + 8, NULL, 0, two, 2, NULL, NULL);
    assert(apply(&b, assets) && !memcmp(catalog_data(MASK) + HEADER, mask, sizeof mask));
    build_file_at(&b, "test", "Test", RVT_HEADER_BYTES - 4, NULL, 0, two, 2, NULL, NULL);
    assert(!apply(&b, assets));
    build_file_at(&b, "test", "Test", RVT_HEADER_BYTES + 2, NULL, 0, two, 2, NULL, NULL);
    assert(!apply(&b, assets));
    build(&b, two, 2, NULL);
    file = (struct rv_file){b.bytes, b.size, 0};
    look = TEST_LOOK;
    assert(look_pack_place(&file, &look, &place));
    {
        struct pack pack;
        assert(!pack_open(&file, look.header_crc ^ 1, &place, &pack));
        assert(pack_open(&file, look.header_crc, &place, &pack) && pack.count == 2);
    }
    reset_bank();

    /* Ids and names: lower-case ASCII ids that are not the built-in's,
     * printable UTF-8 names. */
    static const char *const good_ids[] = {"a", "flat", "a-b_c9", "0123456789012345678901234567890"};
    static const char *const bad_ids[] = {"", "Flat", "-a", "a b", "classic",
                                          "01234567890123456789012345678901"};
    char id[RVT_ID_BYTES];
    for (unsigned i = 0; i < sizeof good_ids / sizeof *good_ids; i++) {
        memset(id, 0, sizeof id);
        strcpy(id, good_ids[i]);
        assert(id_valid(id));
    }
    for (unsigned i = 0; i < sizeof bad_ids / sizeof *bad_ids; i++) {
        memset(id, 0, sizeof id);
        memcpy(id, bad_ids[i], strlen(bad_ids[i]) < sizeof id ? strlen(bad_ids[i]) : sizeof id);
        assert(!id_valid(id));
    }
    uint8_t name[RVT_NAME_BYTES] = "Caf\xc3\xa9";
    assert(name_valid(name, sizeof name));
    name[5] = 1; /* a control character after the NUL is not zero padding */
    assert(!name_valid(name, sizeof name));
    name[5] = 0;
    name[0] = 0x7f;
    assert(!name_valid(name, sizeof name));
    name[0] = 0;
    assert(!name_valid(name, sizeof name));
    memset(name, 'a', sizeof name); /* no NUL inside the field */
    assert(!name_valid(name, sizeof name));
    memcpy(name, "\xed\xa0\x80\0", 4); /* a surrogate */
    assert(!name_valid(name, sizeof name));

    /* A style with a tint byte outside the roles or on a bitmap past the
     * catalog, or a clear colour, is refused before anything is written. */
    uint8_t tint[RVT_CATALOG_MAX] = {0};
    tint[MASK] = RVT_TINT_ROLE + RVT_ROLES;
    build(&b, NULL, 0, tint);
    assert(!apply(&b, assets));
    tint[MASK] = 0;
    tint[CATALOG_COUNT] = RVT_TINT_ROLE + RVT_ROLE_TEXT;
    build(&b, NULL, 0, tint);
    assert(!apply(&b, assets));
    tint[CATALOG_COUNT] = 0;
    build(&b, NULL, 0, NULL);
    b.bytes[RVT_HEADER_BYTES + RVT_PACK_HEADER_BYTES + RVT_STYLE_COLORS + 3] = 0;
    assert(!apply(&b, assets));

    /* A tinted mask needs no record. A tinted paletted asset without one is
     * tinted stock art: with darkness coverage its alpha follows its luma. */
    tint[MASK] = RVT_TINT_ROLE + RVT_ROLE_TEXT;
    build(&b, NULL, 0, tint);
    assert(apply(&b, assets) && !bit_test(assets, MASK));
    tint[MASK] = 0;
    tint[SECOND] = (RVT_TINT_ROLE + RVT_ROLE_ICON_TINT) | RVT_TINT_DARKNESS;
    build(&b, NULL, 0, tint);
    assert(apply(&b, assets) && bit_test(assets, SECOND));
    assert(catalog_data(SECOND)[at_data(SECOND) + 3] == 255 && catalog_data(SECOND)[at_data(SECOND) + 7] == 0);
    reset_bank();
    tint[SECOND] = RVT_TINT_ROLE + RVT_ROLE_ICON_TINT;
    build(&b, NULL, 0, tint);
    assert(apply(&b, assets) && !memcmp(catalog_data(SECOND) + at_data(SECOND), BLACK, 4));
    tint[SECOND] = RVT_TINT_DARKNESS; /* darkness without a tint */
    build(&b, NULL, 0, tint);
    assert(!apply(&b, assets));
    tint[SECOND] = 0;

    /* Recolour: a swap is two lookups against the stock colours, not a chain;
     * RGB565 colours must be exact; ARGB pixels are keyed like palette
     * entries, clear ones included; the count must match the payload and stay
     * within RVT_RECOLOR_MAX. */
    uint8_t swap[4 + 16] = {2, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255};
    struct rec recolor = {SECOND, RVT_KIND_RECOLOR, sizeof swap, swap};
    build(&b, &recolor, 1, NULL);
    assert(apply(&b, assets) && bit_test(assets, SECOND));
    assert(!memcmp(catalog_data(SECOND) + at_data(SECOND), WHITE, 4) &&
           !memcmp(catalog_data(SECOND) + at_data(SECOND) + 4, BLACK, 4));
    reset_bank();
    uint8_t unsorted[4 + 16];
    memcpy(unsorted, swap, sizeof swap);
    memcpy(unsorted + 4, swap + 12, 8);
    memcpy(unsorted + 12, swap + 4, 8);
    recolor.payload = unsorted;
    build(&b, &recolor, 1, NULL);
    assert(!apply(&b, assets));
    uint8_t rgb_pair[4 + 8] = {1, 0, 0, 0, 0, 0, 0, 255, 0x18, 0x20, 0x29, 255};
    struct rec rgb = {RGB, RVT_KIND_RECOLOR, sizeof rgb_pair, rgb_pair};
    build(&b, &rgb, 1, NULL);
    assert(!apply(&b, assets));
    rgb_pair[8] = 0xf8, rgb_pair[9] = 0, rgb_pair[10] = 0;
    build(&b, &rgb, 1, NULL);
    assert(apply(&b, assets) && catalog_data(RGB)[HEADER] == 0 && catalog_data(RGB)[HEADER + 1] == 0xf8 &&
           catalog_data(RGB)[HEADER + 5] == 0xf8);
    reset_bank();
    struct rec recolor_mask = {MASK, RVT_KIND_RECOLOR, sizeof swap, swap};
    build(&b, &recolor_mask, 1, NULL);
    assert(!apply(&b, assets));
    uint8_t blue_to_red[4 + 8] = {1, 0, 0, 0, 35, 108, 197, 255, 200, 0, 0, 255};
    struct rec recolor_argb = {ARGB, RVT_KIND_RECOLOR, sizeof blue_to_red, blue_to_red};
    build(&b, &recolor_argb, 1, NULL);
    assert(apply(&b, assets));
    assert(!memcmp(catalog_data(ARGB) + HEADER, (uint8_t[]){0, 0, 200, 255}, 4) &&
           !memcmp(catalog_data(ARGB) + HEADER + 4, WHITE, 4) &&
           !memcmp(catalog_data(ARGB) + HEADER + 12, (uint8_t[]){0, 0, 200, 255}, 4));
    reset_bank();
    static uint8_t many[4 + (RVT_RECOLOR_MAX + 1) * 8];
    put32(many, RVT_RECOLOR_MAX + 1);
    for (uint32_t i = 0; i <= RVT_RECOLOR_MAX; i++) {
        put32(many + 4 + i * 8, i);
        put32(many + 8 + i * 8, i + 0x1000000u);
    }
    struct rec too_many = {ARGB, RVT_KIND_RECOLOR, sizeof many, many};
    build(&b, &too_many, 1, NULL);
    assert(!apply(&b, assets));
    put32(many, RVT_RECOLOR_MAX);
    struct rec miscounted = {ARGB, RVT_KIND_RECOLOR, sizeof many, many};
    build(&b, &miscounted, 1, NULL);
    assert(!apply(&b, assets)); /* pairs != (size - 4) / 8 */
    uint8_t wrapped[4 + 16];
    memcpy(wrapped, swap, sizeof swap);
    put32(wrapped, 2 + (1u << 29)); /* a count whose times eight wraps to the payload size */
    struct rec wrapped_count = {SECOND, RVT_KIND_RECOLOR, sizeof wrapped, wrapped};
    build(&b, &wrapped_count, 1, NULL);
    assert(!apply(&b, assets));
    struct rec max_pairs = {ARGB, RVT_KIND_RECOLOR, sizeof many - 8, many};
    build(&b, &max_pairs, 1, NULL);
    assert(apply(&b, assets)); /* a shared map may name colours the bitmap lacks */
    reset_bank();

    /* Invert keeps alpha, also on ARGB and RGB565; a mask cannot be inverted. */
    struct rec invert = {SECOND, RVT_KIND_INVERT, 0, NULL};
    build(&b, &invert, 1, NULL);
    assert(apply(&b, assets) && !memcmp(catalog_data(SECOND) + at_data(SECOND), WHITE, 4));
    reset_bank();
    struct rec invert_argb = {ARGB, RVT_KIND_INVERT, 0, NULL};
    build(&b, &invert_argb, 1, NULL);
    assert(apply(&b, assets));
    assert(!memcmp(catalog_data(ARGB) + HEADER, (uint8_t[]){58, 147, 220, 255}, 4) &&
           !memcmp(catalog_data(ARGB) + HEADER + 4, (uint8_t[]){0, 0, 0, 255}, 4) &&
           !memcmp(catalog_data(ARGB) + HEADER + 28, (uint8_t[]){255, 255, 255, 0}, 4));
    reset_bank();
    uint8_t rgb_stock[6];
    memcpy(rgb_stock, stock_bank + AT_RGB + HEADER, 6);
    memcpy(catalog_data(RGB) + HEADER, (uint8_t[]){0x00, 0xf8, 0xe0, 0x07, 0x1f, 0x00}, 6);
    memcpy(stock_bank + AT_RGB + HEADER, catalog_data(RGB) + HEADER, 6);
    catalog_crcs[RGB] = (uint16_t)~crc32_update(0xffffffffu, catalog_data(RGB), catalog_size(RGB));
    struct rec invert_rgb = {RGB, RVT_KIND_INVERT, 0, NULL};
    build(&b, &invert_rgb, 1, NULL);
    assert(apply(&b, assets));
    assert(!memcmp(catalog_data(RGB) + HEADER, (uint8_t[]){0xff, 0x07, 0x1f, 0xf8, 0xe0, 0xff}, 6));
    memcpy(stock_bank + AT_RGB + HEADER, rgb_stock, 6);
    reset_bank();
    catalog_crcs[RGB] = (uint16_t)~crc32_update(0xffffffffu, catalog_data(RGB), catalog_size(RGB));
    struct rec invert_mask = {MASK, RVT_KIND_INVERT, 0, NULL};
    build(&b, &invert_mask, 1, NULL);
    assert(!apply(&b, assets));

    /* Relight: the background rows take bg gray, the icon pixel icon gray, and
     * the palette is rebuilt in ascending order; a rule the palette cannot
     * hold, or with words outside their bounds, is refused. */
    int32_t rule[RVT_RELIGHT_WORDS] = {
        3 * 256, 14 * 256, 25 * 256, 216 * 256, 91, 24 * 256, 24 * 256,
        101018,  -300,     -453,     25 * 256,  25 * 256,
    };
    struct rec relight = {PALETTED, RVT_KIND_RELIGHT, sizeof rule, (const uint8_t *)rule};
    build(&b, &relight, 1, NULL);
    assert(apply(&b, assets) && bit_test(assets, PALETTED));
    assert(catalog_data(PALETTED)[at_data(PALETTED)] == 24 && catalog_data(PALETTED)[at_data(PALETTED) + 4] == 25);
    assert(!memcmp(catalog_data(PALETTED) + HEADER + 12, (uint8_t[]){0, 0, 1, 0, 0, 1, 1, 0}, 8));
    reset_bank();
    rule[RVT_RELIGHT_ICON_HI] = 26 * 256;
    build(&b, &relight, 1, NULL);
    assert(!apply(&b, assets));
    rule[RVT_RELIGHT_ICON_HI] = 25 * 256;
    static const struct {
        unsigned word;
        int32_t value;
    } out_of_bounds[] = {
        {RVT_RELIGHT_T_ZERO, -1},     {RVT_RELIGHT_T_FULL, 256 * 256},
        {RVT_RELIGHT_BG_BASE, 1025 * 256}, {RVT_RELIGHT_BG_REF, -256 * 256},
        {RVT_RELIGHT_BG_SLOPE, 17 * 256}, {RVT_RELIGHT_ICON_BASE, -1025 * 256},
        {RVT_RELIGHT_ICON_LUMA, 256 * 256}, {RVT_RELIGHT_ICON_ROW, -256 * 256},
    };
    for (unsigned i = 0; i < sizeof out_of_bounds / sizeof *out_of_bounds; i++) {
        int32_t kept = rule[out_of_bounds[i].word];
        rule[out_of_bounds[i].word] = out_of_bounds[i].value;
        build(&b, &relight, 1, NULL);
        assert(!apply(&b, assets));
        rule[out_of_bounds[i].word] = kept;
    }
    struct rec relight_rgb = {RGB, RVT_KIND_RELIGHT, sizeof rule, (const uint8_t *)rule};
    build(&b, &relight_rgb, 1, NULL);
    assert(!apply(&b, assets));
    expect_relight(WIDE, wide_plain_in, wide_plain_rule, wide_plain_out);
    expect_relight(WIDE, wide_shadow_in, wide_shadow_rule, wide_shadow_out);
    expect_relight(PALETTED, paletted_two_in, paletted_two_rule, paletted_two_out);
    /* Shadow pruning: a partly covered pixel with no fully covered neighbour
     * becomes background. Bitmap 5's third colour is black, far past k; the
     * shade at (1, 0), offset 8 from the row's background, has no full
     * neighbour and is dropped; the same shade at (3, 0) touches the black
     * pixel at (3, 1) and stays a mid gray. */
    uint8_t *wide = catalog_data(WIDE) + at_data(WIDE), wide_stock[24];
    memcpy(wide_stock, wide, 24);
    memcpy(wide, LIGHT, 4);
    memcpy(wide + 4, (uint8_t[]){239, 236, 232, 255}, 4);
    memcpy(wide + 8, BLACK, 4);
    memcpy(wide + 12, LIGHT, 4);
    memcpy(wide + 16, (uint8_t[]){0, 1, 0, 1, 0, 0, 0, 2}, 8);
    memcpy(stock_bank + AT_WIDE + at_data(WIDE), wide, 24);
    catalog_crcs[WIDE] = (uint16_t)~crc32_update(0xffffffffu, catalog_data(WIDE), catalog_size(WIDE));
    int32_t wide_rule[RVT_RELIGHT_WORDS] = {
        3 * 256, 14 * 256, 25 * 256, 216 * 256, 91, 24 * 256, 24 * 256,
        27 * 256, 0, 0, 27 * 256, 27 * 256,
    };
    struct rec relight_wide = {WIDE, RVT_KIND_RELIGHT, sizeof wide_rule, (const uint8_t *)wide_rule};
    build(&b, &relight_wide, 1, NULL);
    assert(apply(&b, assets));
    {
        const uint8_t *rows = catalog_data(WIDE) + at_data(WIDE) + 16;
        const uint8_t *palette = catalog_data(WIDE) + at_data(WIDE);
        assert(palette[rows[1] * 4] == 24); /* pruned to the background gray */
        assert(palette[rows[7] * 4] == 27); /* the black pixel, fully covered */
        assert(palette[rows[3] * 4] == 25); /* kept and blended */
        assert(!memcmp(palette, (uint8_t[]){24, 24, 24, 255, 25, 25, 25, 255, 27, 27, 27, 255, 0, 0, 0, 0}, 16));
    }
    memcpy(stock_bank + AT_WIDE + at_data(WIDE), wide_stock, 24);
    reset_bank();
    catalog_crcs[WIDE] = (uint16_t)~crc32_update(0xffffffffu, catalog_data(WIDE), catalog_size(WIDE));

    /* Alias: applied after the other records, so a source with a higher index
     * is final; a chain, a self alias, a different size and a tinted source
     * are refused; a source of another layout is re-encoded when the palette
     * has room. */
    uint32_t three = SECOND;
    struct rec alias_up[2] = {{PALETTED, RVT_KIND_ALIAS, 4, (const uint8_t *)&three}, invert};
    build(&b, alias_up, 2, NULL);
    assert(apply(&b, assets) && !memcmp(catalog_data(PALETTED) + at_data(PALETTED), catalog_data(SECOND) + at_data(SECOND), PALETTED_BYTES - HEADER - 4));
    assert(!memcmp(catalog_data(PALETTED) + at_data(PALETTED), WHITE, 4));
    reset_bank();
    uint32_t one = PALETTED;
    struct rec chain[2] = {{PALETTED, RVT_KIND_ALIAS, 4, (const uint8_t *)&three},
                           {SECOND, RVT_KIND_ALIAS, 4, (const uint8_t *)&one}};
    build(&b, chain, 2, NULL);
    assert(!apply(&b, assets));
    struct rec self_alias = {PALETTED, RVT_KIND_ALIAS, 4, (const uint8_t *)&one};
    build(&b, &self_alias, 1, NULL);
    assert(!apply(&b, assets));
    uint32_t two_ = RGB;
    struct rec wrong_size = {PALETTED, RVT_KIND_ALIAS, 4, (const uint8_t *)&two_};
    build(&b, &wrong_size, 1, NULL);
    assert(!apply(&b, assets));
    tint[SECOND] = RVT_TINT_ROLE + RVT_ROLE_ICON_TINT;
    build(&b, alias_up, 1, tint);
    assert(!apply(&b, assets));
    tint[SECOND] = 0;
    uint32_t four = ARGB, five = WIDE;
    struct rec argb_into_wide = {WIDE, RVT_KIND_ALIAS, 4, (const uint8_t *)&four};
    build(&b, &argb_into_wide, 1, NULL);
    assert(apply(&b, assets)); /* blue, white and clear white: three of four entries */
    assert(!memcmp(catalog_data(WIDE) + at_data(WIDE), BLUE, 4) &&
           !memcmp(catalog_data(WIDE) + at_data(WIDE) + 4, WHITE, 4) &&
           !memcmp(catalog_data(WIDE) + at_data(WIDE) + 8, (uint8_t[]){255, 255, 255, 0}, 4) &&
           !memcmp(catalog_data(WIDE) + at_data(WIDE) + 12, (uint8_t[]){0, 0, 0, 0}, 4) &&
           !memcmp(catalog_data(WIDE) + at_data(WIDE) + 16, (uint8_t[]){0, 1, 1, 0, 1, 1, 0, 2}, 8));
    reset_bank();
    struct rec wide_into_argb = {ARGB, RVT_KIND_ALIAS, 4, (const uint8_t *)&five};
    build(&b, &wide_into_argb, 1, NULL);
    assert(apply(&b, assets));
    assert(!memcmp(catalog_data(ARGB) + HEADER, BLUE, 4) && !memcmp(catalog_data(ARGB) + HEADER + 4, GREY, 4) &&
           !memcmp(catalog_data(ARGB) + HEADER + 28, BLUE, 4));
    reset_bank();
    struct rec wide_into_paletted = {PALETTED, RVT_KIND_ALIAS, 4, (const uint8_t *)&five};
    build(&b, &wide_into_paletted, 1, NULL);
    assert(!apply(&b, assets)); /* four colours into two entries */
    assert(catalog_data(PALETTED)[at_data(PALETTED)] == 0);
    reset_bank();

    /* A derived record on a bitmap the active look changed fetches stock first;
     * one the look left alone is derived in place. */
    uint32_t current[BITMAP_WORDS] = {0};
    bit_set(current, SECOND);
    memset(catalog_data(SECOND) + at_data(SECOND), 0x55, 8);
    opens = 0;
    build(&b, &invert, 1, NULL);
    assert(apply_from(&b, assets, current) && opens == 1);
    assert(!memcmp(catalog_data(SECOND) + at_data(SECOND), WHITE, 4));
    reset_bank();
    memset(current, 0, sizeof current);

    /* A tint on a derived record is refused. */
    tint[SECOND] = RVT_TINT_ROLE + RVT_ROLE_ICON_TINT;
    build(&b, &invert, 1, tint);
    assert(!apply(&b, assets));
    tint[SECOND] = 0;

    /* Compressed pixels decode straight into the bitmap: a literal group,
     * then a match; a long match extends its length; the stream must end
     * where the bitmap is full and must not end before. */
    static const uint8_t lz_paletted[14] = {0x00, 9, 9, 9, 255, 8, 8, 8, 255, 0x04, 0, 1, 0x30, 0x01};
    struct rec lz = {PALETTED, RVT_KIND_PIXELS_LZ, sizeof lz_paletted, lz_paletted};
    build(&b, &lz, 1, NULL);
    assert(apply(&b, assets) && bit_test(assets, PALETTED));
    assert(!memcmp(catalog_data(PALETTED) + at_data(PALETTED),
                   (uint8_t[]){9, 9, 9, 255, 8, 8, 8, 255, 0, 1, 0, 1, 0, 1, 0, 1}, 16));
    reset_bank();
    static const uint8_t lz_zeros[5] = {0x02, 0, 0xf0, 0x00, 0x0d};
    struct rec lz_argb = {ARGB, RVT_KIND_PIXELS_LZ, sizeof lz_zeros, lz_zeros};
    build(&b, &lz_argb, 1, NULL);
    assert(apply(&b, assets));
    for (unsigned i = 0; i < 32; i++)
        assert(catalog_data(ARGB)[HEADER + i] == 0);
    reset_bank();
    struct rec lz_short = {PALETTED, RVT_KIND_PIXELS_LZ, sizeof lz_paletted - 1, lz_paletted};
    build(&b, &lz_short, 1, NULL);
    assert(!apply(&b, assets) && catalog_data(PALETTED)[at_data(PALETTED)] == 0);
    reset_bank();
    static uint8_t lz_long[15];
    memcpy(lz_long, lz_paletted, 14);
    struct rec lz_trailing = {PALETTED, RVT_KIND_PIXELS_LZ, sizeof lz_long, lz_long};
    build(&b, &lz_trailing, 1, NULL);
    assert(!apply(&b, assets) && catalog_data(PALETTED)[at_data(PALETTED)] == 0);
    reset_bank();
    static const uint8_t lz_backwards[3] = {0x01, 0x00, 0x05};
    struct rec lz_before = {PALETTED, RVT_KIND_PIXELS_LZ, sizeof lz_backwards, lz_backwards};
    build(&b, &lz_before, 1, NULL);
    assert(!apply(&b, assets));
    static const uint8_t lz_overrun[4] = {0x02, 0, 0xf0, 0x00}; /* 18 more bytes than the 16 */
    struct rec lz_over = {PALETTED, RVT_KIND_PIXELS_LZ, sizeof lz_overrun, lz_overrun};
    build(&b, &lz_over, 1, NULL);
    assert(!apply(&b, assets));
    struct rec lz_empty = {PALETTED, RVT_KIND_PIXELS_LZ, 0, lz_paletted};
    build(&b, &lz_empty, 1, NULL);
    assert(!apply(&b, assets));
    reset_bank();
    /* A paletted record's indices are checked after decoding. */
    static const uint8_t lz_bad_index[14] = {0x00, 9, 9, 9, 255, 8, 8, 8, 255, 0x04, 0, 2, 0x30, 0x01};
    struct rec lz_bad = {PALETTED, RVT_KIND_PIXELS_LZ, sizeof lz_bad_index, lz_bad_index};
    build(&b, &lz_bad, 1, NULL);
    assert(!apply(&b, assets) && catalog_data(PALETTED)[at_data(PALETTED)] == 0);
    reset_bank();

    /* A truncated file fails on the record it cuts; what it wrote before
     * stays marked for the switch to restore. */
    build(&b, two, 2, NULL);
    b.size -= 8;
    assert(!apply(&b, assets) && bit_test(assets, MASK) && bit_test(assets, PALETTED));
    assert(!memcmp(catalog_data(MASK) + HEADER, mask, sizeof mask));
    assert(catalog_data(PALETTED)[at_data(PALETTED)] == 0);
    reset_bank();

    /* The CRC-32 vector. */
    assert(crc("123456789", 9) == 0xcbf43926u);
}

/* The switch module over the host firmware file: what a switch leaves in the
 * bank, in the state and in the style, and how it comes back from damage. */
static void switch_contracts(void)
{
    struct built b;
    struct look theme = {.id = "test", .name = "Test", .appearances = RVT_APPEARANCE_DARK, .dark = 1};
    struct look stock = {0};
    uint8_t mask[4 * 2] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t paletted[2 * 4 + 4 * 2] = {9, 9, 9, 255, 8, 8, 8, 255, 0, 1, 0, 1, 1, 0, 1, 0};
    struct rec two[2] = {pixels(MASK, mask), pixels(PALETTED, paletted)};
    memset(&look_state, 0, sizeof look_state);
    style_reset();
    style_set_dark(0);
    style_set_accent(ACCENT_DEFAULT);

    /* A theme goes on and comes off; the firmware file is checked once. */
    build(&b, two, 2, NULL);
    opens = closes = 0;
    assert(switch_to(&b, &theme) == SWITCH_OK);
    assert(opens == 2 && closes == 2 && look_state.stock_checked); /* the check, then the switch */
    assert(!strcmp(look_state.active.id, "test") && look_state.active.dark && style_from_theme() && style_dark());
    assert(bit_test(look_state.assets, MASK) && bit_test(look_state.assets, PALETTED));
    /* Under a theme the stock accent art follows the theme's accent fill. */
    assert(bit_test(look_state.assets, WIDE) && bit_test(look_state.accented, WIDE));
    assert(!memcmp(catalog_data(WIDE) + at_data(WIDE), (uint8_t[]){197, 108, 35, 255}, 4));
    assert(!memcmp(catalog_data(MASK) + HEADER, mask, sizeof mask));
    assert(switch_apply(NULL, &stock, NULL) == SWITCH_OK && opens == 3);
    assert(bank_is_stock() && !look_state.active.id[0] && !style_from_theme() && !style_dark());
    for (unsigned i = 0; i < BITMAP_WORDS; i++)
        assert(!look_state.assets[i] && !look_state.accented[i]);

    /* Without the firmware file nothing changes. */
    firmware_missing = 1;
    look_state.stock_checked = 0;
    assert(switch_to(&b, &theme) == SWITCH_LOAD_FAILED && bank_is_stock() && !look_state.stock_checked);
    firmware_missing = 0;
    assert(switch_to(&b, &theme) == SWITCH_OK);

    /* A pack that fails part way: the stock light look comes back and every
     * touched bitmap is restored. */
    paletted[8] = 2;
    memcpy(mask, (uint8_t[]){8, 7, 6, 5, 4, 3, 2, 1}, 8);
    two[0] = pixels(MASK, mask);
    build(&b, two, 2, NULL);
    assert(switch_to(&b, &theme) == SWITCH_RECOVERED);
    assert(bank_is_stock() && !look_state.active.id[0] && !style_from_theme() && !style_dark() &&
           style_accent() == ACCENT_DEFAULT && !firmware_open_now);
    for (unsigned i = 0; i < BITMAP_WORDS; i++)
        assert(!look_state.assets[i]);
    paletted[8] = 0;

    /* When the firmware file cannot restore a bitmap, theming is off: the
     * attempt's bitmaps stay marked and the file is checked again next time. */
    build(&b, two, 2, NULL);
    assert(switch_to(&b, &theme) == SWITCH_OK);
    corrupt_asset = MASK;
    assert(switch_apply(NULL, &stock, NULL) == SWITCH_RESTORE_FAILED);
    assert(bit_test(look_state.assets, MASK) && bit_test(look_state.assets, PALETTED) && !look_state.stock_checked &&
           !look_state.active.id[0] && !style_from_theme());
    assert(catalog_data(MASK)[HEADER] == 0); /* zeroed by the failed fetch */
    corrupt_asset = -1;
    assert(switch_apply(NULL, &stock, NULL) == SWITCH_OK && bank_is_stock() && look_state.stock_checked);

    /* Theme to theme: the second pack applies, then a bitmap only the first
     * touched cannot be restored; the recovery cannot restore it either, so
     * theming is off with both packs' bitmaps marked. */
    struct built second;
    build(&b, two, 2, NULL);
    assert(switch_to(&b, &theme) == SWITCH_OK);
    build(&second, two + 1, 1, NULL);
    corrupt_asset = MASK;
    assert(switch_to(&second, &theme) == SWITCH_RESTORE_FAILED);
    assert(bit_test(look_state.assets, MASK) && bit_test(look_state.assets, PALETTED) &&
           !look_state.stock_checked && !look_state.active.id[0] && !style_from_theme());
    corrupt_asset = -1;
    assert(switch_apply(NULL, &stock, NULL) == SWITCH_OK && bank_is_stock() && look_state.stock_checked);

    /* Classic dark: a pack with colours only; one with a tint or a flag is refused. */
    struct look dark = {.dark = 1};
    build(&b, two, 2, NULL);
    assert(switch_to(&b, &dark) == SWITCH_OK && !style_from_theme() && style_dark());
    assert(!memcmp(catalog_data(MASK) + HEADER, mask, sizeof mask));
    uint8_t tint[RVT_CATALOG_MAX] = {0};
    tint[MASK] = RVT_TINT_ROLE + RVT_ROLE_ICON_TINT;
    build(&b, NULL, 0, tint);
    assert(switch_to(&b, &dark) == SWITCH_LOAD_FAILED);
    build(&b, NULL, 0, NULL);
    put32(b.bytes + RVT_HEADER_BYTES + RVT_PACK_HEADER_BYTES + RVT_STYLE_FLAGS, RVT_FLAG_FLAT_SELECTION);
    resign_dark_pack(&b);
    assert(switch_to(&b, &dark) == SWITCH_LOAD_FAILED);
    assert(!memcmp(catalog_data(MASK) + HEADER, mask, sizeof mask)); /* the dark look stays */
    assert(switch_apply(NULL, &stock, NULL) == SWITCH_OK && bank_is_stock());

    /* An accent on Classic recolours the stock accent art from stock pixels
     * and restores it exactly; Blue and Theme Default leave it alone. */
    struct look pink = {.accent = ACCENT_FIRST + 6};
    assert(!strcmp(accent_labels[pink.accent], "Pink"));
    assert(switch_apply(NULL, &pink, NULL) == SWITCH_OK && bit_test(look_state.assets, WIDE) && bit_test(look_state.accented, WIDE));
    const uint8_t *fill = style_color(RVT_ROLE_ACCENT_FILL);
    assert(fill[0] == 196 && fill[1] == 30 && fill[2] == 86);
    assert(!memcmp(catalog_data(WIDE) + at_data(WIDE), (uint8_t[]){86, 30, 196, 255}, 4));
    assert(!memcmp(catalog_data(WIDE) + at_data(WIDE) + 4, GREY, 4)); /* grey stays grey */
    assert(switch_accent(ACCENT_FIRST + 2) == SWITCH_OK); /* Green, from stock again */
    assert(!memcmp(catalog_data(WIDE) + at_data(WIDE), (uint8_t[]){58, 128, 30, 255}, 4));
    assert(switch_accent(ACCENT_FIRST) == SWITCH_OK && bank_is_stock() && !bit_test(look_state.assets, WIDE));
    assert(switch_accent(ACCENT_DEFAULT) == SWITCH_OK && bank_is_stock());
    /* A failed restore during an accent change recovers like a switch. */
    assert(switch_accent(pink.accent) == SWITCH_OK);
    corrupt_asset = WIDE;
    assert(switch_accent(ACCENT_FIRST + 2) == SWITCH_RESTORE_FAILED && !look_state.stock_checked &&
           style_accent() == ACCENT_DEFAULT && bit_test(look_state.assets, WIDE));
    corrupt_asset = -1;
    assert(switch_apply(NULL, &stock, NULL) == SWITCH_OK && bank_is_stock());

    /* A pack that replaces the accent art keeps its own art under any accent. */
    uint8_t wide_art[4 * 4 + 8] = {1, 1, 1, 255, 2, 2, 2, 255, 3, 3, 3, 255, 4, 4, 4, 255, 0, 1, 2, 3, 0, 1, 2, 3};
    struct rec own = pixels(WIDE, wide_art);
    build(&b, &own, 1, NULL);
    theme.accent = pink.accent;
    assert(switch_to(&b, &theme) == SWITCH_OK && !bit_test(look_state.accented, WIDE));
    assert(!memcmp(catalog_data(WIDE) + at_data(WIDE), wide_art, sizeof wide_art));
    assert(switch_accent(ACCENT_FIRST + 2) == SWITCH_OK &&
           !memcmp(catalog_data(WIDE) + at_data(WIDE), wide_art, sizeof wide_art));
    theme.accent = 0;

    /* Unload puts everything back. */
    switch_restore_all();
    assert(bank_is_stock() && !look_state.active.id[0] && !style_from_theme());
    for (unsigned i = 0; i < BITMAP_WORDS; i++)
        assert(!look_state.assets[i]);
    assert(!firmware_open_now && !allocations);
}


/* A theme's bitmaps for extensions: the table after a pack's records goes
 * into the index, checked entry by entry; the provider serves the core from
 * the index and the theme file; the index goes when the look does. */
#define OWNER_A 0x1000u
#define OWNER_B 0x2000u
/* A Settings icon: 78 by 121 Mask8, stride 80. */
#define MENU_STRIDE 80
#define MENU_ROWS (MENU_STRIDE * 121)
#define MENU_BMAP_BYTES (HEADER + MENU_ROWS)
#define LZ_BOUND(n) ((n) + ((n) + 7) / 8)
static uint8_t menu_a[MENU_BMAP_BYTES], menu_b[MENU_BMAP_BYTES];
/* menu_lz: the header, then a literal 0xab and one match of 9,679 bytes from
 * one byte back: 15 + 3, extended by 37 bytes of 255 and one of 226. */
static uint8_t menu_lz[HEADER + 4 + 38];
/* A canvas bitmap: 4 by 2 Indexed8 with two colours, raw and as literals. */
static uint8_t small[PALETTED_BYTES], small_lz[HEADER + LZ_BOUND(PALETTED_BYTES - HEADER)];

/* `n` bytes as an lz stream of literals: a flag byte per eight. */
static uint32_t lz_literals(uint8_t *out, const uint8_t *in, uint32_t n)
{
    uint32_t written = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (i % 8 == 0)
            out[written++] = 0;
        out[written++] = in[i];
    }
    return written;
}

static void asset_payloads(void)
{
    bmap(menu_a, BMapFormat_Mask8, MENU_STRIDE, 8, 78, 121, sizeof menu_a);
    bmap(menu_b, BMapFormat_Mask8, MENU_STRIDE, 8, 78, 121, sizeof menu_b);
    for (uint32_t i = HEADER; i < sizeof menu_a; i++) {
        menu_a[i] = (uint8_t)(i * 7);
        menu_b[i] = (uint8_t)(i * 3 + 1);
    }
    memcpy(menu_lz, menu_a, HEADER);
    menu_lz[HEADER] = 0x02;
    menu_lz[HEADER + 1] = 0xab;
    menu_lz[HEADER + 2] = 0xf0;
    menu_lz[HEADER + 3] = 0x00;
    memset(menu_lz + HEADER + 4, 255, 37);
    menu_lz[HEADER + 41] = 226;
    bmap(small, BMapFormat_Indexed8, 4, 8, 4, 2, sizeof small);
    put32(small + HEADER, 2);
    memcpy(small + HEADER + 4, LIGHT, 4);
    memcpy(small + HEADER + 8, ICON, 4);
    for (uint32_t i = 0; i < 8; i++)
        small[HEADER + 12 + i] = (uint8_t)(i & 1);
    memcpy(small_lz, small, HEADER);
    assert(lz_literals(small_lz + HEADER, small + HEADER, sizeof small - HEADER) ==
           sizeof small_lz - HEADER);
}

static void with_assets(const struct asset_payload *table, uint32_t count, struct built *b)
{
    asset_table = table;
    asset_table_count = count;
    build(b, NULL, 0, NULL);
    asset_table = NULL;
    asset_table_count = 0;
}

static void prefix_custom(uint8_t *p, const uint8_t roles[RVT_ROLES][3], uint32_t flags,
                          unsigned palette, const uint8_t accent[RVT_ACCENT_SLOTS][3]);

/* The core's fill of a probed block: 1 when the provider filled `bytes` of
 * `block` with the asset; the block is the caller's. */
static int fill(uint32_t owner, uint32_t asset, uint8_t *block, uint32_t bytes)
{
    memset(block, 0x5a, bytes);
    return replace(owner, asset, block, bytes);
}

static void assets_contracts(void)
{
    static uint8_t block[MENU_BMAP_BYTES], zeros[MENU_BMAP_BYTES], edited[MENU_BMAP_BYTES];
    uint32_t assets[BITMAP_WORDS];
    struct built b;
    asset_payloads();
    const struct asset_payload four[4] = {
        {OWNER_A, 5, RVT_EXTENSION_BITMAP, sizeof menu_a, menu_a},
        {OWNER_A, 9, RVT_EXTENSION_BITMAP_LZ, sizeof menu_lz, menu_lz},
        {OWNER_B, 1, RVT_EXTENSION_BITMAP, sizeof small, small},
        {OWNER_B, 2, RVT_EXTENSION_BITMAP_LZ, sizeof small_lz, small_lz},
    };

    /* A good table: one entry per asset, in order, each header's offset in
     * the file, the stored bytes, the decoded bytes and the kind. */
    with_assets(four, 4, &b);
    assert(apply(&b, assets) && live_count == 4 && allocations == 1);
    for (unsigned i = 0; i < 4; i++) {
        assert(live[i].owner == four[i].owner && live[i].asset == four[i].asset);
        assert(live[i].kind == four[i].kind && live[i].bytes == four[i].size);
        assert(live[i].decoded == HEADER + le32(four[i].payload + 24));
        assert(!memcmp(b.bytes + live[i].offset, four[i].payload, four[i].size));
    }
    assert(live[0].offset == RVT_HEADER_BYTES + RVT_PACK_HEADER_BYTES + RVT_STYLE_BYTES + RVT_EXTENSION_BITMAP_BYTES);
    assert(live[1].decoded == MENU_BMAP_BYTES && live[3].decoded == PALETTED_BYTES);

    /* Refused, the index that was on staying: out of order or repeated keys;
     * a kind that is not a bitmap; an entry shorter than a header; a header
     * whose bounds are not a rectangle, whose rows overrun the stride or
     * whose payload word is over the cap or under the rows; a raw payload of
     * another size than the header says; an empty lz stream or one over the
     * literal bound; padding that is not zero; a count the body does not
     * hold, one that leaves a table unread, and a table past the limit. */
    struct asset_payload bad[4];
    memcpy(bad, four, sizeof bad);
    bad[0] = four[1], bad[1] = four[0];
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets) && live_count == 4);
    memcpy(bad, four, sizeof bad);
    bad[1].asset = 5;
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    memcpy(bad, four, sizeof bad);
    bad[2].owner = OWNER_A, bad[2].asset = 2;
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    memcpy(bad, four, sizeof bad);
    bad[0].kind = RVT_EXTENSION_KINDS;
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    bad[0].kind = 0;
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    memcpy(bad, four, sizeof bad);
    bad[2].size = HEADER - 1;
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    memcpy(edited, menu_a, sizeof edited);
    put32(edited + 24, 0); /* a header without rows, as the compiler refuses it */
    bad[2] = (struct asset_payload){OWNER_B, 1, RVT_EXTENSION_BITMAP, HEADER, edited};
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    memcpy(edited, menu_a, sizeof edited);
    bad[2] = (struct asset_payload){OWNER_B, 1, RVT_EXTENSION_BITMAP, sizeof edited, edited};
    put32(edited + 16, (uint32_t)-1); /* bottom above top */
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    memcpy(edited, menu_a, sizeof edited);
    put32(edited + 8, (uint32_t)-1); /* a negative top */
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    memcpy(edited, menu_a, sizeof edited);
    put16(edited + 4, 72); /* 78 pixels of 8 bits in a 72-byte stride */
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    memcpy(edited, menu_a, sizeof edited);
    put32(edited + 24, MENU_ROWS - 1); /* rows past the payload word */
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    memcpy(edited, menu_a, sizeof edited);
    put32(edited + 24, MENU_ROWS + 1); /* a raw payload of another size */
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    memcpy(edited, menu_a, sizeof edited);
    bad[2].size = sizeof edited - 1;
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    memcpy(edited, menu_a, sizeof edited);
    put16(edited, 0x9); /* no such format */
    bad[2] = (struct asset_payload){OWNER_B, 1, RVT_EXTENSION_BITMAP, sizeof edited, edited};
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    memcpy(edited, small, sizeof small);
    put32(edited + HEADER, 5); /* five colours do not fit a 20-byte payload with eight pixels */
    bad[2] = (struct asset_payload){OWNER_B, 1, RVT_EXTENSION_BITMAP, sizeof small, edited};
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    put32(edited + HEADER, 300); /* past a palette's 256 */
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    put32(edited + HEADER, 3); /* a third colour, in place of a pixel row */
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    memcpy(edited, menu_lz, sizeof menu_lz);
    bad[2] = (struct asset_payload){OWNER_B, 1, RVT_EXTENSION_BITMAP_LZ, sizeof menu_lz, edited};
    put32(edited + 24, RVT_EXTENSION_BITMAP_MAX_BYTES - HEADER + 1); /* over the cap */
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    put32(edited + 24, RVT_EXTENSION_BITMAP_MAX_BYTES - HEADER); /* at it: the provider decides */
    with_assets(bad, 4, &b);
    assert(apply(&b, assets) && live_count == 4);
    memcpy(bad, four, sizeof bad);
    bad[1].size = HEADER;
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    static uint8_t overlong[HEADER + LZ_BOUND(MENU_ROWS) + 1];
    memcpy(overlong, menu_a, HEADER);
    bad[1].size = sizeof overlong, bad[1].payload = overlong;
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    bad[1].size = sizeof overlong - 1;
    with_assets(bad, 4, &b);
    assert(apply(&b, assets) && live_count == 4); /* parsed; the provider decides */
    memcpy(bad, four, sizeof bad);
    static uint8_t small_overlong[sizeof small_lz + 1];
    memcpy(small_overlong, small_lz, sizeof small_lz);
    bad[3].size = sizeof small_overlong, bad[3].payload = small_overlong;
    with_assets(bad, 4, &b);
    assert(!apply(&b, assets));
    with_assets(four, 4, &b);
    b.bytes[b.size - 1] ^= 1;
    assert(!apply(&b, assets)); /* the checksum covers the table */
    with_assets(four, 4, &b);
    b.bytes[live[1].offset + sizeof menu_lz] = 1; /* padding after the lz stream */
    resign_dark_pack(&b);
    assert(!apply(&b, assets));
    with_assets(four, 4, &b);
    put32(b.bytes + RVT_HEADER_BYTES + RVT_PACK_EXTENSION_BITMAPS, 5);
    assert(!apply(&b, assets));
    put32(b.bytes + RVT_HEADER_BYTES + RVT_PACK_EXTENSION_BITMAPS, 3);
    assert(!apply(&b, assets));
    static struct asset_payload many[RVT_EXTENSION_BITMAPS_MAX + 1];
    for (uint32_t i = 0; i < RVT_EXTENSION_BITMAPS_MAX + 1; i++)
        many[i] = (struct asset_payload){OWNER_A, i, RVT_EXTENSION_BITMAP_LZ, sizeof menu_lz,
                                         menu_lz};
    with_assets(many, RVT_EXTENSION_BITMAPS_MAX + 1, &b);
    assert(!apply(&b, assets));
    with_assets(many, RVT_EXTENSION_BITMAPS_MAX, &b);
    assert(apply(&b, assets) && live_count == RVT_EXTENSION_BITMAPS_MAX);
    /* A pack without a table leaves no index. */
    build(&b, NULL, 0, NULL);
    assert(apply(&b, assets) && !live_count && !allocations);

    /* The probe answers the decoded size of a listed asset, 0 for any other;
     * the fill puts the whole BMap, header and rows, raw or decoded, into a
     * block of that size and nothing into one of another size. */
    with_assets(four, 4, &b);
    assert(apply(&b, assets));
    provided = &b;
    memcpy(look_state.active.id, PROVIDED_ID, sizeof PROVIDED_ID);
    assert(replacement_bytes(OWNER_A, 5) == MENU_BMAP_BYTES);
    assert(replacement_bytes(OWNER_A, 9) == MENU_BMAP_BYTES);
    assert(replacement_bytes(OWNER_B, 1) == PALETTED_BYTES);
    assert(replacement_bytes(OWNER_B, 2) == PALETTED_BYTES);
    assert(!replacement_bytes(OWNER_A, 7) && !replacement_bytes(OWNER_A, 4));
    assert(!replacement_bytes(OWNER_B, 3) && !replacement_bytes(0, 5));
    assert(fill(OWNER_A, 5, block, MENU_BMAP_BYTES) && !memcmp(block, menu_a, MENU_BMAP_BYTES));
    assert(fill(OWNER_A, 9, block, MENU_BMAP_BYTES) && !memcmp(block, menu_a, HEADER));
    for (uint32_t i = HEADER; i < MENU_BMAP_BYTES; i++)
        assert(block[i] == 0xab);
    assert(fill(OWNER_B, 1, block, PALETTED_BYTES) && !memcmp(block, small, PALETTED_BYTES));
    assert(fill(OWNER_B, 2, block, PALETTED_BYTES) && !memcmp(block, small, PALETTED_BYTES));
    assert(!fill(OWNER_A, 7, block, MENU_BMAP_BYTES));
    assert(!fill(OWNER_A, 5, block, MENU_BMAP_BYTES - 1));
    assert(!fill(OWNER_B, 1, block, MENU_BMAP_BYTES));
    assert(!open_files && allocations == 1);
    /* The file is not the one indexed: a header of the same size that the
     * index would have refused, here a palette past the payload, is not
     * served; the lz palette is checked once decoded. */
    put32(b.bytes + live[2].offset + HEADER, 5);
    assert(!fill(OWNER_B, 1, block, PALETTED_BYTES));
    put32(b.bytes + live[2].offset + HEADER, 2);
    assert(fill(OWNER_B, 1, block, PALETTED_BYTES));
    b.bytes[live[3].offset + HEADER + 1] = 5; /* the colour count's literal */
    assert(!fill(OWNER_B, 2, block, PALETTED_BYTES));
    b.bytes[live[3].offset + HEADER + 1] = 2;
    assert(fill(OWNER_B, 2, block, PALETTED_BYTES));
    put32(b.bytes + live[0].offset + 24, MENU_ROWS - MENU_STRIDE); /* a payload word the rows overrun */
    assert(!fill(OWNER_A, 5, block, MENU_BMAP_BYTES));
    put32(b.bytes + live[0].offset + 24, MENU_ROWS);
    assert(fill(OWNER_A, 5, block, MENU_BMAP_BYTES) && !open_files);
    /* A file that no longer yields the bitmap: cut inside the header, inside
     * a raw payload, inside an lz stream, a stream that ends before the rows
     * are full, one that runs past them, and one with bytes after them. The
     * fill says 0 and holds nothing. */
    b.size = live[0].offset + 10;
    assert(!fill(OWNER_A, 5, block, MENU_BMAP_BYTES));
    b.size = live[0].offset + 100;
    assert(!fill(OWNER_A, 5, block, MENU_BMAP_BYTES));
    b.size = live[1].offset + HEADER + 10;
    assert(!fill(OWNER_A, 9, block, MENU_BMAP_BYTES));
    struct asset_payload variant[4];
    memcpy(variant, four, sizeof variant);
    static uint8_t lz_short[HEADER + 2];
    memcpy(lz_short, menu_a, HEADER);
    lz_short[HEADER + 1] = 0xab;
    variant[1].size = sizeof lz_short, variant[1].payload = lz_short;
    with_assets(variant, 4, &b);
    assert(apply(&b, assets) && !fill(OWNER_A, 9, block, MENU_BMAP_BYTES));
    static uint8_t lz_long[sizeof menu_lz];
    memcpy(lz_long, menu_lz, sizeof menu_lz);
    lz_long[HEADER + 41] = 227;
    variant[1].size = sizeof lz_long, variant[1].payload = lz_long;
    with_assets(variant, 4, &b);
    assert(apply(&b, assets) && !fill(OWNER_A, 9, block, MENU_BMAP_BYTES));
    static uint8_t lz_trailing[sizeof menu_lz + 1];
    memcpy(lz_trailing, menu_lz, sizeof menu_lz);
    variant[1].size = sizeof lz_trailing, variant[1].payload = lz_trailing;
    with_assets(variant, 4, &b);
    assert(apply(&b, assets) && !fill(OWNER_A, 9, block, MENU_BMAP_BYTES));
    assert(!open_files && allocations == 1);
    /* A theme file that is gone. */
    provided = NULL;
    unreadable = PROVIDED_ID;
    assert(replacement_bytes(OWNER_A, 5) == MENU_BMAP_BYTES);
    assert(!fill(OWNER_A, 5, block, MENU_BMAP_BYTES) && !open_files);
    unreadable = NULL;
    provided = &b;
    /* Classic dark serves its bitmaps from the classic asset; Classic light
     * has no file. */
    memset(look_state.active.id, 0, sizeof look_state.active.id);
    look_state.active.dark = 1;
    assert(!fill(OWNER_A, 5, block, MENU_BMAP_BYTES) && !open_files); /* the asset here holds none */
    look_state.active.dark = 0;
    assert(!fill(OWNER_A, 5, block, MENU_BMAP_BYTES) && !open_files);
    memcpy(look_state.active.id, PROVIDED_ID, sizeof PROVIDED_ID);

    /* The extension's own bitmap is left alone, until a flat-icon look: then
     * a Settings icon loses the rows under its art, and nothing else changes. */
    memcpy(block, menu_a, sizeof block);
    assert(!adjust(OWNER_A, 7, block, MENU_BMAP_BYTES) && !memcmp(block, menu_a, sizeof block));
    uint8_t prefix[RVT_STYLE_BYTES];
    static const uint8_t roles[RVT_ROLES][3] = {{0}};
    prefix_custom(prefix, roles, RVT_FLAG_FLAT_ICONS, 0, NULL);
    style_set(prefix);
    assert(adjust(OWNER_A, 7, block, MENU_BMAP_BYTES));
    assert(!memcmp(block, menu_a, HEADER + 79 * MENU_STRIDE) &&
           !memcmp(block + HEADER + 79 * MENU_STRIDE, zeros, (121 - 79) * MENU_STRIDE));
    memcpy(block, small, sizeof small);
    assert(!adjust(OWNER_B, 1, block, PALETTED_BYTES) && !memcmp(block, small, sizeof small));
    memcpy(block, menu_a, sizeof block);
    put16(block, BMapFormat_Indexed8);
    assert(!adjust(OWNER_A, 7, block, MENU_BMAP_BYTES));
    put16(block, BMapFormat_Mask8);
    assert(!adjust(OWNER_A, 7, block, MENU_BMAP_BYTES - 1));
    put32(block + 20, 80);
    assert(!adjust(OWNER_A, 7, block, MENU_BMAP_BYTES));
    assert(!memcmp(block + HEADER, menu_a + HEADER, MENU_ROWS));
    style_reset();

    /* The end of disk mode checks the theme file's header: the same file
     * keeps the index, another under the id or none ends it. */
    look_state.active.header_crc = le32(b.bytes + RVT_HEADER_CRC);
    look_volume_changed();
    assert(replacement_bytes(OWNER_A, 5) == MENU_BMAP_BYTES && live_count == 4);
    struct built swapped;
    asset_table = four, asset_table_count = 4;
    build_file_at(&swapped, PROVIDED_ID, "Other", RVT_HEADER_BYTES, NULL, 0, NULL, 0, NULL, NULL);
    asset_table = NULL, asset_table_count = 0;
    provided = &swapped;
    look_volume_changed();
    assert(!replacement_bytes(OWNER_A, 5));
    assert(!live_count && !allocations && !open_files);
    assert(apply(&b, assets) && live_count == 4);
    unreadable = PROVIDED_ID;
    look_volume_changed();
    assert(!live_count && !allocations && !open_files);
    unreadable = NULL;
    provided = &b;

    /* The switch: a look with bitmaps holds the index; the stock look, a
     * recovered switch and unload free it; a switch that fails before it
     * changes anything keeps the index of the look that stays. */
    struct look theme = {.id = "test", .name = "Test", .appearances = RVT_APPEARANCE_DARK, .dark = 1};
    struct look stock = {0};
    with_assets(four, 4, &b);
    assert(switch_to(&b, &theme) == SWITCH_OK && live_count == 4 && allocations == 1);
    assert(switch_apply(NULL, &stock, NULL) == SWITCH_OK && !live_count && !allocations);
    assert(switch_to(&b, &theme) == SWITCH_OK && live_count == 4);
    struct built failing;
    memcpy(bad, four, sizeof bad);
    bad[0].kind = RVT_EXTENSION_KINDS; /* no records: a bad table alone */
    with_assets(bad, 4, &failing);
    assert(switch_to(&failing, &theme) == SWITCH_LOAD_FAILED && live_count == 4 && allocations == 1);
    uint8_t paletted[2 * 4 + 4 * 2] = {9, 9, 9, 255, 8, 8, 8, 255, 0, 1, 0, 2, 1, 0, 1, 0};
    struct rec broken = pixels(PALETTED, paletted);
    asset_table = four, asset_table_count = 4;
    build(&failing, &broken, 1, NULL);
    asset_table = NULL, asset_table_count = 0;
    assert(switch_to(&failing, &theme) == SWITCH_RECOVERED && !live_count && !allocations);
    assert(switch_to(&b, &theme) == SWITCH_OK && live_count == 4);
    switch_restore_all();
    assert(!live_count && !allocations && bank_is_stock());
    provided = NULL;
    memset(&look_state.active, 0, sizeof look_state.active);
}

static void scan_contracts(void)
{
    static const char *const names[] = {"zeta.rvt", "ALPHA.RVT", "Mid.Rvt", "notes.txt", "BAD~1.RVT",
                                        "classic.rvt", "alpha.rvt", "broken.rvt", "-dash.rvt"};
    struct theme_entry entries[64];
    int overflow = 1;
    folder = names;
    folder_count = sizeof names / sizeof *names;
    unreadable = "broken";
    /* Counted: zeta, ALPHA, Mid, alpha, broken; listed in order without the
     * duplicate and the file that does not open. */
    assert(theme_count() == 5);
    uint32_t n = theme_scan(entries, 64, &overflow);
    assert(n == 3 && !overflow);
    assert(!strcmp(entries[0].id, "alpha") && !strcmp(entries[1].id, "mid") && !strcmp(entries[2].id, "zeta"));
    assert(!strcmp(entries[0].name, "Named"));

    /* More files than the list holds: the first in order are kept and the
     * overflow reported; a smaller capacity caps the same way. */
    static char many_names[70][16];
    static const char *many[70];
    for (unsigned i = 0; i < 70; i++) {
        snprintf(many_names[i], sizeof many_names[i], "t%02u.rvt", 69 - i);
        many[i] = many_names[i];
    }
    folder = many;
    folder_count = 70;
    unreadable = NULL;
    assert(theme_count() == 70);
    n = theme_scan(entries, 64, &overflow);
    assert(n == 64 && overflow && !strcmp(entries[0].id, "t00") && !strcmp(entries[63].id, "t63"));
    n = theme_scan(entries, 3, &overflow);
    assert(n == 3 && overflow && !strcmp(entries[2].id, "t02"));

    /* Opening a theme: by id, in the appearance asked for or the one it has;
     * the reserved id and a missing file are unavailable. */
    struct rv_file *file;
    struct look look;
    struct pack_place place;
    char id[RVT_ID_BYTES] = "t05";
    assert(theme_open(id, 1, &file, &look, &place) == THEME_OK && look.dark && file);
    file_close(NULL, file);
    assert(theme_open(id, 0, &file, &look, &place) == THEME_NOT_IN_APPEARANCE && look.dark && file);
    assert(!strcmp(look.id, "t05") && look.appearances == RVT_APPEARANCE_DARK);
    assert(place.offset == RVT_HEADER_BYTES && place.bytes == opened_theme.size - RVT_HEADER_BYTES);
    file_close(NULL, file);
    strcpy(id, "classic");
    assert(theme_open(id, 1, &file, &look, &place) == THEME_UNAVAILABLE && !file);
    strcpy(id, "broken");
    unreadable = "broken";
    assert(theme_open(id, 1, &file, &look, &place) == THEME_UNAVAILABLE && !file);
    assert(!open_files);
}

/* A style prefix with the given role colours (RGB), flags and one accent palette. */
static void prefix_custom(uint8_t *p, const uint8_t roles[RVT_ROLES][3], uint32_t flags,
                          unsigned palette, const uint8_t accent[RVT_ACCENT_SLOTS][3])
{
    prefix_plain(p);
    for (unsigned i = 0; i < RVT_ROLES; i++)
        memcpy(p + RVT_STYLE_COLORS + i * 4, roles[i], 3);
    put32(p + RVT_STYLE_FLAGS, flags);
    if (accent)
        for (unsigned i = 0; i < RVT_ACCENT_SLOTS; i++)
            memcpy(p + RVT_STYLE_PALETTES + (palette * RVT_ACCENT_SLOTS + i) * 4, accent[i], 3);
}

static void expect_text(const uint8_t *color, uint32_t view, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t mapped[4];
    const uint8_t *out = map_text(color, view, mapped);
    assert(out[0] == r && out[1] == g && out[2] == b && out[3] == color[3]);
}

static void expect_fill(const uint8_t *color, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t mapped[4];
    const uint8_t *out = map_fill(color, mapped);
    assert(out[0] == r && out[1] == g && out[2] == b && out[3] == color[3]);
}

static void expect_background(uint32_t r, uint32_t g, uint32_t b, uint32_t rgb565)
{
    assert(clear_color(0x10000, 0x10000, 0x10000, 0x4321, NULL) == 0x600d);
    assert(passed[0] == r && passed[1] == g && passed[2] == b && passed[3] == 0x4321);
    assert(reflection_blend(0xabcd, 0xffff, 0x8000, NULL) == 0x600d);
    assert(passed[0] == 0xabcd && passed[1] == rgb565 && passed[2] == 0x8000);
}

static void expect_bar(const uint8_t *color, enum bar_slot slot, unsigned well, uint8_t r, uint8_t g,
                       uint8_t b)
{
    uint8_t mapped[4];
    const uint8_t *out = map_bar_label(color, slot, well, mapped);
    assert(out[0] == r && out[1] == g && out[2] == b && out[3] == color[3]);
}

static void expect_day(const uint8_t *color, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t mapped[4];
    const uint8_t *out = map_calendar_day(color, mapped);
    assert(out[0] == r && out[1] == g && out[2] == b && out[3] == color[3]);
}

static const uint8_t white[] = {255, 255, 255, 173}, light_gray[] = {0xee, 0xee, 0xee, 9},
                     pale_gray[] = {0xb0, 0xb0, 0xb0, 10}, below_gray[] = {0xaf, 0xaf, 0xaf, 11},
                     cover_border[] = {0x7e, 0x81, 0x83, 127}, accent_fill[] = {0x23, 0x6c, 0xc5, 91},
                     accent_text[] = {0x31, 0x85, 0xd0, 77}, black[] = {0, 0, 0, 123},
                     dim_text[] = {0x80, 0x80, 0x80, 5}, light_text[] = {0x81, 0x81, 0x81, 6},
                     inactive_day[] = {0x66, 0x66, 0x66, 255};
enum { ORDINARY_VIEW = 0x0dad0001 };
#define DARK16(c) ((uint32_t)(c) * 0x10000 / 255)

/* Classic with Theme Default or Blue is exact in both appearances. */
static void classic_mapping(void)
{
    for (unsigned a = ACCENT_DEFAULT; a <= ACCENT_FIRST; a++) {
        style_set_accent(a);
        for (int dark = 0; dark < 2; dark++) {
            style_set_dark(dark);
            for (unsigned c = 0; c < 256; c++) {
                const uint8_t native_gray[] = {(uint8_t)c, (uint8_t)c, (uint8_t)c, 33};
                uint8_t fill_shade = (uint8_t)(dark && c >= 0xb0 ? 0x18 + (255 - c) / 4 : c);
                uint8_t text_shade = (uint8_t)(dark && c <= 0x80 ? 0xe8 - c / 2 : c);
                expect_fill(native_gray, fill_shade, fill_shade, fill_shade);
                expect_text(native_gray, ORDINARY_VIEW, text_shade, text_shade, text_shade);
            }
            expect_text(Palette_White, ORDINARY_VIEW, 255, 255, 255);
            expect_text(white, ORDINARY_VIEW, 255, 255, 255);
            if (dark) {
                expect_fill(cover_border, 0x38, 0x38, 0x38);
                expect_fill(accent_fill, 0, 161, 255);
                expect_text(accent_fill, ORDINARY_VIEW, 0, 161, 255);
                expect_text(accent_text, ORDINARY_VIEW, 0, 161, 255);
                expect_background(DARK16(0x18), DARK16(0x18), DARK16(0x18), 0x18c3);
            } else {
                expect_fill(cover_border, 0x7e, 0x81, 0x83);
                expect_fill(accent_fill, 0x23, 0x6c, 0xc5);
                expect_text(accent_fill, ORDINARY_VIEW, 0x23, 0x6c, 0xc5);
                expect_text(accent_text, ORDINARY_VIEW, 0x31, 0x85, 0xd0);
                expect_background(0x10000, 0x10000, 0x10000, 0xffff);
            }
        }
    }
    style_set_dark(0);
    style_set_accent(ACCENT_DEFAULT);
}

static void accent_mapping(void)
{
    /* Purple recolours accents but leaves Classic light surfaces and text alone. */
    style_set_accent(ACCENT_FIRST + 7);
    assert(!strcmp(accent_labels[ACCENT_FIRST + 7], "Purple"));
    style_set_dark(0);
    expect_fill(white, 255, 255, 255);
    expect_fill(light_gray, 0xee, 0xee, 0xee);
    expect_fill(cover_border, 0x7e, 0x81, 0x83);
    expect_text(black, ORDINARY_VIEW, 0, 0, 0);
    expect_text(dim_text, ORDINARY_VIEW, 0x80, 0x80, 0x80);
    expect_text(Palette_White, ORDINARY_VIEW, 255, 255, 255);
    expect_fill(accent_fill, 137, 68, 171);
    expect_text(accent_fill, ORDINARY_VIEW, 137, 68, 171);
    expect_text(accent_text, ORDINARY_VIEW, 137, 68, 171);
    expect_background(0x10000, 0x10000, 0x10000, 0xffff);
    style_set_dark(1);
    expect_fill(white, 0x18, 0x18, 0x18);
    expect_fill(accent_fill, 150, 78, 188);
    expect_text(accent_fill, ORDINARY_VIEW, 150, 78, 188);
    expect_text(accent_text, ORDINARY_VIEW, 218, 143, 255);
    expect_background(DARK16(0x18), DARK16(0x18), DARK16(0x18), 0x18c3);
    /* Native-colour views keep their colours under every accent. */
    expect_text(accent_text, RES_Search_Input_Text, 0x31, 0x85, 0xd0);
    expect_text(black, RES_Search_Input_Text, 0, 0, 0);
    /* Pink, Red, Orange, Green: the accent text presets, light and dark. */
    static const struct {
        const char *label;
        uint8_t light[3], dark[3];
    } presets[] = {
        {"Pink", {196, 30, 86}, {255, 100, 145}},
        {"Red", {208, 32, 40}, {255, 105, 97}},
        {"Orange", {184, 72, 0}, {255, 159, 10}},
        {"Green", {30, 128, 58}, {48, 209, 88}},
    };
    for (unsigned i = 0; i < 4; i++) {
        unsigned a = ACCENT_FIRST;
        while (strcmp(accent_labels[a], presets[i].label))
            a++;
        style_set_accent(a);
        style_set_dark(0);
        expect_text(accent_text, ORDINARY_VIEW, presets[i].light[0], presets[i].light[1], presets[i].light[2]);
        style_set_dark(1);
        expect_text(accent_text, ORDINARY_VIEW, presets[i].dark[0], presets[i].dark[1], presets[i].dark[2]);
    }
    /* An accent past the list is Theme Default. */
    style_set_accent(ACCENTS);
    assert(style_accent() == ACCENT_DEFAULT);
    style_set_dark(0);
}

static void theme_mapping(void)
{
    uint8_t prefix[RVT_STYLE_BYTES];
    static const uint8_t sepia[RVT_ROLES][3] = {
        [RVT_ROLE_BACKGROUND] = {250, 240, 220}, [RVT_ROLE_TEXT] = {60, 40, 20},
        [RVT_ROLE_ACCENT_FILL] = {160, 80, 30},  [RVT_ROLE_ACCENT_TEXT] = {140, 70, 20},
        [RVT_ROLE_ON_ACCENT] = {20, 10, 0},      [RVT_ROLE_SELECTION] = {160, 80, 30},
        [RVT_ROLE_COVER_BORDER] = {120, 100, 80},
    };
    static const uint8_t orange[RVT_ACCENT_SLOTS][3] = {
        [RVT_SLOT_ACCENT_FILL] = {190, 76, 0}, [RVT_SLOT_ACCENT_TEXT] = {255, 159, 10},
        [RVT_SLOT_SELECTION] = {190, 76, 0}, [RVT_SLOT_ON_ACCENT] = {255, 255, 255},
        [RVT_SLOT_ICON_TINT] = {255, 159, 10},
    };
    prefix_custom(prefix, sepia, RVT_FLAG_FLAT_NOW_PLAYING, RVT_ACCENT_ORANGE, orange);
    style_set(prefix);
    tint_refresh();
    /* A theme's colours hold in both appearances. */
    for (int dark = 0; dark < 2; dark++) {
        style_set_dark(dark);
        expect_fill(white, 250, 240, 220);
        expect_fill(light_gray, 250 - 17, 240 - 17, 220 - 17);
        expect_fill(pale_gray, 250 - 79, 240 - 79, 220 - 79);
        expect_fill(below_gray, 0xaf, 0xaf, 0xaf);
        expect_fill(cover_border, 120, 100, 80);
        expect_fill(accent_fill, 160, 80, 30);
        expect_text(black, ORDINARY_VIEW, 60 - 32, 40 - 32, 0);
        expect_text(dim_text, ORDINARY_VIEW, 60 + 96, 40 + 96, 20 + 96);
        expect_text(light_text, ORDINARY_VIEW, 0x81, 0x81, 0x81);
        expect_text(accent_fill, ORDINARY_VIEW, 160, 80, 30);
        expect_text(accent_text, ORDINARY_VIEW, 140, 70, 20);
        /* Selected-row text follows on_accent by pointer; other white text
         * takes the text role on this light background. */
        expect_text(Palette_White, ORDINARY_VIEW, 20, 10, 0);
        expect_text(white, ORDINARY_VIEW, 60, 40, 20);
        expect_text(light_gray, ORDINARY_VIEW, 0xee, 0xee, 0xee);
        expect_background(DARK16(250), DARK16(240), DARK16(220), (31u << 11) | (60u << 5) | 27u);
        /* Retained-light views stay unmapped. */
        expect_text(black, RES_Settings_DateTime_NumericDay, 0, 0, 0);
        expect_text(Palette_White, RES_Settings_DateTime_NumericDay, 255, 255, 255);
        assert(stock_background(RES_NowPlaying_White_Background_View) &&
               !stock_background(ORDINARY_VIEW));
        /* A view drawn inside another's background fill gives the outer
         * rule back. */
        int outer = enter_background(RES_NowPlaying_White_Background_View);
        assert(!outer && stock_fill);
        int inner = enter_background(ORDINARY_VIEW);
        assert(inner && !stock_fill);
        stock_fill = inner;
        assert(stock_fill);
        stock_fill = outer;
        assert(!stock_fill);
        /* A fill onto a coverage mask keeps its colour: white clears a text
         * bitmap; onto a pixel surface it is mapped. */
        static uint8_t surface[0x20];
        uint8_t mapped[4];
        *(uint16_t *)(surface + Surface_format) = BMapFormat_Mask8 | BMapFormat_ByteSwapped;
        assert(fill_color(surface, white, mapped) == white);
        *(uint16_t *)(surface + Surface_format) = BMapFormat_Mask4;
        assert(fill_color(surface, white, mapped) == white);
        *(uint16_t *)(surface + Surface_format) = BMapFormat_RGB565;
        assert(fill_color(surface, white, mapped) == mapped && mapped[0] == 250);
        *(uint16_t *)(surface + Surface_format) = BMapFormat_Indexed8;
        assert(fill_color(surface, white, mapped) == mapped);
    }
    /* Accents on a theme use its authored palette, icon tint included. */
    unsigned a = ACCENT_FIRST;
    while (strcmp(accent_labels[a], "Orange"))
        a++;
    style_set_accent(a);
    expect_fill(accent_fill, 190, 76, 0);
    expect_text(accent_text, ORDINARY_VIEW, 255, 159, 10);
    expect_fill(white, 250, 240, 220);
    assert(style_color(RVT_ROLE_ICON_TINT)[0] == 255 && style_color(RVT_ROLE_ICON_TINT)[1] == 159);
    style_set_accent(ACCENT_DEFAULT);
    expect_fill(accent_fill, 160, 80, 30);
    assert(!style_color(RVT_ROLE_ICON_TINT)[0]); /* the prefix's own, zero here */

    /* A dark theme inverts the stock shades and clamps the channels. */
    static const uint8_t night[RVT_ROLES][3] = {
        [RVT_ROLE_BACKGROUND] = {250, 10, 40}, [RVT_ROLE_TEXT] = {255, 250, 100},
        [RVT_ROLE_ACCENT_FILL] = {160, 80, 30}, [RVT_ROLE_ACCENT_TEXT] = {140, 70, 20},
        [RVT_ROLE_ON_ACCENT] = {20, 10, 0},    [RVT_ROLE_SELECTION] = {160, 80, 30},
        [RVT_ROLE_COVER_BORDER] = {120, 100, 80},
    };
    prefix_custom(prefix, night, 0, 0, NULL);
    style_set(prefix);
    tint_refresh();
    style_set_dark(0);
    expect_fill(white, 250, 10, 40);
    expect_fill(pale_gray, 255, 10 + 19, 40 + 19);
    expect_text(black, ORDINARY_VIEW, 255, 250, 100);
    expect_text(dim_text, ORDINARY_VIEW, 255 - 64, 250 - 64, 100 - 64);
    expect_text(white, ORDINARY_VIEW, 255, 255, 255); /* a dark background keeps white */
    expect_background(DARK16(250), DARK16(10), DARK16(40), (31u << 11) | (2u << 5) | 5u);
    style_reset();
    style_set_dark(0);
    expect_fill(white, 255, 255, 255);
}

/* Bar labels: the highlighted one takes on_accent whenever anything is
 * themed; the others take the colour derived from the well art under a
 * theme; the Now Playing bar follows a theme only. */
static void bar_mapping(void)
{
    static const uint8_t native[] = {255, 255, 255, 201};
    uint8_t prefix[RVT_STYLE_BYTES];
    style_reset();
    style_set_dark(0);
    style_set_accent(ACCENT_FIRST + 7); /* Purple on Classic: white on_accent, stock wells */
    expect_bar(native, BAR_SELECTED, WELL_BLACK, 255, 255, 255);
    expect_bar(native, BAR_LABEL, WELL_BLACK, 255, 255, 255);
    expect_bar(native, BAR_OTHER, WELL_WHITE, 255, 255, 255);
    assert(!tint_well_letter_color(WELL_BLACK));
    style_set_accent(ACCENT_DEFAULT);
    /* Under a theme the wells' letters contrast with the well art: bitmap 1's
     * centre is the light colour, so the dark text role. */
    static const uint8_t sepia[RVT_ROLES][3] = {
        [RVT_ROLE_BACKGROUND] = {250, 240, 220}, [RVT_ROLE_TEXT] = {60, 40, 20},
        [RVT_ROLE_ACCENT_FILL] = {160, 80, 30},  [RVT_ROLE_ACCENT_TEXT] = {140, 70, 20},
        [RVT_ROLE_ON_ACCENT] = {20, 10, 0},      [RVT_ROLE_SELECTION] = {160, 80, 30},
        [RVT_ROLE_COVER_BORDER] = {120, 100, 80},
    };
    prefix_custom(prefix, sepia, 0, 0, NULL);
    style_set(prefix);
    tint_refresh();
    for (unsigned well = 0; well < WELLS; well++) {
        expect_bar(native, BAR_SELECTED, well, 20, 10, 0);
        expect_bar(native, BAR_LABEL, well, 60, 40, 20);
        expect_bar(native, BAR_OTHER, well, 255, 255, 255);
    }
    assert(!tint_well_letter_color(WELLS));
    /* The Search field text follows the field art the same way; the caret's
     * accent blue takes accent_text like any other text. */
    expect_text(black, RES_Search_Input_Text, 60, 40, 20);
    expect_text(dim_text, RES_Search_Input_Text, 60, 40, 20);
    expect_text(accent_text, RES_Search_Input_Text, 140, 70, 20);
    /* Labels over listed art take the contrast colour, also over the
     * white-text mapping; other views do not. */
    static const uint8_t status[] = {0xd3, 0xd3, 0xd3, 255};
    expect_text(status, RES_NowPlaying_StatusBar_Text_View, 60, 40, 20);
    expect_text(white, RES_NowPlaying_StatusBar_Text_View, 60, 40, 20);
    expect_text(status, ORDINARY_VIEW, 0xd3, 0xd3, 0xd3);
    /* Clear art keeps the stock colour. */
    uint8_t *center = catalog_data(PALETTED) + at_data(PALETTED) + 3;
    *center = 0;
    tint_refresh();
    assert(!tint_field_text_color() && !tint_well_letter_color(WELL_WHITE) &&
           !tint_contrast_text_color(RES_NowPlaying_StatusBar_Text_View));
    expect_text(status, RES_NowPlaying_StatusBar_Text_View, 0xd3, 0xd3, 0xd3);
    expect_text(dim_text, RES_Search_Input_Text, 60 + 96, 40 + 96, 20 + 96);
    *center = 255;
    tint_refresh();
    /* Calendar days: the shown month's on the active cell's contrast colour,
     * a neighbouring month's mixed halfway toward the cell, weekday names
     * halfway toward the page, the selected day in on_accent. */
    uint8_t out[4], half[4];
    expect_day(black, 60, 40, 20);
    static const uint8_t light_rgba[4] = {240, 244, 247, 255}; /* LIGHT, read out of BGRA */
    color_mix(half, style_color(RVT_ROLE_TEXT), light_rgba, 128);
    expect_day(inactive_day, half[0], half[1], half[2]);
    color_mix(half, style_color(RVT_ROLE_TEXT), style_color(RVT_ROLE_BACKGROUND), 128);
    assert(tint_calendar_text_color(CALENDAR_HEADER, out) == out && !memcmp(out, half, 4));
    assert(!tint_calendar_text_color(CALENDAR_HILITE, out));
    expect_day(white, 20, 10, 0);
    /* Classic keeps every native colour. */
    style_reset();
    expect_day(black, 0, 0, 0);
    expect_day(white, 255, 255, 255);
    assert(!tint_calendar_text_color(CALENDAR_HEADER, out) && !tint_field_text_color());
    expect_text(black, RES_Search_Input_Text, 0, 0, 0);
}

/* Tinted art: a highlighted view draws a tinted paletted bitmap in on_accent
 * and puts the tint back; tinted ARGB art keeps its tint; masks are coloured
 * while drawing; an unknown bitmap takes the icon tint role under a theme. */
static void tint_contracts(void)
{
    uint8_t prefix[RVT_STYLE_BYTES], tint[RVT_CATALOG_MAX] = {0};
    static const uint8_t roles[RVT_ROLES][3] = {
        [RVT_ROLE_BACKGROUND] = {1, 2, 3},   [RVT_ROLE_TEXT] = {4, 5, 6},
        [RVT_ROLE_ACCENT_FILL] = {7, 8, 9},  [RVT_ROLE_ACCENT_TEXT] = {40, 50, 60},
        [RVT_ROLE_ON_ACCENT] = {88, 0, 0},   [RVT_ROLE_SELECTION] = {11, 12, 13},
        [RVT_ROLE_COVER_BORDER] = {14, 15, 16}, [RVT_ROLE_ICON_TINT] = {10, 20, 30},
    };
    tint[MASK] = RVT_TINT_ROLE + RVT_ROLE_ICON_TINT;
    tint[SECOND] = RVT_TINT_ROLE + RVT_ROLE_ICON_TINT;
    tint[ARGB] = RVT_TINT_ROLE + RVT_ROLE_SELECTION;
    tint[WIDE] = RVT_TINT_ROLE + RVT_ROLE_TEXT;
    prefix_custom(prefix, roles, 0, 0, NULL);
    memcpy(prefix + RVT_STYLE_TINTS, tint, RVT_CATALOG_MAX);
    style_set(prefix);
    tint_refresh();
    assert(!memcmp(catalog_data(SECOND) + at_data(SECOND), (uint8_t[]){30, 20, 10, 255}, 4));
    assert(!memcmp(catalog_data(ARGB) + HEADER, (uint8_t[]){13, 12, 11, 255}, 4) &&
           catalog_data(ARGB)[HEADER + 31] == 0);
    /* Views and contexts as the hooks see them. */
    static uint8_t view[0x200], context[0x40], bitmap[0x20], foreign[0x20], other_context[0x40];
    const uint8_t *pixels = catalog_data(SECOND) + HEADER;
    memcpy(bitmap + Bitmap_pixels, &pixels, sizeof pixels);
    uint8_t *bitmap_p = bitmap;
    memcpy(context + GraphicsContext_bitmap, &bitmap_p, sizeof bitmap_p);
    uint8_t *context_p = context;
    memcpy(view + BitmapView_context, &context_p, sizeof context_p);
    assert(tint_mask_color(view)[0] == 10);
    *(uint32_t *)(view + View_flags) = ViewFlags_Highlighted;
    assert(tint_mask_color(view)[0] == 88);
    tint_highlight_begin(view, context);
    assert(catalog_data(SECOND)[at_data(SECOND) + 2] == 88 && catalog_data(SECOND)[at_data(SECOND) + 6] == 88);
    tint_highlight_end();
    assert(catalog_data(SECOND)[at_data(SECOND) + 2] == 10);
    /* A blit inside the two halves of another: the first asset stays the
     * highlighted one and the second keeps its tint. */
    static uint8_t wide_bitmap[0x20], wide_context[0x40];
    const uint8_t *wide_pixels = catalog_data(WIDE) + HEADER;
    memcpy(wide_bitmap + Bitmap_pixels, &wide_pixels, sizeof wide_pixels);
    uint8_t *wide_bitmap_p = wide_bitmap;
    memcpy(wide_context + GraphicsContext_bitmap, &wide_bitmap_p, sizeof wide_bitmap_p);
    uint8_t wide_tinted[4];
    memcpy(wide_tinted, catalog_data(WIDE) + at_data(WIDE), 4);
    tint_highlight_begin(view, context);
    tint_highlight_begin(view, wide_context);
    assert(highlighted == SECOND + 1 && !memcmp(catalog_data(WIDE) + at_data(WIDE), wide_tinted, 4));
    tint_highlight_end();
    assert(catalog_data(SECOND)[at_data(SECOND) + 2] == 10 && !highlighted);
    tint_highlight_begin(view, wide_context);
    assert(highlighted == WIDE + 1 && catalog_data(WIDE)[at_data(WIDE) + 2] == 88);
    tint_highlight_end();
    assert(!memcmp(catalog_data(WIDE) + at_data(WIDE), wide_tinted, 4));
    /* A pointer into a bitmap's middle and a bitmap outside the bank are no
     * catalog bitmap: an extension's own icon, drawn in the icon tint role. */
    pixels = catalog_data(SECOND) + HEADER + 4;
    memcpy(bitmap + Bitmap_pixels, &pixels, sizeof pixels);
    assert(tint_mask_color(view)[0] == 88);
    pixels = foreign;
    memcpy(bitmap + Bitmap_pixels, &pixels, sizeof pixels);
    assert(tint_mask_color(view)[0] == 88);
    *(uint32_t *)(view + View_flags) = 0;
    assert(tint_mask_color(view)[0] == 10);
    pixels = catalog_data(PALETTED) + HEADER;
    memcpy(bitmap + Bitmap_pixels, &pixels, sizeof pixels);
    assert(!tint_mask_color(view));
    /* ARGB art is not rewritten for a highlight. */
    pixels = catalog_data(ARGB) + HEADER;
    memcpy(bitmap + Bitmap_pixels, &pixels, sizeof pixels);
    *(uint32_t *)(view + View_flags) = ViewFlags_Highlighted;
    tint_highlight_begin(view, context);
    assert(catalog_data(ARGB)[HEADER + 2] == 11);
    tint_highlight_end();
    /* A calendar marker turns on_accent over the selected day's cell art only. */
    static uint8_t cell[0x200];
    pixels = catalog_data(SECOND) + HEADER;
    memcpy(bitmap + Bitmap_pixels, &pixels, sizeof pixels);
    memcpy(cell + CalendarDayCell_art, &context_p, sizeof context_p);
    const uint8_t *hilite = catalog_data(PALETTED) + HEADER;
    static uint8_t hilite_bitmap[0x20];
    memcpy(hilite_bitmap + Bitmap_pixels, &hilite, sizeof hilite);
    uint8_t *hilite_p = hilite_bitmap;
    memcpy(other_context + GraphicsContext_bitmap, &hilite_p, sizeof hilite_p);
    uint8_t *other_p = other_context;
    memcpy(cell + CalendarDayCell_art, &other_p, sizeof other_p);
    tint_marker_highlight_begin(cell, context);
    assert(catalog_data(SECOND)[at_data(SECOND) + 2] == 88);
    tint_highlight_end();
    memcpy(cell + CalendarDayCell_art, &context_p, sizeof context_p);
    tint_marker_highlight_begin(cell, context);
    assert(catalog_data(SECOND)[at_data(SECOND) + 2] == 10);
    tint_highlight_end();
    style_reset();
    assert(!tint_mask_color(view));
    reset_bank();
}

/* A flat look copies only a 128 by 128 RGB565 image with packed rows into the
 * colour buffer and skips the edge lines; any other image, or a look without
 * the flag, keeps the stock drawing. The copy itself needs the 32-bit Bitmap
 * layout (the pixel pointer and the stride overlap on a 64-bit host), so the
 * emulator covers it. */
static void cover_contracts(void)
{
    uint8_t prefix[RVT_STYLE_BYTES];
    static const uint8_t roles[RVT_ROLES][3] = {{0}};
    int32_t bounds[4] = {0, 0, 128, 128};
    memcpy(cover_image + Bitmap_bounds, bounds, sizeof bounds);
    *(uint16_t *)(cover_image + Bitmap_format) = BMapFormat_RGB565;
    *(uint32_t *)(cover_image + Bitmap_stride) = 256;
    assert(copyable(cover_image));
    *(uint32_t *)(cover_image + Bitmap_stride) = 512;
    assert(!copyable(cover_image));
    *(uint32_t *)(cover_image + Bitmap_stride) = 256;
    *(uint16_t *)(cover_image + Bitmap_format) = BMapFormat_BGRA8888;
    assert(!copyable(cover_image));
    *(uint16_t *)(cover_image + Bitmap_format) = BMapFormat_RGB565;
    bounds[2] = 100;
    memcpy(cover_image + Bitmap_bounds, bounds, sizeof bounds);
    assert(!copyable(cover_image));
    prefix_custom(prefix, roles, RVT_FLAG_FLAT_NOW_PLAYING, 0, NULL);
    style_set(prefix);
    assert(flat_cover_edge_lines(2, NULL) == 0);
    drawing_view = &cover_view; /* inside the view's draw, but the image is not copyable */
    assert(flat_cover_renderer_draw(1, NULL) == 0x600d && passed[0] == 1);
    drawing_view = NULL;
    assert(flat_cover_renderer_draw(1, NULL) == 0x600d);
    style_reset();
    assert(flat_cover_renderer_draw(1, NULL) == 0x600d && flat_cover_edge_lines(2, NULL) == 0x600d);
}


/* The latch: the first action binds the watch and holds presses off until
 * the message loop runs dry after the switch, which also ends the watch; a
 * watch still bound goes at unload; without memory for the watch the switch
 * runs unlatched. */
static void latch_contracts(void)
{
    int before = binds;
    assert(latch_begin() && binds == before + 1 && !latch_begin());
    next_result = 0;
    assert(process_message(1, 0, NULL) == 0 && binds == before + 1 && latched); /* still loading */
    latch_finish();
    next_result = 0x600d;
    assert(process_message(1, 0, NULL) == 0x600d && latched); /* a message handled */
    next_result = 0;
    assert(process_message(1, 0, NULL) == 0 && !latched && binds == before);
    assert(latch_begin() && latched && binds == before + 1);
    latch_finish();
    latch_unload();
    assert(binds == before && !latched);
    bind_failures_at = binds + 1;
    assert(latch_begin() && binds == before && !latched);
    bind_failures_at = 0;
    latch_finish();
    latch_unload();
    next_result = 0x600d;
}

/* The Theme list: Classic, the folder's themes in order, the active theme
 * kept in order when the list is full, and a last row for the overflow or
 * the trouble. */
static void picker_contracts(void)
{
    static char many_names[70][16];
    static const char *many[70];
    const char *detail = (const char *)1;
    for (unsigned i = 0; i < 70; i++) {
        snprintf(many_names[i], sizeof many_names[i], "t%02u.rvt", i);
        many[i] = many_names[i];
    }
    folder = many;
    folder_count = 70;
    unreadable = NULL;
    provided = NULL;
    memset(&look_state.active, 0, sizeof look_state.active);
    strcpy(look_state.active.id, "t99");
    strcpy(look_state.active.name, "Last");
    assert(theme_picker_rows(NULL) == 1 + 64 + 1 && allocations == 1 && count == 64);
    assert(!strcmp(theme_picker_row(NULL, 0, &detail), "Classic") && !detail);
    assert(!strcmp(theme_picker_row(NULL, 1, &detail), "Named") && !strcmp(entries[0].id, "t00"));
    assert(!strcmp(entries[62].id, "t62") && !strcmp(entries[63].id, "t99"));
    assert(!strcmp(theme_picker_row(NULL, 64, &detail), "Last"));
    assert(!strcmp(theme_picker_row(NULL, 65, &detail), "Showing 64 themes"));
    assert(theme_picker_initial(NULL) == 64 && !theme_picker_select(NULL, 65) && !module_loaded);
    theme_picker_closed(NULL);
    assert(!allocations && !count && !overflow && !scanned);
    /* Sorting inside the list: inserted in place, the last entry dropped. */
    strcpy(look_state.active.id, "t005");
    assert(theme_picker_rows(NULL) == 66 && !strcmp(entries[1].id, "t005") && !strcmp(entries[2].id, "t01"));
    assert(!strcmp(entries[63].id, "t62") && theme_picker_initial(NULL) == 2);
    theme_picker_closed(NULL);
    /* Listed already: nothing moves. */
    strcpy(look_state.active.id, "t10");
    assert(theme_picker_rows(NULL) == 66 && !strcmp(entries[10].id, "t10") && !strcmp(entries[63].id, "t63"));
    assert(theme_picker_initial(NULL) == 11);
    theme_picker_closed(NULL);
    /* Room for every theme: the active one is among them or it is Classic's turn. */
    folder_count = 5;
    strcpy(look_state.active.id, "zzz");
    assert(theme_picker_rows(NULL) == 6 && theme_picker_initial(NULL) == 0);
    theme_picker_closed(NULL);
    strcpy(look_state.active.id, "t03");
    assert(theme_picker_rows(NULL) == 6 && theme_picker_initial(NULL) == 4);
    theme_picker_closed(NULL);
    /* An empty folder lists Classic alone; no memory for the list says so. */
    folder_count = 0;
    assert(theme_picker_rows(NULL) == 1 && !allocations);
    theme_picker_closed(NULL);
    folder_count = 5;
    alloc_fails = 1;
    assert(theme_picker_rows(NULL) == 2 && !strcmp(theme_picker_row(NULL, 1, &detail), "Not enough memory"));
    theme_picker_closed(NULL);
    assert(!allocations);
    folder_count = 0;
    memset(&look_state.active, 0, sizeof look_state.active);
}

/* The draw hooks bind while a look is on; a bind that fails leaves none bound. */
static void bind_contracts(void)
{
    assert(render_bind(1) && binds == (int)THEMED_HOOKS);
    assert(render_bind(1) && binds == (int)THEMED_HOOKS);
    assert(render_bind(0) && binds == 0);
    bind_failures_at = 5;
    assert(!render_bind(1) && binds == 0);
    bind_failures_at = 0;
    assert(render_bind(0) && binds == 0);
}

/* The whole of a switch from main's side: a saved theme whose file is gone
 * falls back to Classic in the saved appearance, the settings keep the
 * appearance and drop the id, the draw hooks and the latch bind, and unload
 * puts everything back. */
static void look_contracts(void)
{
    char id[RVT_ID_BYTES] = "flat";
    memset(&look_state, 0, sizeof look_state);
    style_reset();
    style_set_dark(0);
    style_set_accent(ACCENT_DEFAULT);
    strcpy(theme_id_setting, "flat");
    setting_ints[KEY_DARK] = 1;
    setting_ints[KEY_ACCENT] = ACCENT_DEFAULT;
    setting_ints[KEY_AVAILABLE] = 1;
    folder_count = 0;
    unreadable = "flat";
    redraws = 0;
    look_switch(id, 1, ACCENT_DEFAULT, PANE_THEME);
    assert(!look_state.active.id[0] && look_state.active.dark && style_dark() && !style_from_theme());
    assert(setting_ints[KEY_DARK] == 1 && !strcmp(theme_id_setting, "flat") && setting_ints[KEY_AVAILABLE] == 1);
    assert(!strcmp(look_problem(PANE_THEME), "Theme unavailable") && !look_problem(PANE_DARK));
    assert(look_dark_available() && binds == (int)THEMED_HOOKS + 1); /* the draw hooks and the latch */
    assert(canvas_colors && redraws == 1 && !module_loaded && !open_files && !look_theming_off());
    /* A second press while the latch holds changes nothing. */
    look_switch(id, 0, ACCENT_DEFAULT, PANE_DARK);
    assert(look_state.active.dark && setting_ints[KEY_DARK] == 1 && redraws == 1);
    /* An accent change keeps the stored theme too; choosing Classic drops it. */
    latch_unload();
    look_accent(ACCENT_FIRST);
    assert(!strcmp(theme_id_setting, "flat") && style_accent() == ACCENT_FIRST);
    latch_unload();
    look_switch("", 1, ACCENT_DEFAULT, PANE_THEME);
    assert(!theme_id_setting[0] && !look_problem(PANE_THEME));
    latch_unload();
    look_unload();
    assert(bank_is_stock() && binds == 0 && !canvas_colors && !look_state.active.dark);

    /* A theme with one appearance is shown in it whatever Dark Mode asked:
     * the row's value and the stored setting say which, the pane says what
     * the theme lacks, and a refused toggle leaves them so. */
    struct built light_only, dark_only;
    build_light_only(&light_only, PROVIDED_ID, "Day");
    build_file_at(&dark_only, PROVIDED_ID, "Night", RVT_HEADER_BYTES, NULL, 0, NULL, 0, NULL, NULL);
    provided = &light_only;
    unreadable = NULL;
    strcpy(id, PROVIDED_ID);
    look_switch(id, 1, ACCENT_DEFAULT, PANE_THEME);
    assert(!strcmp(look_state.active.id, PROVIDED_ID) && !look_state.active.dark && !style_dark());
    assert(setting_ints[KEY_DARK] == 0 && !look_dark_available() && !look_problem(PANE_DARK) &&
           !look_problem(PANE_THEME) && !strcmp(look_appearance_note(), "Theme only supports light mode"));
    latch_unload();
    setting_ints[KEY_DARK] = 1; /* the press, as the store sees it first */
    look_switch(id, 1, ACCENT_DEFAULT, PANE_DARK);
    assert(!look_state.active.dark && setting_ints[KEY_DARK] == 0 && !look_problem(PANE_DARK) &&
           !strcmp(look_appearance_note(), "Theme only supports light mode") && !open_files);
    latch_unload();
    provided = &dark_only;
    look_switch(id, 0, ACCENT_DEFAULT, PANE_THEME);
    assert(look_state.active.dark && style_dark() && setting_ints[KEY_DARK] == 1 &&
           !strcmp(look_appearance_note(), "Theme only supports dark mode"));
    latch_unload();
    setting_ints[KEY_DARK] = 0;
    look_switch(id, 0, ACCENT_DEFAULT, PANE_DARK);
    assert(look_state.active.dark && setting_ints[KEY_DARK] == 1 && !look_problem(PANE_DARK) &&
           !strcmp(look_appearance_note(), "Theme only supports dark mode"));
    latch_unload();
    look_switch("", 1, ACCENT_DEFAULT, PANE_THEME);
    assert(!look_appearance_note() && look_dark_available());
    latch_unload();
    look_unload();
    provided = NULL;

    /* A theme with bitmaps for extensions, or one asking for flat icons,
     * holds the core's asset provider; Classic and unload release it and free
     * the index; the end of disk mode keeps both until another file turns up
     * under the id; a provider another extension holds is said under Theme. */
    struct built with, flat, swapped;
    const struct asset_payload one = {OWNER_A, 5, RVT_EXTENSION_BITMAP, sizeof menu_a, menu_a};
    with_assets(&one, 1, &with);
    provided = &with;
    unreadable = NULL;
    strcpy(id, PROVIDED_ID);
    look_switch(id, 1, ACCENT_DEFAULT, PANE_THEME);
    assert(!strcmp(look_state.active.id, PROVIDED_ID) && core_provider == &provider && live_count == 1);
    assert(look_state.active.header_crc == le32(with.bytes + RVT_HEADER_CRC) && !look_problem(PANE_THEME));
    latch_unload(); /* the message queue empties between presses */
    look_accent(ACCENT_FIRST);
    assert(core_provider == &provider && live_count == 1);
    latch_unload();
    look_switch("", 1, ACCENT_DEFAULT, PANE_THEME);
    assert(!core_provider && !live_count && !allocations);
    latch_unload();
    look_switch(id, 1, ACCENT_DEFAULT, PANE_THEME);
    assert(core_provider == &provider);
    latch_unload();
    look_volume_changed();
    assert(core_provider == &provider && live_count == 1 && !look_state.stock_checked);
    build_file_at(&swapped, PROVIDED_ID, "Other", RVT_HEADER_BYTES, NULL, 0, NULL, 0, NULL, NULL);
    provided = &swapped;
    look_volume_changed();
    assert(!core_provider && !live_count && !allocations && !open_files);
    uint8_t prefix[RVT_STYLE_BYTES];
    static const uint8_t roles[RVT_ROLES][3] = {{0}};
    prefix_custom(prefix, roles, RVT_FLAG_FLAT_ICONS, 0, NULL);
    build_file_at(&flat, PROVIDED_ID, "Flat", RVT_HEADER_BYTES, NULL, 0, NULL, 0, NULL, prefix);
    provided = &flat;
    look_switch(id, 1, ACCENT_DEFAULT, PANE_THEME);
    assert(core_provider == &provider && !live_count);
    latch_unload();
    provided = &swapped; /* flat icons stay bound without an index */
    look_volume_changed();
    assert(core_provider == &provider && !live_count);
    provided = &flat;
    look_switch("", 1, ACCENT_DEFAULT, PANE_THEME);
    assert(!core_provider);
    latch_unload();
    provider_held = 1;
    provided = &with;
    look_switch(id, 1, ACCENT_DEFAULT, PANE_THEME);
    assert(!strcmp(look_problem(PANE_THEME), "Artwork held by another extension") && live_count == 1);
    assert(!core_provider);
    provider_held = 0;
    look_unload();
    assert(!core_provider && !live_count && !allocations && bank_is_stock());
    provided = NULL;
}

int main(void)
{
    rv_module_entry(&fake, NULL);
    assert(main_module == &host_service);
    build_bank();

    reader_contracts();
    puts("PASS: theme file reader");
    switch_contracts();
    puts("PASS: switch and recovery");
    assets_contracts();
    puts("PASS: bitmaps for extensions");
    scan_contracts();
    puts("PASS: theme folder");
    classic_mapping();
    accent_mapping();
    theme_mapping();
    bar_mapping();
    puts("PASS: colour mapping");
    tint_contracts();
    cover_contracts();
    latch_contracts();
    picker_contracts();
    bind_contracts();
    puts("PASS: tinted art, flat cover, latch, theme list, hook binding");
    look_contracts();
    puts("PASS: look fallback and unload");
    return 0;
}
