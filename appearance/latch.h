/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef LATCH_H
#define LATCH_H

/* A switch blocks the UI task while it streams, and the presses that queue
 * meanwhile must not undo it: the first action latches and watches the
 * message loop; the latch opens and the watch ends once the loop reports an
 * empty queue after the switch is done. Nothing is bound while idle. */
int latch_begin(void);
void latch_finish(void);
/* Ends the watch, before the extension unloads. */
void latch_unload(void);

#endif
