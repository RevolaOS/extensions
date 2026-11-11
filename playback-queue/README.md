# Playback Queue

Partly adapted from [RepriseOS](https://github.com/lgreitis/osos-lab/tree/c997a5147a3bca81499c58419073709b8d7a2e8f/payload), by lgreitis and brilliant-alien.

Use Play Next or Play Last from a song, album, artist or audio podcast episode's context menu. Play Next inserts at the beginning of Queue; Play Last appends. Album and artist selections keep their internal order. Queue shows titles and artists or publishers in playback order and excludes the current item.

Select an upcoming item to start playing it and open Now Playing. Returning to Queue highlights the first remaining item. This removes the current item and any skipped items from the queue. Items played earlier remain in the playback history, so you can return to them with the Previous/Rewind button. Hold Select on an upcoming item to move it up or down, send it to the top or bottom, or remove it. After removing an item, the highlight moves to the item above it, or stays at the top. Move commands that wouldn't change the order are hidden.

Adding items while idle builds the queue without starting playback. Play/Pause starts the first pending item; selecting a row starts there and discards preceding pending items. Adding while paused leaves playback paused. Playing a selection directly from the library replaces the queue. Podcast episodes use Apple’s saved playback position and played-status handling. Video podcasts and whole-show additions are not supported.

Choose Clear Queue from the Queue or Now Playing context menu to remove all upcoming items. You'll be asked to confirm. The current item keeps playing, and repeat behavior stays unchanged.

Playback Queue is enabled by default. Queue appears in Music and is hidden from the Main Menu. You can change either through Settings > Music Menu or Settings > Main Menu. The list updates as playback advances and shows “No Items in Queue” when the queue is empty.

The screen's code stays in memory while the extension is enabled, so opening Queue doesn't load another module from disk. Queue editing requires a player-owned list of media entries without nested groups; other list types are left unchanged. Counts and indices use the core's 32-bit track-count support.

Build with `make -C playback-queue SDK=/path/to/revola-sdk` from the extensions repository, using SDK API 1.33 or later. Copy `playback-queue/build/playback-queue.rvx` to `/Revola/Extensions/playback-queue.rvx` on an iPod running the matching core.
