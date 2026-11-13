/* SPDX-License-Identifier: GPL-3.0-only */

/* The grid's layout inside the canvas, shared with the host tests: four
 * columns of covers with gaps and margins, two full rows above the caption
 * strip and a third peeking in, the lifted selected cover and the scrollbar. */

#ifndef COVER_GRID_GEOMETRY_H
#define COVER_GRID_GEOMETRY_H
#include <stdint.h>

enum {
    GRID_COLUMNS = 4,
    GRID_COVER = 77,
    GRID_GAP = 2,
    GRID_MARGIN = 3,
    GRID_PITCH = GRID_COVER + GRID_GAP,
    /* The selected cover grows by this much on each side, inside a
     * selection-coloured edge and a shadow. */
    GRID_LIFT = 5,
    GRID_LIFT_SIZE = GRID_COVER + 2 * GRID_LIFT,
    GRID_EDGE = 2,
    GRID_SHADOW = 3,
    /* The caption strip at the bottom: the visible rows of the Now Playing
     * status bar's background. */
    GRID_CAPTION = 22,
    /* Rows that fit entirely above the caption strip; a third peeks below. */
    GRID_FULL_ROWS = 2,
    GRID_VISIBLE_ROWS = GRID_FULL_ROWS + 1,
    GRID_VISIBLE_COVERS = GRID_COLUMNS * GRID_VISIBLE_ROWS,
};

static inline int grid_clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

/* The first visible row after moving the selection to `index`: the selection
 * stays within the two full rows, scrolling by whole rows. */
static inline int grid_top_row(int top, int index)
{
    int row = index / GRID_COLUMNS;
    if (row < top)
        return row;
    if (row >= top + GRID_FULL_ROWS)
        return row - GRID_FULL_ROWS + 1;
    return top;
}

/* The top left of album `index`'s cell with `top` the first visible row; 0
 * when the cell is not among the visible rows. */
static inline int grid_cell(int index, int top, int *x, int *y)
{
    int row = index / GRID_COLUMNS - top;
    if (index < 0 || row < 0 || row >= GRID_VISIBLE_ROWS)
        return 0;
    *x = GRID_MARGIN + index % GRID_COLUMNS * GRID_PITCH;
    *y = GRID_MARGIN + row * GRID_PITCH;
    return 1;
}

/* The lifted cover's origin for the cover at (x, y): centred on it and shifted
 * inward so that its edge stays inside `width` and above the caption strip,
 * `height` being the covers' area. */
static inline void grid_lift_origin(int x, int y, int width, int height, int *lx, int *ly)
{
    *lx = grid_clamp(x - GRID_LIFT, GRID_EDGE, width - GRID_LIFT_SIZE - GRID_EDGE);
    *ly = grid_clamp(y - GRID_LIFT, GRID_EDGE, height - GRID_LIFT_SIZE - GRID_EDGE);
}

/* The stock list thumb: a track from 1 px below the top to 2 px above the
 * bottom of the covers' area, a thumb as long as the visible fraction (at
 * least `minimum`) placed by the scrolled fraction, with its art reaching 1 px
 * above and 2 px below it. 0 when the grid does not scroll. */
static inline int grid_scrollbar_span(int count, int top, int height, int minimum, int *y0,
                                      int *y1)
{
    int rows = (count + GRID_COLUMNS - 1) / GRID_COLUMNS;
    if (rows <= GRID_FULL_ROWS)
        return 0;
    int track = height - 3;
    int scroll = (rows - GRID_FULL_ROWS) * GRID_PITCH;
    int length = track * height / (scroll + height);
    if (length < minimum)
        length = minimum;
    if (length > track)
        length = track;
    int position = (track - length) * top * GRID_PITCH / scroll;
    *y0 = position;
    *y1 = position + length + 3;
    return 1;
}

#endif
