/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef PLAYBACK_QUEUE_H
#define PLAYBACK_QUEUE_H

#include <stdint.h>

/* The native playback list, from the UI task. Queue access takes the player lock. */

/* Whether the player has a list the queue can change, including an idle list. */
int queue_available(void);
int queue_can_add(void);
int queue_can_add_item(const void *item);
int queue_start_idle(void);

/* Inserts a supported context item at the first pending position, or at the
 * end with `last`. Creates an idle native list when needed. */
void queue_add_item(const void *item, int last);

/* Inserts the songs of an album or artist selection as Add to On-The-Go would
 * add them; `browser` is what Apple hands to the On-The-Go playlist. */
void queue_add_browser(void *browser, int last);

/* Pending items, starting at row 0: all items when idle, otherwise those
 * after the current one. */
uint32_t queue_rows(void);

/* Whether the playback list, current track, position, count or order changed since
 * the previous check. Called on the UI task while Up Next is open. */
int queue_changed(void);

/* Title (or artist with `artist`) of row `row` into `out`, empty for a missing
 * row. Capacity includes the terminator; UTF-8 characters are kept whole. */
void queue_text(uint32_t row, int artist, char *out, uint32_t capacity);

/* Plays row `row`: the songs before it, the current one included,
 * leave the list. Earlier songs stay, so Previous still reaches them. */
void queue_play(uint32_t row);

/* Captures the row a context menu opens on; 0 for a missing row. */
int queue_capture(uint32_t row);

/* Upcoming row `from` to row `to`, if it still names the captured entry. */
void queue_move(uint32_t from, uint32_t to);

/* Removes upcoming row `row`. */
void queue_remove(uint32_t row);

/* Removes all pending items, preserving the current item and earlier history. */
void queue_clear(void);

#endif
