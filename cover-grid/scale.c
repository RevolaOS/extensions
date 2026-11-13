/* SPDX-License-Identifier: GPL-3.0-only */

#include "scale.h"

/* The CPU has no divider. Each output pixel covers 2 or 3 source pixels per
 * axis, so the filter divides by 4, 6 or 9; this 16.16 reciprocal rounds
 * exactly for every channel sum it can see (at most 9 * 63). */
static inline uint32_t divide_rounded(uint32_t sum, uint32_t n)
{
    static const uint16_t reciprocal[10] = {0,     0,     32768, 21846, 16384,
                                            13108, 10923, 9363,  8192,  7282};
    return (sum + n / 2) * reciprocal[n] >> 16;
}

/* RGB565 spread so that nine pixels sum without carries: blue in bits 0-10,
 * red in 11-20 and green from 21. */
static inline uint32_t spread(uint32_t pixel)
{
    return (pixel | pixel << 16) & 0x07e0f81fu;
}

void scale_init(struct scale *scale, int size)
{
    for (int i = 0; i < size; i++) {
        scale->first[i] = (uint8_t)(i * SCALE_SOURCE / size);
        scale->span[i] = (uint8_t)(((i + 1) * SCALE_SOURCE + size - 1) / size - scale->first[i]);
    }
}

void scale_row(uint16_t *out, const struct scale *scale, const uint8_t *pixels,
               uint32_t row_bytes, int y, int x0, int x1)
{
    const uint8_t *first = scale->first, *span = scale->span;
    const uint16_t *row0 = (const uint16_t *)(pixels + first[y] * row_bytes);
    const uint16_t *row1 = (const uint16_t *)((const uint8_t *)row0 + row_bytes);
    const uint16_t *row2 = (const uint16_t *)((const uint8_t *)row1 + row_bytes);
    int tall = span[y] == 3;
    for (int x = x0; x < x1; x++) {
        int c = first[x], wide = span[x] == 3;
        uint32_t sum = spread(row0[c]) + spread(row0[c + 1]) + spread(row1[c]) +
                       spread(row1[c + 1]);
        if (wide)
            sum += spread(row0[c + 2]) + spread(row1[c + 2]);
        if (tall) {
            sum += spread(row2[c]) + spread(row2[c + 1]);
            if (wide)
                sum += spread(row2[c + 2]);
        }
        uint32_t n = (uint32_t)span[y] * span[x];
        *out++ = (uint16_t)(divide_rounded(sum >> 11 & 0x3ff, n) << 11 |
                            divide_rounded(sum >> 21, n) << 5 | divide_rounded(sum & 0x7ff, n));
    }
}
