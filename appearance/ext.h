/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef EXT_H
#define EXT_H
#include "rv_api.h"
#include <stdint.h>

/* The core's table and this extension's handle, set by each module's entry. */
extern const struct rv_api *api;
extern rv_extension_handle self;

/* The settings. KEY_AVAILABLE shows the Appearance row: on from load until a
 * failed restore turns theming off for the session. */
enum {
    KEY_THEME = 1,   /* string: the theme id, empty for Classic */
    KEY_DARK,        /* bool */
    KEY_ACCENT,      /* choice */
    KEY_MENU_COVERS, /* choice: Theme, On, Off */
    KEY_AVAILABLE,   /* bool without a row */
};

/* The manual module that streams a switch, by its index in the manifest. */
#define SWITCH_MODULE 1

/* Exactly `bytes` of `file` into `out`. */
static inline int read_exact(struct rv_file *file, void *out, uint32_t bytes)
{
    uint32_t got = 0;
    return api->file_read(self, file, out, bytes, &got) == RV_OK && got == bytes;
}

#endif
