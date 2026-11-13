/* SPDX-License-Identifier: GPL-3.0-only */

/* The RVT_LZ stream of a pixels record or an extension bitmap, read from the
 * file in stack-sized pieces and decoded straight into its destination. In
 * the main module, so the asset provider has it while the switch module is
 * unloaded; the switch module calls it through main_service. */

#ifndef LZ_H
#define LZ_H
#include "rv_api.h"
#include <stdint.h>

/* Decodes the stream of `stream_bytes` at the file's position into exactly
 * `bytes` of `out`, continuing *crc over every stream byte read: 0 when the
 * stream ends early, a match reaches before the output, the output would
 * overrun, or the stream does not end where the output is full. */
int lz_decode(struct rv_file *file, uint32_t stream_bytes, uint8_t *out, uint32_t bytes,
              uint32_t *crc);

#endif
