# Chapters

Adds Chapters to the context menu (hold Select) of a podcast or iTunes U episode, and to the Now Playing context menu, when the item has chapters. The screen lists each chapter's title with its start time beside it, "m:ss" or "h:mm:ss".

| Opened from | Highlighted | Choosing a chapter |
|---|---|---|
| Now Playing | the chapter playing | seeks the track there and returns to Now Playing |
| An episode list | the first chapter | plays the episode from there in Now Playing, as choosing an audiobook chapter does |

A chapter with an empty title shows as "Chapter 3". Apple's library leaves out a chapter that has no name atom at all. Videos are left out; audiobooks have Apple's own chapter list. If playback moved to another track, or the library reloaded, while the list was open, choosing a chapter does nothing.

Enabled by default. The extension holds no memory beyond its image and 44 bytes for the row being drawn. No settings.

Build with `make SDK=/path/to/revola-sdk`; install `build/chapters.rvx` as `/Revola/Extensions/chapters.rvx`. Requires SDK/core API 1.36 or later and Apple firmware 2.0.5.

Run `make check SDK=/path/to/revola-sdk` for the row, seek and play contracts. Tests require Python 3 and a C compiler with AddressSanitizer and UndefinedBehaviorSanitizer. Native pointer layouts are widened for the host; emulator verification covers the actual ARM ABI and firmware lifecycle.
