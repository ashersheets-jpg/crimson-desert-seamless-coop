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
- [~] **T05 — Drop-in mid-event.** Joining during combat, a boss fight, a cutscene, a loading screen or a menu catches the guest up with no stuck state. *Done when:* each of those five situations is tested.
- [x] **T06 — Desync detection and resync.** Periodically compare key state (positions, enemy set, health, time/weather). Auto-resync on drift and add a manual "resync me" action. *Done when:* an intentionally induced desync is detected and repaired.
- [x] **T07 — Host leaves or session ends.** When the host quits or ends the session, the guest returns cleanly to their own world: partner entities removed, hidden enemies restored, UI cleared. *Done when:* both exit paths (host quit, guest leave) leave no leftovers.
- [x] **T08 — Guest-return state integrity.** After a session, verify the guest's own save and world carry no leaked co-op state (scaling, hidden enemies, loot flags, cutscene flags, time and weather). Build an automated before/after state diff. *Done when:* the diff is clean after three different session types.
- [~] **T09 — Menus and pause.** Opening the map, inventory or pause menu must not freeze, desync or soft-lock the partner's world. Define and implement the pause behavior. *Done when:* every menu type is tested while a partner is fighting.
- [~] **T10 — Shared-kill loot and XP for the guest.** The guest receives drops and XP for kills they contribute to, with no duplicates and none missing. Make loot rules configurable (shared, per-player, first come). *Done when:* verified with the stand-in; `[needs-2PC]` for the real guest.
- [x] **T11 — Network stress and performance.** Test with simulated latency, jitter and packet loss, and with 33+ entities. Measure bandwidth and FPS cost, set entity and bandwidth budgets, and degrade gracefully (lower update rate before dropping features). Record results. *Done when:* a documented stress report exists and no crash occurs under bad networks.

### P1 — Core experience

