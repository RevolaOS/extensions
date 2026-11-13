# Conventions

First-party extensions follow the [principles](../STYLE.md) of every RevolaOS repository and these rules of their own.

## One directory per extension

Each directory holds `manifest.toml`, a `Makefile` that includes the SDK's `extension/rv.mk`, the sources, optional `assets/` and `tests/`, and a `README.md`. The directory name is the manifest id.

## README

An extension's README says what the extension does for the person using it, with the attribution line first when the code is ported. Tables list modes, values and ranges. It ends with one table:

| | |
|---|---|
| Default | on or off after install, as the core's `loader/bundled.def` lists it |
| Memory | what it holds beyond its image while idle |
| Settings | what it stores, or none |
| Minimum SDK | the `api` in its manifest |

Build and test steps are in [Building and testing](building.md), not repeated per extension.

## Core sites only

Hooks bind core sites. When no site covers the code, add one to the core; a `[[site]]` table in a first-party manifest is rejected by CI.

## Names

An extension is named after what it does.

## Licence

GPL-3.0-only, see the [README](../README.md). Ported Rockbox code keeps its GPL-2.0-or-later notice, names its authors in the README and is listed in NOTICE.
