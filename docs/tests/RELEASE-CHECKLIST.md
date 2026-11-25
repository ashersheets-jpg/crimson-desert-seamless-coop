# Release checklist (run before ANY build is posted)

## How a pass is run

- How the checklist is run (the owner, 2026-11-14 23:00; eight passes in three days cost ~20 h):
  1. A failing step does not stop the pass: note it and run every remaining step on the same package, so one pass
     finds every problem. Fix them all, package once, then one final full pass.
  2. A fix that only touches test-kit code (code that runs only under [dev] switches or in tools/, never for a
     player) does not restart the pass: re-run the affected steps plus a smoke set (static, fresh, solostart) on the
     new package, and say so in PROGRESS and the post's test line.
  3. Post less often: bundle more fixes and features per post (about one full pass per post, not per fix).
  The full pass on the final package before any post stays (rule 2 is the only exception, and only for test-kit-only
  changes).

Why this exists: on 2026-11-11 five builds were posted in one morning. Testers hit three faults that every test had
missed, because the tests did not look like a player's PC:

| What the tests did | What a player has | Fault it hid |
| --- | --- | --- |
| a settings file with the first-launch marks already set | a fresh `coopplus.ini` | a first-launch card that could not be answered |
| lowest graphics, no frame generation | XeSS / FSR / DLSS frame generation | no Co-op menu at all (overlay never drawn) |
| saves with 490+ missions done | a new or early save | a solo-prologue hold in the shared world |
| the build folder's files, test roles | the zip's five files, started by Steam | (the package as installed was never run) |
| every start from a fresh game process at the title | a second try in the same game; a world loaded before | Start Co-op that did nothing; a host that refused the restarted guest |

Rule: a build is posted only after every step below has been run on **that package** and its result is written in
PROGRESS.md. A feature is called "works" only together with what it was tested under.

## How to run

The package must be built first (`tools\package-native.ps1`), so the files tested are the files in the zip (the
`static` step checks this by hash). Then, one step per call (each game step takes 8 to 12 minutes; no game step
starts while Crimson Desert is running):

```bash
bash tools/prepost.sh pp1 dist/CrimsonDesert-SeamlessCoop-<version>-shared-world static
```

Steps, in order: `static real fresh xess fsr latehook display lowsave stall fall chest revive loot crime leave shop talkwhile armwrest door farfight farfight2 newsave solostart mounts mountfight nosave retry stuck mismatch hostrevive lagrun`, then `summary` (31 steps; the passes run the town steps armwrest / shop / talkwhile / chest first). `talkwhile` joined the list on 2026-11-25 (BUG 2 fixed, cs128). `fall` joined it the same day (the fall guard, cs130a). (`fresh` and most other steps take the first card, Continue Our Co-op Save; `newsave` and `solostart` are the other two ways a session begins.)
The report is `build/native/prepost-<tag>.txt`. After each game step, LOOK at the screenshots the report lists
(`build/native/shots/`): a log line saying "overlay draws" is not the same as the menu being on screen.

## What each step proves, and what it does not