- [x] **T12 — NPC and wildlife consistency.** The same NPCs and animals appear in both players' views within range (or clear, deliberate rules exist). Required for fair hunting contests and for not shooting an animal only one player sees. *Done when:* hunting contest kill counting is verified.
- [~] **T13 — Boss mechanics audit.** List every boss mechanic that assumes one player: grabs, finishers, phase triggers, arena locks, targeting. Make each work with two players, or define a safe fallback. Keep the host-lands-final-blow rule. *Done when:* an audit table exists and the first five bosses encountered are verified; `[needs-2PC]`.
- [x] **T14 — Partner party frame.** An always-visible, game-styled partner health and stamina element, with a low-health alert. *Done when:* visible on keyboard and controller, with no overlap with game HUD.
- [~] **T15 — Teammate revive.** When downed, a bleed-out timer starts and the partner can revive by interacting. Show revive progress and credit. Keep the existing Q/U revives as fallback. *Done when:* revive works in a normal fight and a boss fight; `[needs-2PC]`.
- [~] **T16 — Crime, guards and bounty.** Define and implement how guards and NPCs react to the guest's crimes in the host's world, including shared vs per-player bounty (config). The host must never be punished for something only the guest did unless configured. *Done when:* tested with theft and assault.
- [~] **T17 — Anti-grief protections.** The guest cannot kill essential or quest-critical NPCs, destroy host quest items, or consume story-critical objects. Configurable. *Done when:* each protection is tested.
- [!] **T18 — Trading and gifting.** Players can send items to each other with a confirmation step. No duplication exploits, a transaction log, and works on controller. *Done when:* duplication attempts (disconnect mid-trade, spam) fail safely.
- [~] **T19 — Party travel and summon.** Add party fast-travel (both players go, with consent), "summon to me" (with consent and cooldown), and an optional auto catch-up if the partner is too far. Never place players in invalid or underground spots. *Done when:* tested from several regions including cliffs and water.
- [x] **T20 — World-state persistence rules.** Define and implement what happens to world state changed during co-op (cleared camps, killed bosses, gathered nodes, opened doors) for host and guest. Document the rules in the README. *Done when:* the rules are written and a test confirms host and guest outcomes.
- [!] **T21 — Fix: empty arm-wrestling table.** The pub table has no NPCs when the stand-in is connected from the start. Find and fix the cause. *Done when:* arm wrestling works regardless of connection timing.
- [~] **T22 — World event sync.** Sieges, faction events and other dynamic events that start in the host's world are visible and participatable for the guest. *Done when:* at least the most common event types are verified.
- [ ] **T23 — Object interaction sync.** Doors, gates, levers, movable and thrown objects, and destructibles look consistent to both players. *Done when:* each is tested with both players nearby.
- [x] **T24 — Communication.** Add in-session text chat and ping types (danger, loot, go here) with game-styled display. *Done when:* usable on keyboard and controller.
- [~] **T25 — Mount edge cases.** Mount summon, despawn, death, mount swapping, and flying-mount landing all stay in sync. Two players on one mount only if feasible. *Done when:* each case is tested.
- [~] **T26 — Death penalty and respawn options.** Config options for penalty (none, partial loss, full) and respawn rules, applied consistently on both sides. *Done when:* every option is tested.
- [~] **T27 — Nexus release package.** README (install, uninstall, requirements, controls, known issues), credits and permissions for all other authors' mods (list as requirements unless permission is on file), license, changelog, bug-report template, compatibility list, screenshot and GIF shot list, and verified manual and Vortex install. Nothing from game files bundled. *Done when:* a clean-machine install-and-uninstall walkthrough passes.
- [~] **T28 — Mod compatibility testing.** Identify the most-downloaded mods for the game, test alongside the co-op mod, and document conflicts. Detect known conflicts at launch and warn the user. *Done when:* a compatibility table exists.
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
- [~] **T44 — Group photo mode.** Posed shots with both characters.
- [!] **T45 — Shared stash.**
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
- [ ] **B02 — Same NPCs and townsfolk** (native: one world; check towns, schedules, the playtest's missing NPCs).
  - [~] **B02a — The guest talks to NPCs on its own progress** (2026-11-08: greeting works, tcB88/89/91; npc_category).
  - [~] **B02b — Merchants for the guest (user 2026-11-08):** the guest opens a merchant, buys and sells with its own
    money and stock, and starts and finishes the merchants' quests on its own, without the host at that step.
    (2026-11-09: merchants shown in co-op for both (merchant_unhide login guard); the guest's own shop scene
    (shop_twin) -- the guest BOUGHT and SOLD on its own at the Hernand butcher (tcB166/167/170), on by default in the
    release ini. Open: merchant quests (none available in the test saves yet), shops with the host far away (the
    server builds the world only around the host: the B02 far-world gap, ByJunter's user2_stages, not ported).)
    Start from ByJunter's S46 shop work (docs/research/s25/s46-tenderos.md: own shop scenes for user 2, U2 routing,
    T1/T2, R0), never run in the game and written for 2976.
- [ ] **B03 — World events and sieges** joinable by the guest.
- [ ] **B04 — Shared map and knowledge** (fog / visited zones, knowledge learned by one counts for both).
- [ ] **B05 — Other mounts and vehicles** (bears, wolves, raptors...; wagon, skiff, mech) seen and ridden.
- [ ] **B06 — Mounted combat damage** on shared enemies.
- [ ] **B07 — Boss mechanics for two** (grabs, finishers, phase triggers, arena locks).
- [-] **B08 — Join mid-fight, boss, cutscene or loading screen.** DROPPED (user 2026-11-07: "Being able to join
  mid-game is not a priority ... people can only join parties from the main menu screen"). Joining is from the title
  screen only (B01's join flow).
- [ ] **B09 — Shared cutscenes on 3576** (the guest sees the host's boss cutscenes; late1's old crash).
- [ ] **B10 — Quest sharing on by default** (live quest steps, co-op save per friend: out of experimental).
  **Design (user 2026-11-08, after a Discord poll):** option 3 as the base -- each player keeps and plays their OWN
  quests and story in the shared world (start / finish individually, fight together); option 2 as an optional "do it
  together" bonus -- when both are on the SAME step of the same quest, one completing it checks it off for both
  (never a step a player has not reached). Rejected: copying every completion to both (skips steps, rewards and
  cutscenes) and host-only missions.

**Items** (decomp: the bag's item transaction, docs/research/reverse-engineering-3576.md "Items")
- [ ] **B11 — Item gifting and trading** (confirm step, no duplication, a log, controller).
- [ ] **B12 — Shared stash.**

**Minigames and challenges**
- [ ] **B13 — Real PvP minigames** (arm wrestling on each other's live gauge; shot contests, boxing, spear duel, cards).
- [ ] **B14 — The empty arm-wrestling table** (T21).
- [ ] **B15 — Duel extras and scoreboard** (wagers, arenas, spectator camera, rematch, titles).
- [ ] **B16 — Emotes and group photo mode.**

**Combat and safety**
- [ ] **B17 — Teammate revive rework** (bleed-out timer, revive by interacting, progress and credit; Q/U kept).
- [ ] **B18 — Combat feedback for both** (status effects, knockback, damage numbers, per-player lock-on).
- [ ] **B19 — Anti-grief protections** (quest NPCs, the host's story items; setting).
- [ ] **B20 — Partner audio** (footsteps, combat sounds, voice lines, distance falloff).

**Last (needs the rest stable)**
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
