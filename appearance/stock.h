/* SPDX-License-Identifier: GPL-3.0-only */

/* Stock artwork comes back from the firmware file: /Revola/Firmware/revola.bin
 * is Apple's image with the core appended, so every bitmap lies there at its
 * catalog offset, unchanged. The file must be the image the iPod booted: it
 * is checked once per session before a switch writes anything, so recovery
 * is possible afterwards, and again after USB disk mode, which may have
 * replaced it. */

#ifndef STOCK_H
#define STOCK_H
#include <stdint.h>

/* The file opens and its last bitmap carries the catalog's checksum, so it is
 * the booted image. */
int stock_file_matches(void);
/* Keeps the file open across one switch, so each fetch is a seek and a read;
 * stock_fetch and stock_restore open it for themselves when it is not. */
int stock_open(void);
void stock_close(void);
/* Reads one bitmap's bytes after its header into `dest` and checks them, with
 * the resident header, against the stock checksum. 0 when they do not match;
 * `dest` is then zero. */
int stock_fetch(uint32_t index, uint8_t *dest);
/* stock_fetch of every bitmap set in `bitmaps` into its resident bitmap; 0
 * when one fails. */
int stock_restore(const uint32_t *bitmaps);

#endif
