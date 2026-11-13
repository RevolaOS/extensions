/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef FLAT_COVER_H
#define FLAT_COVER_H
#include <stdint.h>

/* The Now Playing cover drawn face on, when the look asks for it. */
uint32_t flat_cover_view_draw(uint32_t view, uint32_t argument, uint32_t clip, const void *next);
uint32_t flat_cover_renderer_draw(uint32_t renderer, const void *next);
uint32_t flat_cover_edge_lines(uint32_t buffer, const void *next);

#endif
