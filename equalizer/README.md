# Equalizer

Partly adapted from [RepriseOS](https://github.com/lgreitis/osos-lab/tree/93f6a45/payload), by lgreitis.

Apple's EQ clips: each of its three filter stages rounds to 16 bits, so a preset that boosts distorts loud passages. Equalizer runs the same presets through its own filter chain, which keeps 42 dB of headroom between stages, saturates once at the end and turns the whole preset down by its peak gain so a boost has room. Every stock preset keeps its sound, only quieter where it would have clipped.

The EQ list gets a Custom preset after Apple's. It has five bands, each off, peaking, low shelf or high shelf, with frequency from 20 Hz to 20 kHz, Q from 0.3 to 4.0 and gain from -12 to +12 dB, and a precut that is automatic (the chain's peak gain) or fixed down to -10 dB. Shape it in Settings > Custom EQ: the wheel picks a band, Select steps into its parameters and then into editing one, Menu steps back out. Every change plays at once; moving the Hold switch shows the response curve of what plays, with the precut applied. Custom, like Flat, is not replaced by a track's own EQ setting.

Enabling the extension while music plays applies the fix from the next track or preset change. Disabling it with Custom selected sets the EQ to Off.

Build with `make -C equalizer SDK=/path/to/revola-sdk` from the extensions repository, using SDK API 1.37 or later. The Settings icon `assets/settings-eq.png` is drawn by `assets/render.py` with ImageMagick; `--check` compares the committed file with a fresh render. Copy `equalizer/build/equalizer.rvx` to `/Revola/Extensions/equalizer.rvx` on an iPod running the matching core.
