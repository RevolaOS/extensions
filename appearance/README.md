# Appearance

Adds Appearance to Settings, after Brightness, with Theme, Dark Mode, Accent Color and Menu Covers inside. Dark Mode turns the stock look dark; Accent Color replaces the stock blue in the selection bar, the accent artwork and the labels that use it; a theme replaces colours and artwork wholesale; Menu Covers chooses whether the Main and Music menus show the highlighted row's cover (Theme leaves it to the look, which for Classic is on).

Themes are files a listener copies to `/Revola/Themes/`, one `<id>.rvt` each, built with the compiler in the [themes repository](https://github.com/RevolaOS/themes), which also holds the first-party themes. The Theme row lists Classic and up to 64 themes by name, scanned each time it opens; a listener's theme named `classic` is ignored, since that id is the built-in look's. A theme may carry a light appearance, a dark one or both; with only one, Dark Mode shows it in that appearance, the row's value says which, and the pane says "Theme only supports dark mode" or "Theme only supports light mode". A theme whose file is missing shows Classic and says "Theme unavailable" under Theme; the choice is kept and returns when the file does.

A theme that cannot be read leaves the look unchanged; one that fails part way through is undone, the stock light look comes back and the row the action came from says so. When the stock artwork cannot be restored from `/Revola/Firmware/revola.bin`, the Appearance row disappears until the next boot. Presses queued while a switch runs cannot reverse it. Turning the extension off puts the stock artwork back.

## Colour roles

| Role | Classic light | Classic dark | Draws |
|---|---|---|---|
| background | `#ffffff` | `#181818` | surfaces, the light greys of the stock screens |
| text | `#202020` | `#e8e8e8` | labels and the dark greys |
| accent_fill | `#236cc5` | `#00a1ff` | accent fills and the artwork that follows the accent |
| accent_text | `#3185d0` | `#00a1ff` | accent-coloured labels |
| on_accent | `#ffffff` | `#ffffff` | labels on the selection bar and on accent fills |
| selection | `#236cc5` | `#00a1ff` | the selection bar |
| cover_border | `#7e8183` | `#383838` | the Cover Flow border |
| icon_tint | `#3185d0` | `#00a1ff` | under a theme, extension icons and the artwork the theme tints with it |

The accents are Blue, Teal, Green, Yellow, Orange, Red, Pink, Purple and Graphite; Theme Default keeps the look's own. A theme may give each accent its own palette. A theme's bitmaps for extensions replace theirs in the Settings panes and canvases. Bitmaps the theme does not replace keep the extension's art; with `flat_icons` a Settings icon's art is drawn without its reflection.

## Layout

The `main` module is always loaded: the bitmap catalog, the colour roles, the draw hooks (bound only while a look other than stock light is on), the index of the look's bitmaps for extensions with the provider that reads them from its file when the core decodes one, the Theme list and the settings. The `switch` module reads the theme file, derives artwork and restores stock bitmaps from the firmware file for a switch, an accent change or unload; it stays loaded while Settings > Extensions > Optimize keeps optional modules and is unloaded again otherwise. `rvt.h` and `catalog.json` are the themes repository's, at the commit in `themes.lock`; `build/classic.rvt`, the Classic dark artwork, is compiled from the same checkout.

| | |
|---|---|
| Default | off |
| Memory | idle: the main module with the bitmap catalog and its state; a look on: an index entry per bitmap the theme holds for extensions and the draw hook blocks; during a switch, or always under Optimize's Performance: the switch module; during a switch: one scratch block; the Theme list open: one entry per theme |
| Settings | theme id, Dark Mode, Accent Color, Menu Covers, and `available`, the hidden bool that shows the Appearance row |
| Minimum SDK | 1.47 |
