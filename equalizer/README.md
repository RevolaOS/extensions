# Equalizer

Partly adapted from [RepriseOS](https://github.com/lgreitis/osos-lab/tree/93f6a45/payload), by lgreitis.

Apple's EQ clips: each of its three filter stages rounds to 16 bits, so a preset that boosts distorts loud passages. Equalizer runs the same presets through its own filter chain, which keeps 42 dB of headroom between stages, saturates once at the end and turns the whole preset down by its peak gain so a boost has room. Every stock preset keeps its sound, only quieter where it would have clipped.

The EQ list gets a Custom preset after Apple's. Shape it in Settings > Edit EQ, a row that shows while Custom is the preset: the wheel picks a band, Select steps into its parameters and then into editing one, Menu steps back out. Every change plays at once; moving the Hold switch shows the response curve of what plays, with the precut applied. Custom, like Flat, is not replaced by a track's own EQ setting. Each of the five bands has:

| Parameter | Range |
|---|---|
| Type | off, peaking, low shelf, high shelf |
| Frequency | 20 Hz to 20 kHz |
| Q | 0.3 to 4.0 |
| Gain | -12 to +12 dB |
| Precut, for the whole preset | automatic (the chain's peak gain) or fixed down to -10 dB |

Enabling the extension while music plays applies the fix from the next track or preset change. Disabling it with Custom selected sets the EQ to Off.

| | |
|---|---|
| Default | on |
| Memory | image only |
| Settings | the Custom preset and whether the Edit EQ row shows |
| Minimum SDK | 1.42 |
