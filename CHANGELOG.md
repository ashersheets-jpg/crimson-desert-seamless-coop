# Changelog

## 0.1.0-alpha (unreleased; single-PC tested on game 1.0.0.3479)

Hotfix (2026-11-07, after a playtester's report):
- Fixed: the friend's game could skip its own prologue-ending cutscene and stay stuck on "Go to Hernand" (and then the guard for the inn's arm wrestling never came). The host's story cutscenes are no longer started in the guest's game by default ([coop] stage_cinema=1 turns that back on).

Performance and crash fixes (2026-11-07, 0.1.0-alpha+4effc5c0, from a full decompile of game 1.0.0.3576):
- Frame drops fixed: Co-op+ checked memory with a slow Windows call before every read of the game (about 100,000 a second), which stalled the game's own threads; it now remembers which memory is safe. Same test: 21-26 fps before, 92-94 fps after; the mod's network loop went from about 100 ms to about 2 ms.
- Fixed: a guest crash (exe+0xA12E4E) right after joining; the guard is now on the function that really crashes.
- An enemy debugging watch that ran for everyone (and added lag) now runs only with [dev] enemy_relay.
- Your friend's horse shows up when it is in mount slot 2 or 3 too (only slot 1 worked).

Radmin VPN / Direct IP in the co-op menu (2026-11-07, Plitos's handoff): Host copies an invite (coopplus1/<address>:37015/<secret>), the friend copies it and presses Join; a random secret, new on every Host, lets only the invited player in. Steam invites stay the default; Back to Steam Invite or Leave returns to them. The ini setup still works.

Since 0.1.0-alpha+f3517194 (2026-11-06 / 07, game 1.0.0.3576; tested with two copies of the game on one PC, sandbox saves only):
- Shared experience: every skill-point gain one player gets is given to the other too, through the game's own experience routine, anywhere on the map (no distance limit). Kills the guest finishes on the host's enemies now give experience to both (the amount each enemy kind gives is learnt from your own kills and kept in cdmp_kill_xp.txt; 15 until a kind has been seen).
- The host's enemies fight the guest: they target and attack the guest's character (they ignored it on 1.0.0.3576), and the guest's game no longer crashes when enemy copies load in a camp.
- The guest's blows stagger the host's enemies like the host's own blows ([coop] guest_hit_stagger, now 1; an update from an older zip turns it on).
- Kill counts: the "Journey Together" card credits the guest's kills to the guest (they counted for the host).
- Fixed: a false "out of sync" could bring back every hidden enemy at once (dead enemies kept for looting were counted).
- Prologue: co-op waits while either player is still in the opening tutorial and starts by itself the first time that player rides a horse (after the prologue); a banner says so. [coop] prologue_missions / solo_prologue.
- Co-op main menu at the title: Start Solo / Invite Friends / Start Co-op, the party with a crown on the host, and the host's choice of Continue Our Co-op Save / Start From My Solo Progress / New Co-op Save (the guest follows on the host's difficulty). A Co-op tab in the game's Start menu holds every co-op setting with its explanation.
- Lower latency: your friend is drawn about 50 ms after the network (was about 350 ms), smoothly.
- Direct play without Steam (Radmin VPN, LAN, any VPN): [net] transport=direct, see DIRECT PLAY in README.txt. Asked for by a playtester so they need not patch each release.
- From a playtester's patch (Plitos, merged): a quest step from before a co-op save switch is no longer applied, live quest steps are checked again before they run, and malformed cutscene messages are ignored.

Linux/Proton (2026-11-05, patch 0004): the panel, HUD and first-launch cards now appear under Proton/Wine. The overlay used to hook only NVIDIA Streamline's swap chain class, which is not the one presenting there (`Overlay: presents 0` in `cdmp.log`); on Wine it now also hooks DXGI's own class. Windows is unchanged. Steam: one fault in Proton's Steam bridge no longer turns co-op off for the rest of the session — the mod waits for Steam's relay network and retries with backoff (5 attempts).

