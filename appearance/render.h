/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef RENDER_H
#define RENDER_H

/* The draw hooks: the colour sites that map the stock colours to the roles in
 * force, the blits that colour highlighted tinted art and the flat cover.
 * Bound while a look other than stock light is on, unbound otherwise, so
 * the stock look draws without them; 1 when the hooks are as asked. */
int render_bind(int on);

#endif
