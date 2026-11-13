/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef CRC_H
#define CRC_H
#include <stdint.h>

/* Reflected CRC-32 over an inverted seed: start from 0xffffffff, invert the
 * result. The main module holds the one copy; the switch module calls it
 * through main_service. */
uint32_t crc32_update(uint32_t crc, const void *data, uint32_t size);

#endif
