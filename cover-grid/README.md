# Cover Grid

Browse albums as a grid of covers. Cover Grid is a row in Music, next to Cover Flow, and on the main menu; it runs Cover Flow's own library query, so it lists the same music-only albums sorted by artist and does not open for a library without music. It reopens where it was left, as Cover Flow does.

The grid is four columns of covers with two rows above the caption and a third peeking in below them. The selected cover is lifted over its neighbours inside an edge in the selection colour and a soft shadow, so the selection reads on any artwork. The album and artist of the selected cover appear in the bar along the bottom, scrolling when they are too long for it. A scrollbar appears while the grid scrolls and fades out again.

| Action | Result |
|---|---|
| Wheel | moves the selection, scrolling by whole rows once it leaves the two full rows |
| Select | opens the album's songs, at the top of the list |
| Menu | leaves the grid |

Covers stream in from the same loader Cover Flow uses, off the drawing path, so the grid opens at once and fills as its covers arrive; albums whose cover has not arrived show Cover Flow's placeholder. Each cover is scaled once and kept while the grid is on screen. Colours follow the stock roles, so a theme restyles the grid with the rest of the system.

Leaving the grid frees the covers. Opening an album's songs keeps them for a second in case you come straight back, then frees them too; returning streams them in again.

| | |
|---|---|
| Default | off |
| Memory | the extension image while closed; about 160 KB of scaled covers and the artwork session while the grid is on screen |
| Settings | the two menu visibility flags |
| Minimum SDK | 1.50 |
