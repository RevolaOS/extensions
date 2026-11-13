/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef THEME_PICKER_H
#define THEME_PICKER_H
#include "rv_api.h"
#include "rv_ui.h"

/* The Theme screen: Classic, then the themes in the folder. */
uint32_t theme_picker_rows(const struct rv_context *ctx);
const char *theme_picker_row(const struct rv_context *ctx, uint32_t index, const char **detail);
int theme_picker_select(const struct rv_context *ctx, uint32_t index);
uint32_t theme_picker_initial(const struct rv_context *ctx);
void theme_picker_closed(const struct rv_context *ctx);
#define THEME_PICKER_SCREEN                                                                   \
    RV_SCREEN_DYNAMIC_LIST("theme", "Theme", theme_picker_rows, theme_picker_row,             \
                           theme_picker_select, .dynamic.initial = theme_picker_initial,      \
                           .closed = theme_picker_closed, .bitmap = RV_BITMAP("settings-theme"))

#endif
