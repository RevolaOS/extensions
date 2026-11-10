# USB Debug

Debugs RevolaOS on a device over USB. While enabled, a plugged-in iPod never enters disk mode: the UI and playback keep running, and `revola debug` on the host reads and writes memory, takes screenshots, presses buttons, watches the heap and samples the CPU. To sync, disable the extension, then unplug and replug the iPod.

Disabled by default. While enabled and idle, it holds no memory beyond its image. The profiler ring and capture buffers exist only while in use.

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
| 0xD6 SAMPLES | IN | 0 / 0 | {count, dropped since the last drain}, then count × {pc, lr} |
| 0xD7 SCREEN | OUT | 0 / 0 | captures every string drawn in the next full redraw |
| 0xD8 SCREEN_DATA | IN | 0 / 0 | header {"RVSC", status (0 pending, 1 ready), count u16, bytes u32}, then records {top, left, bottom, right i16, colour u32, view vtable u32, length u8, UTF-8 text} padded to 4 bytes; reading a ready capture frees it |

INFO: "RVUD", protocol version u16 (1), pad, firmware id, core build, maximum transfer, display pipe base, Apple heap total and free bytes, address of the core's extension list head, then u16 offsets of next, owner, file (the extension's file name without `.rvx`), live, peak and failure in a core extension record.

TODO: hold switch input needs a site on the hold switch read.

## Emulator test build

The emulator has no USB controller. `make SDK=… MAILBOX=1` builds a variant that also services a mailbox in its own memory every 100 ms through the same request handler. The mailbox address is the core's last log line. Layout: tag "USBDBG", the 8-byte setup packet, status (1 request, 2 done, 3 stall, 4 not this extension's), reply address, reply length. Never install that build on a device.
