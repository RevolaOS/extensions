# Homebrew

Based on [patch out manifest sig check](https://github.com/lgreitis/osos-lab/commit/fd5d3678cbadada0abaff181643903b92ea7988a) by lgreitis in RepriseOS.

Lists apps and games from the RevolaOS folders and lets Apple's game runtime start them without an Apple signature. A package is a folder with the program, a `Manifest.plist` and an icon, built with the [eApp SDK](https://github.com/RevolaOS/revola/blob/main/docs/sdk/apps.md).

| Folder | Appears |
|---|---|
| `/Revola/Games/<id>/` | Extras > Games, after the games from iTunes |
| `/Revola/Apps/<id>/` | Extras > Apps, a row the extension adds after Games |

Only a manifest at `/Revola/Games/<id>/Manifest.plist` or `/Revola/Apps/<id>/Manifest.plist` is read without its signature; the games under `iPod_Control/` keep Apple's check. The id is one folder name, without `\` or `:` and not starting with a dot. Ids share one namespace with extensions, so `/Revola/Data/<id>/` belongs to one of them.

Turned off, the Apps row and the packages from both folders disappear from Extras once the lists are built again, and the games from iTunes are as before.

| | |
|---|---|
| Default | on |
| Memory | image only |
| Settings | Apps in Extras, whether the Apps row shows |
| Minimum SDK | 1.51 |
