/* SPDX-License-Identifier: GPL-3.0-only */

#include "stock.h"
#include "ext.h"
#include "rv_libc.h"
#include "switch.h"
#include <stddef.h>

static struct rv_file *kept; /* the file while a switch holds it open */

static struct rv_file *open_firmware(void)
{
    struct rv_file *file;
    return api->firmware_open(self, &file) == RV_OK ? file : NULL;
}

static int read_at(struct rv_file *file, uint32_t offset, void *out, uint32_t bytes)
{
    return api->file_seek(self, file, offset) == RV_OK && read_exact(file, out, bytes);
}

static int crc_matches(uint32_t crc, uint32_t index)
{
    return (uint16_t)~crc == main_module->catalog_crc(index);
}

/* Read in pieces through the stack. */
int stock_file_matches(void)
{
    struct rv_file *file = open_firmware();
    uint8_t piece[256];
    if (!file)
        return 0;
    uint32_t last = main_module->catalog_count() - 1, left = main_module->catalog_size(last);
    uint32_t crc = 0xffffffffu;
    int ok = api->file_seek(self, file, main_module->catalog_file_offset(last)) == RV_OK;
    while (ok && left) {
        uint32_t n = left < sizeof piece ? left : sizeof piece;
        ok = read_exact(file, piece, n);
        crc = main_module->crc32_update(crc, piece, n);
        left -= n;
    }
    api->file_close(self, file);
    return ok && crc_matches(crc, last);
}

int stock_open(void)
{
    if (!kept)
        kept = open_firmware();
    return kept != NULL;
}

void stock_close(void)
{
    if (kept)
        api->file_close(self, kept);
    kept = NULL;
}

static int fetch(struct rv_file *file, uint32_t index, uint8_t *dest)
{
    struct layout b;
    if (!main_module->catalog_layout(index, &b))
        return 0;
    const uint8_t *bitmap = main_module->catalog_data(index);
    uint32_t size = main_module->catalog_size(index) - b.start;
    uint32_t crc = main_module->crc32_update(0xffffffffu, bitmap, b.start);
    if (!read_at(file, main_module->catalog_file_offset(index) + b.start, dest, size) ||
        !crc_matches(main_module->crc32_update(crc, dest, size), index)) {
        memset(dest, 0, size);
        return 0;
    }
    return 1;
}

int stock_fetch(uint32_t index, uint8_t *dest)
{
    if (kept)
        return fetch(kept, index, dest);
    struct rv_file *file = open_firmware();
    if (!file)
        return 0;
    int ok = fetch(file, index, dest);
    api->file_close(self, file);
    return ok;
}

int stock_restore(const uint32_t *bitmaps)
{
    int opened = !kept && stock_open(), ok = kept != NULL;
    for (uint32_t i = 0; ok && i < main_module->catalog_count(); i++)
        if (bit_test(bitmaps, i)) {
            struct layout b;
            if (!main_module->catalog_layout(i, &b) ||
                !fetch(kept, i, main_module->catalog_data(i) + b.start))
                ok = 0;
        }
    if (opened)
        stock_close();
    return ok;
}
