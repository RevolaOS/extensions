# Building and testing

Every extension builds against an unpacked revola SDK, from the [revola releases](https://github.com/RevolaOS/revola/releases) or built with `python3 tools/build.py sdk` in a revola checkout, which writes `build/revola-sdk-<version>.tar.gz`. Every extension targets Apple firmware 2.0.5, the only firmware the SDK describes. The SDK must match the core on the iPod: an extension built against another API major or firmware id shows as Incompatible in Settings > Extensions.

## Build

```sh
make -C <directory> SDK=/path/to/revola-sdk
```

The result is `<directory>/build/<id>.rvx`. Copy it to `/Revola/Extensions/<id>.rvx` on the iPod, or onto an emulator disk with `ipodvm disk --copy`. `make SDK=... THEMES=...` at the top level builds every extension into `dist/<id>.rvx`, which is what a release packages; `make check SDK=...` there runs every host test suite.

The top-level build also writes `dist/assets.json`, one record per bitmap asset of the core and of every extension (id, asset, format, size, whether it is a menu bitmap): the catalog a theme is checked against when it replaces extension artwork. The themes repository vendors it.

`THEMES` names a checkout of the [themes repository](https://github.com/RevolaOS/themes) at the commit recorded in `appearance/themes.lock`. The appearance extension compiles its Classic dark artwork from `themes/classic` there and checks its vendored copies of the theme file header and the bitmap catalog against that checkout. Only `make -C appearance` needs it; without it the build keeps the Classic artwork it already has and fails only when there is none.

Requirements are those of the SDK: GNU make, Rockbox's `arm-elf-eabi-` GCC 9.5.0 and Python 3.11+.

## Host tests

Extensions with a `tests/` directory run contract tests on the host:

```sh
make -C <directory> check SDK=/path/to/revola-sdk
```

They need Python 3 and a C compiler with AddressSanitizer and UndefinedBehaviorSanitizer. The tests compile the extension's logic natively with widened pointer layouts; the ARM ABI and the firmware lifecycle are verified in the emulator, not here.

| Extension | Host tests cover |
|---|---|
| Appearance | the theme file reader (records, lz streams, derivations, recovery), the bitmaps a theme holds for extensions and the provider that serves them, the theme folder scan, the colour mapping of the stock screens, tinted art and hook binding |
| Chapter Marks | the crowding rule |
| Chapter Menu | row, seek and play contracts |
| Compressor, Crossfeed, Channels | an integer model of Rockbox's loops |
| Equalizer | the filter engine |
| File Info | row and formatting contracts |
| Progress Indicator | playback, bitmap and unload contracts |

## Settings icons

An extension with a row under Settings ships a menu bitmap in `assets/`. The DSP extensions draw theirs with `assets/render.py` and ImageMagick; `python3 assets/render.py --check` compares the committed PNG with a fresh render.
