/* SPDX-License-Identifier: GPL-3.0-only */

/* What the main module hands the grid screen: the library model the grid
 * browses and the selection it reopens on. */

#ifndef COVER_GRID_SHARED_H
#define COVER_GRID_SHARED_H
#include "osos-2.0.5.h"
#include "rv_api.h"
#include <stddef.h>

#define KEY_IN_MUSIC 1 /* Settings > Music Menu and Main Menu flip these two */
#define KEY_IN_MAIN 2

struct grid_link {
    void *reference; /* LibraryModelRef on the shared music browser, NULL when none */
    int selected, top;
};

/* The shared browser behind a library model, and its album groups. */
static inline void *grid_browser(void *model)
{
    return model ? RV_FIELD(model, LibraryBrowser_browser, void *) : NULL;
}

static inline int grid_album_count(void *model)
{
    uint8_t *browser = grid_browser(model);
    if (!browser)
        return 0;
    const uint8_t *begin = RV_FIELD(browser, MediaBrowser_albums, const uint8_t *);
    const uint8_t *end = RV_FIELD(browser, MediaBrowser_albums_end, const uint8_t *);
    return begin ? (int)((end - begin) / AlbumGroup_bytes) : 0;
}

#endif
