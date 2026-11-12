# Compressor

Ported from Rockbox's compressor (`lib/rbcodec/dsp/compressor.c`) by Jeffrey Goode, GPL-2.0-or-later.

A compressor turns the loud parts of a recording down so the quiet parts can be heard at the same volume: classical and jazz on a train, films and audiobooks at night. It runs after Channels on the shared [output stage](https://github.com/RevolaOS/revola/blob/main/docs/sdk/audio.md), so it applies to all audio.

Settings > Compressor opens a plot of the output level for every input level: a straight line while nothing changes, bending at the threshold once the compressor works. The six values sit in two rows under the plot: the wheel picks one, Select edits it and Menu steps back out. Every change plays at once and the values keep across restarts. The Settings pane says Off or On.

| Value | Range | Default | What it does |
|---|---|---|---|
| Threshold | Off, -3 to -24 dB in 3 dB steps | Off | the level above which the gain comes down; Off switches the compressor off |
| Ratio | 2:1, 4:1, 6:1, 10:1, Limit | 4:1 | how much of a rise above the threshold still reaches the output; Limit holds it there |
| Knee | Hard, Soft | Soft | Soft eases the gain in over 3 dB either side of the threshold |
| Makeup | Off, Auto | Auto | Auto lifts the output back up by what a full-scale signal lost |
| Attack | 0 to 30 ms in 5 ms steps | 5 ms | how quickly the gain comes down |
| Release | 100 to 1000 ms in 100 ms steps | 500 ms | how quickly it comes back up |

The gain follows the average of the two channels with the treble weighted up, as Rockbox's sidechain does, and applies to the samples of 3 ms earlier, so a limiter can pull a peak down before it plays. With Makeup on, a loud passage can reach full scale; the output saturates rather than wraps.

Enabling it adds the Compressor row after Sound Check. With Threshold at Off it costs nothing per sample.

| | |
|---|---|
| Default | off |
| Memory | image only |
| Settings | the six values |
| Minimum SDK | 1.40 |
