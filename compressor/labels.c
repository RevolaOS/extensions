/* SPDX-License-Identifier: GPL-3.0-only */

#include "labels.h"
#include "rv_libc.h"

static const char *const ratios[RATIO_LABELS] = {"2:1", "4:1", "6:1", "10:1", "Limit"};

const char *compressor_label(int parameter, int index, char out[LABEL_BYTES])
{
    switch (parameter) {
    case PARAM_THRESHOLD:
        if (index == 0)
            return "Off";
        rv_format_int(out, LABEL_BYTES, threshold_db(index), " dB");
        return out;
    case PARAM_RATIO:
        return ratios[index];
    case PARAM_KNEE:
        return index ? "Soft" : "Hard";
    case PARAM_MAKEUP:
        return index ? "Auto" : "Off";
    case PARAM_ATTACK:
        rv_format_int(out, LABEL_BYTES, (int)attack_ms(index), " ms");
        return out;
    default:
        rv_format_int(out, LABEL_BYTES, (int)release_ms(index), " ms");
        return out;
    }
}
