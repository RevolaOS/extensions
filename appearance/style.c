/* SPDX-License-Identifier: GPL-3.0-only */

#include "style.h"
#include "pixel.h"
#include "rv_libc.h"

#define RGB(r, g, b) {r, g, b, 255}

/* The stock look's roles, light then dark, in rvt_role order. */
static const uint8_t classic[2][RVT_ROLES][4] = {
    {RGB(255, 255, 255), RGB(32, 32, 32), RGB(35, 108, 197), RGB(49, 133, 208), RGB(255, 255, 255),
     RGB(35, 108, 197), RGB(126, 129, 131), RGB(49, 133, 208)},
    {RGB(24, 24, 24), RGB(232, 232, 232), RGB(0, 161, 255), RGB(0, 161, 255), RGB(255, 255, 255),
     RGB(0, 161, 255), RGB(56, 56, 56), RGB(0, 161, 255)},
};

/* The stock look's accents: fill and text, light then dark. Light colours keep
 * 5:1 against white; dark fills keep 4.5:1 under white text and dark text 6:1
 * on the background. Blue is the stock colour itself. */
static const uint8_t accents[RVT_ACCENTS][2][2][4] = {
    {{RGB(35, 108, 197), RGB(49, 133, 208)}, {RGB(0, 161, 255), RGB(0, 161, 255)}},
    {{RGB(137, 68, 171), RGB(137, 68, 171)}, {RGB(150, 78, 188), RGB(218, 143, 255)}},
    {{RGB(196, 30, 86), RGB(196, 30, 86)}, {RGB(206, 38, 96), RGB(255, 100, 145)}},
    {{RGB(208, 32, 40), RGB(208, 32, 40)}, {RGB(212, 40, 48), RGB(255, 105, 97)}},
    {{RGB(184, 72, 0), RGB(184, 72, 0)}, {RGB(190, 76, 0), RGB(255, 159, 10)}},
    {{RGB(30, 128, 58), RGB(30, 128, 58)}, {RGB(32, 134, 62), RGB(48, 209, 88)}},
    {{RGB(0, 115, 120), RGB(0, 115, 120)}, {RGB(0, 120, 125), RGB(64, 210, 215)}},
    {{RGB(130, 101, 0), RGB(130, 101, 0)}, {RGB(132, 103, 0), RGB(255, 214, 10)}},
    {{RGB(92, 96, 102), RGB(92, 96, 102)}, {RGB(100, 104, 110), RGB(190, 194, 200)}},
};
#undef RGB

const char *const accent_labels[ACCENTS] = {
    "Theme", "Blue", "Teal", "Green", "Yellow", "Orange", "Red", "Pink", "Purple", "Graphite",
};

/* The palette each listed accent is, in rvt_accent order. */
static const uint8_t palette_of[ACCENTS - ACCENT_FIRST] = {
    RVT_ACCENT_BLUE, RVT_ACCENT_TEAL, RVT_ACCENT_GREEN,  RVT_ACCENT_YELLOW,  RVT_ACCENT_ORANGE,
    RVT_ACCENT_RED,  RVT_ACCENT_PINK, RVT_ACCENT_PURPLE, RVT_ACCENT_GRAPHITE,
};

/* Which palette slot of an accent replaces a role, plus one; 0 keeps the role. */
static const uint8_t accent_slot[RVT_ROLES] = {
    [RVT_ROLE_ACCENT_FILL] = RVT_SLOT_ACCENT_FILL + 1,
    [RVT_ROLE_ACCENT_TEXT] = RVT_SLOT_ACCENT_TEXT + 1,
    [RVT_ROLE_SELECTION] = RVT_SLOT_SELECTION + 1,
    [RVT_ROLE_ON_ACCENT] = RVT_SLOT_ON_ACCENT + 1,
    [RVT_ROLE_ICON_TINT] = RVT_SLOT_ICON_TINT + 1,
};

