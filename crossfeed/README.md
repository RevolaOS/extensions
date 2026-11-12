# Crossfeed

Ported from Rockbox's crossfeed (`lib/rbcodec/dsp/crossfeed.c`) by Thom Johansen, Bertrik Sikken and Michael Sevakis, GPL-2.0-or-later. Simple is Jan Meier's passive circuit.

On headphones each ear hears one channel only, so a recording with an instrument hard-panned to one side is tiring to listen to. Crossfeed feeds a delayed, filtered part of each channel into the other, as sound from two speakers reaches both ears. It runs on the output after the EQ, so it applies to all audio.

Settings > Crossfeed chooses the mode:

| Mode | What plays |
|---|---|
| Off | nothing changes |
| Simple | Meier's circuit: each channel's filtered difference signal leaks into the other, with no settings |
| Custom | each channel reaches the other after about 300 µs through a high shelf, and the direct path is turned down; four values below |

Edit Crossfeed, a row that shows while Custom is chosen, opens a plot of what each ear hears: a flat line for the ear's own channel at Direct, and the other channel's shelf, at Cross in the bass and falling to Highs above the Cutoff. The five values sit in a row under the plot: the wheel picks one, Select edits it and Menu steps back out. Every change plays at once, and the values keep their settings when the mode changes and across restarts.

| Value | Range | Default |
|---|---|---|
| Direct | -6.0 to 0.0 dB in 0.5 dB steps | -1.5 dB |
| Cross | -12.0 to -3.0 dB in 0.5 dB steps | -6.0 dB |
| Highs | -24.0 to -6.0 dB in 0.5 dB steps | -16.0 dB |
| Cutoff | 500 to 2000 Hz in 100 Hz steps | 700 Hz |
| Precut | Auto, or 0.0 to -6.0 dB in 0.5 dB steps | Auto |

Cross is the level of the other channel's low frequencies, Highs the level of its high frequencies, and Cutoff where the shelf between the two sits. A centred passage reaches each ear through both paths, so below the Cutoff it plays at Direct plus Cross, about +2.5 dB at the defaults, and a loud one would clip. Precut lowers both paths by the same amount: Auto picks the smallest half-decibel step that keeps Direct plus Cross at or below 0 dB, so nothing clips; a fixed value sets the level by hand, and 0.0 dB is Rockbox's behaviour, where the output saturates rather than wraps. The plot shows both curves with the precut applied.

Both modes follow the output sample rate, 8 to 48 kHz. 16-bit stereo only, which is everything the iPod plays.

Disabled by default. Enabling it adds the Crossfeed row after Sound Check; with the mode Off it costs its image and nothing per sample.

Build with `make -C crossfeed SDK=/path/to/revola-sdk` from the extensions repository, using SDK API 1.39 or later; `make check SDK=...` runs the host tests against an integer model of Rockbox's loops. The Settings icon `assets/settings-crossfeed.png` is drawn by `assets/render.py` with ImageMagick; `--check` compares the committed file with a fresh render. Copy `crossfeed/build/crossfeed.rvx` to `/Revola/Extensions/crossfeed.rvx` on an iPod running the matching core.
