/* SPDX-License-Identifier: GPL-3.0-only */

/* The mode and the Custom values as text; shared.h gives each index its value. */

#include "labels.h"

const char *const mode_labels[MODE_LABELS] = {"Off", "Simple", "Custom"};

const char *const direct_labels[DIRECT_LABELS] = {
    "-6.0 dB", "-5.5 dB", "-5.0 dB", "-4.5 dB", "-4.0 dB", "-3.5 dB", "-3.0 dB", "-2.5 dB",
    "-2.0 dB", "-1.5 dB", "-1.0 dB", "-0.5 dB", "0.0 dB",
};

const char *const cross_labels[CROSS_LABELS] = {
    "-12.0 dB", "-11.5 dB", "-11.0 dB", "-10.5 dB", "-10.0 dB", "-9.5 dB", "-9.0 dB", "-8.5 dB",
    "-8.0 dB", "-7.5 dB", "-7.0 dB", "-6.5 dB", "-6.0 dB", "-5.5 dB", "-5.0 dB", "-4.5 dB",
    "-4.0 dB", "-3.5 dB", "-3.0 dB",
};

const char *const hf_labels[HF_LABELS] = {
    "-24.0 dB", "-23.5 dB", "-23.0 dB", "-22.5 dB", "-22.0 dB", "-21.5 dB", "-21.0 dB",
    "-20.5 dB", "-20.0 dB", "-19.5 dB", "-19.0 dB", "-18.5 dB", "-18.0 dB", "-17.5 dB",
    "-17.0 dB", "-16.5 dB", "-16.0 dB", "-15.5 dB", "-15.0 dB", "-14.5 dB", "-14.0 dB",
    "-13.5 dB", "-13.0 dB", "-12.5 dB", "-12.0 dB", "-11.5 dB", "-11.0 dB", "-10.5 dB",
    "-10.0 dB", "-9.5 dB", "-9.0 dB", "-8.5 dB", "-8.0 dB", "-7.5 dB", "-7.0 dB", "-6.5 dB",
    "-6.0 dB",
};

const char *const cutoff_labels[CUTOFF_LABELS] = {
    "500 Hz", "600 Hz", "700 Hz", "800 Hz", "900 Hz", "1000 Hz", "1100 Hz", "1200 Hz",
    "1300 Hz", "1400 Hz", "1500 Hz", "1600 Hz", "1700 Hz", "1800 Hz", "1900 Hz", "2000 Hz",
};

const char *const precut_labels[PRECUT_LABELS] = {
    "Auto",    "0.0 dB",  "-0.5 dB", "-1.0 dB", "-1.5 dB", "-2.0 dB", "-2.5 dB",
    "-3.0 dB", "-3.5 dB", "-4.0 dB", "-4.5 dB", "-5.0 dB", "-5.5 dB", "-6.0 dB",
};