New (2026-11-05, after the first playtest):
- Hide and seek, a new challenge: the challenger counts first with eyes closed (45 s) while the friend hides within 100 m (a gold ring on the ground marks the edge, red when you are outside). The seeker has 3 minutes; the hider's name tag, distance and map marker are hidden, and Cold / Warm / Hot tells how close you are. Within 4 m = found. Roles swap every round, first to 2.
- Fishing contest, a new challenge: six minutes, each player fishes in their own world; every fish kept scores by kind (river 1, sea 2, legendary 5) and the friend sees each catch ("Ana caught a Trout +1"). A draw plays another round.
- "Journey Together": when your friend leaves, a card sums up the session (time together, enemies each of you defeated, bosses, revives, challenges won, quest steps finished together, distance travelled side by side).
- Fixed: your friend's marker was missing from the minimap and the map while they were near (within 400 m); it showed only when they were far away.

First two-player playtest fixes (2026-11-05):
- A new journey together: the first time two friends play with co-op saves on, both begin a new game at the start of the story in a co-op save of its own ("A New Journey with <friend>"); each accepts the agreement and picks a difficulty. Co-op saves made by earlier versions (copies of your own progress): the host is asked once whether to begin anew (the old one is kept as a backup) or continue. The friend's character no longer crashes a brand-new game.
- Switching saves asks first: a card offers "now" (the game goes to the title and loads it, with a notice on screen) or "later" (go to the title yourself; nothing is pressed for you). The game no longer takes over the keyboard and mouse unannounced.
- Dying in co-op no longer leaves you without HUD, pause menu or the "Ride" prompt (every revive did); the death screen looks like the game's own. It also appears when you die after your friend has left.
- Duels: hits land again (a player who had not been hurt yet could not be hit at all), judged against where your friend really is.
- The other player is drawn with less delay (playout 300 to 150 ms).
- Challenges are one match each (duel, race, hunt: first to two rounds; rock-paper-scissors and arm wrestling: one match) and end with a Victory or Defeat card.

Game 1.0.0.3576 (2026-11-06): Steam updated the game again, and this time its code moved. Every address the mod uses was mapped again from both versions of the game (docs/research/port-3576.md); offline checks pass, and co-op, live quests, the Focus block, far-apart partners, the opt-in card, duels and death with revive were verified live on one PC. Fixed on the way: the hook that lets enemies take turns attacking your friend sat on the wrong game function since 1.0.0.3527 (enemies could lose interest in your friend early); it is on the right one now.

Game 1.0.0.3549 (2026-11-04): Steam updated the game again. No game address moved (docs/research/port-3549.md); offline checks pass and co-op, live quests, the Focus block, far-apart partners and the opt-in card were verified live on one PC. Fixed on the way: loading screens took about 50 s longer since the slow-motion block (each received message was checked with a slow memory query), and a hook that needed a second try could leave your friend's horse switched off for the session.

No slow motion in co-op (2026-11-04, playtester poll): while a co-op session is up, Focus no longer slows the world. Focus is a "special mode" (101) the game's in-process server switches on; the mod does not pass that switch to the client, so the ability still triggers but time keeps its normal speed and both worlds stay in step. Alone, nothing changes. F8 > Settings "Slow motion (Focus) in co-op" ([coop] block_slowmo) turns it back on.

Fix (2026-11-04, from a Reddit question): when your friend is far away (another region), their character is no longer left frozen next to you and reloaded every 15-20 s. It is hidden beyond 600 m (the partner frame shows the real distance) and comes back once they are within 400 m. Your own character is never affected; there is no distance limit.

Co-op saves + live quests, EXPERIMENTAL and opt-in (2026-11-04):
- A card on the first launch asks whether to turn them on (Shift+F5 on, Shift+F6 not now; pad View + D-pad Up / Down); F8 > Settings switches them too ([coop] coop_worlds). Off = the mod is as before (none of their hooks installed).
- Co-op saves: each friend gets a separate save, for the host and the guest; the game goes to the title screen and loads it when a session starts, and back to your own saves when it ends. Your own saves are never changed. Saves live in %LOCALAPPDATA%\CrimsonDesert-CoopPlus\Worlds (outside Steam Cloud).
- Live quests: when your friend finishes a quest step you also have under way, your game finishes it too, through the game's own quest routine (rewards, cleared camps and the next step included). Proven on one PC for go-to, defeat and talk steps; steps you have not started are left alone.

