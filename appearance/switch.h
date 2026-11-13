/* SPDX-License-Identifier: GPL-3.0-only */

/* The switch module: loaded by main for one switch, accent change or unload,
 * it streams a pack into the resident bitmaps and restores stock artwork from
 * the firmware file. Its files reach main's catalog, checksum, style and
 * tint code through main's service table.
 *
 * A switch streams the pack's artwork straight into the firmware's resident
 * bitmaps and restores what the previous look changed from the firmware
 * file, reading only the bitmaps that need it and checking each against its
 * stock checksum, so nothing is staged in memory. A failure part way through
 * restores every bitmap either look touched and brings back the stock light
 * look; when that restore fails too, theming is off for the session. */

#ifndef SWITCH_H
#define SWITCH_H
#include "services.h"

/* main's table, fetched by the module's entry. */
extern const struct main_service *main_module;

#endif
