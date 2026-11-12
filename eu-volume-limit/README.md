# EU Volume Limit

Changes Apple's EU volume rules on 2.0.5. Bit 0 and bit 7 are Apple's region flags; where the table says bit 7, Apple also accepts a stored override. iPods with neither flag run at 256 without EU rules and are unchanged.

| | Stock | With the extension |
|---|---|---|
| EU Volume Limit switch in Settings > Volume Limit | bit 7 | bit 0 or bit 7 |
| Ceiling with the switch off | 222 | 256, 15% higher on Apple's scale |
| Ceiling with the switch on | 168 | 168 |
| Default volume limit | 222 | 256; a saved limit is kept |
| High-volume warning | bit 7 | never |
| Saved volume clamped at startup | bit 7 | never |
| Volume lowered after 20 hours of listening | bit 7 | never |

An iPod with bit 0 alone is held at 222 in stock, with no switch to change it.

Disabled, Apple's behaviour returns at once, except the startup clamp, which returns at the next boot. The current volume and the saved limit stay as they are.

| | |
|---|---|
| Default | off |
| Memory | image only |
| Settings | none; Apple stores the switch |
| Minimum SDK | 1.30 |
