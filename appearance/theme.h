/* SPDX-License-Identifier: GPL-3.0-only */

/* Theme files a listener copies to /Revola/Themes/: <id>.rvt, named after
 * the id inside, and the `classic` asset, a theme file with the Classic dark
 * pack. This reads and checks their headers; the switch module reads the
 * packs where the header places them. */

#ifndef THEME_H
#define THEME_H
#include "look.h"
#include "rv_api.h"
#include <stdint.h>

struct theme_entry {
    char id[RVT_ID_BYTES];
    char name[RVT_NAME_BYTES];
};

/* How many files in the folder are named like a theme, before any is opened. */
uint32_t theme_count(void);
/* The readable themes sorted by id, at most `capacity`; *overflow says more
 * were named like one. */
uint32_t theme_scan(struct theme_entry *out, uint32_t capacity, int *overflow);

enum theme_result {
    THEME_OK,
    THEME_UNAVAILABLE,      /* no such file, or not a theme of that id */
    THEME_NOT_IN_APPEARANCE /* opened, but has only the other appearance */
};
/* Opens theme `id` and fills `look` from its header, with `look->dark` the
 * appearance asked for, or the only one the theme has, and `pack` where that
 * appearance's pack is. The file is the caller's to close unless the theme is
 * unavailable. */
enum theme_result theme_open(const char *id, int dark, struct rv_file **file, struct look *look,
                             struct pack_place *pack);
/* Opens the file a look's pack is in without reading it: a theme's by id,
 * Classic dark's the `classic` asset. 0 for Classic light, which has none. */
int look_file_open(const struct look *look, struct rv_file **file);
/* Checks the header of `file`, opened for `look` (its id when it has one),
 * sets look->header_crc and places the pack of look->dark; 0 when the file is
 * no theme of that id or lacks the pack. */
int look_pack_place(struct rv_file *file, struct look *look, struct pack_place *pack);
/* Whether theme `id`'s file is still the one whose header checksum is `crc`. */
int theme_file_matches(const char *id, uint32_t crc);

#endif