/* The pack's style prefix, tint roles packed two per byte. */
static struct {
    uint8_t colors[RVT_ROLES][4];
    uint32_t flags;
    uint8_t accents[RVT_ACCENTS][RVT_ACCENT_SLOTS][4];
    uint8_t tint[RVT_CATALOG_MAX / 2];
} pack;
static int from_theme, dark; /* a pack's colours are in force; the dark appearance is */
static unsigned accent;      /* enum accent */

static unsigned palette(void)
{
    return palette_of[accent - ACCENT_FIRST];
}

void color_mix(uint8_t out[4], const uint8_t *from, const uint8_t *to, unsigned weight)
{
    for (unsigned i = 0; i < 3; i++)
        out[i] = (uint8_t)div255(from[i] * (255 - weight) + to[i] * weight + 127);
    out[3] = 255;
}

const uint8_t *style_color(enum rvt_role role)
{
    if ((unsigned)role >= RVT_ROLES)
        role = RVT_ROLE_TEXT;
    if (from_theme) {
        unsigned slot = accent_slot[role];
        return accent && slot ? pack.accents[palette()][slot - 1] : pack.colors[role];
    }
    if (accent) {
        const uint8_t(*preset)[4] = accents[palette()][dark];
        if (role == RVT_ROLE_ACCENT_FILL || role == RVT_ROLE_SELECTION)
            return preset[0];
        if (role == RVT_ROLE_ACCENT_TEXT || role == RVT_ROLE_ICON_TINT)
            return preset[1];
    }
    return classic[dark][role];
}

const uint8_t *style_stock_color(enum rvt_role role)
{
    return classic[0][(unsigned)role < RVT_ROLES ? role : RVT_ROLE_TEXT];
}

void style_set(const uint8_t *prefix)
{
    const uint8_t *tint = prefix + RVT_STYLE_TINTS;
    memcpy(pack.colors, prefix + RVT_STYLE_COLORS, sizeof pack.colors);
    memcpy(&pack.flags, prefix + RVT_STYLE_FLAGS, sizeof pack.flags);
    memcpy(pack.accents, prefix + RVT_STYLE_PALETTES, sizeof pack.accents);
    for (unsigned i = 0; i < RVT_CATALOG_MAX; i += 2)
        pack.tint[i / 2] = (uint8_t)((tint[i] & 15) | (tint[i + 1] << 4));
    from_theme = 1;
}

void style_reset(void)
{
    from_theme = 0;
}

int style_from_theme(void)
{
    return from_theme;
}

void style_set_dark(int value)
{
    dark = !!value;
}

int style_dark(void)
{
    return dark;
}

void style_set_accent(unsigned value)
{
    accent = value < ACCENTS ? value : ACCENT_DEFAULT;
}

unsigned style_accent(void)
{
    return accent;
}

int style_not_stock(void)
{
    return dark || from_theme || style_accent_art_active();
}

unsigned style_flags(void)
{
    return from_theme ? pack.flags : 0;
}

static unsigned tint_role(unsigned asset)
{
    if (!from_theme || asset >= RVT_CATALOG_MAX)
        return RVT_TINT_NONE;
    return (pack.tint[asset / 2] >> (asset % 2 * 4)) & 15;
}

int style_tinted(unsigned asset)
{
    return tint_role(asset) != RVT_TINT_NONE;
}

const uint8_t *style_tint_color(unsigned asset)
{
    return style_color((enum rvt_role)(tint_role(asset) - RVT_TINT_ROLE));
}

int style_accent_art_active(void)
{
    return from_theme || (accent && palette() != RVT_ACCENT_BLUE);
}

/* The stock gradient's stops, lightened toward white by these / 255. */
static const uint8_t bar_lift[3] = {64, 0, 140};

int style_selection_bar(uint8_t out[3][4])
{
    static const uint8_t white[4] = {255, 255, 255, 255};
    int flat = style_flags() & RVT_FLAG_FLAT_SELECTION;
    if (!flat && !style_accent_art_active())
        return 0;
    const uint8_t *base = style_color(RVT_ROLE_SELECTION);
    for (unsigned stop = 0; stop < 3; stop++)
        color_mix(out[stop], base, white, flat ? 0 : bar_lift[stop]);
    return 1;
}
