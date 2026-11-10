# USB Debug

Debugs RevolaOS on a device over USB. While enabled, a plugged-in iPod never enters disk mode: the UI and playback keep running, and `revola debug` on the host reads and writes memory, takes screenshots, presses buttons, watches the heap and the UI stack, samples the CPU, times the boot, and raises faults, flushes and resets for recovery tests. To sync, disable the extension, then unplug and replug the iPod.

Disabled by default. While enabled and idle, it holds no memory beyond its image. The profiler ring and capture buffers exist only while in use. It stores one setting, the FLUSH sequence number.

## Protocol

Vendor control requests to the device (IN bmRequestType 0xC0, OUT 0x40), all little-endian. Other requests go to Apple's handlers. Invalid arguments stall, and so does an IN request with wLength above 4096. A reply shorter than wLength may end with 4 bytes of padding: Apple sends no zero-length packet, so a reply must not end on a packet boundary.

| bRequest | Dir | wValue / wIndex | Data |
|---|---|---|---|
| 0xD0 INFO | IN | 0 / 0 | 64 B: see below |
| 0xD1 READ | IN | address low / high | up to 4096 B from DRAM, IRAM or the display pipe's registers (whole words) |
| 0xD2 SET_ADDRESS | OUT | address low / high | |
| 0xD3 WRITE32 | OUT | value low / high | writes the word at the address, which then advances by 4; aligned DRAM or IRAM only |
| 0xD4 INPUT | OUT | kind / argument | 1 buttons (WheelButton mask), 2 touch (wheel position 0..95), 3 lift. One step per request; the host waits between steps |
| 0xD5 PROFILE | OUT | rate Hz / ring entries | starts PC sampling; 0 / 0 stops and frees the ring |
| 0xD6 SAMPLES | IN | 0 / 0 | {count, dropped since the last drain}, then count × {pc, lr, µs} |
| 0xD7 SCREEN | OUT | 0 / 0 | captures every string drawn in the next full redraw |
| 0xD8 SCREEN_DATA | IN | 0 / 0 | header {"RVSC", status (0 pending, 1 ready), count u16, bytes u32}, then records {top, left, bottom, right i16, colour u32, view vtable u32, length u8, UTF-8 text} padded to 4 bytes; reading a ready capture frees it |
| 0xD9 CLOCK | IN | 0 / 0 | µs u32 from Apple's microsecond counter, which starts with OSOS (not at reset) and wraps after 71.6 minutes |
| 0xDA BOOT | IN | 0 / 0 | µs at the extension's entry, at the first main menu (0 until known) and at the first UI timer tick after the entry; the FLUSH sequence number at the entry and the last one written |
| 0xDB HEAP_WATCH | OUT | period ms / 0 | samples Apple's free heap bytes on a UI timer from now on; 0 stops |
| 0xDC HEAP_STATS | IN | 0 / 0 | {samples, latest, minimum, maximum free bytes, heap total} |
| 0xDD STACK | IN | 0 or 1 / 0 | {UI stack base, size, top of the filled part, lowest word that changed}; the stack is filled with a pattern from its base to 512 bytes below the stack pointer at the entry, and 1 refills it at the next UI timer tick after replying. Stalls when the entry did not run on the UI stack |
| 0xDE FAULT | OUT | kind / 0 | at the next UI timer tick: 1 undefined instruction, 2 Runtime_Abort |
| 0xDF RESET | OUT | 0 / 0 | warm reset (WarmBootMode_None) about 50 ms after the reply |
| 0xE0 FLUSH | OUT | mode / delay | at the next UI timer tick, stores the next sequence number and flushes the settings. Mode 1 then resets: `delay` × 100 µs after the flush began (from an IRQ, so possibly during the write), or as soon as it returns when `delay` is 0 |

INFO: "RVUD", protocol version u16 (2), pad, firmware id, core build, maximum transfer, display pipe base, Apple heap total and free bytes, address of the core's extension list head, u16 offsets of next, owner, file (the extension's file name without `.rvx`), live, peak and failure in a core extension record, then the clock rate in Hz.

TODO: hold switch input needs a site on the hold switch read.

## Emulator test build

The emulator has no USB controller. `make SDK=… MAILBOX=1` builds a variant that also services a mailbox in its own memory every 100 ms through the same request handler. The mailbox address is the core's last log line. Layout: tag "USBDBG", the 8-byte setup packet, status (1 request, 2 done, 3 stall, 4 not this extension's), reply address, reply length. Never install that build on a device.
