/* SPDX-License-Identifier: GPL-3.0-only */

#include "menu_covers.h"
#include "ext.h"
#include "rvt.h"
#include "style.h"

const char *const menu_covers_labels[3] = {"Theme", "On", "Off"};
static int choice; /* the setting */

void menu_covers_set(int value)
{
    choice = value;
}

int menu_covers_enabled(void)
{
    if (choice == COVERS_THEME)
        return !(style_flags() & RVT_FLAG_MENU_COVERS_OFF);
    return choice == COVERS_ON;
}

uint32_t menu_covers_query(uint32_t provider, uint32_t slot, const void *next)
{
    if (slot == 0 && !menu_covers_enabled())
        return 0;
    return api->hook_next(next, provider, slot, 0, 0, 0, 0);
}
