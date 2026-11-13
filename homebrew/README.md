# Homebrew

Based on [patch out manifest sig check](https://github.com/lgreitis/osos-lab/commit/fd5d3678cbadada0abaff181643903b92ea7988a) by lgreitis in RepriseOS.

Lists apps and games from the RevolaOS folders and lets Apple's game runtime start them without an Apple signature. A package is a folder with the program, a `Manifest.plist` and an icon, built with the [eApp SDK](https://github.com/RevolaOS/revola/blob/main/docs/sdk/apps.md).

| Folder | Appears |
|---|---|
| `/Revola/Games/<id>/` | Extras > Games, with the built-in games and those from iTunes |
| `/Revola/Apps/<id>/` | Extras > Apps |

Apple keeps one games list and builds it once. Opening Apps rebuilds it from `/Revola/Apps/`; the next Games list, Games count or Extras pane after leaving Apps rebuilds it from the stock folders and `/Revola/Games/`. A rebuild reads every package's manifest once. The list is never rebuilt while a package runs.

Only a manifest at `/Revola/Games/<id>/Manifest.plist` or `/Revola/Apps/<id>/Manifest.plist` is read without its signature; the games under `iPod_Control/` keep Apple's check. The id is one folder name, without `\` or `:` and not starting with a dot. Ids share one namespace with extensions, so `/Revola/Data/<id>/` belongs to one of them, and with Apple's games: a package with the id of a game under `iPod_Control/` takes that game's place in the list. Apple's play counts and saved data of a package go where they go for any game, under `iPod_Control/`.

Turned off, the Apps row and the packages from both folders disappear once the list is built again, and the games from iTunes are as before.

| | |
|---|---|
| Default | on |
| Memory | image only; the ids of the listed folder while a list holds them |
| Settings | none |
| Minimum SDK | 1.54 |
