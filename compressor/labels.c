/* SPDX-License-Identifier: GPL-3.0-only */

/* The values as text; shared.h gives each index its value. */

#include "labels.h"

const char *const threshold_labels[THRESHOLD_LABELS] = {
    "Off", "-3 dB", "-6 dB", "-9 dB", "-12 dB", "-15 dB", "-18 dB", "-21 dB", "-24 dB",
};

const char *const makeup_labels[MAKEUP_LABELS] = {"Off", "Auto"};

const char *const ratio_labels[RATIO_LABELS] = {"2:1", "4:1", "6:1", "10:1", "Limit"};

const char *const knee_labels[KNEE_LABELS] = {"Hard", "Soft"};

const char *const attack_labels[ATTACK_LABELS] = {
    "0 ms", "5 ms", "10 ms", "15 ms", "20 ms", "25 ms", "30 ms",
};

const char *const release_labels[RELEASE_LABELS] = {
    "100 ms", "200 ms", "300 ms", "400 ms", "500 ms",
    "600 ms", "700 ms", "800 ms", "900 ms", "1000 ms",
};
