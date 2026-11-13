/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef SELECTION_BAR_H
#define SELECTION_BAR_H

/* Gives the selection bar the look's colours while they differ from stock and
 * has the cached drawings, the bar and the Now Playing status icons, drawn
 * again; after every switch and accent change. Restore puts the stock colours
 * back. */
void selection_bar_apply(void);
void selection_bar_restore(void);

#endif
