/* SPDX-License-Identifier: GPL-3.0-only */

#include "crc.h"

/* Polynomial 0xedb88320, four bits at a time. */
static const uint32_t crc_nibbles[16] = {
    0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu, 0x76dc4190u, 0x6b6b51f4u,
    0x4db26158u, 0x5005713cu, 0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
    0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu,
};

uint32_t crc32_update(uint32_t crc, const void *data, uint32_t size)
{
    const uint8_t *p = data;
    while (size--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ crc_nibbles[crc & 15u];
        crc = (crc >> 4) ^ crc_nibbles[crc & 15u];
    }
    return crc;
}
