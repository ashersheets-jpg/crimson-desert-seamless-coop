# Crimson Desert Seamless Co-op+ — Autonomous Roadmap

You are continuing development of the mod "Crimson Desert Seamless Co-op+" (base game only, no DLC). Work through this file top to bottom without stopping to ask the user questions. The user will not be watching.

---

## 1. Operating rules (read first, follow always)

**The loop**
1. Open this file and `PROGRESS.md` (create it if missing). Pick the first task that is `[ ]` (not done, not blocked), in order.
2. Read the relevant existing code and docs. Write a 3-6 line plan for the task into `PROGRESS.md` under the task ID.
3. Implement it, following the project's existing code style, hook patterns and config conventions (e.g. `scaling.toml`, the F8 panel, the Diagnose report).
4. Test it. Use the in-game stand-in copy of the player on this PC wherever possible. Run any existing automated tests and the Diagnose report.
5. Run the **regression checklist** (section 2). Fix anything you broke before moving on.
6. Tick the task `[x]`, add a one-line entry to `CHANGELOG.md`, log what you did and learned in `PROGRESS.md`, and commit with the message `T##: <short title>`.
7. **Immediately start the next task.** Do not wait for confirmation and do not stop to summarize between tasks.

**When something goes wrong (do not give up early)**
- Find the root cause: read logs, add temporary logging, build a minimal repro, and check whether a recent change caused it.
- Try at least three genuinely different approaches before declaring a task blocked (different hook point, different data source, different sync strategy, a workaround, research of docs or modding resources online).
- If the ideal solution is impossible, ship the best degraded version that is safe and honest (clear in-game message, config toggle to disable it) and note the limitation in `PROGRESS.md`.
- If a task is still impossible after that, append it to `BLOCKED.md` with: what you tried, what failed, and what would unblock it. Mark it `[!]` here and **move on to the next task**.

**Human-only work**
- Anything that needs two real PCs or a real second player is tagged `[needs-2PC]`. Implement and self-test as far as you can with the stand-in, add precise steps to `docs/tests/PLAYTEST-1.md` (or the next PLAYTEST file), mark the task `[~]` (built, awaiting human test), and move on. Never stall waiting for it.

**Hard safety rules**
- Never modify, move or delete the user's real save files except through the existing backup-first mechanism. Back up before every test run that touches saves.
- Never break the single-player game: Vanilla on/off switch and auto-disable after game updates must keep working.
- Do not copy, bundle or redistribute game files or assets. Do not bundle other authors' mods without written permission; list them as requirements with credit instead.
- Do not touch any online, ranked or account-related game features. Keep co-op strictly separate from them.
- Never add telemetry or send user data anywhere.
- Do not rebuild anything listed in section 3. Extend it only when a task says so.

**Definition of done for every task**
- Works in-game on this PC (or is marked `[~]` with test steps).
- Has a config toggle if it changes gameplay rules.
- User-facing text is in the game's own style and says what to do next on failure.
- Regression checklist passes. `CHANGELOG.md` and `PROGRESS.md` updated. Committed.

**Finishing**
When every task is `[x]`, `[~]` or `[!]`, write `FINAL-REPORT.md`: what's done, what needs a human test, what's blocked and why. Then stop.

**Status key:** `[ ]` todo · `[x]` done · `[~]` built, needs human 2-PC test · `[!]` blocked (see BLOCKED.md)

---

## 2. Regression checklist (run after every task)

- Invite, join, leave, rejoin work (Steam and room code).
- Partner character appears, moves, fights, rides, glides, climbs; gear changes show.
- Nameplate shows and fades; hidden behind map and menus.
- Shared enemies: both players can hit and kill them; party scaling applies; guest's own enemies hidden within 150 m and restored after.
- Death and revive flow (Q and U) still works.
- One challenge (duel or arm wrestling) starts, finishes and cleans up.
- Time of day and weather follow the host.
- Controller prompts still work.
- Leaving a session returns the player to a clean single-player state.
- Vanilla off switch disables everything.

---

## 3. Already built — do NOT rebuild

Steam join/invite/leave and room code, friends-only vs invite-only, join/leave notices, partner character with full movement and gear sync, nameplates, partner horse and flying mount sync, shared enemy visibility and combat, party scaling (`scaling.toml`), no friendly fire outside duels, per-player chests and world items, shared waypoints, ping (Ctrl+F3), go-to-friend (Ctrl+F4), time and weather sync, cutscene watching notice and skip-vote, custom death screen with Q/U revive, boss-death waiting rules, duel, arm wrestling, rock-paper-scissors, hunting contest, party journal, game-styled UI, controller support, save backups, auto-disable after game updates, one-click install/uninstall, Vanilla switch, Diagnose.

---

## 4. Tasks

### P0 — Integrity and release blockers

