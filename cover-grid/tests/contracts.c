/* SPDX-License-Identifier: GPL-3.0-only */

/* Host contracts for the grid's maths: the box filter against a floating-point
 * box average, the scroll and lift geometry against the rules they implement. */

#include "../geometry.h"
#include "../scale.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            failures++;                                                                        \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #condition);                             \
        }                                                                                      \
    } while (0)

static uint32_t next = 1;
static uint16_t noise(void)
{
    next = next * 1103515245u + 12345u;
    return (uint16_t)(next >> 8);
}

/* The exact box average of the source pixels an output pixel covers, each
 * channel rounded half up. */
static uint16_t reference(const uint16_t *source, int size, int x, int y)
{
    int x0 = x * SCALE_SOURCE / size, x1 = ((x + 1) * SCALE_SOURCE + size - 1) / size;
    int y0 = y * SCALE_SOURCE / size, y1 = ((y + 1) * SCALE_SOURCE + size - 1) / size;
    double r = 0, g = 0, b = 0;
    int n = (x1 - x0) * (y1 - y0);
    for (int sy = y0; sy < y1; sy++)
        for (int sx = x0; sx < x1; sx++) {
            uint16_t p = source[sy * SCALE_SOURCE + sx];
            r += p >> 11;
            g += p >> 5 & 0x3f;
            b += p & 0x1f;
        }
    return (uint16_t)((unsigned)floor(r / n + 0.5) << 11 | (unsigned)floor(g / n + 0.5) << 5 |
                      (unsigned)floor(b / n + 0.5));
}

static void test_scale(void)
{
    static uint16_t source[SCALE_SOURCE * SCALE_SOURCE];
    static uint16_t row[SCALE_MAX], part[SCALE_MAX];
    static const int sizes[] = {GRID_COVER, GRID_LIFT_SIZE};
    for (unsigned int i = 0; i < sizeof source / sizeof source[0]; i++)
        source[i] = noise();
    for (unsigned int s = 0; s < 2; s++) {
        int size = sizes[s];
        struct scale scale;
        scale_init(&scale, size);
        /* Windows of 2 or 3 that overlap, cover the source without a gap and
         * never reach past it: scale_row reads first[i] + span[i] - 1. */
        CHECK(scale.first[0] == 0);
        for (int i = 0; i < size; i++) {
            CHECK(scale.span[i] == 2 || scale.span[i] == 3);
            CHECK(scale.first[i] + scale.span[i] <= SCALE_SOURCE);
            if (i) {
                CHECK(scale.first[i] >= scale.first[i - 1]);
                CHECK(scale.first[i] <= scale.first[i - 1] + scale.span[i - 1]);
            }
        }
        CHECK(scale.first[size - 1] + scale.span[size - 1] == SCALE_SOURCE);
        for (int y = 0; y < size; y++) {
            scale_row(row, &scale, (const uint8_t *)source, SCALE_SOURCE * 2, y, 0, size);
            for (int x = 0; x < size; x++)
                CHECK(row[x] == reference(source, size, x, y));
            int x0 = y % 7, x1 = size - y % 5;
            scale_row(part, &scale, (const uint8_t *)source, SCALE_SOURCE * 2, y, x0, x1);
            for (int x = x0; x < x1; x++)
                CHECK(part[x - x0] == row[x]);
        }
    }
}

static void test_top_row(void)
{
    for (int count = 1; count <= 50; count++) {
        int rows = (count + GRID_COLUMNS - 1) / GRID_COLUMNS;
        for (int top = 0; top < rows; top++)
            for (int index = 0; index < count; index++) {
                int next_top = grid_top_row(top, index), row = index / GRID_COLUMNS;
                CHECK(row >= next_top && row < next_top + GRID_FULL_ROWS);
                CHECK(next_top >= 0);
                CHECK(grid_top_row(next_top, index) == next_top);
                if (row >= top && row < top + GRID_FULL_ROWS)
                    CHECK(next_top == top);
            }
        /* Down to the end and back: the top row never jumps by more than one. */
        int top = 0;
        for (int index = 1; index < count; index++) {
            int after = grid_top_row(top, index);
            CHECK(after - top >= 0 && after - top <= 1);
            top = after;
        }
        for (int index = count - 2; index >= 0; index--) {
            int after = grid_top_row(top, index);
            CHECK(top - after >= 0 && top - after <= 1);
            top = after;
        }
        CHECK(top == 0);
    }
}

