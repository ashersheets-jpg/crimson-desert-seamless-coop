# Co-op+ and other mods

Last reviewed: 2026-11-22, game 1.0.0.3732 (section 2b: the 20 most downloaded on Nexus that day). Section 2 is from
2026-11-03 (game 1.0.0.3479), from `NOTES/RESEARCH/nexus-survey.md`.
Nothing here was tested with the real third-party files: we don't download or bundle other authors' mods. The rows
are what follows from how each kind of mod loads. Section 3 says what was tested on this PC.

## 1. What Co-op+ checks at launch (T28)

When the game starts, Co-op+ looks in `bin64` (the folder with `CrimsonDesert.exe`) for:

| Found | Why it matters | What Co-op+ does |
|---|---|---|
| `version.dll`, `dinput8.dll`, `dsound.dll`, `winhttp.dll`, `xinput1_3.dll`, `d3d9.dll`, `d3d11.dll`, `wininet.dll`, `msacm32.dll` | Usually a second copy of Ultimate ASI Loader under another name. Co-op+ ships its own as `winmm.dll`, and two loaders load every `.asi` twice | Warning in `cdmp.log` and an on-screen notice once in the world. If Co-op+ really is loaded twice, the second copy stays off (log: "Co-op+ was loaded twice") |
| `dxgi.dll`, `ReShade64.dll`, `ReShade.ini` | ReShade (or another dxgi proxy). It also hooks the game's drawing, like Co-op+'s panel | Warning and notice only |
| any other `*.asi` | Another native mod, loaded by our `winmm.dll` | Warning and notice only, naming the files |

The check never stops the game or co-op. It also never sends anything anywhere. If co-op misbehaves, the notice's
advice is to take the listed files out first and try again.

## 2. By kind of mod

| Kind | Examples (Nexus id) | Works with Co-op+? | Notes |
|---|---|---|---|
| Data patches (JSON / overlay PAZ through DMM or CDUMM) | Fat Stacks (157), Inventory Expander (56), Infinite Stamina (107), Free Gliding (353), Barber Unlocked (591 / 3528), HUD layouts | Yes, loads separately | **Gameplay patches must be the same on both PCs** (stamina, stacks, cooldowns, loot, inventory caps). If they differ, each player's game follows its own rules and things drift. Cosmetic ones (barber, HUD, map icons) only change your own screen |
| Mod managers | Definitive Mod Manager (633), CDUMM (207) | Yes | They write overlay archives and leave `bin64` alone. Install Co-op+ with its own installer, or as a manual install (see the README) |
| ASI mods loaded by our `winmm.dll` | Outfit Loadouts (3767), Live Transmog (1056), Master Looter (3402), CDAutoLoot (3380), Ultra Parry and Dodge (607 / 2987), DesertLink mods, Crimson FastLoad (3768), Stack Master (3548), teleporters (713) | Usually, but untested | They load next to Co-op+ in the same game. Ones that hook the same game functions (actors, loot, combat, teleport) can clash. Ones that change gameplay must match on both PCs. The partner sees only what goes over the co-op link: a transmog look is not sent, so your friend sees your real armour |
| Another ASI loader | `version.dll` / `dinput8.dll` copies of Ultimate ASI Loader, CDSmoothmove's `version.dll` | Remove the extra loader | Keep one loader. Co-op+'s `winmm.dll` already loads every `.asi` in `bin64`. A mod that ships its own `version.dll` loader: drop that file and keep the mod's `.asi` |
| ReShade presets | Visual Redux (12), other ReShade presets | Expected to work, untested | Both hook the frame. Co-op+'s panel and HUD draw on top. ReShade add-on GUIs (Live Transmog) open their own overlay. If the Co-op+ panel is missing, try with ReShade off |
| Trainers and Cheat Engine tables | Trinity, CE tables (64, 3209) | Not supported | They write game memory Co-op+ also reads (health, position). Don't use them during co-op |
| Character Creator (837) | data + ASI part | Expected to work | Your own look. The partner copy is built from the equipment sent over the link, not from Character Creator data |

