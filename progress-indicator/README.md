# Progress Indicator

Shows half-filled status circles for partially played podcasts and iTunes U items in Apple's individual-item lists. Unplayed and completed items retain their native indicators. An unfinished replay shows a half circle even if the item was played before.

The indicator follows saved resume position, or live position when that exact item is playing. Audio and video variants retain the native outline and current colors. Group and All rows retain Apple's behavior.

Turn it on or off in Settings > Extensions > Progress Indicator. Disabling restores native icons and releases the extension's memory. No additional settings.

Build with `make SDK=/path/to/revola-sdk`; install `build/progress-indicator.rvx` in `/Revola/Extensions/`. Requires SDK/core API 1.34 or later and Apple firmware 2.0.5.

Run `make check SDK=/path/to/revola-sdk` for playback, bitmap, and unload contracts. Tests require Python 3 and a C compiler with AddressSanitizer and UndefinedBehaviorSanitizer. Native pointer layouts are widened for the host; emulator verification covers the actual ARM ABI and firmware lifecycle.
