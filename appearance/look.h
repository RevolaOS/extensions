/* SPDX-License-Identifier: GPL-3.0-only */

/* A look is what the screen shows: Classic or a theme, in the light or dark
 * appearance, with an accent. Putting one on the screen loads the switch
 * module, streams the artwork, applies the colours and writes the settings
 * back to say what is there. */

#ifndef LOOK_H
#define LOOK_H
#include "catalog.h"
#include "rv_api.h"
#include <stdint.h>

struct look {
    char id[RVT_ID_BYTES]; /* empty for Classic */
    char name[RVT_NAME_BYTES];
    uint32_t appearances; /* RVT_APPEARANCE_* bits the theme file has */
    int dark;
    unsigned accent;
    uint32_t header_crc; /* the header checksum word of the look's file */
};

/* Where a look's pack is in its file: the pack header's offset and the
 * pack's size, header included. */
struct pack_place {
    uint32_t offset, bytes;
};

enum switch_result {
    SWITCH_OK,
    SWITCH_LOAD_FAILED,   /* the pack or the firmware file is unusable; nothing changed */
    SWITCH_RECOVERED,     /* a write failed; the stock light look is back */
    SWITCH_RESTORE_FAILED /* and some bitmaps could not be restored: theming is off */
};

/* What outlives a switch: the look on the screen, the bitmaps that differ
 * from stock (the pack's replacements and the tinted accent art), which of
 * those are the accent art, and whether the firmware file has been checked
 * against the resident bank this session. */
struct look_state {
    struct look active;
    uint32_t assets[BITMAP_WORDS];
    uint32_t accented[BITMAP_WORDS];
    int stock_checked;
};

extern struct look_state look_state;
const struct look *look_active(void);
/* For the Dark Mode pane of a theme with one appearance: which it has; NULL
 * when both are there. */
const char *look_appearance_note(void);

/* The pane an action came from, which is where its failure is shown. */
enum look_pane { PANE_THEME, PANE_DARK, PANE_ACCENT };
/* What the last switch could not do, or NULL; shown under `pane` only. */
const char *look_problem(enum look_pane pane);

/* Theme `id` ("" for Classic) in the current appearance and accent, from the
 * picker. */
void look_select(const char *id);
/* Theme `id`, or Classic in the `dark` or light appearance, with `accent`.
 * A theme whose file is missing shows Classic and says so under Theme; the
 * stored id stays, so the theme returns when its file does. */
void look_switch(const char *id, int dark, unsigned accent, enum look_pane pane);
/* Another accent on the current look. */
void look_accent(unsigned accent);
/* A switch is writing settings; their change callbacks are its own. */
int look_busy(void);
/* A restore failed this session: theming stays off until the next boot. */
int look_theming_off(void);
/* Disk mode may have replaced the firmware file and the theme file: the first
 * is checked again next switch; the second is checked now, and when it is
 * another file or gone, the index of its bitmaps for extensions goes. */
void look_volume_changed(void);
/* Every bitmap back to stock and the stock colours, before the extension unloads. */
void look_unload(void);

#endif
