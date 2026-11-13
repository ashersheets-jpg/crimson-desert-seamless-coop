# Crimson Desert Seamless Co-op+

Play Crimson Desert (PC, Steam, base game) with friends in co-op that feels official: join through Steam, shared
story and world progress, loot for everyone, enemies and bosses that scale with the party, names over heads and
friends on the map, and an optional Comfort Pack of quality-of-life tweaks.

**Status (2026-11-12): alpha, in playtest.** The playtest builds are the "shared world" builds: one world for both
players, joined from the title screen, for game build 1.0.0.3663. They are tested with two copies of the game on one
PC; two PCs, AMD / Intel cards and long sessions are what the playtest is finding out. The working branch is
`phase-a/local-pc-setup` (`main` is old). What changed and when: `CHANGELOG.md`. Licence: GPL-3.0.

## What is where
A shared-world build is three files, and their source is all here:
- **`native-base/`**: the native shared-world base (`cdmp.asi`). This is **ByJunter's Crimson Desert Multiplayer
  native server** (https://github.com/ByJunter/CrimsonDesertMultiplayer-NativeServer, GPL-3.0) with our changes on
  top: his commit and each of ours are in this repository's history under that folder. Its own `LICENSE`, `README.md`
  and research documents are his.
- **`upstream/cd-coop-3446/`**: the Co-op+ layer (`coopplus.asi`): the title menu, Steam invites, the overlay, loot
  rules, revive, the report tool's game side. It began as cdmp by ByJunter as ported by nouzert1 (see `CREDITS.md`).
- `kit/`, `tools/`: the installer, the report tool ("Diagnose Co-op+"), packaging and the release checklist
  (`docs/tests/RELEASE-CHECKLIST.md`).
- `PROGRESS.md`: the developer's running notes (long, unedited).
The sections below describe the older, non-shared-world package and are kept for reference.

## For players
Build the package (below), unzip it, double-click `Install Co-op+.cmd`, start the game through Steam, press
Ctrl+F1 to invite a friend. The package's README.txt has the details and the limitations. Back up your saves
(`%LOCALAPPDATA%\Pearl Abyss\CD\save`; the installer does it for you).

## For developers (Windows, Visual Studio 2022 Build Tools)
- Mod + tools + unit tests: `upstream\cd-coop-3446\scripts\build.ps1`
- Offline checks against the installed game exe: `upstream\cd-coop-3446\scripts\verify-port.ps1 -Exe <...\CrimsonDesert.exe>`
- Two mod instances on one PC, no game: `upstream\cd-coop-3446\scripts\e2e-loopback.ps1`
- Player package: `tools\package-coopplus.ps1` -> `dist\CrimsonDesert-SeamlessCoop-<version>.zip`
- Our own pure layers (sim, protocol, config) and their tests: `cmake -S . -B build\msvc -G Ninja` (inside VsDevCmd)
- Changes to upstream code are kept as patches in `upstream/patches/` (see `upstream/README.md`).
- Test plans: `docs/tests/M0.md` (one PC), `docs/tests/M1.md` (two PCs). Project state: `NOTES/STATE.md`.
- Design: `docs/architecture.md`, `docs/integration-plan.md`, `docs/comfort-pack.md`, `docs/mod-permissions.md`.
