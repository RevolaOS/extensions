# Fast Seek

The same approach as [Seek MP4 audio by chunk instead of sample by sample](https://github.com/lgreitis/osos-lab/pull/63) by kayboll for RepriseOS, arrived at independently.

Apple's MP4 parser seeks an AAC or Apple Lossless file (M4A, M4B) by stepping through every sample between its current position and the target, reading the sample-size table from disk in 128 KB windows as it goes; a target behind the current position starts over from the first sample. A jump of several hours in an audiobook steps through more than a million frames and reads the table dozens of times, which is seconds on the CPU and many disk accesses on a hard-disk iPod. Choosing a chapter, dragging the scrubber or starting an episode at a remembered position all take that path.

The extension binds the entry of the sample walk. From the time-to-sample and sample-to-chunk tables, which are in memory, it works out the chunk that holds the target and sets the track's position to that chunk's first sample; Apple's own loop then walks the rest of the chunk, at most a few seconds of audio in iTunes files, and loads the table windows as usual. Files whose tables are not in the plain shape (more than one edit, more than one frame length before the target) seek as before.

Disabling it restores Apple's walk at once.

| | |
|---|---|
| Default | on |
| Memory | image only |
| Settings | none |
| Minimum SDK | 1.43 |
