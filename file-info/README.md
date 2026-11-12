# File Info

A rewrite of [Song Info](https://github.com/lgreitis/osos-lab/commit/beef0e9acbfda31bc679d35d649b6a63380e3d5f) by lgreitis in RepriseOS.

Adds File Info to the context menu (hold Select) of a song, podcast episode, iTunes U episode, audiobook, movie, TV episode or video playlist entry, and to the Now Playing context menu. The screen lists the item's tags and file properties as label and value rows.

| Media kind | Rows |
|---|---|
| Song | Title, Artist, Album, Year, Genre, Composer, Grouping, Comment, Duration, Track, Disc, Kind, Sample Rate, Bitrate, File Size, Volume Adjustment, Rating, Play Count, Last Played, Skip Count, Last Skipped, Date Added, Date Modified |
| Podcast episode | Title, Podcast, Artist, Category, Genre, Description, Release Date, Duration, Chapters, Kind, Sample Rate, Bitrate, File Size, Volume Adjustment, Play Count, Last Played, Date Added |
| iTunes U episode | as a podcast episode, with Album in place of Podcast |
| Audiobook | Title, Author, Book, Genre, Description, Duration, Chapters, Kind, Sample Rate, Bitrate, File Size, Volume Adjustment, Play Count, Last Played, Date Added |
| Music video | Title, Artist, Album, Year, Genre, Comment, Duration, Kind, Bitrate, File Size, Volume Adjustment, Rating, Play Count, Last Played, Date Added |
| Movie, TV episode, rental | Title, Show, Year, Genre, Description, Release Date, Duration, Kind, Bitrate, File Size, Volume Adjustment, Rating, Play Count, Last Played, Date Added |

A row without a value is left out.

| Row | Format |
|---|---|
| Kind | iTunes's file kind, such as "AAC audio file" |
| Track, Disc | "3 of 12" when the total is known |
| dates | local time, "2024-03-05 14:07" |
| Volume Adjustment | the Sound Check offset as a percentage |
| Rating | stars out of five, or Unrated |
| File Size | MiB with one decimal |

Values are read from Apple's library when the screen opens; if the library reloads underneath it, the values go blank.

| | |
|---|---|
| Default | on |
| Memory | image only, plus a row table of about 100 bytes |
| Settings | none |
| Minimum SDK | 1.42 |
