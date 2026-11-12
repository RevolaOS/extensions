# EU Volume Limit

iPods sold in the EU cap the volume below the rest of the world, warn when it goes up, and turn it back down after a restart or after hours of listening. The extension gives an EU iPod the behaviour of a non-EU one: with the EU Volume Limit switch in Settings > Volume Limit off, the volume goes 15% higher than before; with it on, the EU cap stays. There is no warning and nothing is lowered behind your back. iPods that never had the EU rules are unchanged.

| | Stock EU iPod | With the extension |
|---|---|---|
| Ceiling with the switch off | 222 | 256 on Apple's scale, 15% higher |
| Ceiling with the switch on | 168 | 168 |
| Default volume limit | 222 | 256; a saved limit is kept |
| High-volume warning | yes | never |
| Saved volume lowered at startup | yes | never |
| Volume lowered after 20 hours of listening | yes | never |

Apple decides by two region flags, bit 0 and bit 7; where it checks bit 7 it also accepts a stored override. Stock shows the switch only for bit 7 and holds a bit-0 iPod at 222 with no switch at all; the extension shows the switch for either flag. The extension patches seven constants in 2.0.5 and stores nothing of its own.

Turned off, Apple's behaviour returns at once, except the startup clamp, which returns at the next boot. The current volume and the saved limit stay as they are.

| | |
|---|---|
| Default | off |
| Memory | image only |
| Settings | none; Apple stores the switch |
| Minimum SDK | 1.30 |
