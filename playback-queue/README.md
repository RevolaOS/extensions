# Playback Queue

Partly adapted from [RepriseOS](https://github.com/lgreitis/osos-lab/tree/c997a5147a3bca81499c58419073709b8d7a2e8f/payload), by lgreitis and brilliant-alien.

Adds Play Next and Play Last to the context menu of a song, album, artist or audio podcast episode, and a Queue screen of what comes next. Play Next inserts at the beginning of the queue, Play Last appends. Album and artist selections keep their internal order. Queue shows titles and artists or publishers in playback order, without the current item, and says "No Items in Queue" when it is empty.

| Action | Result |
|---|---|
| Select an item | plays it and opens Now Playing; it and the items skipped over leave the queue, and stay in the playback history for Previous |
| Hold Select on an item | move it up or down, send it to the top or bottom, or remove it; moves that would change nothing are hidden |
| Clear Queue, from the Queue or Now Playing context menu | removes every upcoming item after a confirmation; the current item keeps playing and repeat stays as set |

After removing an item the highlight moves to the item above it, or stays at the top. Returning to Queue highlights the first remaining item. The list updates as playback advances.

Adding items while nothing plays builds the queue without starting playback. Play/Pause starts the first pending item; selecting a row starts there and drops the pending items before it. Adding while paused leaves playback paused. Playing a selection from the library replaces the queue.

Podcast episodes keep Apple's saved position and played status. Video podcasts and whole-show additions are not supported. Queue editing needs a player-owned list of media entries without nested groups; other list kinds are left unchanged. Counts and indices use the core's 32-bit track counts.

Queue appears in Music and is hidden from the Main Menu; Settings > Music Menu and Settings > Main Menu change either. The screen's code stays loaded while the extension is enabled, so opening Queue reads nothing from disk.

| | |
|---|---|
| Default | on |
| Memory | image only |
| Settings | the two menu visibility flags |
| Minimum SDK | 1.33 |