- [x] **T01 — Two-PC playtest pack.** Review every `[needs-2PC]` item (Steam joining, loot for guest, shared-set enemies, boss cutscenes for guest, boss final-blow rule, boss death waiting rules). Make `docs/tests/PLAYTEST-1.md` a complete step-by-step checklist with expected results and a place to record results. *Done when:* every 2-PC item has numbered steps and pass/fail boxes.
- [x] **T02 — Anti-cheat and terms check.** Research whether the game or its launcher uses anti-cheat or online services that a modded launch could trip. Add a launch guard so co-op never runs alongside any online-integrity feature, and document findings and the safe install path in the README. *Done when:* a findings note exists and the guard is tested.
- [x] **T03 — Version handshake.** On join, exchange game version and mod version. Refuse mismatches with a message naming both versions and what to update. *Done when:* a simulated mismatch is refused cleanly with no partial state.
- [x] **T04 — Reconnect and rejoin.** If either side crashes, alt-tabs out for a long time, or loses connection, the player can rejoin (Steam or room code) and be restored with no duplicate characters, duplicate enemies, or stuck UI. *Done when:* kill-and-rejoin works 5 times in a row.
- [-] **T05 — Drop-in mid-event.** Joining during combat, a boss fight, a cutscene, a loading screen or a menu catches the guest up with no stuck state. *Done when:* each of those five situations is tested. (DROPPED with B08, the owner 2026-11-07: joining is from the main menu only.)
- [x] **T06 — Desync detection and resync.** Periodically compare key state (positions, enemy set, health, time/weather). Auto-resync on drift and add a manual "resync me" action. *Done when:* an intentionally induced desync is detected and repaired.
- [x] **T07 — Host leaves or session ends.** When the host quits or ends the session, the guest returns cleanly to their own world: partner entities removed, hidden enemies restored, UI cleared. *Done when:* both exit paths (host quit, guest leave) leave no leftovers.
- [x] **T08 — Guest-return state integrity.** After a session, verify the guest's own save and world carry no leaked co-op state (scaling, hidden enemies, loot flags, cutscene flags, time and weather). Build an automated before/after state diff. *Done when:* the diff is clean after three different session types.
- [~] **T09 — Menus and pause.** Opening the map, inventory or pause menu must not freeze, desync or soft-lock the partner's world. Define and implement the pause behavior. *Done when:* every menu type is tested while a partner is fighting.
- [~] **T10 — Shared-kill loot and XP for the guest.** The guest receives drops and XP for kills they contribute to, with no duplicates and none missing. Make loot rules configurable (shared, per-player, first come). *Done when:* verified with the stand-in; `[needs-2PC]` for the real guest.
- [x] **T11 — Network stress and performance.** Test with simulated latency, jitter and packet loss, and with 33+ entities. Measure bandwidth and FPS cost, set entity and bandwidth budgets, and degrade gracefully (lower update rate before dropping features). Record results. *Done when:* a documented stress report exists and no crash occurs under bad networks.
- [~] **M01 — Constants the ports left at older builds (the 3732 port audit, 2026-11-21).** A read-only audit of the native code found ten groups of game addresses still at 2976's or 3576's values (docs/research/port-gaps-3732.md: the table, the log evidence, the plan). (1) guest_save_logic.h's seven caller returns (every LoadSlot / save / flush caller read "OTHER"): fix written (scratchpad port_gap_rets.py, = kCallSites + 5 tied by static_asserts), next batch. (2) Two crash risks on a later build or with an option on (game_errors' one-byte check; guest_sched_route without its teardown guard): to fail closed. (3) Checked after: U-b's timer returns and user2_shops' protected range are already 3732's (pins73.py; the same range), User2Stages D/S/F/H only count and log. (4) mount_seat's vf178 judge is real (seats rely on the veto while it is unproven): confirmed on the 3732 exe, the actor's judge is exe+0x5DF500 and the "+0x94 alone" one exe+0x9BEEB0; the fix (vf178_fix.py) is in the next batch. (5) The rest are off or diagnostic. (6) A second audit of the Co-op+ layer: nothing it runs in native mode is stale; dormant two-worlds puppet constants, a stamina stat id that hides the partner frame's stamina bar (minor), two log labels. *Done when:* (1), (2) and (4) are in a build that passed the checklist.

### P1 — Core experience

