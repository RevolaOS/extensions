# USB Debug protocol

Vendor control requests to the device (IN bmRequestType 0xC0, OUT 0x40), all little-endian. Other requests go to Apple's handlers. Invalid arguments stall, and so does an IN request with wLength above 4096. A reply shorter than wLength may end with 4 bytes of padding: Apple sends no zero-length packet, so a reply must not end on a packet boundary.

## Requests

| bRequest | Dir | wValue / wIndex | Data |
|---|---|---|---|
| 0xD0 INFO | IN | 0 / 0 | 64 B, see below |
| 0xD1 READ | IN | address low / high | up to 4096 B from DRAM, IRAM or the display pipe's registers (whole words) |
| 0xD2 SET_ADDRESS | OUT | address low / high | |
| 0xD3 WRITE32 | OUT | value low / high | writes the word at the address, which then advances by 4; aligned DRAM or IRAM only |
| 0xD4 INPUT | OUT | kind / argument | 1 buttons (WheelButton mask), 2 touch (wheel position 0..95), 3 lift. One step per request; the host waits between steps |
| 0xD5 PROFILE | OUT | rate Hz / ring entries | starts PC sampling; 0 / 0 stops and frees the ring |
| 0xD6 SAMPLES | IN | 0 / 0 | {count, dropped since the last drain}, then count × {pc, lr, µs} |
| 0xD7 SCREEN | OUT | 0 / 0 | captures every string drawn in the next full redraw, the text of scrolling (marquee) rows included |
| 0xD8 SCREEN_DATA | IN | 0 / 0 | header {"RVSC", status (0 pending, 1 ready), count u16, bytes u32}, then records {top, left, bottom, right i16, colour u32, view vtable u32, length u8, UTF-8 text} padded to 4 bytes; reading a ready capture frees it |
| 0xD9 CLOCK | IN | 0 / 0 | µs u32 from Apple's microsecond counter, which starts with OSOS (not at reset) and wraps after 71.6 minutes |
| 0xDA BOOT | IN | 0 / 0 | µs at the extension's entry, at the first main menu (the first layout shown without an animation, 0 when that was another layout) and at the first UI timer tick after the entry; the FLUSH sequence number at the entry and the last one written. The times describe the boot when the extension loaded at boot |
| 0xDB HEAP_WATCH | OUT | period ms / 0 | samples Apple's free heap bytes on a UI timer from now on; 0 stops |
| 0xDC HEAP_STATS | IN | 0 / 0 | {samples, latest, minimum, maximum free bytes, heap total} |
| 0xDD STACK | IN | 0 or 1 / 0 | {UI stack base, size, top of the filled part, lowest word that changed}; the stack is filled with a pattern from its base to 512 bytes below the stack pointer at the entry, and 1 refills it at the next UI timer tick after replying. Stalls when the entry did not run on the UI stack |
| 0xDE FAULT | OUT | kind / 0 | at the next UI timer tick: 1 undefined instruction, 2 Runtime_Abort, 3 data abort (a read of MMU_UNMAPPED_ADDRESS) |
| 0xDF RESET | OUT | 0 / 0 | warm reset (WarmBootMode_None) about 50 ms after the reply |
| 0xE0 FLUSH | OUT | mode / delay | at the next UI timer tick, stores the next sequence number and flushes the settings. Mode 1 then resets: `delay` × 100 µs after the flush began (from an IRQ, so possibly during the write), or as soon as it returns when `delay` is 0 |
| 0xE1 TIME_SITE | OUT | site id low / high | at the next UI timer tick, puts a timing filter in front of every handler on the core site (RV_SITE_* id), up to 8 sites; 0 removes them all. A site that takes no filter, or whose exclusive handler is bound, is not timed |
| 0xE2 TIMINGS | IN | 0 / 0 | {count}, then count × {site id, calls, total, minimum, maximum µs} |
| 0xE3 EVENTS | OUT | ring entries / 0 | at the next UI timer tick, starts the event log; 0 stops and frees it |
| 0xE4 EVENT_LOG | IN | 0 / 0 | {count, dropped since the last drain}, then count × {µs, kind, argument}: the core's rv_trace_kind events, 0x100 disk controller powered (argument 1) or off (0), 0x101 the audio output queue's underrun count changed (argument the count; one per output buffer filled with silence). The disk and underrun state is polled every 50 ms; a powered controller does not mean a spinning disk |
| 0xE5 HOLD | OUT | state / 0 | at the next UI timer tick: 1 locked, 2 unlocked, 0 back to the switch. Buttons and the lock icon follow; wheel motion does not read the switch and keeps scrolling |
| 0xE6 EXTENSION | OUT | 1 on, 0 off / 0 | at the next UI timer tick, turns on or off the extension whose file name (without `.rvx`) the host wrote into the 32-byte buffer INFO names, as Settings > Extensions does. Stalls while one is pending |
| 0xE7 EXTENSION_RESULT | IN | 0 / 0 | {state (0 none, 1 pending, 2 done), rv_result} |

INFO: "RVUD", protocol version u16 (2), pad, firmware id, core build, maximum transfer, display pipe base, Apple heap total and free bytes, address of the core's extension list head, u16 offsets of next, owner, file (the extension's file name without `.rvx`), live, peak and failure in a core extension record, the clock rate in Hz, then the address of EXTENSION's name buffer.

## Host sequencing

TIME_SITE has one pending request slot. After requesting a site, poll TIMINGS until that site appears before sending another registration; after requesting stop, wait for an empty result. An unsupported or exclusively bound site may never appear, so use a bounded wait and report that it was not timed. Stop timing after collection or registration errors as well. An unacknowledged stop means timing may still be active and must be reported as a failure.

INPUT wheel movements use Apple's accumulator and are not exact list-row increments. The host waits for new motion to be consumed before lifting. This does not lock navigation to a screen: wheel motion on Now Playing changes volume, and a preceding SCREEN capture cannot prevent a later screen transition.

EVENT_LOG's underrun count covers output buffers filled with silence because the queue was empty. Partial buffers are not counted. Disk-power events describe the ATA controller clock gate, not the drive's spin state.

FAULT enters a panic handler with interrupts stopped, so USB Debug cannot receive a subsequent RESET there. Use the panic screen's fresh Center press for controlled recovery. Menu + Center is a different reset path and may discard the pending crash record.

## Emulator mailbox

The emulator has no USB controller. `make SDK=… MAILBOX=1` builds a variant that also services a mailbox in its own memory every 100 ms through the same request handler. The mailbox address is the core's last log line.

| Field | |
|---|---|
| tag | "USBDBG" |
| setup | the 8-byte setup packet |
| status | 1 request, 2 done, 3 stall, 4 not this extension's |
| reply | address and length |

Never install that build on a device.
