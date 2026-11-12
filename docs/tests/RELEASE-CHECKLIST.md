# Release checklist (run before ANY build is posted)

Why this exists: on 2026-11-11 five builds were posted in one morning. Testers hit three faults that every test had
missed, because the tests did not look like a player's PC:

| What the tests did | What a player has | Fault it hid |
| --- | --- | --- |
| a settings file with the first-launch marks already set | a fresh `coopplus.ini` | a first-launch card that could not be answered |
| lowest graphics, no frame generation | XeSS / FSR / DLSS frame generation | no Co-op menu at all (overlay never drawn) |
| saves with 490+ missions done | a new or early save | a solo-prologue hold in the shared world |
| the build folder's files, test roles | the zip's five files, started by Steam | (the package as installed was never run) |

Rule: a build is posted only after every step below has been run on **that package** and its result is written in
PROGRESS.md. A feature is called "works" only together with what it was tested under.

## How to run

The package must be built first (`tools\package-native.ps1`), so the files tested are the files in the zip (the
`static` step checks this by hash). Then, one step per call (each game step takes 8 to 12 minutes; no game step
starts while Crimson Desert is running):

```bash
bash tools/prepost.sh pp1 dist/CrimsonDesert-SeamlessCoop-<version>-shared-world static
```

Steps, in order: `static real fresh xess fsr display lowsave chest revive loot crime farfight newsave solostart mounts`, then `summary`. (`fresh` and most other steps take the first card, Continue Our Co-op Save; `newsave` and `solostart` are the other two ways a session begins.)
The report is `build/native/prepost-<tag>.txt`. After each game step, LOOK at the screenshots the report lists
(`build/native/shots/`): a log line saying "overlay draws" is not the same as the menu being on screen.

## What each step proves, and what it does not

| Step | What runs | Passes when | Does NOT cover |
| --- | --- | --- | --- |
| static | no game: zip, hashes, settings file, unit tests, install script on a fake game folder | zip files = the tested build; `coopplus.ini` is a first-time player's; unit tests pass (native: no failure outside the 31 old-fixture tests); install / uninstall / vanilla pass | a real game folder |
| real | the package's five files in the real game folder, started as Steam starts it, held at the title, then the player's files put back | overlay ready and drawing, Steam room open, title Co-op menu on the screenshots, real saves unchanged | anything past the title; another PC |
| fresh | two copies, first-time settings from the package, title -> Start Co-op -> shared world | both reach the world, linked, overlay drawn on both, no hold, no error lines | two PCs, the internet, Steam invites between two accounts |
| xess | the same with Intel XeSS frame generation | the same + the XeSS-FG class hooked | Intel hardware |
| fsr | the same with AMD FSR frame generation | the same + the FSR-FG class hooked | AMD hardware (this PC has an Nvidia card; FSR runs, but its driver path differs) |
| display | the player's own display mode and graphics settings, frame generation on, real Steam lobby, title only | overlay ready and drawing, title menu on the screenshots | exclusive fullscreen on other monitors |
| lowsave | a guest on a save with 1 mission done, saved 1.3 km from the host, joins a host on its usual save (the host's co-op world persists between runs) | both reach the world, linked, no solo-prologue hold, the guest is moved to the host's side and ends within 30 m | both players new (needs an empty co-op world); every early story state |
| chest | host opens the Greymane Shrine chest while the guest is away; guest returns | share kept, waits, notice shown to the guest, guest's bag grows at the chest | the guest opening first (needs two players) |
| revive | the guest is taken down, lies down, sees the death screen, cannot move, presses Q | the death reaches the client with the game-over state skipped, input held, revive asked, alive again, input back; the lying character and the HUD on the screenshots | the friend holding Interact to revive; going down while riding or in a duel |
| loot | the host's script kills two bandits and loots the bodies with the guest nearby | the bodies are looted, the guest gets its own share, its bag grows | what each player sees on screen at the body; a kill by weapon |
| crime | both fight at the bandit camp | guest's hits are seen and none is booked as a crime | town NPCs, guards, a real wanted level |
| farfight | the host draws its quest bandits to the dock and leaves for a spot 108 m away; the guest stays and swings 50 times | the damage range hook is in, hits far from the host are counted, the guest kills at least one enemy and loses health | ordinary camps (only the host's quest bandits are known by coordinates); more than ~130 m from the host |
| newsave | the host picks New Co-op Save; in the test copy the harness answers the game's agreement and difficulty; the guest waits on "Waiting for Host" | both reach the new world, the guest's login waited for the host's first save, its own save was moved in, no login refused; the test co-op world is put back | the story's opening minutes after that (cutscenes, the prologue's steps); a real player answering the agreement |
| solostart | the host picks Start From My Solo Progress: a new co-op world is made from its own save, the guest joins | both reach the world, linked, the guest's own save loaded, no login refused; the test co-op world is put back | a host save from late in the story; two PCs |
| mounts | each player calls its horse (simulated pad), goes to it, mounts and rides | each player's horse came, each mount was published to the other player; both mounted on the screenshots | a real controller; fighting from horseback; other mounts; swapping horses; a long ride |

Every game step also checks what the session leaves behind (added 2026-11-11 after two faults found by reading the
logs, not by a test): no packets recorded to disk (the base's log must not say "recording through a disk queue"; the
real install must say "not recording packets to disk"), no log line that repeats more than 30 times a minute, and a
ceiling on log lines per minute.

Never covered on this PC (say so in the post): two different PCs and Steam accounts, a real internet connection,
AMD and Intel graphics cards, a controller in a player's hands, sessions longer than about ten minutes.

## Before the post

1. Every step PASS, screenshots looked at, result table copied into PROGRESS.md under the build's id.
2. One post per batch of fixes. After a tester report: reproduce it as a real player (`real`, `fresh`, the tester's
   graphics mode) BEFORE fixing, fix everything found, run the whole list again, then post once. A second post on the
   same day only when testers cannot play at all.
3. The post says what was tested and what was not (the "Never covered" line above).
4. Posting needs the owner's OK (unless a standing authorization with a time limit is written in PROGRESS.md).
