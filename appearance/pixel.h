/* SPDX-License-Identifier: GPL-3.0-only */

/* Little-endian words, clamping and RGB565 packing, shared by the file
 * reader, the derivations and the draw hooks. */

#ifndef PIXEL_H
#define PIXEL_H
#include <stdint.h>

static inline uint32_t le16(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8;
}

static inline uint32_t le32(const uint8_t *p)
{
    return le16(p) | le16(p + 2) << 16;
}

static inline int32_t clamp(int32_t value, int32_t low, int32_t high)
{
    return value < low ? low : value > high ? high : value;
}

/* value / 255 exactly for value up to 65534, without a division. */
static inline uint32_t div255(uint32_t value)
{
    return (value + 1 + (value >> 8)) >> 8;
}

/* x / d for x below 2^24 and 0 < d below 2^16 as one multiply by the
 * reciprocal, exact over that range (the divisor's 16 bits plus the
 * dividend's 24 fit a 40-bit shift). Division by a constant folds at compile
 * time; the relight rule computes its reciprocal once per bitmap. */
static inline uint64_t reciprocal(uint32_t d)
{
    /* 2^40 / d from 2^32 = 0xffffffff + 1, in 32-bit steps. */
    uint32_t quotient = 0xffffffffu / d, remainder = 0xffffffffu % d + 1;
    if (remainder == d) {
        quotient++;
        remainder = 0;
    }
    return ((uint64_t)quotient << 8) + (remainder << 8) / d + 1;
}

static inline uint32_t divide(uint32_t x, uint64_t reciprocal_of_d)
{
    return (uint32_t)((x * reciprocal_of_d) >> 40);
}

/* RGB565 words from and to RGB bytes. Unpacking widens by shifting, so a
 * colour that is exactly 5-6-5 comes back unchanged. */
static inline uint32_t rgb565_pack(const uint8_t *rgb)
{
    return (uint32_t)(rgb[0] >> 3) << 11 | (uint32_t)(rgb[1] >> 2) << 5 | rgb[2] >> 3;
}

static inline void rgb565_unpack(uint32_t word, uint8_t rgb[3])
{
    rgb[0] = (uint8_t)((word >> 11) << 3);
    rgb[1] = (uint8_t)((word >> 5 & 63) << 2);
    rgb[2] = (uint8_t)((word & 31) << 3);
}

#endif
