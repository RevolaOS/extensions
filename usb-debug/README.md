# USB Debug

Debugs RevolaOS on a device over USB. While enabled, a plugged-in iPod never enters disk mode: the UI and playback keep running, and `revola debug` on the host reads and writes memory, takes screenshots, presses buttons, watches the heap and the UI stack, samples the CPU, times the boot and any core hook site, logs disk, audio and core events, injects the hold switch, turns extensions on and off, and raises faults, flushes and resets for recovery tests. To sync, disable the extension, then unplug and replug the iPod.

The host side is `revola debug` from the installer repository, described in its [debugging page](https://github.com/RevolaOS/installer/blob/main/docs/debug.md). The wire protocol is in [PROTOCOL.md](PROTOCOL.md).

The emulator has no USB controller. `make SDK=… MAILBOX=1` builds a variant that also services a mailbox in its own memory through the same request handler, described there too; never install that build on a device.

| | |
|---|---|
| Default | off |
| Memory | image only while idle; the profiler ring, the event log and capture buffers exist only while in use |
| Settings | the FLUSH sequence number |
| Minimum SDK | 1.28 |
