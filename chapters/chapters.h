/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CHAPTERS_H
#define CHAPTERS_H
#include <stdint.h>

/* `item` is a MediaItem of the symbols header. */

/* How many chapters `item` has; 0 when it is no longer valid or is a video. */
uint32_t chapters_count(const void *item);
/* Chapter `index`'s title ("Chapter 3" when it has none) and in *start its
 * start time as "m:ss" or "h:mm:ss". Valid until the next call or
 * chapters_release. */
const char *chapters_row(const void *item, uint32_t index, const char **start);
void chapters_release(void);
/* The chapter the playing track is in if it is still `item`, else 0. */
uint32_t chapters_current(const void *item);
/* Seeks the playing track to chapter `index` if it is still `item`. */
void chapters_seek(const void *item, uint32_t index);
/* Plays model row `row` of the episode list `list` from chapter `index` if
 * that row is still `item`. */
void chapters_play(void *list, int32_t row, const void *item, uint32_t index);

#endif
