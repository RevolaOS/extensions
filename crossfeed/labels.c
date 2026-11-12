/* SPDX-License-Identifier: GPL-3.0-only */

#include "labels.h"
#include "modes.h"
#include "rv_libc.h"

const char *crossfeed_label(int parameter, int index, char out[LABEL_BYTES])
{
    int tenths;
    switch (parameter) {
    case PARAM_MODE:
        return mode_labels[index];
    case PARAM_PRECUT:
        if (index == 0)
            return "Auto";
        tenths = precut_tenths(index);
        break;
    case PARAM_DIRECT:
        tenths = direct_tenths(index);
        break;
    case PARAM_CROSS:
        tenths = cross_tenths(index);
        break;
    case PARAM_HF:
        tenths = hf_tenths(index);
        break;
    default:
        rv_format_hz(out, LABEL_BYTES, cutoff_hz(index));
        return out;
    }
    rv_format_tenths(out, LABEL_BYTES, tenths, 0, " dB");
    return out;
}
