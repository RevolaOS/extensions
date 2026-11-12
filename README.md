# extensions

First-party RevolaOS extensions. One directory per extension, each with its own `manifest.toml`, built against a pinned revola SDK release. An extension is one file on the iPod, `/Revola/Extensions/<id>.rvx`, turned on and off in Settings > Extensions. A disabled extension uses no RAM.

| Directory | Extension | What it does | Default |
|---|---|---|---|
| `channels` | Channels | Rockbox's channel modes and a stereo width from 0 to 250 % | off |
| `chapter-marks` | Chapter Marks | Leaves the chapter marks off the Now Playing bar when there are too many to tell apart | on |
| `chapters` | Chapters | Chapter list of a podcast, iTunes U episode or the playing track; choosing one plays from there | on |
| `clicker-modes` | Clicker Modes | Click wheel sound on the speaker, headphones, both or off | on |
| `compressor` | Compressor | Rockbox's compressor with a graphical editor | off |
| `crossfeed` | Crossfeed | Rockbox's crossfeed for headphones, Simple or Custom | off |
| `equalizer` | Equalizer | Apple's EQ presets without distortion, and a five-band Custom preset | on |
| `eu-volume-limit` | EU Volume Limit | Full volume range with the EU limit switched off | on |
| `fast-seek` | Fast Seek | Seeks in AAC and Apple Lossless files jump to the target instead of stepping through every frame | on |
| `file-info` | File Info | Tags and file properties of a song, episode, audiobook or video | on |
| `iap-auth-bypass` | iAP Auth Bypass | Authentication-gated iAP commands without an Apple authentication chip | on |
| `long-audio-fix` | Long Audio Fix | AAC and Apple Lossless files longer than 13.5 hours play to the end | on |
| `playback-queue` | Playback Queue | Play Next, Play Last and a queue to reorder | on |
| `progress-indicator` | Progress Indicator | Half-filled status circles for partially played podcasts and iTunes U items | on |
| `usb-debug` | USB Debug | Inspect and control the iPod from a computer over USB | off |

## Build

```sh
make -C <directory> SDK=/path/to/revola-sdk   # one extension, into <directory>/build/<id>.rvx
make SDK=/path/to/revola-sdk                  # every extension, into dist/<id>.rvx
```

`<id>.rvx` is what goes to `/Revola/Extensions/<id>.rvx`. Tests, icons and the SDK are in [Building and testing](docs/building.md).

## Documentation

| Page | Contents |
|---|---|
| [docs/](docs/README.md) | Building, conventions, the shared audio stage |
| [SDK](https://github.com/RevolaOS/revola/tree/main/docs/sdk) | Writing an extension |
| [STYLE.md](STYLE.md) | Principles and rules for every file |

## Development

Large language models are used for much of the coding on RevolaOS. Their output is checked in the emulator, its memory use comes from the heap profile, and every release is tested on an iPod before it ships. Contributions written with the same tools are welcome and tested in the same way.

## License

GPL-3.0-only. See COPYING and NOTICE. Third-party extensions must use a GPL-compatible license because they run in the same address space as the core.
