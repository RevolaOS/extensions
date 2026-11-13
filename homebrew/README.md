# Homebrew

Based on [patch out manifest sig check](https://github.com/lgreitis/osos-lab/commit/fd5d3678cbadada0abaff181643903b92ea7988a) by lgreitis in RepriseOS.

Lists apps and games from the RevolaOS folders and lets Apple's game runtime start them without an Apple signature. A package is a folder with the program, a `Manifest.plist` and an icon, built with the [eApp SDK](https://github.com/RevolaOS/revola/blob/main/docs/sdk/apps.md).

| Folder | Appears |
|---|---|
| `/Revola/Games/<id>/` | Extras > Games, with the built-in games and those from iTunes |
| `/Revola/Apps/<id>/` | Extras > Apps |

Apple keeps one games list and builds it once. Opening Apps rebuilds it from `/Revola/Apps/`; the next Games list, Games count or Extras pane after leaving Apps rebuilds it from the stock folders and `/Revola/Games/`. A rebuild reads every package's manifest once. The list is never rebuilt while a package runs.

Only a manifest at `/Revola/Games/<id>/Manifest.plist` or `/Revola/Apps/<id>/Manifest.plist` is read without its signature; the games under `iPod_Control/` keep Apple's check. The id is one folder name, without `\` or `:` and not starting with a dot. A package's data folder is `/Revola/Data/Games/<id>/` or `/Revola/Data/Apps/<id>/`: the host's game-data folder maps to it, its stats folder to `Stats/` and its shared-data folder to `Shared/` inside it, so nothing of a package lands under `iPod_Control/`. On another firmware the same package uses Apple's folders like any game. Ids share a namespace with Apple's games: a package with the id of a game under `iPod_Control/` takes that game's place in the list.

Turned off, the Apps row and the packages from both folders disappear once the list is built again, and the games from iTunes are as before.

| | |
|---|---|
| Default | on |
| Memory | image only; the ids of the listed folder while a list holds them |
| Settings | none |
| Minimum SDK | 1.54 |