| Step | What runs | Passes when | Does NOT cover |
| --- | --- | --- | --- |
| static | no game: zip, hashes, settings file, unit tests, install script on a fake game folder | zip files = the tested build; `coopplus.ini` is a first-time player's; unit tests pass (native: no failure outside the 31 old-fixture tests); install / uninstall / vanilla pass | a real game folder |
| real | the package's five files in the real game folder, started as Steam starts it, held at the title, then the player's files put back | overlay ready and drawing, Steam room open, title Co-op menu on the screenshots, real saves unchanged | anything past the title; another PC |
| fresh | two copies, first-time settings from the package, title -> Start Co-op -> shared world | both reach the world, linked, overlay drawn on both, no hold, no error lines | two PCs, the internet, Steam invites between two accounts |
| xess | the same with Intel XeSS frame generation | the same + the XeSS-FG class hooked | Intel hardware |
| latehook | both copies with Streamline's swap chain class left unhooked (dev switch): no frame reaches the mod, as on a tester's AMD card | the watcher sees drawing without frames, hooks the class a new swap chain gets, the overlay draws, the whole flow into the world | a real AMD / Intel card: there the class is DXGI's own, here it is Streamline's again (only the mechanism is tested) |
| fsr | the same with AMD FSR frame generation | the same + the FSR-FG class hooked | AMD hardware (this PC has an Nvidia card; FSR runs, but its driver path differs) |
| display | the player's own display mode and graphics settings, frame generation on, real Steam lobby, title only | overlay ready and drawing, title menu on the screenshots | exclusive fullscreen on other monitors |
| lowsave | a guest on a save with 1 mission done, saved 1.3 km from the host, joins a host on its usual save (the host's co-op world persists between runs) | both reach the world, linked, no solo-prologue hold, the guest is moved to the host's side and ends within 30 m | both players new (needs an empty co-op world); every early story state |
| stall | (2026-11-25, Borque's endless loading screen) the lowsave join with the host's server made to stop answering for 25 s right after the guest arrives (TEST-ONLY dev_server_stall_ms: ServerRunThread sleeps at user 2's 2nd MoveFieldCompleteReq, op 2723) | the host's stall lines name the last item (user 2's 2723) and carry stack samples (our sleep on ServerRunThread's stack); the host tells the guest; the guest's join trip to the host WAITS in the world (no request, no loading screen) until the host says the guest's own load into its field is done on its server (cs127c: a trip asked while that load was still completing was undone), then the game's own travel, no second request, takes, and ends within 30 m; no timeout or give-up | a real stall's cause (another mod, a slower PC); a "go to my friend" during a stall (the far travel's own wait for the server: logged, not scripted here); a stall that starts after the travel's request (its wait in the loading screen); two PCs |
| fall | (2026-11-25, Brycecco + Arcticwolf: the joined player followed the host onto an Abyss island of the host's story and fell through the world) the lowsave join, then the guest (TEST-ONLY [dev] fall_guard_test: no "the friend stands up there" check) far-travels to a point 150 m above the host | the game holds it in the air, it falls, the guard takes it back to where the move started before it lands (no death, its lowest height above 600 m); the same point again is refused with a note; the ground beside the host is allowed and the guest ends within 30 m | a real story place (an Abyss island: its levels exist only in the host's game); the friend check itself (skipped by the switch); a fall during hops or a revive (the same watch, not scripted here); two PCs |
| chest | host opens the Greymane Shrine chest while the guest is away; guest returns | share kept, waits, notice shown to the guest, guest's bag grows at the chest | the guest opening first (needs two players) |
| revive | the guest is taken down, lies down, sees the death screen, cannot move, presses Q | the death reaches the client with the game-over state skipped, input held, revive asked, alive again, input back; the lying character and the HUD on the screenshots | the friend holding Interact to revive; going down while riding or in a duel |
| loot | the host's script kills two bandits and loots the bodies with the guest nearby | the bodies are looted, the guest gets its own share, its bag grows | what each player sees on screen at the body; a kill by weapon |
| crime | both fight at the bandit camp | guest's hits are seen and none is booked as a crime | town NPCs, guards, a real wanted level |
| leave | the guest fights at the bandit camp (the fight rule gives the bandits it hits to its game), the host comes to the dock and does not fight; the guest's game is killed mid-fight (-GuestLeave); the host plays on 60 s | the host keeps running; the departure sweep gives the NPCs the guest's game ran back to the host's game; the player's saves untouched | a disconnect over Steam (the host's side of a link drop is the same), a guest that loads elsewhere (fast travel, a respawn) |
| shop | the host parks 440 m away (94 m above the Rodbel dock); the guest, at the Hernand butcher, opens her shop alone; the host comes back and stands beside her; the guest opens the shop again | both of the guest's shop requests reach the host's server and are accepted (alone: its own shop scene; with the host there: ShopTwin's twin, whose menu is answered on the guest's game); the talk rule hands her to the guest's game | a guest that fast-travels into a town whose shop scenes the host already runs (the twins lost to the teleport, shop_twin_again's second round); other shopkeepers; buying and selling |
| talkwhile | (2026-11-25, the testers' BUG 2; in the list since cs128 passed with the C0 talk window) the guest opens the Hernand butcher's shop with the host far, then with the host at the inn table 73 m away, then DURING the host's talk with the arm-wrestling organizer (left open) | all three of the guest's shop requests accepted; its shop menu opens with the host at the inn, before and during the host's talk (the twin's menu answered twice); never refused as busy; the per-player focus slot ran its talks with its own focus on the server's logic thread (C0, 2026-11-25), no swap failed or unbalanced | the guest talking to the SAME NPC as the host (the game's one-talker rule, left as it is); cutscenes; talks that start story scenes |
| door | the joined player reaches the Hernand tavern door first (the host ~300 m off, on a copy of the owner's save) and opens it; the host comes and opens it too | the host's server made the tavern's levels the joined player loaded first ready for it (scene_level_share, on by default), the joined player's door request was answered (Ack) and none refused (Nak 0); the door open on the shots | other doors, gates and levers; a level the host loads first and the joined player later (S43's known gap: the host's own load with DL != 0) |
| farfight | the host draws its quest bandits to the dock and leaves for a spot 108 m away; the guest stays and swings 50 times, taps Q (the instant revive: the bot never blocks, so five bandits can down it) and swings 30 more | the damage range hook is in, hits far from the host are counted, the guest kills at least one enemy and loses health | ordinary camps (only the host's quest bandits are known by coordinates); more than ~130 m from the host |
| farfight2 | the same fight with the host 484 m away in another direction (Hernand) | hits counted because the guest was in range, the hook's logged distances are right, the guest lost health | real free roaming kilometres apart; enemies that never met the host |
| newsave | the host picks New Co-op Save; in the test copy the harness answers the game's agreement and difficulty; the guest waits on "Waiting for Host" | both reach the new world, the guest's login waited for the host's first save, its own save was moved in, no login refused; the test co-op world is put back | the story's opening minutes after that (cutscenes, the prologue's steps); a real player answering the agreement; since 2026-11-12 also: the friend's own save was not used, and Continue of that world with another guest save brings the world's character back |
| solostart | the host picks Start From My Solo Progress: a new co-op world is made from its own save, the guest joins | both reach the world, linked, the guest's own save loaded, no login refused; the test co-op world is put back | a host save from late in the story; two PCs |
| mounts | each player calls its horse (simulated pad), goes to it, mounts and rides | each player's horse came, each mount was published to the other player; both mounted on the screenshots | a real controller; fighting from horseback; other mounts; swapping horses; a long ride |
| mountfight | the guest mounts at the dock, the host arrives and wakes its bandits, the guest swings from the saddle | the guest's blows were seen, it lost health; on the screenshots it is in the saddle | the host fighting mounted; being thrown off; a mounted enemy; other mounts |
| nosave | a guest with no save at all tries to join | its game goes back to solo with the instruction, the host is told and does not load in alone | the same check before either game restarts; a real new character for such a guest |
| retry | the host's first automatic start is a second Start Co-op instead (dev switch) | both games restart AGAIN (the guest although it already is a guest), the host's base replaces the guest's old connection instead of stopping its tunnel, both reach the world | a second start after the host has been in the world (that is `stuck`) |
| stuck | the guest does not press Play (dev switch: as when the host's game never answers); the host answers with Leave the session and a new Start Co-op (dev switch, the menu row's own code) | the guest says it is stuck, the host is told, the Leave goes to the title at once, the guest stays in the party at its menu, the host's second start restarts the game although it had been in a world, both reach the world in the second round | the host going back through the game's own menu; a real unanswered login (the host's side of it); a host that loaded a SOLO world first (a solo test copy would load the real save) |
| mismatch | the guest says it is another build (dev switch) | both title menus show "Can't play together: ... has another Co-op+ build", the host's Start Co-op is greyed, nothing starts | a real older package on one side (it reports the old frozen number, which is refused the same way) |
| hostrevive | the host goes down among the dock's bandits with the guest beside it and revives at the checkpoint | the host is moved by the game's own travel, its stages close "at the owner's load" with the guest in the world, no crash, the schedule timers go on to the end, none given up | the rare moment itself (a schedule timer while the owner has no manager: 1 in about 300 reloads on a tester's PC); a death inside a story mission |
| lagrun | the guest runs out and back while both tunnels add internet-like lag (TEST sim_lag: 30 + 0..60 ms, rare 400 ms stalls) | the host's view of the running guest has at most a few short mid-run holds (remote_interp_char's delay) | a real internet connection, two PCs |

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