## 2b. The 20 most downloaded (Nexus, sorted by downloads, read 2026-11-22; shared world, game 1.0.0.3732)

In the shared world the HOST's game runs the world's rules for both players (one server, in the host's game), while
each player's own game draws, plays sounds and runs its own interface. So a patch that changes a rule (stack sizes,
stamina, trust, vendors, cooldowns) is the host's rule for both, and a joined player with a different patch sees its
own screen disagree with the world; a cosmetic or interface patch only changes the screen it is installed on.

| # | Mod (Nexus id) | Kind | With Co-op+ in the shared world |
|---|---|---|---|
| 1 | Definitive Mod Manager (633) | manager (overlay PAZ, JSON, ASI/DLL plugins) | Yes. If it installs its own ASI loader (`version.dll` / `dinput8.dll`), keep only one loader (section 1) |
| 2 | Character Creator (837) | data + ASI | Your own look on your screen; what your friend sees of you is not known yet (untested) |
| 3 | JSON Mod Manager (113) | manager (JSON byte patches) | Yes |
| 4 | Fat Stacks (157) | rule patch | Same patch on both PCs; the host's stacks apply in the world |
| 5 | Inventory Expander (56) | rule patch | Same on both PCs (the bag lives on the host's server for both) |
| 6 | LET ME SLEEP (355) | rule patch | Same on both PCs; sleeping / waiting is the host's world time for both |
| 7 | SWISS Knife Save Editor (20) | save editor (outside the game) | Edit only your own SOLO saves, never the co-op saves (`coop_worlds`) or the mailbox of a friend's save |
| 8 | CDUMM (207) | manager | Yes |
| 9 | Minimap Tweaks (275) | interface | Yes, your screen only |
| 10 | RenoDX (7) | ReShade add-on (swap chain / HDR) | Untested: both it and Co-op+'s panel touch the frame; if the panel is missing, try without it |
| 11 | Trust Me (350) | rule patch | Same on both PCs; trust is counted by the host's server |
| 12 | Visual Redux ReShade (12) | ReShade preset | Expected to work (section 2) |
| 13 | Proper 3rd Person Camera (112) | camera data | Yes, your screen only |
| 14 | Skip All Loading Scene (774) | animation data | Your screen only; the other player may still see the full animation |
| 15 | Unlimited Dragon Flying (356) | rule patch | Same on both PCs; the host's rules apply to both riders |
| 16 | Infinite Stamina (107) | rule patch | Same on both PCs |
| 17 | Internal Graphics Overhaul (651) | graphics | Untested; graphics only |
| 18 | Crimson Female Voices (287) | audio data | Your screen's sounds: you hear the voice your own files have for your friend too |
| 19 | Expanded Vendor Inventory (2281) | rule patch (vendors) | Same on both PCs; the joined player's shops are run by the host's server |
| 20 | Better Inventory and Trade UI (452) | interface | Yes, your screen only |

Still to do (T28): one two-copy run with each of the code mods among them (2, 10, 17 and a manager's loader) next to
Co-op+. Downloading from Nexus needs a signed-in account in the browser; nothing was downloaded yet.

## 3. Tested on this PC (2026-11-03, echo stand-in)

- With a stand-in `ReShade.ini` next to the game, the log and notice name it (`build/port3479/compat_test.sh`, run a).
- Without it, the log says "no other ASI loader, .asi mod or ReShade next to the game" (run b).
- A double load can't happen here (one loader). The guard is a per-process named mutex, so a second copy of `cdmp.asi`
  in the same game returns before installing anything.
- Testing this found a real crash, now fixed (T28 in `PROGRESS.md`). On a launch where a crash-guard hook failed the
  first time, the retry wrote into its read-only return stub.

## 4. To test with real mods (2-PC playtest)

`docs/tests/PLAYTEST-1.md`, section "Other mods". Each player installs the mods they normally use, then:
1. starts the game and reads the notice;
2. plays 10 minutes of co-op with a fight and loot;
3. sends both `cdmp.log` files if anything is off.
