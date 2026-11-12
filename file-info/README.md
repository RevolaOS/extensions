# File Info

Adds File Info to the context menu (hold Select) of a song, podcast episode, iTunes U episode, audiobook, movie, TV episode or video playlist entry, and to the Now Playing context menu. The screen lists the item's tags and file properties as label and value rows.

A rewrite of [Song Info](https://github.com/lgreitis/osos-lab/commit/beef0e9acbfda31bc679d35d649b6a63380e3d5f) by lgreitis in RepriseOS.

| Media kind | Rows |
|---|---|
| Song | Title, Artist, Album, Year, Genre, Composer, Grouping, Comment, Duration, Track, Disc, Kind, Sample Rate, Bitrate, File Size, Volume Adjustment, Rating, Play Count, Last Played, Skip Count, Last Skipped, Date Added, Date Modified |
| Podcast episode | Title, Podcast, Artist, Category, Genre, Description, Release Date, Duration, Chapters, Kind, Sample Rate, Bitrate, File Size, Volume Adjustment, Play Count, Last Played, Date Added |
| iTunes U episode | as a podcast episode, with Album in place of Podcast |
| Audiobook | Title, Author, Book, Genre, Description, Duration, Chapters, Kind, Sample Rate, Bitrate, File Size, Volume Adjustment, Play Count, Last Played, Date Added |
| Music video | Title, Artist, Album, Year, Genre, Comment, Duration, Kind, Bitrate, File Size, Volume Adjustment, Rating, Play Count, Last Played, Date Added |
| Movie, TV episode, rental | Title, Show, Year, Genre, Description, Release Date, Duration, Kind, Bitrate, File Size, Volume Adjustment, Rating, Play Count, Last Played, Date Added |

A row without a value is left out. Kind is iTunes's file kind, such as "AAC audio file". Track and Disc show "3 of 12" when the total is known. Dates are local time, "2024-03-05 14:07". Volume Adjustment is the Sound Check offset as a percentage. Rating shows stars out of five or Unrated. File Size is in MiB with one decimal. Values are read from Apple's library when the screen opens; if the library reloads underneath it, the values go blank.

Enabled by default. The extension holds no memory beyond its image and a row table of about 100 bytes. No settings.

Build with `make SDK=/path/to/revola-sdk`; install `build/file-info.rvx` as `/Revola/Extensions/file-info.rvx`. Requires SDK/core API 1.35 or later and Apple firmware 2.0.5.

Run `make check SDK=/path/to/revola-sdk` for the row and formatting contracts. Tests require Python 3 and a C compiler with AddressSanitizer and UndefinedBehaviorSanitizer. Native pointer layouts are widened for the host; emulator verification covers the actual ARM ABI and firmware lifecycle.
