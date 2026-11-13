/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef MENU_COVERS_H
#define MENU_COVERS_H
#include "rv_api.h"

/* Menu Covers: whether the Main and Music menus show the highlighted row's
 * cover. The setting is Theme (what the look says; stock shows them), On or
 * Off. */
enum { COVERS_THEME, COVERS_ON, COVERS_OFF };
extern const char *const menu_covers_labels[3];
void menu_covers_set(int choice);
int menu_covers_enabled(void);
/* (provider, slot): the artwork lookup the menus make; 0 hides the cover. */
uint32_t menu_covers_query(uint32_t provider, uint32_t slot, const void *next);

#endif
