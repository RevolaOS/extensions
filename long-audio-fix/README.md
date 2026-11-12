# Long Audio Fix

The same fix as [Play MP4 audiobooks past 13.5 hours](https://github.com/lgreitis/osos-lab/pull/62) by kayboll for RepriseOS, arrived at independently.

Apple's MP4 parser stops playing an AAC or Apple Lossless file (M4A, M4B) once its position passes 2^31 samples: 13:31:35 at 44.1 kHz, 12:25:39 at 48 kHz. From there every sample is treated as lying outside the file's edit list and dropped, so the track ends and the next one starts. A seek past that point ends the track at once. Files without an edit list are not affected, but iTunes, ffmpeg and most audiobook tools write one.

The parser keeps the decode time in 64 bits and compares it with the edit list in 32. The extension replaces the three sign-extensions of the low word with loads of the high word, so the compares see the full value.

Disabling it restores Apple's compares at once.

| | |
|---|---|
| Default | on |
| Memory | image only |
| Settings | none |
| Minimum SDK | 1.43 |
