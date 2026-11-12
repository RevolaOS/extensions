# Channels

Ported from Rockbox's channel modes (`lib/rbcodec/dsp/channel_mode.c`) by Thom Johansen and Michael Sevakis, GPL-2.0-or-later.

A recording mixed for speakers can sound too wide on headphones, and one mixed narrow can sound flat; Channels mixes each channel with a part of the other to narrow or widen the image. The same stage plays Mono for one earbud, one channel in both ears, Karaoke and swapped channels. It runs on the output after the EQ and Crossfeed, and before the Compressor, so it applies to all audio.

Settings > Channels opens a half disc with the listener at its centre: the lobe inside it is as wide as the chosen mode and width leave the image, with L and R at its arms. The two values sit in a row under it: the wheel picks one, Select edits it and Menu steps back out. Every change plays at once, and the values keep their settings across restarts. The Settings pane names the mode, or the width under Custom.

| Mode | What plays |
|---|---|
| Stereo | nothing changes |
| Mono | both ears hear the average of the channels |
| Left Only | both ears hear the left channel |
| Right Only | both ears hear the right channel |
| Karaoke | the difference between the channels, which drops a voice mixed in the centre |
| Swap | the channels exchanged |
| Custom | the image at Width: 0 to 250 % in 5 % steps, 100 % by default |

Below 100 % a part of each channel is added to the other, down to Mono at 0 %. Above 100 % a part of each channel is taken away from the other, so sounds move further out. No width can clip: at every width the two parts add up to one.

16-bit stereo only, which is everything the iPod plays. The output-stage effects run in a fixed order: Crossfeed, Channels, Compressor.

Disabled by default. Enabling it adds the Channels row after Sound Check; with the mode Stereo it costs its image and nothing per sample.

Build with `make -C channels SDK=/path/to/revola-sdk` from the extensions repository, using SDK API 1.40 or later; `make check SDK=...` runs the host tests against an integer model of Rockbox's loops. The Settings icon `assets/settings-channels.png` is drawn by `assets/render.py` with ImageMagick; `--check` compares the committed file with a fresh render. Copy `channels/build/channels.rvx` to `/Revola/Extensions/channels.rvx` on an iPod running the matching core.