Hotfix (2026-11-04): story sync switched off. It rewrote the quest progress inside the guest's own save (after a backup); a player's own save must never be changed. A story the previous build was still holding is discarded, not copied. It returns with separate co-op saves (one per friend, host and guest) and live quest progress.

Game 1.0.0.3527 (2026-11-04): Steam updated the game during this release. Every address re-found in the new exe (docs/research/port-3527.md): offline checks all pass, co-op and story sync verified live on one PC.

Story sync (2026-11-04, asked for by a playtester; D47):
- The guest can copy the host's story (quests, missions, main story) into their own save: F8 settings, "Copy my friend's story into my save"; the host's mod sends it, and it goes in at the guest's next game start after a backup (Documents/CrimsonDesert-CoopPlus-SaveBackups/before-story-sync-<date>). Everything else stays the guest's. Hosts can say no ([coop] share_story=0). Same game version required; the guest is warned if the host is behind.
- Save tools: found why changed saves crashed the game at load (pointers in the faction spawn data); cdmp_savetool for support.

Roadmap work (2026-11-03, one line per task; details in PROGRESS.md):
- T01: two-player playtest checklist rewritten as numbered steps with expected results and pass/fail boxes.
- T02: anti-cheat guard: if an anti-cheat is ever found at start, Co-op+ installs nothing and says so; findings in docs/research/online-integrity.md.
- T03: version handshake: players with different Co-op+ builds or game versions are refused with a notice naming both (protocol 11).
- T04: reconnect / rejoin verified: 5 partner crashes in a row each clean (partner and enemy copies removed, sent again once); a long alt-tab keeps the session.
- T05: joining while a menu is open (or any rejoin after 8 s) no longer loads the friend's character twice; drop-in during menus, loading and a fight verified.
- T06: desync detection: enemy-copy digests every 10 s (3 off in a row = automatic resync), the friend's character reloaded if drawn 40+ m from where they are, and Shift+F8 (pad View + RB) resyncs by hand; a rare launch where the crash guards did not install (shared enemies off all session) now retries.
- T07: leaving (Ctrl+F2) now removes the copies of your friend's enemies, their waypoint pin and their name tag (they stayed before); Ctrl+F2 also works for room-code guests.
- T08: a state audit checks 30 s after every session that nothing of it is left in your world (log line); fixed what it found: enemy copies sent after a lost link, the host's weather kept after leaving, and co-op switching itself off for the rest of the game when the network could not restart.
- T09: menus: while you are in the map, inventory or pause menu your friend's enemies leave your character alone, no blow reaches you, and their name tag for you says "in a menu".
- T10: loot rules for kills you share: [coop] loot_mode = per_player (each their own drop, default), host_only, or first_come (the first to loot takes it, the other's body is emptied). Experience for the guest from those kills is not possible yet (BLOCKED.md).
- T11: network stress tested (up to 300 ms + 15 % loss and 30 % loss, 13 enemy copies): no crash or drop, no FPS cost, 5-10 kB/s; a poor link now halves the update rate first; bandwidth shown in the log (docs/tests/stress-report.md).
- T12: wildlife and townsfolk are per player (each sees their own world's; enemies are the shared ones), written in the README; hunting contest kill counting verified.
- T13: boss mechanics audit for two players (docs/research/boss-audit.md): killing blow, finishers, staggers, phases, cinematics, barrier, targeting, grabs, deaths, scaling, loot and quests, each with its handling or fallback; first five bosses on the playtest list.
- T14: partner frame on the left edge in the game's style: name, health, stamina, distance or "in a menu", and a red "Badly hurt" alert under 25 % (protocol 12 carries stamina).
- T15: teammate revive: for 30 s after you go down your friend can get you up where you fell by holding Shift+F7 (pad View + LB) beside you; progress and credit on both screens; Q / U stay as before.
- T16: crimes and bounties stay with the player who committed them (the host is never punished for the guest); the other player gets a notice when one becomes wanted. A shared bounty is not possible yet (BLOCKED.md).
- T17: anti-grief: the guest never reaches the host's townsfolk, items or story objects, and its finishing blows count as the host's kills; new strict mode [coop] guest_finishes=0 (the guest's blows stop at 1 HP); a blow that must not kill now really stops at 1 HP (it could reach 0 before).
- T18: trading and gifting is blocked: no request was found that the receiver's game accepts for adding a chosen item (BLOCKED.md).
- T19: summon (Shift+F4 / pad View + right stick): your friend is asked to come and accepts with their go-to; after a fast travel the other is offered to follow; 30 s cooldown.
- T20: what a session leaves in each world is written down (README "What stays after a session", docs/world-state-rules.md) and backed by the state audit and loot tests.
- T21: the empty arm wrestling table is not fixed (BLOCKED.md): found that it happens when your friend's character arrives at the pub together with you; your server is no longer told about the copy's triggers, regions and gimmick bumps (cleaner either way).
- T22: world events (reinforcements, bounty hunters, camp and outpost activity; 519 named from the game data) are announced to the other player; their fighters are already shared as enemies.
- T24: text chat (Shift+F2) and quick messages with ping types (Ctrl+F3 / pad View + D-pad Left: Go here, Danger here!, Loot here, Follow me, Wait, Help!, Thanks!, Ready?), shown as notices and a chat log.
- T25: mounts: after your friend dismounts, their horse is shown where it really is (it came back where they had first called it).
- T26: respawn option [coop] respawn_at=partner (after a checkpoint revive you are taken back to your friend); death penalties stay the game's own (partial / full loss options blocked: BLOCKED.md).
- T28: launch-time note naming other ASI loaders, .asi mods and ReShade next to the game (log + notice, never blocking); a second copy of Co-op+ stays off; docs/compatibility.md; fixed a crash when a crash-guard hook needed a second try.
- T27: release package - README install / manual / Vortex / other mods / known issues, BUG-REPORT.txt, OTHER-MODS.md, a mod-manager zip (bin64 layout), Nexus page and shot list in docs/release, fake-folder install test (tools/test-install-fake.ps1).
- T29: failure states listed with their messages (docs/error-messages.md); new plain-language lines with the next step for join not reaching the host, Steam not ready, bad room code, network not starting, save backup failed, safety hooks off (panel + top strip); clearer version and connection-lost messages.
- T30: co-op settings in the F8 panel (arrows / D-pad pick and change, saved to cdmp.ini, live where possible); co-op hotkeys rebindable under [keys] with warnings for clashes and plain game keys.
- T40: enemy and boss health with a guest (x1.0-x3.0) changeable in the F8 settings, live, saved in cdmp.ini (wins over scaling.toml's 2-player values).
- T31: first-run controls card (game style, key/pad glyphs), shown once the first time in the world; Help (Shift+F1, pad View + L3) opens and closes it any time.
- T32: connection quality - packet loss counted from the other side's packet numbers; ping and loss (coloured) on the F8 panel, an optional small readout top right (F8 settings), "lost" in the log.
- T33: partner audio checked as far as one PC allows (the copy runs on the game's own animation actions, so the game plays its footsteps and swings with its own falloff); a loopback measuring tool (tools/audio); two-PC listening step in PLAYTEST 2.7c.
- T34: player colours - host gold with a diamond mark, guest blue with a round mark, on the name tag, partner frame and chat; a colour-blind-safe pair and the friend's map pin colour in the F8 settings.
- T35: emotes and resting - the game has no emote system to sync (none found); sitting and campfire rest are the game's own actions, which the copy already replays, and the host's Wait moves both clocks (time sync); two-PC step PLAYTEST 2.7e.
- T36: kill feed on the right of the screen for both players' kills (animal names from the hunting table, "an enemy" otherwise); stagger and knockback come from real hits already; lock-on and status effects left to the two-PC test.
- T37: Diagnose makes a one-click bundle (report + whole log, zipped, on the clipboard) with Steam names, SteamIDs, lobby ids, room codes, Windows user / PC name, IPs and e-mails taken out; tools/test-diagnose-redact.ps1 checks it.
- T38: post-patch playbook (docs/post-patch-playbook.md) and tools/port/post_patch_check.ps1 (offline verifier + the hook problems in the last log), checked on the current exe and on a copy with two hook sites changed.
- T39: spoiler guard (F8 settings, [coop] spoiler_guard): the friend's quest steps you have not reached are not named, their story scenes are not started in your game. Also fixed: cutscene, duel and quest events ran the menu code since T09 (the partner could read as "in a menu").
- T41: new challenge "Race to the waypoint" (the host's map waypoint is the finish, 3-2-1, first within 12 m, times on both screens, a new waypoint = next round).
- T42: duel extras - the duel ground size (12 / 20 / 35 m) in the F8 settings (the one who places it decides, both see it), and quick rematch (the picker opens on the last game).
- T43: lasting scoreboard per friend (cdmp_scores.ini) shown on the F8 panel, and titles from wins that the friend sees after your name on their partner frame.
- T44: group photos use the game's own photo mode (the friend is a real character in the scene); quick message "Hold still: photo!"; the partner frame hides while the F8 panel is open.
- T45: shared stash blocked (moving an item into the other save needs the add-item request T18 is missing; BLOCKED.md). T46: baby wyvern stays blocked (pet only in this game version).
Built on cdmp (ByJunter) as ported to game build 1.0.0.3446 by nouzert1 (cd-coop-3446, commit b7ae133), ported by
Co-op+ to game 1.0.0.3460 and then 1.0.0.3479.

Game 1.0.0.3479 (game version 2.04.05; Co-op+ port, tested in the game on one PC with the echo peer):
- Every address re-found for 3479 (a small patch: code moved by up to 0x70 bytes, data by ~0xFB0, message numbers
  unchanged). The other player's character loads, is drawn and stands next to you again.
- Cutscenes: "<name> is watching a cutscene" when your friend is in one within 100 m. A shared cutscene skips only
  when everyone in it pressed Esc (user rule). Boss safety now uses the game's own "cutscene playing" flag (its old
  cinematic check never fired on 3479).
- Boss and story cutscenes of the host are started in the guest's game as well (the stage the cutscene belongs to is
  copied into the guest's client and ended with the host's cutscene; [coop] stage_cinema). Needs the two-PC test.
- Weather follows the host: the game picks random weather stages per load; the guest now runs the host's instead of
  its own ([coop] weather_sync). Time of day follows the host by default ([coop] time_sync=3; it never worked on
  3479 before: wrong address).
- Looks like the game: notices, banners and prompts in the game's serif type and dialog style; the death screen
  redrawn as the game's own; your friend's name tag and the boss / opponent health bar redrawn the game's way;
  the co-op panel is hidden until F8.
- Controller support for every co-op action (hold View / the touchpad button + a button); prompts show keys, Xbox
  buttons or PlayStation symbols, whichever you used last.
- Duels: challenge a friend to a 1v1 fight (Shift+F5 -> Duel). A gold ring marks the duel ground; 3, 2, 1, Fight!;
  your friend's health at the top; hits make the other player flinch and lose health on both screens; nobody dies
  (3% = you yield); 10 s outside the ring loses the round; healed after each round; series score.
- Challenge picker: Duel, Rock-paper-scissors, Hunting contest, and the wrestling table you stand at.
- Rock-paper-scissors: each player's hand appears over their character at the reveal.
- Hunting contest: 3 minutes, the most game brought down wins (big game scores more).
- Party journal: when your friend completes a quest step or challenge you get the game-style notice with its name,
  marked if you are on the same quest.
- Challenge your friend (Shift+F5): to arm / hand wrestling at a table (both play a match at the same table in their
  own worlds and share one strength gauge: the faster masher wins on both screens), or anywhere to a rock-paper-
  scissors duel (U / F / E, both hands shown at once, first to 2). Shift+F5 accepts an invite, Shift+F6 declines;
  a series score is kept until either player ends the challenge. Versus play happens only inside a challenge.
- Minigames: "<name> is playing a minigame" / "<name> finished arm wrestling" (each player plays in their own world).
- Flying mounts: a summoned dragon is sent to your friend like a horse (seen flying on one PC with a test save).
- Long trips with Ctrl+F4 / checkpoint revive: hops also try higher, shorter and sideways (past cliff walls).
- Riding together: your friend's horse appears in your world and their character rides it beside you
  (on by default).
- Shared waypoints and pings: your map waypoint shows on your friend's map; Ctrl+F3 pings your position with a
  message; Ctrl+F4 takes you to your friend.
- Party scaling is on by default: with a guest in, the host's enemies are tougher (x1.5 health, bosses x1.7,
  more for 3-4 players in scaling.toml).
- Boss fights (user request): if you die during one you wait until your partner beats the boss (then you get up
  where you fell, full health) or is down too (then only "Revive at Checkpoint" is offered). The guest's boss
  health bar can now show (the boss's health was never passed to it). [coop] boss_death_wait=0 turns the wait off.
- Deaths work while connected: once the other player's character had been in your world, the game never showed its
  "Death has found you" screen again and you stayed dead. Co-op+ now shows the same choice itself (Q Instant
  Revive, U Revive at Checkpoint) and sends the game's own revive requests. The checkpoint revive gives full
  health and moves you to the checkpoint in short hops (no loading screen). The other player's character
  stays in your world meanwhile.
- The server messages relayed onto the other player's character (health, buffs, death, revive, wetness...) were
  still matched by their 1.0.0.3424 numbers, so none of them were passed on (and a few unrelated messages were).
  Renumbered, plus one more stale number in the "lethal blow" path (it sent a guild request instead).
- Gear changes are shared live: take off or put on a piece of gear and your friend sees it on your character
  (tested both ways). The old way of catching them did not exist in these builds; the mod now reads your
  character's equipped slots itself.
- Fixed a crash at startup with NVIDIA DLSS Frame Generation (about one launch in five): the overlay no longer
  creates a temporary swap chain to find the game's draw function.
- The name tag sits a little lower, just above the other player's head.
- The name tag is no longer drawn on top of the map, inventory, journal or pause menu.
- Enemies are recognised again: since 1.0.0.3460 every enemy was rejected (a field the mod reads moved by 4
  bytes), so the shared-enemy feature could never start. Bandits now pass the check on both sides.
- Shared enemies work on 1.0.0.3479: the guest sees the host's enemies (its own there are hidden) and both can
  fight them. The crash of a copy right after loading is fixed; re-tested with 33 enemy copies at once.
- Arm / hand wrestling: during a challenge your friend's character sits in the opponent's chair (the game's NPC
  opponent is hidden for the match).
- The game's clock could never be read (an address check compared the wrong numbers); the mod now falls back to
  estimating it, as designed, instead of waiting forever.

Game 1.0.0.3460 (Co-op+ port, tested in the game on one PC with the echo peer):
- Every address re-found for 3460; the movement messages' new layout; all message numbers from the live game.
- The other player's character now finishes loading, is drawn, and follows (it stayed half-loaded and invisible).
- The other player is loaded only once our own world has finished loading (it could take our control away).
- Camera fix for the name plates on 3460.

Added by Co-op+:
- Steam "Join Game" and overlay invites: everyone hosts their own world (`[net] role=auto`); Ctrl+F1 opens the Steam
  invite dialog; accepting an invite or using "Join Game" switches you into your friend's world, even if your game
  was closed (Steam starts it); Ctrl+F2 leaves. Rich presence shows "hosting (1/2)" or "in <friend>'s world".
- Who can join: `[net] join=friends` (default) or `join=invite` (only friends you invite).
- Your Steam name is your player name; it shows over your head on your friend's screen, fading with distance
  (Ctrl+F8 toggles it; F8 toggles the panel).
- A short message when your friend joins or leaves.
- Save guard: your saves are copied to Documents\CrimsonDesert-CoopPlus-SaveBackups each time a co-op session starts
  (newest 10 kept), and the panel warns if the game saves during co-op.
- Party scaling (experimental, off by default): with a guest in the host's world, enemies take less damage
  (health x1.5, bosses x1.7 from cdcoop\config\scaling.toml).
- After a game update the mod stays off and says so on the panel, instead of looking broken.
- One-click installer (finds the game through Steam, backs up saves), uninstaller, Vanilla mode on-off, Diagnose.

Fixed in the upstream code:
- The wall-climb and mount fixes for the other player's character pointed 0x10 bytes into their game functions on
  1.0.0.3446 and never installed (other player froze on walls); now correct and checked offline.
- The UTF-16 settings profiles were garbled on Windows checkouts (git line-ending conversion).
- `opcodes_resolved()` could not be linked (defined in an anonymous namespace).