static void test_cell(void)
{
    int x, y;
    CHECK(grid_cell(0, 0, &x, &y) && x == GRID_MARGIN && y == GRID_MARGIN);
    CHECK(grid_cell(5, 0, &x, &y) && x == GRID_MARGIN + GRID_PITCH && y == GRID_MARGIN + GRID_PITCH);
    CHECK(grid_cell(8, 0, &x, &y) && y == GRID_MARGIN + 2 * GRID_PITCH);
    CHECK(!grid_cell(12, 0, &x, &y));
    CHECK(!grid_cell(3, 1, &x, &y));
    CHECK(grid_cell(4, 1, &x, &y) && y == GRID_MARGIN);
    CHECK(!grid_cell(-1, 0, &x, &y));
}

static void test_lift_origin(void)
{
    enum { WIDTH = 320, HEIGHT = 220 - GRID_CAPTION };
    for (int index = 0; index < GRID_VISIBLE_COVERS; index++) {
        int x, y, lx, ly;
        CHECK(grid_cell(index, 0, &x, &y));
        grid_lift_origin(x, y, WIDTH, HEIGHT, &lx, &ly);
        /* The edge stays on screen wherever the cell is. */
        CHECK(lx - GRID_EDGE >= 0 && lx + GRID_LIFT_SIZE + GRID_EDGE <= WIDTH);
        CHECK(ly - GRID_EDGE >= 0 && ly + GRID_LIFT_SIZE + GRID_EDGE <= HEIGHT);
        /* grid_top_row keeps the selection in the full rows, and there the
         * lifted cover hides the cover beneath it. */
        if (index < GRID_FULL_ROWS * GRID_COLUMNS) {
            CHECK(lx <= x && lx + GRID_LIFT_SIZE >= x + GRID_COVER);
            CHECK(ly <= y && ly + GRID_LIFT_SIZE >= y + GRID_COVER);
        }
    }
    int lx, ly;
    grid_lift_origin(GRID_MARGIN + GRID_PITCH, GRID_MARGIN + GRID_PITCH, WIDTH, HEIGHT, &lx, &ly);
    CHECK(lx == GRID_MARGIN + GRID_PITCH - GRID_LIFT && ly == GRID_MARGIN + GRID_PITCH - GRID_LIFT);
}

static void test_scrollbar(void)
{
    enum { HEIGHT = 220 - GRID_CAPTION, MINIMUM = 16 };
    int y0, y1;
    CHECK(!grid_scrollbar_span(8, 0, HEIGHT, MINIMUM, &y0, &y1));
    CHECK(grid_scrollbar_span(9, 0, HEIGHT, MINIMUM, &y0, &y1));
    for (int count = 9; count < 400; count += 13) {
        int rows = (count + GRID_COLUMNS - 1) / GRID_COLUMNS, last = -1;
        for (int top = 0; top <= rows - GRID_FULL_ROWS; top++) {
            CHECK(grid_scrollbar_span(count, top, HEIGHT, MINIMUM, &y0, &y1));
            CHECK(y0 >= 0 && y1 <= HEIGHT && y1 - y0 >= MINIMUM + 3);
            CHECK(y0 >= last);
            last = y0;
        }
        CHECK(y1 == HEIGHT); /* the thumb reaches the end of the track at the last row */
    }
}

int main(void)
{
    test_scale();
    test_top_row();
    test_cell();
    test_lift_origin();
    test_scrollbar();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    puts("Cover Grid contracts passed");
    return 0;
}
