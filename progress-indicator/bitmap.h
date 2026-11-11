/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PROGRESS_BITMAP_H
#define PROGRESS_BITMAP_H
#include <stdint.h>
/* Four word-aligned variants, borrowed until the next rebuild or unload. */
const void *progress_bitmap(const uint8_t *source, unsigned variant);
int progress_bitmap_owns(const void *payload);
#endif
