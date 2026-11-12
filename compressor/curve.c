/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Ported from Rockbox lib/rbcodec/dsp/compressor.c.
 *
 * Copyright (C) 2009 Jeffrey Goode
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/* The shape of Rockbox's compression curve as decibels: no change below the
 * knee, a straight run across the knee to its offset, then the ratio's slope
 * up to full scale and two bits over. Both modules carry this; the editor
 * plots it, design.c turns it into gains. */

#include "compressor.h"

enum {
    DB_ONE = 1 << 16,
    KNEE_HALF_DB = 3,      /* the soft knee spans the threshold +- 3 dB */
    OVER_DB = 0xC0A8C,     /* two bits above full scale, about 12.04 dB */
    MAKEUP_HEADROOM = 0x4AC4, /* Rockbox's margin under the full-scale offset, about 0.3 dB */
};

/* The gain offset for a rise of `db` in input above the threshold: the input
 * rises, the output rises by 1/ratio of it; Limit holds the output. */
static int32_t slope(int32_t db, int ratio)
{
    return ratio ? -db * (ratio - 1) / ratio : -db;
}

void compressor_points(const struct compressor_set *set, struct compressor_points *out)
{
    static const int8_t ratios[RATIO_COUNT] = {2, 4, 6, 10, 0};
    int ratio = ratios[set->ratio];
    int32_t threshold = set->threshold_db * DB_ONE;
    int32_t knee = set->knee ? KNEE_HALF_DB * DB_ONE : 0;

    out->db[0] = threshold - knee;
    out->db[1] = threshold;
    out->db[2] = threshold + knee;
    out->db[3] = 0;
    out->db[4] = OVER_DB;
    out->offset[0] = 0;
    out->offset[2] = slope(knee, ratio);
    out->offset[1] = out->offset[2] / 2;
    out->offset[3] = slope(-threshold, ratio);
    out->offset[4] = slope(OVER_DB, ratio) + out->offset[3];
    out->makeup = set->makeup ? -out->offset[3] - MAKEUP_HEADROOM : 0;
}
