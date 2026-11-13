# Chapter Menu

Adds Chapters to the context menu (hold Select) of a podcast or iTunes U episode, and to the Now Playing context menu, when the item has chapters. The screen lists each chapter's title with its start time, "m:ss" or "h:mm:ss".

| Opened from | Highlighted | Choosing a chapter |
|---|---|---|
| Now Playing | the chapter playing | seeks the track there and returns to Now Playing |
| An episode list | the first chapter | plays the episode from there in Now Playing, as choosing an audiobook chapter does |

A chapter with an empty title shows as "Chapter 3". Apple's library leaves out a chapter that has no name atom at all. Videos are left out; audiobooks have Apple's own chapter list. If playback moved to another track, or the library reloaded, while the list was open, choosing a chapter does nothing.

| | |
|---|---|
| Default | on |
| Memory | image only, plus 44 bytes for the row being drawn |
| Settings | none |
| Minimum SDK | 1.42 |
