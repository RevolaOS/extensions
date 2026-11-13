/* SPDX-License-Identifier: GPL-3.0-only */

/* Settings > Theme: a list of Classic and the themes in /Revola/Themes/,
 * scanned when the screen opens into a block sized by a count of the folder
 * and freed when it closes. More than THEMES entries end in a row saying so. */

#include "theme_picker.h"
#include "ext.h"
#include "look.h"
#include "rv_libc.h"
#include "theme.h"
#include <stddef.h>

#define THEMES 64
#define STRINGIFY_(n) #n
#define STRINGIFY(n) STRINGIFY_(n)

static struct theme_entry *entries; /* the scanned themes while the screen is open */
static uint32_t count;      /* themes in the list */
static int overflow;        /* the folder held more themes than the list shows */
static int scanned;         /* the folder was read since the screen opened */
static const char *problem; /* the picker's own trouble: no memory for the list */

/* The active theme stays listed when the sorted list is full: in order, in
 * place of the last entry when it sorts after every other. */
static void insert_active(uint32_t capacity)
{
    const struct look *active = look_active();
    uint32_t at = 0;
    while (at < count && memcmp(entries[at].id, active->id, RVT_ID_BYTES) < 0)
        at++;
    if (at < count && !memcmp(entries[at].id, active->id, RVT_ID_BYTES))
        return;
    if (count == capacity) {
        if (at == count)
            at--;
        else
            memmove(entries + at + 1, entries + at, (count - 1 - at) * sizeof *entries);
    } else {
        memmove(entries + at + 1, entries + at, (count - at) * sizeof *entries);
        count++;
    }
    memcpy(entries[at].id, active->id, RVT_ID_BYTES);
    memcpy(entries[at].name, active->name, RVT_NAME_BYTES);
}

/* The list is scanned the first time the screen asks for it, which is while
 * it is built, before `opened`. */
static void scan(void)
{
    if (scanned)
        return;
    scanned = 1;
    uint32_t capacity = theme_count();
    if (!capacity)
        return;
    if (capacity > THEMES)
        capacity = THEMES;
    entries = api->alloc(self, capacity * sizeof *entries);
    if (!entries) {
        problem = "Not enough memory";
        return;
    }
    count = theme_scan(entries, capacity, &overflow);
    if (overflow && count && look_active()->id[0])
        insert_active(capacity);
}

void theme_picker_closed(const struct rv_context *ctx)
{
    (void)ctx;
    if (entries)
        api->free(self, entries);
    entries = NULL;
    problem = NULL;
    count = overflow = scanned = 0;
}

static const char *trouble(void)
{
    return problem ? problem : look_problem(PANE_THEME);
}

uint32_t theme_picker_rows(const struct rv_context *ctx)
{
    (void)ctx;
    scan();
    return 1 + count + (overflow || trouble());
}

const char *theme_picker_row(const struct rv_context *ctx, uint32_t index, const char **detail)
{
    (void)ctx;
    *detail = NULL;
    if (index == 0)
        return "Classic";
    if (index <= count)
        return entries[index - 1].name;
    return trouble() ? trouble() : "Showing " STRINGIFY(THEMES) " themes";
}

int theme_picker_select(const struct rv_context *ctx, uint32_t index)
{
    (void)ctx;
    if (index > count)
        return 0;
    look_select(index ? entries[index - 1].id : "");
    return 0;
}

uint32_t theme_picker_initial(const struct rv_context *ctx)
{
    (void)ctx;
    const struct look *active = look_active();
    for (uint32_t i = 0; i < count; i++)
        if (!memcmp(entries[i].id, active->id, RVT_ID_BYTES))
            return i + 1;
    return 0;
}
