/* SPDX-License-Identifier: GPL-3.0-only */

/* Chapter marks are hidden once they average under MARKS_MIN_SPACING_PX of bar
 * each: at that spacing they merge into a block that says nothing about where
 * the chapters are. Fewer marks are left exactly as stock draws them. */

#include "marks.h"

#define MARKS_MIN_SPACING_PX 3

int marks_hidden(int bar_width, uint32_t count)
{
    return bar_width > 0 && count * MARKS_MIN_SPACING_PX > (uint32_t)bar_width;
}
