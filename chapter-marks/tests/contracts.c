/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include "../marks.c"

int main(void)
{
    /* A 170 px bar keeps up to 170 / MARKS_MIN_SPACING_PX marks. */
    assert(!marks_hidden(170, 0));
    assert(!marks_hidden(170, 25));
    assert(!marks_hidden(170, 170 / MARKS_MIN_SPACING_PX));
    assert(marks_hidden(170, 170 / MARKS_MIN_SPACING_PX + 1));
    assert(marks_hidden(170, 120));
    assert(marks_hidden(170, 2000));
    /* A degenerate bar hides nothing. */
    assert(!marks_hidden(0, 500));
    assert(!marks_hidden(-5, 500));
    puts("Chapter Marks contracts passed");
    return 0;
}
