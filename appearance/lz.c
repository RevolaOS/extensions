/* SPDX-License-Identifier: GPL-3.0-only */

#include "lz.h"
#include "crc.h"
#include "ext.h"
#include "rvt.h"

struct lz_input {
    struct rv_file *file;
    uint32_t left;       /* stream bytes still in the file */
    uint32_t have;       /* bytes left in `chunk` */
    const uint8_t *next; /* the next of them */
    uint32_t crc;
    uint8_t chunk[256];
};

/* The next stream byte, -1 at the end or on a short read. */
static int next_byte(struct lz_input *in)
{
    if (!in->have) {
        uint32_t n = in->left < sizeof in->chunk ? in->left : sizeof in->chunk, got = 0;
        if (!n || api->file_read(self, in->file, in->chunk, n, &got) != RV_OK || got != n)
            return -1;
        in->crc = crc32_update(in->crc, in->chunk, n);
        in->next = in->chunk;
        in->have = n;
        in->left -= n;
    }
    in->have--;
    return *in->next++;
}

static int decode(struct lz_input *in, uint8_t *out, uint32_t bytes)
{
    uint32_t done = 0;
    while (done < bytes) {
        int flags = next_byte(in);
        if (flags < 0)
            return 0;
        for (uint32_t bit = 0; bit < 8 && done < bytes; bit++) {
            int high = next_byte(in);
            if (high < 0)
                return 0;
            if (!(flags >> bit & 1)) {
                out[done++] = (uint8_t)high;
                continue;
            }
            int low = next_byte(in);
            uint32_t length = (uint32_t)(high >> 4) + RVT_LZ_MIN_MATCH;
            uint32_t distance = ((uint32_t)(high & 15) << 8 | (uint32_t)(low & 255)) + 1;
            for (int more = high >> 4 == 15 ? 255 : 0; more == 255; length += (uint32_t)more)
                if ((more = next_byte(in)) < 0)
                    return 0;
            if (low < 0 || distance > done || length > bytes - done)
                return 0;
            for (uint32_t end = done + length; done < end; done++)
                out[done] = out[done - distance];
        }
    }
    return !in->have && !in->left;
}

int lz_decode(struct rv_file *file, uint32_t stream_bytes, uint8_t *out, uint32_t bytes,
              uint32_t *crc)
{
    struct lz_input in;
    in.file = file;
    in.left = stream_bytes;
    in.have = 0;
    in.crc = *crc;
    int whole = decode(&in, out, bytes);
    *crc = in.crc;
    return whole;
}
