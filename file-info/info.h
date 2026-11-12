/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef INFO_H
#define INFO_H
#include <stdint.h>

/* The rows of File Info for `item` (a MediaItem of the symbols header): how
 * many have a value, and the label and value of row `index`. The rows are
 * worked out when `item` changes and kept until info_release. */
uint32_t info_rows(const void *item);
const char *info_row(const void *item, uint32_t index, const char **value);
void info_release(void);

#endif
