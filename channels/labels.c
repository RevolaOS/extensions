/* SPDX-License-Identifier: GPL-3.0-only */

#include "labels.h"
#include "rv_libc.h"

const char *channels_label(int parameter, int index, char out[LABEL_BYTES])
{
    if (parameter == PARAM_MODE)
        return mode_labels[index];
    rv_format_int(out, LABEL_BYTES, (int)width_percent(index), " %");
    return out;
}
