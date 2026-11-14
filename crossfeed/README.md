# Crossfeed

Ported from Rockbox's crossfeed (`lib/rbcodec/dsp/crossfeed.c`) by Thom Johansen, Bertrik Sikken and Michael Sevakis, GPL-2.0-or-later. Simple is Jan Meier's passive circuit.

On headphones each ear hears one channel only, so a recording with an instrument hard-panned to one side is tiring to listen to. Crossfeed feeds a delayed, filtered part of each channel into the other, as sound from two speakers reaches both ears. It runs first on the shared [output stage](https://github.com/RevolaOS/revola/blob/main/docs/sdk/audio.md), so it applies to all audio.

Settings > Crossfeed opens one screen: a plot of what each ear hears over the mode and the Custom values. The wheel picks a value, Select edits it, holding Select puts it back to its default and Menu steps back out. Every change plays at once, blended in over about 3 ms so a wheel step does not click, and the values keep their settings when the mode changes and across restarts.

Mode is the first value:

| Mode | What plays |
|---|---|
| Off | nothing changes |
| Simple | Meier's circuit: each channel's filtered difference signal leaks into the other, with no settings |
| Custom | each channel reaches the other after about 300 µs through a high shelf, and the direct path is turned down; five values below |

The plot draws the ear's own channel and the channel fed across for the mode that plays: flat at 0 dB with nothing fed across while Off, Meier's circuit in Simple, and in Custom a flat line at Direct over the other channel's shelf, at Cross in the bass and falling to Highs above the Cutoff. Mode and Precut share one row under the plot, the four shape values the row below. Precut and the shape values read greyed while a mode other than Custom plays, and still edit.

| Value | Range | Default |
|---|---|---|
| Direct | -6.0 to 0.0 dB in 0.5 dB steps | -1.5 dB |
| Cross | -12.0 to -3.0 dB in 0.5 dB steps | -6.0 dB |
| Highs | -24.0 to -6.0 dB in 0.5 dB steps | -16.0 dB |
| Cutoff | 500 to 2000 Hz in 100 Hz steps | 700 Hz |
| Precut | Auto, or 0.0 to -6.0 dB in 0.5 dB steps | Auto |

Cross is the level of the other channel's low frequencies, Highs the level of its high frequencies, and Cutoff where the shelf between the two sits. A centred passage reaches each ear through both paths, so below the Cutoff it plays at Direct plus Cross, about +2.5 dB at the defaults, and a loud one would clip. Precut lowers both paths by the same amount: Auto picks the smallest half-decibel step that keeps Direct plus Cross at or below 0 dB, so nothing clips; a fixed value sets the level by hand, and 0.0 dB is Rockbox's behaviour, where the output saturates rather than wraps. The plot shows both curves with the precut applied.

Enabling it adds the Crossfeed row after Sound Check, with the mode as its caption. With the mode Off it costs nothing per sample.

| | |
|---|---|
| Default | off |
| Memory | image only |
| Settings | mode, the four shape values and the precut |
| Minimum SDK | 1.58 |
