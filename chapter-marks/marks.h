/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MARKS_H
#define MARKS_H
#include <stdint.h>

/* Whether `count` chapter marks on a bar `bar_width` px wide are too crowded
 * to tell apart, in which case none is drawn. */
int marks_hidden(int bar_width, uint32_t count);

#endif
