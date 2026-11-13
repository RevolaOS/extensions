/* Hand-written for the host tests, in the shape tools/catalog_inc.py writes:
 * a six-bitmap catalog over a host array: a mask, two paletted bitmaps of one
 * layout, an RGB565 one with padding after it, an ARGB one and a paletted one
 * with a wider palette. The checksums are filled in once the bank is built. */
#define CATALOG_COUNT 6
#define CATALOG_BASE ((uintptr_t)host_bank)
#define CATALOG_FILE_OFFSET 0x1000u
#define CATALOG_BANK_BYTES sizeof host_bank
static const uint8_t fingerprint[RVT_FINGERPRINT_BYTES] = {1, 2, 3, 4, 5, 6, 7, 8};
static const uint32_t catalog_offsets[CATALOG_COUNT + 1] = {
    AT_MASK | PAD(MASK_BYTES),       AT_PALETTED | PAD(PALETTED_BYTES), AT_RGB | PAD(RGB_BYTES),
    AT_SECOND | PAD(PALETTED_BYTES), AT_ARGB | PAD(ARGB_BYTES),         AT_WIDE | PAD(WIDE_BYTES),
    BANK_END,
};
static uint16_t catalog_crcs[CATALOG_COUNT];
static const uint16_t well_centers[3] = {1, 1, 1};
static const uint16_t field_center = 1;
static const uint16_t calendar_cells[3] = {1, 1, 1};
static const struct contrast_text contrast_text[] = {
    {RES_NowPlaying_StatusBar_Text_View, 1, 1}, {0x0dad0101u, 4, 1}, {0x0dad0201u, 4, 1},
    {0x0dad0301u, 3, 1},                        {0x0dad0401u, 2, 1},
};
static const struct accent_art accent_art[] = {{5, RVT_ROLE_ACCENT_FILL}};
