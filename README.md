# extensions

First-party RevolaOS extensions. One directory per extension, each with its own `manifest.toml`, built against a pinned revola SDK release.

Each extension is one file on the iPod, `/Revola/Extensions/<id>.rvx`, and can be turned on and off in Settings > Extensions. A disabled extension uses no RAM.

| Directory | Extension | Default |
|---|---|---|
| `clicker-modes` | Clicker Modes: choose where the click wheel sound plays, the iPod's speaker, headphones, both or off | on |
| `eu-volume-limit` | EU Volume Limit: with EU Volume Limit off, volume goes 15% higher than stock, as on non-EU iPods, without the volume warning or automatic lowering | on |
| `file-info` | File Info: a song, episode, audiobook or video's tags and file properties, from its context menu and Now Playing | on |
| `iap-auth-bypass` | iAP Auth Bypass: let accessories use authentication-gated iAP commands without an Apple authentication chip | on |
| `playback-queue` | Playback Queue: Play Next, Play Last, and a list to reorder, remove or clear upcoming songs and audio podcast episodes, including queueing while idle | on |
| `progress-indicator` | Progress Indicator: half-filled status circles for partially played podcasts and iTunes U items | on |
| `usb-debug` | USB Debug: lets a computer inspect and control the iPod over USB for debugging while music keeps playing; turn it off to sync | off |

Build one with `make -C <directory> SDK=/path/to/revola-sdk`; `<directory>/build/<id>.rvx` is what goes to `/Revola/Extensions/<id>.rvx`.

## What drives development

1. **RAM first.** 64 MiB in total, about 20 MiB of heap shared with Apple's music database. Every pull request reports resident and peak bytes from the emulator heap profile. An increase needs a stated reason. A disabled extension costs zero bytes. Reuse what exists before adding anything.
2. **Performance second.** No work in audio callbacks beyond the DSP itself. No redraw of unchanged pixels. No disk reads for things that can be computed. No per-draw checks when a one-time hook can decide. Hot paths bind directly; the dispatcher runs only when two handlers share a site.
3. **Optimise before merge.** Measure, remove what measurement does not justify, and stop when the next step costs more clarity than it saves. "Works" is not the bar.
4. **Complexity matches need.** No abstraction for one caller, no layer that only forwards. Defer anything with one user, delete anything with none. A person must be able to reason about the code.
5. **Clean structure.** One concern per directory. Extensions never reach into core internals. Apple addresses come only from the generated symbols header. Hooks bind core sites only: when no site covers the code, add one to the core instead of declaring a raw `[[site]]` (CI rejects them).
6. **Measure, do not guess.** Emulator heap profile for every RAM claim; hardware for audio, cache and storage behaviour.

## Core or extension

Something is core only if the loader, SDK or settings store needs it, if it must act before the loader can run, or if the boot cannot recover without it. Everything else is an extension, however small. One extension per concern. If one feature cannot work without another, it is a module of that extension, not a dependency.


## License

GPL-3.0-only. See COPYING and NOTICE. Third-party extensions must use a GPL-compatible license because they run in the same address space as the core.