- [x] **T12 — NPC and wildlife consistency.** The same NPCs and animals appear in both players' views within range (or clear, deliberate rules exist). Required for fair hunting contests and for not shooting an animal only one player sees. *Done when:* hunting contest kill counting is verified.
- [~] **T13 — Boss mechanics audit.** List every boss mechanic that assumes one player: grabs, finishers, phase triggers, arena locks, targeting. Make each work with two players, or define a safe fallback. Keep the host-lands-final-blow rule. *Done when:* an audit table exists and the first five bosses encountered are verified; `[needs-2PC]`.
- [x] **T14 — Partner party frame.** An always-visible, game-styled partner health and stamina element, with a low-health alert. *Done when:* visible on keyboard and controller, with no overlap with game HUD.
- [~] **T15 — Teammate revive.** When downed, a bleed-out timer starts and the partner can revive by interacting. Show revive progress and credit. Keep the existing Q/U revives as fallback. *Done when:* revive works in a normal fight and a boss fight; `[needs-2PC]`.
- [~] **T16 — Crime, guards and bounty.** Define and implement how guards and NPCs react to the guest's crimes in the host's world, including shared vs per-player bounty (config). The host must never be punished for something only the guest did unless configured. *Done when:* tested with theft and assault.
- [~] **T17 — Anti-grief protections.** The guest cannot kill essential or quest-critical NPCs, destroy host quest items, or consume story-critical objects. Configurable. *Done when:* each protection is tested.
- [x] **T18 — Trading and gifting.** Players can send items to each other with a confirmation step. No duplication exploits, a transaction log, and works on controller. *Done when:* duplication attempts (disconnect mid-trade, spam) fail safely. (Delivered as B11 in the shared world: give and trade through the game's own item move, confirm step, pad and keyboard, both directions on 3663 (cs21a / cs34a / cs35b); see B11.)
- [~] **T19 — Party travel and summon.** Add party fast-travel (both players go, with consent), "summon to me" (with consent and cooldown), and an optional auto catch-up if the partner is too far. Never place players in invalid or underground spots. *Done when:* tested from several regions including cliffs and water.
- [x] **T20 — World-state persistence rules.** Define and implement what happens to world state changed during co-op (cleared camps, killed bosses, gathered nodes, opened doors) for host and guest. Document the rules in the README. *Done when:* the rules are written and a test confirms host and guest outcomes.
- [~] **T21 — Fix: empty arm-wrestling table.** The pub table has no NPCs when the stand-in is connected from the start. Find and fix the cause. *Done when:* arm wrestling works regardless of connection timing. (The empty table was the two-worlds design's; in the shared world both games list the same 15 people there (cs33a) and the host's match works. Left, as B14: the joined player's own match.)
- [~] **T22 — World event sync.** Sieges, faction events and other dynamic events that start in the host's world are visible and participatable for the guest. *Done when:* at least the most common event types are verified.
- [ ] **T23 — Object interaction sync.** Doors, gates, levers, movable and thrown objects, and destructibles look consistent to both players. *Done when:* each is tested with both players nearby.
  (2026-11-21: in the shared world the objects are the host server's, one copy for both. Known bug from the native
  base's S43 (2976): a scene level the JOINED player's game loads first is never marked ready on the server (only the
  field owner's load does that, and the owner's later load is dropped), so its doors answer "not ready" to BOTH
  players until a fast travel. ByJunter's fix [session2] scene_level_share (host) is ported to 3732 but off and never
  run there; cs88a / cs88b (the Hernand tavern door, the joined player first; probe, then on) are queued.
  2026-11-22: cs88a REPRODUCED it on 3732 (the joined player's door requests: 13-15 Nak, 0 Ack); cs88b with
  scene_level_share=1 FIXED it (the levels "[ready for user 2]", the door's Req -> Ack, Nak 0). On by default for the
  host since 677c87bb. Left: a checklist door step; gates, levers, movable / thrown objects and destructibles.)
- [x] **T24 — Communication.** Add in-session text chat and ping types (danger, loot, go here) with game-styled display. *Done when:* usable on keyboard and controller.
- [~] **T25 — Mount edge cases.** Mount summon, despawn, death, mount swapping, and flying-mount landing all stay in sync. Two players on one mount only if feasible. *Done when:* each case is tested.
- [~] **T26 — Death penalty and respawn options.** Config options for penalty (none, partial loss, full) and respawn rules, applied consistently on both sides. *Done when:* every option is tested. (2026-11-22: the respawn option is done; the penalty options were blocked on the two-worlds design (no removal the server accepts). On the native shared world the game's own jail fine removes items on the host's server (RemoveItemToInventoryAck 2481): a lead to a server-side removal; design in docs/research/t26-death-penalty.md.)
- [~] **T27 — Nexus release package.** README (install, uninstall, requirements, controls, known issues), credits and permissions for all other authors' mods (list as requirements unless permission is on file), license, changelog, bug-report template, compatibility list, screenshot and GIF shot list, and verified manual and Vortex install. Nothing from game files bundled. *Done when:* a clean-machine install-and-uninstall walkthrough passes.
- [~] **T28 — Mod compatibility testing.** Identify the most-downloaded mods for the game, test alongside the co-op mod, and document conflicts. Detect known conflicts at launch and warn the user. *Done when:* a compatibility table exists. (The table by KIND of mod exists, docs/compatibility.md, and the launch notice works. Left: real mods. The owner's standing OK of 2026-11-17 covers downloading Nexus mods: the most-downloaded few, each installed into a test copy with Co-op+ for one two-copy run, between other test chains.)
- [x] **T29 — Error messages.** Audit every failure state (join failed, session lost, version mismatch, network blocked, save backup failed) and make sure each shows a plain-language message with the next step. *Done when:* a list of all states and their messages exists.

### P2 — Polish

- [x] **T30 — Settings and hotkeys.** Expose all config options in the in-game F8 panel, make hotkeys rebindable, and warn about conflicts with game keybinds.
- [x] **T31 — First-run onboarding.** Short guided intro and a controls cheat sheet, shown once and reopenable.
- [x] **T32 — Connection quality indicator.** Ping and packet loss in the F8 panel with an optional small HUD display.
- [~] **T33 — Partner audio.** Footsteps, combat sounds and voice lines for the partner with proper distance falloff.
- [x] **T34 — Per-player colors.** Consistent color coding across map pins, compass markers, nameplates and pings, with a colorblind-safe option.
- [~] **T35 — Emotes and resting together.** Synced emotes and sitting or resting at campfires together.
- [~] **T36 — Combat feedback sync.** Status effects, stagger, knockback, damage numbers and kill feed visible for the partner's hits, plus per-player lock-on.
- [x] **T37 — Crash and log reporting.** Extend Diagnose into a one-click "copy report" bundle with personal info redacted.
- [x] **T38 — Post-patch playbook.** A doc and script to quickly find which hooks or offsets broke after a game update and verify the fixes.
- [x] **T39 — Story spoiler protection.** Optional setting so the guest isn't pushed ahead of or spoiled on the host's story.
- [x] **T40 — In-game difficulty controls.** Change party scaling multipliers from the F8 panel instead of only `scaling.toml`.

### P3 — Fun and stretch (only after everything above)

- [~] **T41 — More challenges.** Horse race, fishing contest, boss rush or arena, hide and seek, scavenger hunt.
- [~] **T42 — Duel extras.** Wagers, selectable arenas, spectator camera, rematch.
- [~] **T43 — Cosmetics and recognition.** Titles, co-op bounties and a scoreboard.
- [x] **T44 — Group photo mode.** Posed shots with both characters. (Done through B16 (1), decided 2026-11-17: the
  game's own Photo Mode shows both characters in the shared world, and our partner frame, name tag and hold prompt are
  hidden while it is open (8a65cf2d, cs38l). Posing / emotes from the game's animations: after the release, B16 (2).)
- [~] **T45 — Shared stash.** (As B12: built on the host's own warehouse; untested until a save has the warehouse
  unlocked, asked.)
- [!] **T46 — Baby wyvern hatching.** Blocked until the game supports it; leave `[!]` unless that changes.
- [ ] **T47 — Voice chat.** Steam voice integration (low priority, friends usually use Discord).
- [ ] **T48 — 3-4 player support.** Only if the architecture allows it without destabilizing 2-player play.

---

## 5. The big update (picked by the user 2026-11-07, after the full decomp)

**Ship rule (user 2026-11-07):** nothing of this update ships until EVERY task below (except B08, dropped) is done and
works flawlessly to our knowledge. The guest must see and use every NPC the host does. B09 comes back in a safe form.

Order: the native shared world first, because it makes most of the world picks real instead of simulated (one world:
same NPCs, events, map, mounts, combat damage, bosses, join mid-event). Then the Co-op+ layer on top, then the
features that stay Co-op+'s own. Each task: decomp first (build/decomp: functions.tsv / classes.tsv / c/*.c), then
build, two-copy test, savecheck, notes, commit. Status key as above.

**Foundation**
- [~] **B01 — Native shared world.** (two copies: join from the title, restart into the shared world, co-op saves per friend, package; Steam and two PCs untested) ByJunter's NativeServer base (upstream/cdmp-native, ported to 3576) under Co-op+:
  native mode in Co-op+ (docs/research/native-coopplus-layer.md), both DLLs in the harness, joining starts both.
  Joining only from the main menu (user 2026-11-07), before a save loads. Open design item: where co-op progress
  is saved (the native host plays in a save sandbox and never writes the real folder; tie it to Co-op+'s co-op
  worlds, one save per friend).
  Shipping it distributes ByJunter's GPL code: ask the user before the release.
- [~] **B02 — Same NPCs and townsfolk** (native: one world; check towns, schedules, the playtest's missing NPCs). (2026-11-22: the same people by key in both games at Hernand (cs67a: 116-120 of 120 matched in time) and Fort Windridge (cs27e: 55/55, 59/60, 62/64); the far joined player's own townsfolk and shops on by default. Left: more towns, which the test saves' story gates limit; the far joined player meeting the host at one shop in a run.)
  (2026-11-09 evening: the far guest's own townsfolk and shops work (U-1 / U-e / user2_shops, tcB201 / 214 / 216).
  S33 / S35 per-NPC containment ported and WORKING (tcB228/229: no run-wide U-e abort), on by default.
  Far shops FIXED and on by default (2026-11-09 night, interaction_share: the game removed an NPC's shop
  interaction by key when one player's scene closed; tcB242 near-host shop, tcB243 / tcB245 shop with the host
  409 m away, tcB245 on plain release defaults). Open: a hand check in more towns; a far guest that then meets the
  host at the same shop in one run. 2026-11-16 cs27e: Fort Windridge, the same NPCs in both games by key (55/55, 59/60, 62/64 within 60 m;
  cs27d's gap was the dev script missing batched 2005s), and its soldiers fought and downed both players.)
  - [~] **B02a — The guest talks to NPCs on its own progress** (2026-11-08: greeting works, tcB88/89/91; npc_category).
  - [~] **B02b — Merchants for the guest (user 2026-11-08):** the guest opens a merchant, buys and sells with its own
    money and stock, and starts and finishes the merchants' quests on its own, without the host at that step.
    (2026-11-09: merchants shown in co-op for both (merchant_unhide login guard); the guest's own shop scene
    (shop_twin) -- the guest BOUGHT and SOLD on its own at the Hernand butcher (tcB166/167/170), on by default in the
    release ini. Shops with the host far away work, on by default (tcB243 / tcB245). Open: merchant quests (none
    available in the test saves yet).)
    Start from ByJunter's S46 shop work (docs/research/s25/s46-tenderos.md: own shop scenes for user 2, U2 routing,
    T1/T2, R0), never run in the game and written for 2976.
    (2026-11-09 evening: FAR FROM THE HOST TOO -- the far guest's town is populated (tcB214) and it opened its own
    shop at the grocer with the host 424 m away (tcB216: U-1 + shop_probe + user2_shops + T2 ported to 3576).)
    (2026-11-09 later: a second merchant works for the guest -- it bought at the Hernand grocer, tcB194. Far from the
    host: user2_stages + world_anchor PORTED to 3576 (arm check READY in tcB198); arming lowered to >= 4 cells for
    the 4-9 cell dead zone; tcB199 checks the guest's market with the host 430 m away.)
- [~] **B03 — World events and sieges** joinable by the guest. (2026-11-15/16: world events are stages; the
  Rodbel dock's "Reinforcements" starts in ~25 checklist runs and BOTH games log it, the guest fighting in it;
  Fort Windridge: the same NPCs in both games and its soldiers fight both players (cs27e). Left: a siege.)
- [~] **B04 — Shared map and knowledge** (fog / visited zones, knowledge learned by one counts for both).
  (2026-11-09: fog_share built -- the guest's map fog at its login OR-ed with the host's on the host's server, on in
  the release. 2026-11-17 SEEN: a less explored friend (save m101) opens its map in the host's world: with fog share
  it shows the host's explored land (cs50d), without it only its own (cs50e); docs/drafts/b04-fog-share-2026-11-17.png.
  A brand-new character has no map yet (it unlocks in the story). Knowledge: 2026-11-16 knowledge_grant built (native 9160d19, [session2] knowledge_grant, default 0):
  a learn by one player is learned by the other through the game's own learn entry; installs cleanly on 3663,
  no learn happened in cs29a / cs28c to prove it -- needs a moment where the game teaches a knowledge.
  2026-11-22 cs91 on 3732 (the chest step that taught "Tough Meat" in pp43): installs, 6 minutes, learns 0, faults 0;
  no "Knowledge Acquired" on the host's screen, so the test save already knew it. Still needs a knowledge the save
  does not have.)
- [~] **B05 — Other mounts and vehicles** (bears, wolves, raptors...; wagon, skiff, mech) seen and ridden.
  (2026-11-15/16: Blackstar works both ways in the shared world -- the guest calls and flies it and the host sees
  it (cs18i); the host calls and flies it and the guest's game loads it and seats the host on it (cs32e/f, by log).
  Left: bear / wolf / raptor / wagon / skiff / mech, which no test save has, the owner's copy included: hands-on.)
- [~] **B06 — Mounted combat damage** on shared enemies. (2026-11-15, from the checklist's mountfight logs: the
  guest's blows from the saddle hurt and kill the host's enemies -- pp22: 16 blows, 0xB01000D2 63 -> 14 -> 0 and
  0xB010031A / 0xB010048A to 0 on the host, 4 kills in the guest's kill feed; pp19 2, pp20 3. 2026-11-17 cs52: the
  silent "we hurt" was a two-worlds-only line, not the saddle (the guest's blows from the saddle carry its own key);
  found with it: the host's layer took the guest's character for its own and "Enemy health with a guest" barely
  worked in the shared world -- fixed in 1c6453e1 (both players' blows, cs55). Left: the host fighting from the
  saddle with the guest watching (cs26a could not script the host's ride).)
- [ ] **B07 — Boss mechanics for two** (grabs, finishers, phase triggers, arena locks).
  (2026-11-21: docs/research/boss-audit.md revised for the shared world (its top section): the first boss test reads
  each mechanic off both screens with the JOINED player landing the final blow, because with each player's own quest
  progress on (QuestRoute) that kill may credit only the joined player's quest and not move the host's story step;
  also whether the AI hand-over's fight rule should leave bosses alone. Needs a save at a boss: asked.)
- [-] **B08 — Join mid-fight, boss, cutscene or loading screen.** DROPPED (user 2026-11-07: "Being able to join
  mid-game is not a priority ... people can only join parties from the main menu screen"). Joining is from the title
  screen only (B01's join flow).
- [~] **B09 — Shared cutscenes on 3576** (the guest sees the host's boss cutscenes; late1's old crash).
  (2026-11-14/15: the READY CHECK, experimental "Story scenes together": the cabin scene waits for both and plays in
  both games, testers saw it work 16 Nov. 2026-11-16 from their zips: no host story scene is copied to the guest any
  more (the black screen after the host's next cinematic; native 781dba6), the NPCs move in the guest's cabin scene
  (cs28b shots), a message when the friend is busy with the NPC (one talk at a time, the server's rule). Left: the
  scenes that start from a TALK (design question to the user), a hands-on / tester check of the fix.
  2026-11-21: the talk scenes were decided (option 2, 11-17); a script may reach them now: talks start with R while
  the camera faces the person, every scripted try had held E (cs79: faceto + R at Marius and Ross).)
- [~] **B10 — Quest sharing on by default** (live quest steps, co-op save per friend: out of experimental).
  (2026-11-09: quest_route -- option 3 LIVE in tcB198: the guest's game events go to its OWN quest manager instead
  of the host's; option 2 = quest_shared_credit, built, untested. A guest quest step advancing still to be shown.)
  (2026-11-21: option 2 gave the joined player no progress in cs85c (its second pass reused the acting player's
  component); v2 (the other player's own component) is in test (cs85d), and cs85e shows option 3 from the joined
  player's side (its own cheat kills at the camp). A consequence of option 3 to settle before the release: an
  objective of the HOST's that the joined player completes (a camp's last kill, a story boss's final blow, B07) only
  counts for the joined player's own quests, so the host's step may stall unless option 2 is on.)
  **Design (user 2026-11-08, after a Discord poll):** option 3 as the base -- each player keeps and plays their OWN
  quests and story in the shared world (start / finish individually, fight together); option 2 as an optional "do it
  together" bonus -- when both are on the SAME step of the same quest, one completing it checks it off for both
  (never a step a player has not reached). Rejected: copying every completion to both (skips steps, rewards and
  cutscenes) and host-only missions.

**Items** (decomp: the bag's item transaction, docs/research/reverse-engineering-3576.md "Items")
- [x] **B11 — Item gifting and trading** (confirm step, no duplication, a log, controller). (2026-11-10: the inventory's
  Discard next to the friend asks Give to <name> / Discard / Cancel, pad and keyboard, tcB299; the give reaches the
  host's server both ways, tcB300; 2026-11-10 tcB321: host -> guest give WORKS through the game's own whole-record
  move (the same-user check passed for the mod's own moves only); hidden behind [coop] item_sharing until the guest
  side is seen. 2026-11-15/16 on 3663: host -> guest cs21a, guest -> host cs34a, both "stage 5 moved", on by
  default (the layer's built-in [coop] item_sharing=1); cs34b: the item arrives in the other bag on screen. Trade (2026-11-16 cs35b): each
  offers a stack ("Offer in a trade" on the same question), both accept a card, each game gives its own; both moves
  "stage 5" on the host's server. One PC only; two PCs with the testers.)
- [~] **B12 — Shared stash.** (user 2026-11-10: the host's own warehouse once the story unlocks it; built, the
  page and card follow the warehouse state; same move as the give; untested with an unlocked warehouse.)

**Minigames and challenges**
- [ ] **B13 — Real PvP minigames** (arm wrestling on each other's live gauge; shot contests, boxing, spear duel, cards).
  (2026-11-21 reading: the layer's versus arm wrestling (D35: one gauge that both players' E presses push, the host
  holds it, each game writes it) is from the two-worlds era (each at its own table, the other's puppet in the NPC's
  seat); its gauge address is ported to 3732 (kMiniGameRoot 0x724B3A8, not checked in a match). In the shared world
  the order is: the guest's own match at the Hernand table (B14, cs76c), then both players in a match at once (one
  server: does it allow two matches at one table, or at two tables?), then the shared gauge; [dev] versus_test checks
  the gauge on one game.)
- [~] **B14 — The empty arm-wrestling table** (T21). (2026-11-16 cs33a: on native both games list the same 15
  people at the Hernand inn table and both screens show them; left: the guest's own "Try arm wrestling" there.)
  (2026-11-21: the table is the host's scene 'cd_minigame_armwrest_base' with no twin, so the joined player's prompt
  there is refused like the host's own shop entries; the way in is a twin like the shops' (research:
  docs/research/armwrest-twin.md; the organizer's menu captured, the match itself next).)
- [~] **B15 — Duel extras and scoreboard** (wagers, arenas, spectator camera, rematch, titles). (Rematch T42,
  scoreboard and titles T43 exist. 2026-11-16 wagers: from a challenge's invite on, "Stake on the challenge" on the
  inventory's Discard question; when the match ends the loser's game gives its stake to the winner (cs36e, one PC).
  2026-11-17: arenas = the duel ground (decided; named arenas after the release); cs43b: leaving the ground 10 s
  loses the round, two rounds and the 2-0 result the same on both games. Left: a spectator camera (needs a third
  player: B21).)
- [~] **B16 — Emotes and group photo mode.** (2026-11-16: the game has no emotes; its Photo Mode (pause menu,
  Others) shows both characters. Our partner frame, name tag and hold prompt were in the shot and are now hidden
  while it is open (the mode id at exe+0x72AF864 / 0x72AF898, cs39e / cs38h / cs38i, pad and mouse). That id also shows while loading, after cutscenes and when the friend
  arrives (pp26); since 8a65cf2 it counts only when it starts as the pause menu closes (cs38l). In 8a65cf2d,
  checklist pp27 running. Emotes made from the game's own animations: the owner's call (waiting list).)

- [~] **B16b — Player interaction menu (user 2026-11-09).** (2026-11-09: built and seen working on the guest,
  tcB209: the hold-R prompt 'Interact' on the other player opens the shop-style menu with every challenge, a greyed
  'Trade (coming soon)' and 'Leave'; W/S, Space, Esc. Pad: SEEN 2026-11-10 (tcB275, simulated pad, two copies, 3576):
  by the friend the prompt shows "(A) Interact", holding A opens the menu, D-pad moves, A selects, B closes (the
  chord + D-pad Up version of 11-09 was replaced by the game's own interact). 2026-11-16: the greyed entry became
  "Give or trade an item" (B11's give and trade, cs35a/b), so trading is in it. Left: the arm-wrestling challenge
  entry, with B13. The pad re-check on 3732 is done: cs89 2026-11-22, simulated pad, "the hold-R prompt on the other
  player opened the interaction menu".) Hold R on the other player (the game's own hold prompt:
  the R key in a circle that fills while held, exactly as at shopkeepers and their shop menus -- NOT a plain
  "R Rock, Paper, Scissors" tap prompt) opens a player interaction menu like the shop menu. It holds every minigame
  challenge the two can do (duel, RPS, race, hunt, fishing, hide and seek, arm wrestling...), later trading (B11) and
  any other player-to-player feature that fits. Starting point: Geonode's proof of concept from the Discord (a "Kliff /
  R Rock, Paper, Scissors" prompt on the other player; screenshot 2026-11-09), which replaces the Shift+F5 picker.

**Combat and safety**
- [~] **B17 — Teammate revive rework** (2026-11-09: revive by interacting -- hold E next to the downed friend --
  works, tcB208; the bleed-out timer, progress line and credit notices were there; Q / U kept. User check left.) (bleed-out timer, revive by interacting, progress and credit; Q/U kept).
- [~] **B18 — Combat feedback for both** (status effects, knockback, damage numbers, per-player lock-on).
  (2026-11-16, Borque's report: damage measured (cs40c / cs40d): on the SAME save the guest and the host take the
  same HP per swing (12.0 / 12.2; the guest's character stats are used); the weaker test save takes less. The
  enemies preferred the host (cs40b: 27 blows on the idle host, 3 on the fighting guest): the host's client runs
  their AI. Fix: the AI hand-over's fight rule ([session2] ai_handoff_fight, native 81f184c, default off): an
  enemy the guest fights runs on the guest's game; cs40f: 9 hand-overs, 0 failed, blows on the guest 3 -> 10.
  The hand-over is on in 8a65cf2d. 2026-11-17 cs45a-e: moving enemies were ~1.1 s / 2.5 m behind on the
  friend's screen (the misses, "hits without touching"); the combat stream (combat_follow v2) ported to 3663
  (239-byte MoveActorAck) and on: 0.5 m / 0.2-0.3 s, in ada1800c. Left: knockback / status effects / lock-on
  by eye.)
- [~] **B19 — Anti-grief protections** (quest NPCs, the host's story items; setting). (2026-11-10: hits on protected
  people taken out of a swing that also hits enemies; an NPC the host fights is fair game. User 2026-11-10: world items
  are per player instead of protected ("Lootr", built and seen working both ways for gathered items, tcB329/332;
  setting "Items in the world"); chests / bodies / across sessions still to do.) (Chests and bodies since done: the
  checklist's chest and loot steps. Across sessions: designed 2026-11-21, docs/research/lootr-across-sessions.md;
  its test needs one save into the test co-op world: asked. Late 21 Nov: v4 / v4.1 written from two independent
  reviews (the file written at the save itself, an exact save-time stamp, a durable two-copy ledger, confirmed
  set-asides), in the worktree, not built yet.)
- [~] **B20 — Partner audio** (footsteps, combat sounds, voice lines, distance falloff). (2026-11-17 cs42a: the
  host's game plays the friend's swings (peaks to 553 vs ~120 ambient). cs47-cs48c: the friend's FOOTSTEPS are
  played too (every step reaches the game's sound player, ~115 in a 50 s walk) and heard: muting them in
  alternate 4 s windows drops the host's loudness after a step from median 14.1 to 11.7 (= no step). cs42a's "not
  heard" was ambient noise. Falloff: the sounds play at the friend's character (quieter as it walks away).
  Left: voices (effort / hurt sounds) checked the same way. 2026-11-21 reading (3576 decomp): voices are played by
  the action's frame events (ClientFrameEventVoiceSound, processor 0x79A160), the same frame-event path as the swing
  sounds (ClientFrameEventSoundEvent 0x795B20) that cs42a heard for the friend; its checks are the character's type
  and its voice set, no local-player test seen: very likely played, to be confirmed by a count as for the steps.)

**Last (needs the rest stable)**
- [~] **P1 — Playtest findings (Borque's two-PC session, 2026-11-17, ada1800c).** Each fixed and tested before the
  release (the ship rule: the guest sees and uses what the host does).
  - [~] The guest's progress was not kept: the host's PC has 3 Steam accounts' save folders and guest_own_save was
    forced off. Fixed (native af4cdfb: Steam's ActiveUser picks the folder). Not yet run with several folders.
  - [~] A failed horse taming (minigame 0x4242) left the guest with no input: the server sent the 2292 of the
    guest's own taming scene to the HOST's character, so the guest's scene never ended. Fixed (observer_seq_copy:
    the host's client's 2292 of a scene of user 2's own is copied to user 2 naming its character). Not yet tested.
  - [~] Horses: the host sees the guest's horse invisible, the guest sees the host's away from the host.
    horse_follow / horse_follow_reverse were still on the 2976 MoveActorAck layout (235 B): every Ack was "bad size",
    none made, in our runs too. Ported to 239 B / the 3460+ offsets. Checked by log (pp30a, 2026-11-18: HorseFollow
    bad size 0, made 1311; the guest's horse on the host's client 736 delivered, was 0); not yet seen on screen.
    Borque 20 Nov (26bf2c71): the joined player on a NON-own horse (a bandit's, a wild one) stayed put on the host's
    screen: the ride rule (native aeb7d33) hands a ridden NPC horse to its rider's game; in 4394b014 and later.
  - [ ] The guest sees city NPCs sitting on benches in a T-pose (the guest only). (Lead: GuestSeqRestore swaps a
    remote NPC's seq_bodyanim_action it cannot find for an idle wait, 31 times in his session; cs58 looks in Hernand.)
    (2026-11-21 late, from B14's cs86f: the arm-wrestling table's people are bound to the HOST's copy of their scene,
    which never runs on the joined player's game, and GuestSeqRestore swapped their seat animation for a wait. The
    bench sitters are most likely the same case. cs87 gives the table's scene to the joined player's game only as its
    own twin: if their animation comes back there, a twin is also the T-pose fix for the scenes the joined player sees.)
  - [~] The guest cannot interact with townsfolk (Borque 20 Nov video: "client still not able to interact with npcs";
    2026-11-21 afternoon: FIXED for shops and greetings on 3732, both checked (cs82c: the shop alone and with the host
    beside the shopkeeper; cs80c: a townsperson's greeting, "Trust +5"); in cfaa3a69 (checklist step "shop" added).
    Left: the twins lost to a far teleport into a town whose shop scenes the host already runs (shop_twin_again, round
    2 in the worktree); the inn patrons' arm wrestling (B14: the table is a host scene with no twin; a twin of it,
    [session2] twin_extra, built 2026-11-21 evening, in test cs86); talks that belong to a
    host scene (B02a's twin for dialog scenes); pp42-shop's one "User2Shops [nested vf48]" (inside a routed 2808 of
    the joined player's OWN shop scene a nested lookup kept the host's manager: check what it touched; none in
    cs82a-c);
    talk greyed). 2026-11-21: reproduced by script on 3732 (cs75c-e at the Hernand butcher, cs76c-e at the inn
    cellar's patrons: no R ever reached the host's server). The guest's prompt check of a person's dynamic entry is
    redirected to the host's stage manager X (GuestXInter) and finds the stage, then refuses (result 0) every time
    (thousands of checks; Borque's client log 173). 3732's check exe+0x23607D0 tests the instance's +0x168, its dead
    check, a condition on the player and a condition list after the lookup: research builds gx1 / gx2 count where it
    stops (cs75g, cs75p1). 3576 got "R Shop" for the guest with the same code (tcB238 / tcB242).
    2026-11-21 noon, what it really is (research gx3-gx7, cs82a): (1) shops: the joined player's usable shop entries
    are ShopTwin's (its own copy of the host's shop scene); the host's own entries are refused there on purpose. The
    cs75 scripts teleported the joined player AFTER the twins were made, and a far teleport ends every scene of its
    own manager, the twins too (8 -> 0); nothing made them again. Fixed in ShopTwin (prepared again). The far shop
    (the joined player's own scene) works on 3732 (cs82a). (2) Greet / talk with townsfolk: NpcCategory's table read
    empty on 3732 (3663's offsets; 3732 is back at 3576's), so the joined player's greet / talk condition failed:
    fixed. (3) Still open: talks that belong to a host scene (B02a's "twin for the NPC scenes with dialogs").
  - [~] The guest's "interact" opens the NPC's menu but the NPC keeps walking (the host's NPCs stop and look).
    (2026-11-21: the talk rule, native 9b9bad7, [session2] ai_handoff_talk=1: the person the guest interacts with runs
    on the guest's game while it stands within 6 m, then back; in single player the talker's game runs the NPC and its
    AI is what stops it. To test: cs75 at the butcher; a walking NPC by hand.)
  - [ ] The host's armour dye is not seen by the guest.
    (Lead: DyeItemListByEquipSlotAck 3042 / DyeItemByInventoryAck 2911 are ToTarget writers, owner_fanout_writers.h:
    only the dyer's client gets them; the guest needs the host's item re-sent with its dye, e.g. an OtherEquipItemAck.
    2026-11-21: on 3732 OtherEquipItemAck is 2426 (a field broadcast, not a ToTarget writer), the dye Acks 3150 /
    2463 go to the dyer only, and nothing broadcasts a dye: the fix re-sends the dyed item as a 2426 to user 2. Needs
    one capture of a real dye (waiting list: the owner's hands-on).)
  - [ ] A tree the guest cuts down is duplicated for the host. (2026-11-21 lead, from the native base's research
    gimmicks-dueno.md section 7: CreateProjectileFromGimmickEventReq has no controller check on the server, so when
    both games run a gimmick's event both send it and the server makes the projectile twice (S27: 10 extra). A felled
    trunk may be such a projectile. gimmick_event_dedup (default 0) withholds the guest's copy when the gimmick's
    controller is the host; its rule: run "probe" first, switch to 1 only if every "would withhold" matches one
    "DUPLICATE created" on the host. The probe now rides in cs73b / cs77 / cs81; a tree to cut is still needed.)
  - [~] Carryable animals (kiwis, goats) are not synced. (2026-11-19/20: attach_follow=carry, the default (native
    914e7d7 / aeb7d33 / 37af9e1): a caught animal nobody owns whose actions name a player's character as Path parent
    follows that character on the other screen, both ways (cs68f: the host's partridge, made poses 1-3 mm from the
    game's own; cs69a: the guest's, 13 poses of an ~8 s hold, made at the host's client). Left: seeing a long carry on
    the other screen (cs69c-h found no catch held long enough), and kiwis / goats (only partridges were caught).)
  - [~] The running "animation freeze" happens almost all the time out of town (his theory: different FPS on the two
    PCs; our runs have the same FPS on both). The host's FPS drops.
    (2026-11-21: REPRODUCED on one PC with the TEST sim_lag (cs74b: held 8.4% vs 2.2%; Borque's two PCs 3-8%): the
    other player's samples arrive LATE for their stamps and the game, reading 200 ms back without extrapolating, holds
    the pose. remote_interp_char (native 6695412) gives the character a delay from that lateness; cs74c / cs74d
    compare. Each game's own frame hitches are logged now (UiPause) for the "both screens at once" case.)
  - [~] Borque (2026-11-18 08:05, 9e85b891, client side): (1) dye vendor / goblin trader missing -> both added to
    the merchant unhide + shop twin, other function NPCs to unhide only (native, to test); (2) talk greyed out
    (wanted after the cart? asked); (3) the client's horse invisible for the host; (4) taming never ends (the wild
    horse stays under the host's control); (5) mining with the hand drill, the host's broken ore; (6) fishing; (7)
    small animals not caught; (8) the host's carried catches float on the client; (9) the client's cart invisible
    for the host (seat change not published). 3-9 wait for the host's zip (asked).
    (3) REPRODUCED in cs63a (two copies): the host's client never links the guest to its horse (RideOn::Enter
    of the remote rider: Link 0); the guest sees the host mounted. CAUSE: mount_sync still checked 2976's
    StartActionAck (725 B; 3663: 729, parent +0xCF, target +0x171) and 2976's "no attachment" type (9; 3663: 12), so
    no remote mount was ever applied on 3663 (either side). Fixed (native 52e5e36 / 214feb6): cs63c the host links AND
    seats the guest on its horse. Lead for (8): attach_follow (an 0xB riding an 0xA parent: a pet on a shoulder, a
    carried animal) is off and still 2976-sized (MoveAck 235, actions 725, parent +0x117): port it, test a carry.
    Also still 2976-sized: enemy_relay / local_capture StartActionReq 725, remote_puppet 725, scene_diag 725.
    2026-11-21 status: (1) both vendors in the lists (7f58ad0, cs60a: the hide bit cleared); (2) the wanted state
    (asked); (3) fixed (cs63c); (4) the 2292 copy (f132243), then "then 2" never answered (20 Nov): ShopProbe B in
    4394b014 logs the host's answer; (5) / (6) the owner's hands-on capture; (7) the catch rule (26bf2c71); (8)
    attach_follow ported (f9791c4) and carry on by default (914e7d7 / aeb7d33), see "Carryable animals" below; (9) the
    ride rule hands a ridden NPC vehicle to its rider's game (aeb7d33), a cart not yet seen. enemy_relay /
    local_capture / remote_puppet are the base's two-worlds puppets, not used in the shared world.
  - [~] JackyChan004 / D3vils (2026-11-18, 9e85b891): after "Enter the cabin" the friend's 2644 reset the host's
    stage manager (F4 off in the release): no bear fight for the host, the friend's stages off for the run, NPCs
    idle on the friend's screen. Fix: user2_revive_manager=1 (in 62f49d19 and every build since; cs44j: "1 LIVE",
    the host's manager kept its state). F4 never had to act in our runs (user 2 has its own field there, the
    testers' friend was in the host's): the testers' retry of the cabin is the check (asked 18 Nov).
  - [ ] Jay (via the owner, 2026-11-17 23:20; an older build, not played for a few versions): small "desync
    hitches" in the other player's animations, seen by both players; polish, not a blocker. Likely the same family
    as the running freeze: when testing that one, also measure short stalls of the remote character's animation
    on both screens (gaps in the remote samples, a pose held for a frame or two), and tell Jay when there is a
    build to try.
- [ ] **B21 — 3-4 player co-op**, only if two-player play stays stable.
- [ ] **B22 — Release**: package, two-copy regression, the user's OK (GPL native code), GitHub and Discord.
  - [~] **B22a — Save protection and installer (Plitos, required for the release; user 2026-11-08: "add this part to
    our final package by the time you release it").** The installer reuses an existing Ultimate ASI Loader (e.g.
    `winhttp.dll`) instead of always deploying `winmm.dll`, keeps occupied proxy DLL names, stops on an ambiguous
    loader setup, backs up BOTH the solo and the co-op save trees and verifies the copies by hash before replacing
    anything, and checks the EXE identity (version, PE timestamp, image size). In the mod: co-op refuses to start
    (solo stays usable, the reason shown) when any required save-redirection hook fails to install -- on native that
    is the native base's save guard (the redirect to the co-op folder), not Co-op+'s save_io. Reference: Plitos's
    C606D401 handoff (Setup-Direct.ps1, save_io ready guard); credit Plitos in the changelog.
    (2026-11-22 cs90 on 3732: the refusal path works end to end -- with the save redirect counted as failed, "Co-op
    REFUSED for this launch", the game ran solo on its own saves, the partner was told, user install restored 5/5.)
- [~] 2026-11-18 Scene Req handlers with an unchecked field-owner stage manager (vf48 -> lock, no null test), found with
  Borque's host crash (2808, guarded by SceneReqGuard 12c9bc8): 2644, 2654, 3141, 2672, 2678, 2563, 2313, 2546, 2924,
  2125 (sites in build/decomp/notes_host_unload_crash_3663.md, private). 2026-11-19 on 3732: 2808, 2678 and 3141
  guarded (each its own not-attached exit, scene_req_guard.cpp); the other seven traced as branching away before
  their callee's first use (build/port3732/nulltrace73.py). Left: a crafted 2808 at the host's load would make the
  guard testable on demand (the notes' section 5).

