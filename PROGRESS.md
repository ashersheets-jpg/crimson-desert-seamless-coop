# PROGRESS (ROADMAP.md work log)

Started 2026-11-03 (user handed over ROADMAP.md). Older history: NOTES/LOG.md, NOTES/DECISIONS.md.
One-PC testing uses the echo peer ("stand-in"): build/port3479/*.sh; after every test session savecheck.sh --fix,
test save Out, shipped cdmp.ini restored.

## T01 -- Two-PC playtest pack
Plan: list every 2-PC item (joining incl. room code and invite-only, guest loot, shared enemy set, boss cutscene,
final blow, boss death wait, challenges, ending); rewrite docs/tests/PLAYTEST-1.md as numbered tables with
expected result, Pass / Fail boxes and Notes, plus a session record header.
Done: PLAYTEST-1.md rewritten (blocks 0-7, 60 numbered steps). Later tasks append their 2-PC steps there.

## T02 -- Anti-cheat and terms check
Plan: research the game's DRM / anti-cheat / online services; list the in-process vs online pieces; add a start-up
guard (anti-cheat modules in the process, anti-cheat files in the game folder) that keeps every part of Co-op+ off;
dev switch to simulate a hit; README + findings note.
Done: no anti-cheat (Denuvo Anti-Tamper = DRM); server is in-process; online pieces = Steam, Denuvo licence,
cross-save (PA account), sentry/kafka. src/mod/online_guard.* ; guard tripped (dev online_guard_test=winmm.dll):
log 8 lines, nothing installed, message box shown; normal start: "co-op allowed", all hooks as before.
docs/research/online-integrity.md, README "IS IT SAFE?". Note: when tripped the overlay is not installed either, so
the message is a plain Windows box (an anti-cheat could flag the DXGI hook).

## T03 -- Version handshake
Plan: Hello / Welcome carry the game's exe version (protocol 10 -> 11; the reader rejects trailing bytes, so a bump);
ClientSession refuses another mod build or game version on both sides (no Connected, so no spawn / events), with a
message naming both versions and what to update; shown as a game-style notice; [dev] fake_peer_build to test it.
Done: unit tests (Welcome with another build / game refused, host refuses a Hello and still answers with its own
versions, matching Hello connects). In game: ver1/ver2 (fake build 12345678): "Can't play together: ... has Co-op+
build 12345678 on game ?, you have build 2526DEB6 on game 1.0.0.3479. Both install the same Co-op+ zip." as a notice,
no puppet; ver3 (control): connects, puppet spawns. Older (protocol 10) builds are refused by the old protocol check.
Note: the relay (cdmp_peer) announces build 0 / no game version: not compared (dev tool only).

## T04 -- Reconnect and rejoin
Plan: one-PC kill-and-rejoin loop (echo peer killed = the partner crashes; started again = rejoin), with and without
enemy copies; long alt-tab; check one spawn per connection, removal on each loss, no duplicate copies, no stuck UI.
Findings (build/port3479/rejoin_test.sh, alttab_test.sh):
- rejoin1: 5 cycles, game alive, 6 connections = 6 spawns, 6 "Puppet: remove queued" on the 5 s timeouts.
- rejoin2 ([dev] zone_copy_test=1): each loss "13 copies of theirs removed", each reconnect sends 13 new ones (new
  keys), never more: no duplicate enemies.
- alttab1: 40 s in the background (Notepad focused): no timeout, session kept (the game keeps running unfocused).
- The partner's own crash/restart is a new process: clean by construction; Steam / room-code rejoin: PLAYTEST 1.6b-d.
No code change was needed. Notes: the peer in the earlier late1 run timed out the client while the game was paused in
a minigame without focus; normal play does not pause.
- Pushed T01-T04 (ab89c6b..16f0872) to phase-a/local-pc-setup.
- T05 note: at the title screen (save not loaded) a connecting partner spawns nothing and leaves nothing behind (dropin1, 3 connects).
- T05 finding (dropin2): every (re)connect logs spawn -> "nothing from the other player for 8 s; removing their
  character" -> "the other player is back; reloading" -> spawn again, within 2 ms: a silence timer left over from the
  previous session. With a menu open the game holds injected packets, so both spawns land together and the game
  refuses the second ("Actor 생성에 실패했습니다" = actor creation failed); the copy is then missing until the
  "gone from our world; loading it again" check respawns it ~10 s later (self-healing, but a needless double spawn).
- T05 fix: CoopLink::on_reliable (Spawn) now sets last_remote_ms_ (net/coop.cpp). dropin3: map / inventory / pause /
  combat drop-ins = 4 connections, 4 spawns, 3 removals (the 3 leaves), no "actor creation failed"; after each menu
  closes one partner copy stands beside Kliff (screens dropin3-*). In the combat case Kliff was killed by the bandits
  before the peer joined: it joined during the death screen, spawned once, no crash.
- T05 status [~]: menus, title screen (dropin1), save loading (every PEER=1 run), cutscene (late2: peer joined during
  the arm wrestling intro), fight/death verified on one PC; a boss fight needs a boss -> PLAYTEST 1.6e.
- Pushed T05 (7e4c13f).
- T06 FINDING (desync1, 09:59): "Enemy relay: wanted but not started (its crash guards did not install)" -- on some launches shared enemies silently never start (the zone request is answered 'we do NOT relay'). dropin3 (09:48) started fine. Investigating.
- T06 root cause so far: desync1's late init had 4 hook failures at once (layer crash guard, HorseDiag "pending
  actions hook failed" = the head bytes matched but safetyhook could not create the hook, minimap icon hook,
  CheckAIBattleType). Same exe as the good runs; 1 of 80 launches in build/port3479 logs. Fix: the containment +
  layer guards are tried up to 5 times, 2 s apart (mod.cpp late init), and every failure now logs the safetyhook
  error type / which head did not match, so a recurrence says why. (Not reproducible on demand: verify in logs.)
- T06 done. coop_sync.* (Activity 15 digests: u16 sent | u16 present; 3 off in a row -> resync = session drop +
  rejoin, at most one automatic per 2 min), coop.cpp drift check (copy drawn > 40 m from the reported position for
  6 s, not while seated -> reloaded there), Resync action Shift+F8 / View + RB. Tests: desync2 ([dev] desync_test=1):
  one copy removed behind the relay -> "13 sent, 12 here" x3 -> resync -> copies sent again; second induced desync
  held by the 2-minute limit; Shift+F8 worked. desync3 (desync_test=2, frozen 60 s, Kliff walked 94 m): "drawn 94 m
  from where they are; loading it again there" -> copy beside Kliff (screen desync3-after-auto).
- Pushed T06 (4ca9e45).
- T07 findings: (1) on Leave (Ctrl+F2) / Join the CoopLink was destroyed before the session reported a lost link, so forget_partner never ran: the copies of the other player's enemies and their waypoint pin stayed (fixed in ~CoopLink); (2) Leave was only read on the Steam transport (moved out of the steam block); (3) the partner's name tag stayed on screen after leaving (HUD not cleared when the network stops: fixed).
- T07 done. leave3: Ctrl+F2 with 13 zone copies -> "13 copies of theirs removed", partner removed, "Network stopped
  (left the other player's world)", "now hosting our own world", screen clean (no name tag). Host-quit path = link
  lost (rejoin2-d1): "Lost the connection to ...'s world", no name tag, copies removed. Hidden natives: both paths
  call suppress_stop(restore=true) (restore verified in the 11-01 suppress lab); weather returns to the guest's own
  20 s after the host's reports stop (D37). Two-PC check: PLAYTEST 7.1 / 7.2.
- Pushed T07 (59e1c6e).
- T08 findings (audit1, first run of the state audit): (1) after a Leave on the one-PC (UDP) transport re-hosting
  could not bind the port the echo relay holds; run_network returned "stop" and the WORKER EXITED, i.e. co-op off for
  the rest of the game (any failed network start did this) -> run_coop now retries every 10 s; (2) audit "host
  weather stages 1" and "duel 1" 30 s after a lost link: both from the one-PC test switches (weather_test followed the
  host without a link; challenge_test kept the challenge) -- the real paths end them; the test paths now end with
  the link too so the audit measures the real behaviour.
- T08 audit2: B clean. A "enemy copies sent 6": g_partner_epoch started at 0 and send_enemy reads 0 as "current
  partner", so a zone tick decided before the first reset still sent after it -> epoch starts at 1. C "host weather
  stages 1" after Leave: weather was only restored by the next session's loop (which on the one-PC transport did not
  start) -> run_network restores our own weather when it stops.
- T08 done. net/state_audit.* (partner, seat, hidden NPC, enemy copies here / sent, our enemies hidden, scaling,
  host weather stages, cutscene copy, duel, hunt) logged 30 s after every session end ("State audit 30 s after the
  session: clean (...)" or "LEFT BEHIND: ..."). audit3: A (zone copies + host rain, partner crashes), B (duel running,
  partner crashes), C (plain session, Ctrl+F2): all three clean. The save is never written by Co-op+; savecheck after
  every session confirms the user's saves equal the 2026-11-01 backup.
- Pushed T08 (a16ff4a).

## T09 -- Menus and pause
Plan: measure (menus while connected: drops? bursts? stuck?); define: a menu pauses only your own world; the other
world goes on with your character in it -> tell the partner (Activity 16, resend 3 s), their enemies do not target
you (IsTargetable hook), no relayed blow is applied to you while in a menu, name tag "in a menu"; test all menus.
Findings: menu1 (map / inventory / pause 20 s each): no silence removal, no link loss, one spawn, copy fine after each.
Done: menu2 (map open inside the camp): "the other player is in a menu" .. "left a menu", aggro "copy 0 attacks"
while in it, first hit 1 s after closing; menu3: all three menus detected (enter + leave). [~]: the partner's view
needs two PCs (PLAYTEST 3.5b). Note: in menu2 the bandits killed Kliff after the map, so inventory / pause there
landed on the death screen (re-run out of combat).
- Pushed T09 (49f2ce9).
- T10/T12 note: in the 11-01 bandit captures NotifyHandleDeadAck = victim | KILLER (0xB01001C9 killed by 0xA0100001); a kill is followed by AddSubLevelExperienceAck 2995 (01 00 10 A0 | i64 max | 00 | ...). XP exists per kill.
- T10 finding (killopt1): KillCheatReq option 0 = death at once (NotifyHandleDeadAck type byte 0x10); options 1/2/3 = death ~5.3 s later (type 0x0F); killer = our player in all four; NO AddExperienceAck / AddSubLevelExperienceAck for any option. A natural kill gives AddSubLevelExperienceAck (11-01 capture). => the guest's XP cannot come from KillCheatReq.

## T10 -- Shared-kill loot and XP for the guest
Plan: XP first (does a kill give XP? can the guest's twin kill give it?), then loot rules: [coop] loot_mode
per_player / host_only / first_come (first looter takes it; the other side's body emptied via RemoveLootableAck).
Done: loot_mode (config, loot_twin, enemy_relay, Activity 17 kLooted both ways). loot1 (echo, first_come): bandit
0x72 killed (KillCheatReq) and looted (LootingDropFromDeadBodyReq 2491 -> RemoveLootableAck) on the host side ->
0.1 s later the guest side emptied copy 0xB01F0017 ("the host looted the original ... first"). Guest-first and the
real guest need two PCs (PLAYTEST 3.8b). XP: 3 approaches failed -> BLOCKED.md. [~].
- Pushed T10 (ae487ea).
- T11 FINDING (stress1): FPS 70 in every phase (no cost), out 4.5-8.6 kB/s, no crash / silence; but enemy copies received in 90 s: good 12, bad (150 ms, 50 jitter, 5% loss) 8, awful (300/100/15%) 0, lossy (100/20/30%) 0. Investigating.
- T11 correction: the copy count in stress1 was from a rate-limited log line; counted by "copy ... exists" every
  phase loaded all 12-13 zone copies, 30 % loss included. Not a problem.
- T11 done: session byte counters + "out/in kB/s" in the Net log line; poor-link half rate (rtt > 250 ms or > 32
  reliable pending; held 5 s; back under 150 ms / < 8). Report: docs/tests/stress-report.md. Note: the relay answers
  pings itself, so rtt does not include its simulated delay (the backlog trigger is the one seen on one PC).
- Pushed T11 (55f5e00).
- Installed package 55f5e003 (P0 complete): Defender clean, cdmp.asi hash matches, shipped ini, test save out, SAVES OK.

## T12 -- NPC and wildlife consistency
Plan: decide the rule (copying animals / townsfolk too would double the relay's load and their AI is not worth
sharing; each world keeps its own, enemies stay shared), write it in the README, verify the contest's counting.
Findings: animals near the bandits save start (npcs.py): geese 15 m (0xB010007F/80), partridges, herons, Stefero;
all in animal_names.inc (hex keys). huntkill2: walking up and swinging hit nothing (birds fly off; a sword cannot
reach them). huntkill3 (no hunt_test bypass): picker -> hunting contest, KillCheatReq on 6 animals -> the death
notices name our player as killer -> "Hunt: we brought down Goose (+1), score 1" and "... score 2". The real-blow
attribution path (guest_hits_we_hit) is the PLAYTEST 6.6 step.
- Pushed T12 (736daaf).

## T13 -- Boss mechanics audit
Plan: table of every one-player assumption (from the mod's boss research and the guides), its risk with two, the
handling or fallback; first five bosses into the playtest. [needs-2PC].
Done: docs/research/boss-audit.md (12 mechanics); PLAYTEST 5.6b per boss. Only open risk: grabs / paired attacks on
the guest's copy (fallback enemies_target_copy=0). Kearush's fog is host-only (guest unaffected; accepted). [~]
- Pushed T13 (0ff8fad).

## T14 -- Partner party frame
Plan: stamina into PlayerState (protocol 12), ui::party_frame (gold diamond + serif name, red health bar, thin pale
stamina bar, status line), drawn whenever connected except over full-screen menus, cutscenes and our death screen;
under 25 % health the bar edge pulses red and the line reads "Badly hurt".
Done: frame3 (echo): normal (health ~60 %, stamina full, "2 m") and after our HP -140 via the dev script ("Badly
hurt - 2 m", red edge) -- crops in build/port3460/shots/frame3-crops.png. frame1: -260 killed Kliff (the frame stays
hidden under a death screen, as designed); frame2: the game did not start (Steam), retried.
- Pushed T14 (9204c57).

## T15 -- Teammate revive
Plan: net/coop_revive.* (bleed-out 30 s, reach 3 m, hold 3 s, Activity 18), revive_in_place (the boss-win path:
the server's checkpoint revive without the move = up where we fell, full health), ReviveFriend action (Shift+F7 /
View + LB), hint strip, credit notices, [coop] teammate_revive, [dev] revive_test for the one-PC loop.
Done: revive1 (one PC): Ctrl+Shift+F9 kill -> game's own death screen + "Hold to revive" -> Shift+F7 held 3.6 s ->
"we revived" -> echoed -> "revived us: up where we fell" -> checkpoint revive steps 1-3 (no move) -> "alive again",
hp 300/300 at the death spot; notice "You revived ...". Boss fights keep boss_death_wait. [~] (PLAYTEST 4.4b).
Note: on this run the game's OWN death screen appeared (not Co-op+'s); fine either way.
- Pushed T15 (80e2881).

## T16 -- Crime, guards and bounty
Plan: confirm the guest cannot commit crimes in the host's world (only enemy copies take the guest's blows), see
what a crime sends, notify the partner, and a shared option if the crime can be put into the other server.
Findings: crime1 (pub, no weapon drawn in town: no crime); the test save already has a wanted record at login
(WantedRegionDataListAck: player | region 0x000F42C3 | ... | price). Crime opcodes: WantedAddCrimeRecordReq 2836 /
Ack 2569, ChangeWantedStateAck 2324, IncreaseWantedPriceAck 2249, SetWantedForDevReq 2212.
Done: activity_observe_s2c -> Activity 19 kWanted (once a minute) -> notice; wanted1: an injected
ChangeWantedStateAck -> "cdmp_peer (relay) is now wanted by the guards (in their own world)", game fine. Shared
bounty -> BLOCKED.md. [~] (PLAYTEST 3.8c with a real crime).
- Pushed T16 (ec5be27).
- T17 finding: guest lethal blows already become the HOST's normal kills (VaryStatCheatReq to 1 HP + our collision: quest credit, skill XP, loot stay with the host). Lead for T10 XP noted in BLOCKED.md.

## T17 -- Anti-grief protections
Plan: list what the guest can reach in the host's world (only copies of enemies take its blows; townsfolk, items,
objects are per world), check quest credit of guest kills, add a strict switch.
Findings: guest lethal blows already become the HOST's normal kills (VaryStatCheatReq to 1 HP + host collision):
quest credit stays with the host. Bug fixed: with the lethal clamp on (bosses), the non-lethal branch applied the
full damage and could still take the enemy to 0 -> now capped at 1 HP. Added [coop] guest_finishes (default 1;
0 = never finish any host enemy) + profile lines (also loot_mode, teammate_revive).
grief1: no relayed blows landed (Kliff's swings never connected with a copy on one PC), so the clamp itself is for
the playtest (PLAYTEST 3.5c). [~]
- Pushed T17 (1a0b9b6).

## T18 -- Trading and gifting  [!] blocked
Plan: find the giver's remove and the receiver's add requests; transaction with confirmation and a log.
Findings: reqsizes.py on the item requests (AddMinusItemReq [u32, 8], DiscardItemReq [u16, u16, 8],
DeleteItemCheatReq [u16, u8], RemoveItemToInventoryReq [u8, u16, u8], CreateItemFromTrItemValueCheatReq: a
structure); addminus1: AddMinusItemReq +1 / -1 on item 22007 -> no answer, no inventory change. No way to add a
chosen item on the receiver -> BLOCKED.md (with what would unblock it).
- Pushed T18 notes (9a710ed).

## T19 -- Party travel and summon
Plan: consent-based on top of the existing go-to (Ctrl+F4 hops, beside the other player, not under ground):
Summon action (Shift+F4 / View + R3, 30 s cooldown) -> Activity 20 -> "<name> asks you to come: {GoToFriend} go"
for 20 s; our own long trip (> 250 m in 5 s) -> "travelled far away: follow them". Auto catch-up: not added (the
offers cover it without moving anyone unasked).
Done: summon1 (echo peer offset 60 m): "You asked ... to come to you", request line on screen, cooldown notice,
Ctrl+F4 moved Kliff 60 m to "them" (an artificial spot inside a cliff -> he ended on the cliff face; a real friend
stands on ground). Party-travel offer not exercised on one PC (needs a fast travel). [~] PLAYTEST 2.7b.
- Pushed T19 (7b57325).

## T20 -- World-state persistence rules
Plan: the per-world design decides most of it; write the table (kills, bosses, gathering/chests/doors, quests, time
and weather, crimes, scaling) for host and guest, README section, and point at the tests that show each outcome.
Done: docs/world-state-rules.md + README "WHAT STAYS AFTER A SESSION". Evidence: audit3 (guest's hidden enemies
back, nothing left), loot1 (the guest's stand-in enemy killed for its drop stays dead), leave3, boss audit. [x]
- Pushed T20 (c3e3a74).
- T21 finding: in the empty-table runs (peer from the start) the table mercenary 0xB010035C runs StartAIFunction 0x67ED3753 (and barely moves: 2 actions vs 106); staffed (no peer) it runs 0xC7A77DBA and sits. The AI reacts to the copy. Testing: copy untargetable for everyone.
- T21 ROOT CAUSE: our client tells its own server when the COPY enters triggers: GamePlayTriggerInfoEnterReq 2849 (01 F0 10 A0 | trigger 0x000F4280 | 01) at 126.944 and the table mercenary's AI switched (StartAIFunction 0x67ED3753, no more sitting) at 127.108; also SequencerTriggerVolumeInOutReq 2620 (copy key at +13). Fix: those messages naming a copy never reach our server.
- T21: the scrub fires (vs4) but the table was empty again at 10:27; staffed runs with the peer were at 10:33-10:35 (aweye1, pw4); without the peer staffed at 10:27 (tt1). Actor keys are per load (0xB010035C was a gimmick in vs4), so the AI comparison above is not reliable. New suspect: time sync (only with a partner) re-evaluates NPC schedules. Testing time_sync=0.
- T21: the copy also bumps pub gimmicks on arrival (UpdateGimmickReq 2787 / GimmickExecuteFrameEventReq naming 01 F0 10 A0 as user: 0xB010030C/30D/31A/35B/35C at 127.2 s); seats may count as used. Trying: such gimmick messages get gimmick key 0.
- T21 objective check (table_check.sh: full circle of R presses, usable = InteractionDoStepReq): with the peer from the start 0/3 (tc1-3, after both scrubs), without 1/1 (tcn1, turn 0). Next: is it the copy standing 2 m from us (interaction focus / blocking)? offset 6 m.
- T21: tcnc (peer connected, [dev] no_copy=1): TABLE USABLE at turn 0 -> the copy's presence is the cause (not time / weather / relay).
- T21: tclate3 (copy loaded AFTER arriving at the table): USABLE -> the problem is the copy present while the pub loads around us. Remaining client messages naming a copy (tco6 capture): RegionInOutReq 2539 (copy body 0xB010F001 entering regions 0x1EB29 / 0x127C5 at the pub arrival), CurrentTargetChangeReq (NPCs targeting the copy: kept, aggro needs it). Adding RegionInOutReq to the scrub.
- T21: tcr1 (region scrub too) and tcu1 (copy untargetable) both still unusable. Next: does a resync (copy unloaded and loaded again after arriving) clear it?
- T21 [!]: resync after arrival (tcrs1) does not clear it either. Five approaches in BLOCKED.md. The scrubs stay
  (our server should not hear about copies in its triggers / regions / gimmicks anyway). Real two-PC play may
  differ (the echo copy teleports in with us).
- Pushed T21 (7541ce4).

## T22 -- World event sync
Plan: events = stages our server starts (PrepareSequencerAck); their fighters are enemies = already shared near the
host. Add the "it is happening" signal: tools/gen/event_names_inc.py (519 Reinforcement_ / WantedChaser_ / Node_
stages, readable names) -> host-side PrepareSequencerAck watch -> Activity 21 kEvent -> notice; [dev] event_test.
Findings: no event stage occurred in today's captures (start area, camp, pub), so natural events are for two PCs.
Done: event1 ([dev] event_test=1001215): "World event near cdmp_peer (relay) / Bounty hunters (Tashkalp, level 1)".
[~] PLAYTEST 3.8d.
- Pushed T22 (8486e81).

## T23 -- Object interaction sync  (deferred, revisit if time remains)
Findings: objects are per world; static scene objects share scene ids across worlds (the rope / tree feature
locates trees by scene id, remote_puppet.cpp g_scene_gimmicks). Plan: relay the host's persisted object states
(UpdateFieldGimmickSaveDataKeyAck: door opened, lever pulled, destroyed) by scene id and inject them into the
guest's client (visual only; the guest's server keeps its own); needs a door / lever near a test save to read the
layout. Not started: lower value per hour than T24-T40.
- T24 BUG found in testing: after chatting, Ctrl (for Ctrl+F3) triggered LEAVE (Ctrl+F2): F2 counted as held (half of its press swallowed by the hook). Fixed: the opening Shift+F2 is never swallowed (seen only); typed keys are swallowed down and up together.

## T24 -- Communication
Plan: net/coop_chat.* (Activity 22: u8 kind | text), typed line through the activity keyboard hook (keys kept from
the game while open), quick list on the Ping action (pad usable), notices + chat log (ui::chat_log), "here" quick
messages drop the old map pin.
Bugs found in testing and fixed: (1) the line reopened after sending and (2) Ctrl then triggered LEAVE (Ctrl+F2):
the hook had swallowed half of the F2 press so F2 stayed "held". Now the opening key is only seen, never
swallowed; typed keys are swallowed down and up together; the line opens from the hook's own Shift+F2 event.
Done: chat7: typing line, "cdmp_peer (relay): hello friend" notice + log, quick list (gold choice), "Danger here!"
sent + pin. Pad: same actions (View + D-pad Left opens, D-pad Left/Right choose), not driven by a pad here.
- Pushed T24 (2edf0f4).

## T25 -- Mount edge cases
Plan: echo horse runs (horse_test.sh, AGAIN=1 for a second summon / mount / dismount); flying mount checked earlier
(D43, dragon tests). Death / swapping need two horses / a fight: playtest.
Finding (mount1, mount2): after a dismount the rider reloads "the horse stays", but the horse copy no longer existed,
so a new copy was prepared from the old spawn data -- at the spot where the horse was first called (-10340, -3928)
while the real horse stood at (-10290, -3990). Fix: the sender's horse position is tracked from its Move /
StartAction (horse_event) and a new horse copy is placed there; forgotten when the horse copy is removed. Also the
kept copy is reused whenever it exists (the key check was not the failing part, kept for safety).
mount3: reload placed the horse copy at (-10265.8, -4017.6), real horse at (-10267.4, -4017.4) (+2 m echo offset).
The second summon in mount1 never reached the horse (Kliff stuck between rocks). [~] PLAYTEST 2.3b.
- Pushed T25 (766a5ae).

## T26 -- Death penalty and respawn options
Plan: penalty options need an item / silver removal the server accepts (none found, T18) -> BLOCKED; respawn option:
[coop] respawn_at=checkpoint|partner -- after any revive that put us more than 50 m from where we fell, 3 s after
getting up, the go-to hop to the other player.
Done: respawn1 (walked away, killed, U): checkpoint revive steps 1-3, "at the checkpoint", then "respawn_at=partner:
from the checkpoint to the other player". Profile line, README, PLAYTEST 4.4c. [~]
- Pushed T26 (63d9d7f).

## T28 -- Mod compatibility testing
Plan: no downloads of other authors' mods, so (1) list the kinds of mods in use (nexus survey) and what each means
for co-op, (2) at launch look in bin64 for other ASI loaders (proxy DLL names), other .asi files and ReShade, and say
so in the log and once on screen (never blocking), (3) guard against Co-op+ being loaded twice (two loaders),
(4) test both cases with a harmless ReShade.ini stand-in.
Finding (compat-a, 15:43): the game crashed 2 s after "Crash guards: try 1 of 5 failed". Symbolised (dbghelp,
cdmp.pdb): horse_diag.cpp:377, `g_ret[0] = 0xC3` -- the retry (T06) wrote into the return stub the first try had made
PAGE_EXECUTE_READ. remote_puppet's g_ret_stub had the same pattern. Both now written once, at allocation. The T06
retry had never actually been exercised before (it fires on ~1 launch in 80).
Also: compat_test first passed a /c/... path to powershell -Command (game never started); fixed with a Windows path.
Done: compat-a (ReShade.ini present): "WRN Compatibility: next to the game: ReShade / a dxgi.dll proxy..."; compat-b:
"Compatibility: no other ASI loader, .asi mod or ReShade next to the game". docs/compatibility.md; PLAYTEST 6b. [~]
(real third-party mods are a 2-PC/user test.)
- Pushed T28 (eb53dd5).

## T27 -- Nexus release package
Plan: README sections for manual / Vortex / MO2 installs, other mods, known issues (from BLOCKED.md and the [~]
items); BUG-REPORT.txt and OTHER-MODS.md (docs/compatibility.md) into the zip; a second zip with the bin64 layout for
mod managers; the Nexus page rewritten for 1.0.0.3479 and today's features (docs/release/nexus-page.md, moved from
docs/drafts); a shot list; a clean-machine walkthrough on a fake game folder.
Done: tools/test-install-fake.ps1 (temp "Crimson Desertin64" with a stand-in exe, another mod's winmm.dll and a
stand-in save): zip contents + SHA256, install (files, the other winmm.dll kept, saves backed up, version note),
update keeps an edited cdmp.ini (defaults as .new), vanilla on/off, uninstall leaves bin64 byte-identical to before,
backups left alone, mod-manager zip lands next to the exe -- 13/13 PASS (eb53dd51 test package).
Not done here (needs the user): the actual Nexus upload, screenshots (shot list) -- FLAGS. Vortex itself is not
installed on this PC, so the Vortex lines are generic ("deploy into the game folder"). [~]
- Pushed T27 (403e466).

## T29 -- Error messages
Plan: list every failure state (mod, installer, uninstaller, diagnose) with what the player sees; give the ones that
were log-only a plain message with the next step; one mechanism: render::hud_set_problem(kind, text) -> red lines at
the top of the F8 panel, and a strip at the top of the screen for 15 s whenever a line changes.
Found log-only before: network not starting (T08 retry loop), save backup failure, crash guards off (enemy relay
dead for the session), a bad room code, a guest that never reaches the host (panel said only "looking for the other
player..."), Steam never logging in. Old protocol message was jargon ("uses protocol 11, we use 12").
Done: errors_test (build/port3479): A guest with no host, 60 s after load: panel "Can't reach the host yet. Check: both
games running with a save loaded, the same Co-op+ zip, Steam online. Still stuck: Ctrl+F2, then Join Game again.";
B stand-in host joins: the line is gone; C stand-in drops: notice "Lost the connection to cdmp_peer (relay)'s world.
Reconnecting by itself; [Ctrl][F2] goes back to your own world" (key glyphs drawn). docs/error-messages.md. [x]
- Pushed T29 (ca3eeee).

## T30 -- Settings and hotkeys
Plan: one settings block at the bottom of the F8 panel (settings_menu.cpp): arrows (keyboard hook, swallowed while
the panel is open) / D-pad without chord (masked from the game) pick and change; each change is written with
WritePrivateProfileString (keeps cdmp.ini UTF-16) and applied live through the modules' setters where they exist
(loot mode, guest finishes, revive, respawn, boss wait, weather, scaling); join and nameplates say "(next start)".
Hotkeys: the bindings table is no longer const; [keys] <Action>=Ctrl+F1 / <Action>_pad=Y; warnings for unknown
names, two co-op actions on one key or pad button, and plain keys (no Ctrl/Shift, not an F-key).
Done: settings_test (build/port3479): [keys] Invite=G -> "Keys: Invite is on G with no Ctrl/Shift: the game probably
uses that key too" (log + notice); F8, Down, Right -> "Settings: Loot of shared kills = host only", ini
loot_mode=host_only; Down x7, Right x2 -> enemy health x1.5 -> x1.7, ini panel_health_2p=1.7; panel screenshot shows
the block with the gold selection. Shipped ini restored after. README. [x]
Not done: a full in-game key-capture rebinding UI (keys are rebound in the ini; the panel says where).
- Pushed T30 (522a3db).

## T40 -- In-game difficulty controls (done with T30)
Plan: two settings rows for the 2-player multipliers (regular, boss), live through new party_scaling setters
(scaling_set_enabled, scaling_set_2p), stored as [scaling] panel_health_2p / panel_boss_health_2p, read at start
after scaling.toml so the panel's value wins; the on/off switch too.
Done: settings_test: enemy health x1.5 -> x1.7 live ("Settings:   Enemy health with a guest = x1.7"), saved in the
ini. 3-4 player values stay in scaling.toml (2 players only today). Applied on the host (it is the host's world). [x]
- Pushed T40 (5da9e59).

## T31 -- First-run onboarding
Plan: a new Help action (Shift+F1 / chord + L3; chord + L3 now masked from the game like R3); ui::guide_card (game
dialog style, action tokens drawn as key / pad glyphs); shown once at the first in-world snapshot when cdmp.ini has
no [general] onboarded=1 (then written), toggled by Help, closes itself after 60 s or when a game menu opens.
Done: guide_test run 1 (25 s load wait) toggled the card during the loading screen (fine, but too early for the
first-run check); run 2 (55 s): first shot in the world shows the card ("Co-op+: first run: controls card shown"),
Shift+F1 closes, Shift+F1 reopens; ini onboarded=1 after. Shipped ini restored. README / Nexus controls. [x]
- Pushed T31 (4e3e2bc).

## T32 -- Connection quality indicator
Plan: every datagram carries the sender's packet number; ClientSession counts the gaps (stats.lost, a late one taken
back); mod.cpp turns lost / received over ~3 s into a loss %; panel line "loss x%" coloured; [overlay] net_hud (F8
settings row "Ping and packet loss on screen") draws "Co-op 28 ms 0% loss" top right, green / yellow / red; the Net
log line gets "lost N".
Finding (netq1): with a stand-in that reconnected, the loss stayed 0: the new peer numbers from 1, and my first test
("seq + 1000 < last" = restart) took 1..800 as late packets. Now anything more than 64 back is a new numbering.
Done: netq2: clean (25 ms delay) "Co-op 28 ms 0% loss" green; 10 % relay loss: the log's lost count grows ~8-11 per
5 s (~10 %), panel "loss 6.9%" yellow, readout "33 ms 7% loss" yellow. [x]
- Pushed T32 (816a660).

## T33 -- Partner audio
Plan: the partner copy is a game character driven by the game's own Move / StartAction events, so its animation-
driven sounds (footsteps, swings, horse) should come from the game's audio engine with the game's 3D falloff. Check:
measure what the PC plays (new tools/audio/loopback_rms.exe, WASAPI loopback, high-passed loudness and onsets) while
the copy walks next to a still player (relay with a 6 s delay replays our walk after we stop) vs no copy.
Result (audio_test, build/port3479): A (no copy) median -78.4 / -76.7 / -77.2 dBFS, max -73; B (copy walking 2 m
away) median -74.6 / -76.8, max -68 / -69. Slightly louder with the copy, but the game's whole output reaches the
loopback at about -77 dBFS here (near silent: the game or its device is turned down on this PC), so this does not
prove it. No sound is added by the mod; voice lines of the player character are not expected for the copy.
Status [~]: PLAYTEST 2.7c (headphones, distances 2 / 20 / 60 m). If the copy is silent there, the next step is to
find the anim-notify sound path for network-created actors.
- Pushed T33 (b77a904).

## T34 -- Per-player colours
Plan: one colour and one mark per role, the same on both screens: host gold + diamond, guest blue + round mark (the
mark makes them read apart without colour); colour-blind-safe pair from Okabe-Ito (orange 230,159,0 / sky blue
86,180,233). game_ui::set_partner(colour, round) used by the name tag, the partner frame and the partner's chat lines.
The game's map pin colours are its own palette (15 entries, not documented): a settings row picks the friend's pin
colour (live through puppet_map_pin_style). Pings use the same pin.
Done: colours_test: as guest the host's tag + frame gold; F8 > Player colours > colour-blind safe ("Settings: Player
colours = colour-blind safe") -> orange, live. The host's view (blue, round) and the pin colour need two PCs:
PLAYTEST 2.7d. [x]
- Pushed T34 (d255c2a).

## T35 -- Emotes and resting together
Plan: find the game's emote / sit / rest systems and sync what the copy does not already show.
Finding (web search 2026-11-03, powerpyx / dexerto / games.gg guides): no emote or gesture system is documented for
Crimson Desert; resting is Focus (Ctrl / LB) on a campfire or cooking pot > Wait (skips time, with a cooldown), or a
bed. Sitting on benches / chairs and the rest are interactions that play the player's own actions -- the copy is
driven by every StartAction we send (the same path as climbing, gliding, the arm wrestling seat), so the friend sees
them. Wait changes the time of day: the host's Wait moves the guest's clock through time sync (T20 rules); the guest's
own Wait is overridden by the host's time, which is the defined rule (time follows the host).
Nothing new to build without an emote system. Not tested on one PC (no campfire near the test saves; the echo copy
mirrors our own actions anyway). [~] PLAYTEST 2.7e.
- Pushed T35 (a649bcd).

## T36 -- Combat feedback sync
Plan: what the partner's hits do to enemies is already real in the host's world (guest_hits sends the game's own
DoFrameEvent / damage, with stagger, T17), so stagger, knockback and the enemy's reactions are the game's; the game
shows no damage numbers. Missing: a kill feed. Built: net/kill_feed.cpp: our kill = a death notice naming us as the
killer, or one of our blows on the victim in the last 3 s (the hunting contest's rule); a line "You defeated a Goose"
and activity type 23 with the name, so the friend sees "<name> defeated a Goose" in our colour (right side, 6 s, 4
lines). Names: the hunting table's animals, "an enemy" otherwise (no enemy name table yet).
Done: killfeed_test: KillCheatReq on 6 animals near the start: "Kill feed: we took down 0xB010007F (Goose)" x4,
"(an enemy)", "(Scolt)"; the screen shows the echoed lines "cdmp_peer (relay) defeated a Goose ..." in gold.
Not done: per-player lock-on (the game's lock-on is local; nothing to sync found), status-effect check, enemy names.
[~] PLAYTEST 3b.
- Pushed T36 (9ddd859).
- Pushed T37 (a069441).

## T37 -- Crash and log reporting
Plan: Diagnose writes the report as before plus a .zip (report.txt + the last 50 000 lines of cdmp.log) and puts the
.zip on the clipboard (Set-Clipboard -Path); everything passes a Redact step: Steam names learnt from the log's own
lines ("playing as", "connected to", "while '...' is in the session", player_name=) -> <player1>/<player2>,
SteamID64 -> <steamid>, lobby ids, room codes, C:\Users\<name>, %USERNAME% / %COMPUTERNAME%, IPv4 (not 127.x), e-mails.
-NoRedact keeps everything.
Findings while testing (tools/test-diagnose-redact.ps1, a fake log full of personal data): (1) Python had turned
"\b" into backspace characters in my regexes -> SteamID, room code and IP leaked; rewritten from a file. (2) "Room:
hosting" came out as "Room: <code>" (case-insensitive 7-letter match); codes are upper case, now matched so.
Done: test: 9/9 secrets gone, game version 1.0.0.3479, coordinates and 127.0.0.1 kept; real install: report + zip
written, no Windows user / PC name in them. [x]
(The first T37 commit a069441 missed these notes and the README lines: a Python escape error; added in the next.)
- Pushed T37 (a069441 code, notes in the next commit).

## T38 -- Post-patch playbook
Plan (drafted with T21, commit 7541ce4): docs/post-patch-playbook.md (keep the old exe, what changed, re-find what
moved with the build/port3479 tools, the order to bring features back, the tests) and tools/port/post_patch_check.ps1
(runs upstream's verify-port.ps1 against the installed exe and lists every hook / offset problem in the last cdmp.log).
Done: on the installed 1.0.0.3479: "RESULT: ALL OFFLINE CHECKS PASSED", "no hook or offset problems logged".
A copy of the exe with 4 bytes changed at kClientPostPacket (exe+0x3A5A30) and kActionLayerAdvance (exe+0x214C070):
"FAIL kClientPostPacket ... expected 48 89 5C 24 10 4C 89 4C, found CC CC CC CC ...", "FAIL kActionLayerAdvance",
"RESULT: 2 CHECK(S) FAILED" -- exactly the two, named with the feature each one guards. Copy deleted. [x]
- Pushed T38 (bca36e4).

## T39 -- Story spoiler protection
Plan: a per-player setting (F8 settings row "My friend's story": shown / hidden, [coop] spoiler_guard), live:
(1) party journal: a friend's quest step that is not one of ours (not under way, not done) shows "A quest you have
not reached yet (name hidden: spoiler guard)" -- not even the log names it; (2) stage cinema: the friend's story /
boss scene is not started in our game (their "is watching a cutscene" notice stays). Being pushed ahead cannot
happen: quests are never changed in the other save (D42).
Finding: in coop_activity's event switch, `case kMenu:` (T09) sat right under `case 7/8/9, kDuel, kQuest:` which
had ended in `break` -- so cutscene, duel and quest events ran the menu code and set "partner in a menu" (copy
untargetable, no damage) from their first data byte. No false "in a menu" line found in later logs (those bytes were
usually 0), but it is fixed (break added).
Done: spoiler_test: dev quest_test "completes" Reconstructing Pailune (1000230) and the stand-in echoes it: notice
"cdmp_peer (relay): Quest Updated / A quest you have not reached yet (name hidden: spoiler guard)". The scene part
needs a host at a story scene: PLAYTEST 2.7f. [x]
- Pushed T39 (f70661b).

## T41 -- More challenges
Plan: of the five (horse race, fishing contest, boss rush / arena, hide and seek, scavenger hunt) the race fits the
existing pieces best: the challenge picker and series, the shared map waypoints, the hunting contest's round state
machine. net/coop_race.cpp: finish = the host's waypoint, else the guest's (both known on both sides: our own from
CreateOrChangeSelfDestinationReq in local_capture, theirs from the Waypoint event in coop.cpp; pings ignored), so no
extra message picks it; 3-2-1, first within 12 m (2D) sends its time (Activity type 24), the other has 60 s, then the
result; a waypoint 30+ m from the last finish starts the next round; banner with both distances.
Done: race_test: picker -> Race (option 5), challenge_test accept, dev script sends our waypoint 60 m east
(c2s 2663): "Race: round 1 to (-10280, -3915)", walked there: "we finished round 1 in 0:27.9", the echo "the other
player finished ... 0:27.9", "round 1 ends"; screen: banner "Race to the waypoint vs ... 0:03.2 you 54 m ...",
"Victory / You 0:27.9 them 0:27.9", toast "You win the race. Series: you 1 - 0". [~]
Not done: fishing contest (no fishing hook found yet), boss rush / arena (bosses are the host's story stages),
hide and seek, scavenger hunt. PLAYTEST 6.9b for the race.
- Pushed T41 (7d8efca).

## T42 -- Duel extras
Plan: of wagers, selectable arenas, spectator camera, rematch: (1) arenas = the duel ground's size, a settings row
([coop] duel_ring_m 12 / 20 / 35); the side that places the ground already sends centre + radius, so both use the
placer's size; (2) rematch = the picker remembers the last game and opens on it (Shift+F5 twice). Wagers need an item
or silver transfer the game server accepts (none found, T18 / BLOCKED.md); a spectator camera needs control of the
game camera (not hooked).
Done: duelx_test: [coop] duel_ring_m=12 -> "Duel: round 1 at (...), radius 12 m"; after ending, the picker opened on
Duel (Duel is also the first option, so this shot does not prove the rematch memory; the code is 4 lines: the
chosen stage is kept and the picker's selection set to it). [~]
- Pushed T42 (f839954).

## T43 -- Cosmetics and recognition
Plan: titles + a scoreboard (co-op bounties left out: bounties are the game's own and per player, T16). Every change
of the challenge series counters (any game) is a round won or lost -> net/scoreboard.cpp writes it per friend in
cdmp_scores.ini next to cdmp.ini (section = the Steam name made safe) and a _total section; the panel shows "Record vs
<friend>: races 1-0, ..." and "Your title: ..."; titles from totals (3 wins the Sparring Partner, 25 the Champion,
10 duels / races / hunts the Duelist / the Swift / the Huntmaster) go to the friend (Activity type 25) and appear after
the name on their partner frame.
Done: scores_test (the race test with a scores file seeded at 2 wins): race won -> "Scoreboard: won race vs cdmp_peer
(relay) (race 1 - 0)"; panel "Record vs cdmp_peer (relay): races 1-0", "Your title: the Sparring Partner"; partner
frame "cdmp_peer (relay), the Sparring Partner" (our title echoed back). Test scores file deleted after. [~]
(co-op bounties not built; the panel is now long with the settings block -- it can cover the partner frame while
open.)
- Pushed T43 (3edc116).

## T44 -- Group photo mode
Finding (web search 2026-11-03, thegameswiki photo-mode page): the game has a photo mode (beta) in the pause menu that
pauses play and frees the camera. The friend's copy is a real game actor in the scene, so it is in the photo as it
stands; the pause freezes it too. What co-op needs is the pose: a ninth quick message "Hold still: photo!" (the quick
list is now sized by its array, not a fixed 8). Our overlay already hides over game menus (ui_menu_open), and now the
partner frame hides while the F8 panel is open (it overlapped the long panel, T43).
Not tested on one PC (the photo mode menu entry was not driven by keys here; the echo copy mirrors us). [~] PLAYTEST 2.7g.
- Pushed T44 (353290d).

## T45 -- Shared stash: [!] (BLOCKED.md): same missing add-item request as T18 (three approaches there); a mod-side
stash would delete items. T46 -- Baby wyvern: stays [!] (pet only in this version; F7).

## T47 -- Voice chat: not started (deferred)
Low priority by the roadmap's own words (friends use Discord). Plan for later: Steam voice through the same
steam_api64 the mod already resolves at runtime (ISteamUser::StartVoiceRecording / GetAvailableVoice / GetVoice on the
sender, DecompressVoice + a waveOut stream on the receiver, push-to-talk on a [keys] binding), sent as an unreliable
event type over the existing Steam transport; volume falloff by distance like T33. Needs two PCs to test at all.

## T48 -- 3-4 player support: not started (deferred)
The architecture is two-player throughout: ClientSession has one peer, the puppet is one copy (kPuppetBody /
kPuppetHorse keys), the enemy relay keys copies by "the partner", challenges and revive assume one other player,
the party frame shows one friend. Scaling already has 3-4 player curves (scaling.toml). Doing it without destabilising
2-player play means a session per peer and N puppets (new reserved actor keys) -- a project of its own; the roadmap
allows skipping it ("only if the architecture allows it").

## Regression run on the evening build (2026-11-03, after T48; ROADMAP section 2)
- Shared enemies + host weather + duel + lost links + leave, each followed by the 30 s state audit (reg-audit):
  21 enemy copies relayed and removed; three audits "clean"; no crash.
- Rejoin x5 (reg-rejoin): spawn 1..6, one character per spawn, no crash.
- Menus (reg-menu): map / inventory / pause each "in a menu" then "left a menu"; spawns 1, links lost 0.
- Teammate revive (reg-revive) and the death screen (reg-qu): Q -> hp 108 (instant), U -> checkpoint revive steps
  1-3, "at the checkpoint", hp 300.
- Horse (reg-horse): ride, gallop, dismount, the horse copy reloaded where the horse is; the script's optional second
  round hung walking to the horse (test script, not the mod); stopped.
- Controls card with a pad (guidepad, [dev] pad_sim=1): pad glyphs (View + Y, B, RB, LB, RS, LS) on the card.
- Vanilla switch on the real install: vanilla.ps1 on -> cdmp.asi.off, game started, no cdmp.log written; off -> back.
- Not covered on one PC: Steam invite / room code (two accounts), a real controller.

## User request 2026-11-03 18:00 -- controls card with controller buttons, a playtest Discord, a feature list
- Controls card: input::connected_pad() (XInput slot answering, or a PlayStation HID reader running); the card draws
  that pad's buttons whenever one is plugged in (not only after it was used last); the pad text skips the keyboard-
  only chat line and says "Hold View / Touchpad for every co-op button"; the F8 panel names the card's key.
- Distribution: decided GitHub Releases on a separate PUBLIC repo that holds only the downloads (the project repo
  stays private): a stable "latest" link for Discord, versions with notes, SHA256, no size limit; source.zip in the
  zip now holds only the build sources (GPL) -- no notes or logs (it was `git archive HEAD` of everything).
- docs/release/FEATURES.md (Reddit, tables) and docs/release/discord-features.txt (3 messages under 2000 chars).
- Final build 7a880b02 packaged, scanned (Defender: no threats), fake-folder install 13/13, installed; savecheck OK, test save out, shipped ini; publishing left to the user (F12). FINAL-REPORT.md.

## Discord server set-up (user request 2026-11-03 18:30, server "Seamless co-op Crimson Desert")
Done in the user's Chrome (signed in as Asher):
- Categories: Start Here (top) -> #announcements, #how-to-install, #features, #download-here (moved in);
  Text Channels -> #general, #clips-and-highlights (unchanged); Playtesting -> #bug-reports, #find-a-co-op-partner,
  #play-testing-feedback (moved in); Voice Channels (unchanged, now last).
- Permissions: Start Here category: @everyone denied Send Messages, Send in Threads, Create Public / Private Threads;
  Owner role allowed Send, threads, Pin, @everyone mentions (+ Attach / Embed on #download-here). #download-here had
  its own overrides denying @everyone nearly everything incl. Read Message History (testers could not have seen the
  download): replaced by the category's read-only rules. #play-testing-feedback denied Send Messages to @everyone:
  now allowed (kept its no @everyone-mention / no TTS denies). Playtesting and Text Channels: open to everyone.
  Every save checked by its API answer (PUT .../permissions 204).
- Posts (docs/release/discord-server-posts.md): #announcements launch post, #how-to-install guide (pinned),
  #features (3 messages), #download-here with both zips of 7a880b02 attached (pinned), #bug-reports guide (pinned),
  #find-a-co-op-partner note. The user's earlier "will be updated when the mod becomes available" note left in place.
- Distribution decision changed: the zip pinned in #download-here (fits Discord's 10 MB with the mod-manager zip);
  the public GitHub release (blocked for me) stays optional.

## GitHub release (user: "I still want everything published to GitHub", 2026-11-03)
- tools/publish-playtest.ps1 first stopped at `gh repo view` (PowerShell 5.1 with ErrorAction Stop turns gh's
  "not found" on stderr into an error); fixed (Continue around that check).
- Published: public repo https://github.com/ashersheets-jpg/crimson-desert-coop-playtest (README with install
  steps, FEATURES.md) and release v0.1.0-alpha-7a880b02 with the player zip and the mod-manager zip (never the
  symbols zip). The project repo stays private.
- Checked: /releases/latest -> the tag (HTTP 200); both files downloaded anonymously are byte-identical to dist\.

## Showcase video and feature PDF (user request 2026-11-03 ~19:00)
- Video: dist/showcase/SeamlessCoopPlus-showcase.mp4 (31.3 s, 76 MB, git-ignored). Recorded with Windows Game Bar
  (Win+Alt+R) in one scripted take (build/port3479/showcase.sh + port3460/showcase_timeline.ps1), cut with Windows'
  own editing API (port3460/cutvideo.ps1, Windows.Media.Editing: trim + join + frame grabs; no ffmpeg here).
  Content: controls card, duel challenge + 3-2-1, Wanderer standing apart, the sprint to Rudbel Fishing Dock, the
  fight in the bandit camp. Raw take kept: dist/showcase/take13-raw.mp4.
- "Lively" teammate (user: not a mirror of us): cdmp_peer gained --name, --record FILE (saves what the game sends,
  with timing) and --mode replay --replay FILE (plays it back as the other player from the game's first state).
  The teammate's part is a separate recorded session (showcase_teammate.sh + teammate_timeline.ps1); both timelines
  start from "the other player is in our world (spawn 1)". Test tool only; the mod is unchanged.
- Findings on the way (12 takes): per-key powershell launches cost ~1-2 s each (one-process timeline instead);
  Game Bar starts recording ~4 s after the hotkey; test saves always load at the bandits start whatever the stored
  position (move_save has no effect on this save); walking routes are unreliable except start -> dock; standing at
  the dock camp >20 s gets you killed.
- The game folder has the released 7a880b02 cdmp.asi again (sha256 076cae39...), shipped ini, saves OK, test save out.
- PDF: docs/release/SeamlessCoopPlus-Features.pdf (2 pages, from docs/release/features-pdf/features.html via Edge
  headless print-to-pdf; images are frames of the showcase video).
- Quest sync (asked in Discord): not implemented: quests stay per save (D42 / NOTES/FLAGS.md F8); the party journal
  notices exist. Possible later: an opt-in offline copy of the host's story progress into the guest's save.
- 2026-11-03: showcase uploaded to the user's Google Drive (My Drive): SeamlessCoopPlus-showcase-1080p.mp4 (80 MB, the original; sent in 9 MB pieces through the Drive page and rejoined there, SHA-256 matched) and SeamlessCoopPlus-showcase-720p.mp4 (9 MB). build/port3460/cutvideo.ps1 got -Kbps (720p re-encode).

## Quest sync (user priority 2026-11-03 ~21:30, asked for by a Discord player)
- Live sync stays impossible: each player's in-process server ignores outside quest requests (D42).
- Offline route investigated: quest progress is one save object, QuestSaveData (stage / mission / quest / quest-gauge
  state lists, last completed main quest; ~1.8 MB, ~50 000 elements). Missions appear in the list as the story
  unlocks them (slot101: 3201 entries / 221 done; slot0: 3475 / 477), so it cannot be an in-place byte patch.
- tools/save/quest_sync.py: transplant(base, donor, object): copies an object between saves, renumbers its elements'
  type indices to the target schema (each save carries its own schema; missing types appended), rebases self
  pointers, splices it in (splice.py) and checks the result (all other objects byte-identical, the moved object
  element-for-element the donor's, encode round trip).
- In game (build/port3479/qsync_test.sh; "Party journal: N missions known, M done" logged at login; test copies of the
  user's saves in slot103 only, removed after; savecheck OK every round):
  * slot0 + slot2's quests (same schema): loads, 2485 known / 456 done. Mechanism works.
  * slot101 + slot0's quests, slot0 + bandit save's quests, bandit save + slot0's character objects, and even the
    40-byte CharacterStatusSaveData alone: all crash at load (exe+0x1563971 -> call 0x0). Identity re-encodes load.
  * Cause: the saves were written by different game versions: the user's slots are the 2026-11-01 backups (older
    build; the game migrates them at load), the bandit save was written by 1.0.0.3479 and its types have more fields
    (FieldNPCSaveData _affinityList, ItemSaveData _averagePrice, QuestSaveData, QuestGaugeStateData...). A block
    written under one field list is unreadable under another.
- Status: works for two saves of the same game version (what co-op partners have); NOT verified for a big story gap,
  because no two same-version saves at different story points exist here and making one needs an in-game save.
  Also open: whether quests alone are enough or the world objects that follow the story (FieldSaveData,
  NPCScheduleStageManagerSaveData, FactionSaveData, KnowledgeSaveData, FogSaveData...) must come along.
- "Continue" picks the slot by the time in its lobby.save: test variants need the bandit save's lobby.save.
- 2026-11-03 ~23:10 FOUND the load crash (exe+0x1563971 -> call 0x0): not the game version. Self pointers come after an
  8-byte run that is FF x 8 everywhere EXCEPT in FactionSpawnStageManagerSaveData, whose elements carry their index
  there (0, 1, 2, ...; 158 in slot101, 170 in slot0). Our tools only knew FF runs, so any change that moved those
  elements (a size change before them: a type added to the schema, a resized object) left their pointers stale, and
  the faction-spawn loader read garbage. Proof chain (all slot103 test copies): one unused type appended -> crash;
  same with a different type, either save -> crash; two types swapped and renumbered (no size change) -> loads;
  header float at 0x08 zeroed -> still crash; plain re-encode -> loads. splice.self_pointers now finds every u32 equal
  to its own position + 4, whatever the 8 bytes before it (slower, ~4 s a save). Also fixed: the blob size stored at
  schema end + 8 was not grown when types were appended.
- RESULT qs7 = slot101 (guest: character at its own spot) + slot0's QuestSaveData (host), QuestGaugeStateData_Stage
  appended: loads, "2486 missions known, 456 done" = exactly the host's totals; position the guest's own. qs6 (type
  appended only) loads with the guest's 2242 / 214. So a story gap of 242 completed missions transfers.
- The earlier "different game versions" verdict was partly this bug: the 40-byte CharacterStatusSaveData move and
  slot101 + slot0 crashed for this reason. Field lists do differ between builds (the bandit save's types have more
  fields), so the tool still refuses saves whose shared types have different fields.
- 2026-11-04 ~00:15 STORY SYNC BUILT AND VERIFIED (D47). common/game_save (C++: decode/encode, schema, object table,
  self pointers, story extract/apply + checks; unit tests: SHA-256/HMAC/ChaCha20 vectors, LZ4, garbage) gives the
  same blob as quest_sync.py for qs7. cdmp_savetool (count/extract/apply/roundtrip/blob) refuses old-format saves
  both ways, damaged files, cut story data, overwriting inputs. Mod: net/story_sync (F8 item "Copy my friend's story
  into my save (guest)", host item "Let my guest copy my story", [coop] share_story, [dev] story_slot), RelKind::Story
  9. cdmp_peer --story ends the game's reliable stream and answers asks. build/port3479/story_e2e.sh (slot103 only):
  ask -> "story ... kept for the next start (492 done; ours 227)" in ~1 s -> restart -> applied in 0.1 s at start,
  backup made -> "Party journal: 2486 missions known, 456 done" at the guest's own spot; ask again + "no" removes the
  pending file. Panel text wraps; the notice waits until the player is in the world (25 s). savecheck OK every run;
  the mod's test backups of slot103 moved to build/test-saves.
- Docs: README STORY SYNC section + Steam Cloud line, FEATURES.md / discord-features.txt (World: Story sync, Works),
  CHANGELOG, PLAYTEST 6c, D47, FLAGS F8 done.
- 2026-11-04 00:15 GAME UPDATED by Steam to 1.0.0.3527 (CrimsonDesert.exe written 00:15:35, PE timestamp 0x6AEA07A6,
  image size 0x13EB2000) in the middle of the final e2e run: phase 1 could not launch, phase 2's mod said "made for
  1.0.0.3479" and stayed off (as designed). The release-candidate zip dd69dd80 (story sync; built, install test 13/13,
  Defender clean, e2e passed on the same binary before the update) is for 3479 only, so it is NOT published: players'
  Steam installs 3527 and the mod would just switch itself off. Next: port to 3527 (docs/post-patch-playbook.md;
  the 3479 exe is in build/exe-archive), then package, publish and update Discord.
- 2026-11-04 ~00:55 PORT TO 1.0.0.3527 (docs/research/port-3527.md). build/port3527/portmap.py maps every address with
  both exes (masked code patterns; globals/vtables through their code references; vtables = RTTI). 40/43 code sites
  one hit; the two action-layer template copies were mapped through their caller (+0x33B0; nearest hit +0x40 was
  wrong). offsets_3527.h generated, 21 files retargeted, horse_diag / load_trace / aggro sites moved, installer and docs
  say 1.0.0.3527; verify-port.ps1 retargeted: ALL OFFLINE CHECKS PASSED. Live: game reader on, 1175 opcodes
  resolved, crash guards installed first try, copy visible, story sync e2e green on 3527 (2486/456 after restart).
  Zone test (bandits save, zone_copy_test): enemy copies loading, suppress decisions correct, no crash after 15 min.
- 2026-11-04 00:54 zone test on 3527 done (build/port3527/regress.sh zone): bandit copies loaded (47 copy lines),
  suppress decisions right, 0 crashes, 0 hook failures; the game's autosave into slot2 restored by savecheck. Running
  duel / revive / rejoin / audit next. User asked for the GitHub link (Reddit): public playtest repo given; the dev
  repo stays private (their call).
- 2026-11-04 01:15 regressions on 3527 (build/port3527/regress.sh, bandits save): duel (rounds, ring-out, scoreboard),
  revive (both ways, up where we fell), rejoin (4 kill/reconnect cycles, spawn per cycle), audit (3 session types,
  each "State audit 30 s after the session: clean"); 0 crashes, 0 hook failures; SAVES OK. Port done.
- 2026-11-04 01:25 RELEASED 0.1.0-alpha+a9c2f5d5 (story sync, game 1.0.0.3527): package (unit tests, install test,
  Defender clean), installed into the game (hash 4DF5FA7A..., smoke start: build A9C2F5D5, reader on, 1175 opcodes,
  no crash, SAVES OK, test save out, shipped ini), GitHub release v0.1.0-alpha-a9c2f5d5 (Latest, public README with
  story sync), Discord updated (docs/release/discord-server-posts.md).

## Co-op saves + live progression (user 2026-11-04 ~01:30: "necessities"; ERSC-style; never overwrite a player's save)
- User: story sync rewriting a player's real save (even with a backup) is unacceptable. Wants: a solo save, a save per
  friend that loads by who you join, back to the solo world on leaving, live quest progress, every save protected.
- FOUND (save_io.cpp, [dev] save_io_log, test save io in slot103): the game READS every slot with CreateFile2
  (save\<id>\slotN\lobby.save + save.save, all slots at the title, again before Continue; caller chain exe+0x15077C6
  <- 0x15064DF <- 0x1506466) and WRITES a slot as a folder swap: MoveFileExW slotN -> slotN_backup, CreateFileW
  slotN_new\<file> (exe+0x1497DF2 <- 0x254B9E8 <- 0x254A93E), MoveFileExW slotN_new -> slotN, slotN_backup ->
  slotN_backup_<n>_delete, DeleteFileW its files (once per file: lobby.save then save.save). All through kernelbase:
  a path redirect at that layer can give co-op play its own save folder (ERSC .co2 style) and keep solo saves untouched.
- Live quest progress idea: quests advance on client requests the in-process server accepts (MissionTriggerInOutReq,
  QuestDialogEndReq, QuestDialogInterrogationAnswerCompleteReq, StartDiscoverLevelSceneObjectReq ...), not the
  cheat ones it ignores: mirror the host's to the guest.
- 2026-11-04 01:45 HOTFIX (user approved): story sync never writes a player's save (story_sync.cpp kWritesPlayerSaves =
  false): asking says it returns with co-op saves; a story the previous build kept is discarded at start, with a notice.
  Verified (story_e2e + a planted pending file): "kept story ... discarded", st1 still 3242/227, folder empty. Docs:
  README section "CO-OP SAVES AND SHARED STORY (coming next)", FEATURES/discord-features "Coming next", CHANGELOG.
  User clarifications: the HOST also gets a save per friend (solo run untouched); quest log/progress must update in
  real time for all players (no relog).
- 2026-11-04 ~01:55 hotfix 2bd32050 RELEASED: package (tests, install test, Defender clean), installed in the game
  (smoke start OK, SAVES OK), GitHub release v0.1.0-alpha-2bd32050 (Latest), Discord updated
  (docs/release/discord-server-posts.md).
- 2026-11-04 02:17 CO-OP WORLDS, SAVE SEPARATION WORKS (save_io.cpp, [dev] coop_world=<name>): kernelbase hooks
  (CreateFile2/W, MoveFileExW, ReplaceFileW, DeleteFileW, Create/RemoveDirectoryW, GetFileAttributes(Ex)W,
  FindFirstFile(Ex)W, CopyFileW/ExW/2, SetFileAttributesW) send every path under save\<account>\ to
  Documents\CrimsonDesert-CoopPlus\Worlds\<world>\<account>\; a new world = a copy of the player's slots (made under
  .making, renamed when complete). Test (world_test.sh, in-game save into SAVE 4): the save went into the world
  (world slot103 new hash 9ccf2ad4..), the REAL slot103 byte-identical (4255263760097ad4, same mtime), all real saves
  = backup. First try failed safely: the game also COPIES files during a save ("CopyFileW Error, Error Code : 3",
  then "Commit Save 실패 [slot103]" and its own rollback); copy calls are hooked now.
- In-game Load path (3527): Esc > Others > Load > slot > Space > hold Space; Continue at the title calls
  exe+0x19B09D0(manager=[[exe+0x71BE3F8]+0x48], out, slot number, ..) via exe+0x2EBB220; the in-game Load goes
  through exe+0x2EE1D90 -> 0x19B09D0. Each load sends GameLoadingStartReq (the sniffer sees it).
- Live quest research: CompleteQuestByCheatReq's Troc handler (vtable 0x5EE9578 slot 2 = exe+0x2CD4180) only parses
  and queues (the generic [this+0x21] check is in every handler); the gate is further in: needs live tracing.
- 2026-11-04 ~02:45 coop_worlds.cpp ([coop] coop_worlds=1, off by default): world per partner (with-steam-<id64>,
  host and guest); session up 8 s -> 10 s notice -> Esc > Others (804,50) > Title Screen (466,724) > hold Space ->
  at the title set_world -> Play (E). First live run: menu -> title -> world made (7 slots copied) OK, but E at +8 s
  was ignored (title not ready): now E is pressed until the sniffer sees GameLoadingStartReq (every 12 s, 8 tries).
  Partner gone 45 s -> back to own saves the same way. Guest: story_auto_request() on connect keeps the host's story
  in memory; at the title it goes into the WORLD's newest slot only (story_put_into_world). docs/design/coop-worlds.md.
- 2026-11-04 ~03:05 worlds tests 2-3: INTO the world works end to end (title, world made, the host's story put into the
  WORLD copy only "492 missions done", Play, loaded). OUT failed: a tutorial popup ("149. Lightning", from the host's
  story's new skills) blocks Esc; quick Space taps do not close it (the test now holds Space as a player would).
  Fixes: retry with a 30 s backoff, 5 tries then a message with the manual way; at the title (out of the world 10 s,
  not loading) the saves switch at once (manual fallback / a game started while a world is wanted); the journal
  starts a new baseline at every load (it had reported the host's 240 completions as ours; now 0). Worlds moved to
  %LOCALAPPDATA%\CrimsonDesert-CoopPlus\Worlds (Documents is OneDrive-synced here; a sync lock mid-swap could fail a
  save). Old test worlds moved out of Documents to build/test-saves/old-test-worlds.
- 2026-11-04 03:10 CO-OP WORLDS FULL CYCLE GREEN (worlds_test.sh run 4, one PC, echo partner with --story m0):
  own world (st1 = m101, journal 2242/214) -> partner connects -> 10 s -> title -> world with-test-echo made (7 slots
  copied) + the host's story put into the WORLD copy -> Play -> "Party journal: 2486 missions known, 456 done" (the
  host's) -> partner gone 45 s -> first try blocked by a tutorial popup (retry 30 s) -> second try: title -> own saves
  -> Play -> 2274/214 (own story). Journal: 0 false completions. Real slot103 byte-identical (c3fb54d4), SAVES OK.
- 2026-11-04 03:25 live quest research: CompleteQuestByCheatReq (3041) sent 12 times (dev script, echo session, recorded:
  the 17-byte packets reach the server) with mission key 0xF42BC and quest keys 0xF42DF / 0xF456C in 4 layouts each:
  no CompleteQuestByCheatAck, no mission/quest list change. Ignored on retail (KillCheatReq still works: the quest
  cheat has its own gate, behind the queued dispatch). Next route: the client DRIVES much of the quest/stage system
  (one session: StageBranchStateReq 9, SetQuestDialogReq 188, GamePlayTriggerInOutReq 195, UpdateStageChartEventReq
  32, SequencerTriggerVolumeInOutReq 4, UpdateSequencerGamePlayDataEventReq 23); mirroring the host's to the guest's
  server (both worlds start from the same QuestSaveData now) is the live-sync candidate. Proof needs a recording of a
  real quest step: record, then replay only those requests on a fresh load and see the step complete.
- 2026-11-04 08:55 LIVE QUEST: MIRRORING WORKS (first proof). build/port3527/live/transitions.py scans recordings for
  mission state changes (UpdateMissionListAck 2360, walked like party_quests.cpp) with the client requests before
  each: the 3479 archive has "Head to Hernand" (FFFFF4D6) 2 -> 5 right after ONE MissionTriggerInOutReq
  (2899: u32 mission, u32 user key, u8; recorded 2899 d6f4ffff 010010a0 00, answered by UpdateTraceMissionAck 2459
  pointing at the next step FFFFF4D5). Replayed on 3527 (mission_test.sh, coop-test-bandits, nobody moves): with
  the real user key A0100001 the server answered 2459 and moved "Head to Hernand" 2 -> 5. With a wrong user key
  (our {U} placeholder gave 90100000) nothing. The user key is in every 2360 at offset 6. The mission finishing made
  the server AUTOSAVE into slot2 (known, savecheck restored it from the backup; in a co-op world it goes to the
  world). Next: does 2899 finish steps of other kinds (talk / defeat / interact / read)?
- 2026-11-04 09:00 2899 for steps of other kinds (talk "Blue Fang", defeat "White Bearclaw", interact "bell in
  Varnia", read "Ursula's journal") and for the NEXT step (not started) did nothing: the server checks the step's
  kind and order. The quest cheat is dead for good: its parser (exe+0x2CD4180; u8 x3 + 14-byte entries u64/u32/u16)
  calls no handler at all on retail (8 more layouts with the user key: nothing). build/port3527/troc_fields.py reads
  any request's field sizes from its parser: QuestDialogEndReq = 30 bytes (u32 NPC, u32 dialog, u32 x3, u8 x2, u32 x2).
- 2026-11-04 09:13 LIVE QUESTS END TO END (one PC): new src/mod/net/quest_live.* (RelKind::Quest = 10, [coop]
  live_quests=1, only while both players are in a co-op world; [dev] live_quests_any_world for tests). Test A
  (live_test.sh, peer --quest): the partner's 2899 for "Head to Hernand" with a FOREIGN user key arrived, the mod
  wrote ours (A0100001), injected it: "Head to Hernand" 2 -> 5 + "Quest step done together" notice; not echoed back.
  Dialog ends are relayed with an NPC mapping from SetQuestDialogAck 3086 (unproven: no talk-step sample yet).
- 2026-11-04 09:21 live quests SENDING side green (qliveC): our client's 2899 for "Head to Hernand" -> "Live quests:
  sending our trigger" -> the peer received 01 01 53 0b d6f4ffff 010010a0 00. Both directions now shown on one PC
  (A: receive + inject + step done; C: observe + send). A plain teleport to the Hernand RPS table did not make 3527's
  client send the trigger (qliveB: no 2899 at all; the zone or a condition differs), so C sent it by dev script
  (the mod sees it exactly as the game's own). Next: a real dialog sample (talk_test.sh: greet the Hernand boy).
- 2026-11-04 09:25 talk sample attempt (talk_test.sh, Hernand boy): Greet = ambient small talk, only
  SetQuestDialogReq/Ack (the boy's dialog key went 0C0B -> 0C0A after the greet), no QuestDialogEndReq. Other NPCs
  near there are townsfolk (farmer, fishermen). No quest NPC reachable by script in the test saves -> F14 narrowed
  to one talk step + one defeat step. Game state after: release asi a51453d2, shipped ini, SAVES OK, test save out.
- 2026-11-04 09:57 defeat steps, one PC: new dev script command `killall` (KillCheatReq on every loaded actor but
  wild animals). defeat1: teleport to Riverward Outpost (4 "Defeat the enemies around ... Posthouse" steps under way),
  74 kills (deaths confirmed, NotifyHandleDeadAck): no mission change (the actors there were mostly Hernand soldiers
  and townsfolk). defeat2: the bandit camp near the start (~10 bandits, -10310 -3675), 77 kills: no mission, quest or
  faction message at all (only an autosave request, restored). Either cheat kills do not count for quests (as for
  challenges, BLOCKED T10) or these camps are not the steps' camps. build/port3527/live/defeat_targets.py maps under-way
  defeat steps to places (all_world_locations.json). Defeat steps stay unproven; a real recording (F14) settles it.
- 2026-11-04 12:30 Roddy's idea (playtest Discord: find quest progress in RAM by diffing memory around a finished
  step) followed. New dev tools: [dev] quest_events_log=1 (src/mod/net/quest_events.*: logs every game event posted to
  the quest system, exe+0x19434D0(handler, pa::GameEventHandlerParameter*); MissionTriggerInOutReq's handler only posts
  one, type 0x11/0x12 + mission + user) and dev script memscan / memdiff / memwatch (src/mod/net/dev_memscan.*; dev_watch
  now watches up to four addresses and passes the copy protection's own single-step exceptions through).
  - qev1: 40 cheat kills posted NO game event at all -> why cheat kills never count for quests or challenges.
    Recordings show hits and deaths are worked out by the game's in-process server (AttackerAck / NotifyHandleDeadAck
    from the server; the client never sends a kill), so a real kill cannot be faked with a request either.
  - msc1: 101 copies of "Head to Hernand"'s key in RAM; after the proven trigger 3 had their byte at key+4 go 2 -> 5.
  - mw2 (hardware watch on 4 copies): both real writes are exe+0x1C119E7 `mov [rdi+0x2C], al` (rax 5, old 2) inside
    a record copy exe+0x1C119C0, called by exe+0x1C37B10 = upsert into the mission table (record: u32 key +0x28,
    u8 state +0x2C, u8 +0x2D). One write on the server thread, one on the client's copy.
  - Server chain: exe+0x2B9AAD0 -> 0x2B95290 -> 0x2B95BE0 -> 0x1C41B00 -> 0x1C41920 -> 0x1C37B10. exe+0x2B95BE0
    (manager, u32* result, u16* mission INDEX, u64* time, u8) is the quest advancement routine: it queues state
    changes with exe+0x1C3F0F0(manager, u32* mission key, ctx, u8 state 2/5, changes*) and commits them with
    exe+0x1C41B00. Seven callers. quest_events now also logs both (ADVANCE / set mission) for the next test.
  - Next: one test logging ADVANCE / set mission around the trigger (manager pointer, index, time), then call
    exe+0x2B95BE0 on the server thread for a defeat step = Roddy's "sync the stage to the host" through the game's
    own routine (runs the next step and the rest, unlike a raw state poke).
- 2026-11-04 12:57 STOP on tests: Steam Cloud. Steam uploads the save slots the moment the game exits (cloud_log:
  "AutoCloud on exit", 12:46:50 slot2 upload OK), seconds BEFORE savecheck restores slot2. So the test autosaves have
  been reaching the user's Steam Cloud copy of slot2 (local slot2 is right: SAVES OK). The 12:50 upload hung
  (googleapis "failed to issue"), and Steam now sits at "Synchronizing cloud" and launches nothing. Fixed in the
  kit: events/mission/live/talk_test.sh run with [dev] coop_world=devtest; events_test.sh builds that world itself
  (copies of the slots + the test save as slot103) so a test never writes the real save folder. Flag F15.
- 2026-11-04 13:36 Steam restarted (user OK): the hung upload cleared and Steam uploaded the REAL local slot2
  (989842 bytes, not the 987757-byte test autosave), so the cloud copy is right again. Since then no test has
  uploaded a slot (tests run in the scratch world devtest; cloud_log shows no "Need to upload"). F15 resolved.
- 2026-11-04 13:48-14:12 the quest advancement routine, called by the mod (dev script `advance <key>`; runs inside
  MissionTriggerInOutReq's handler on the quest server's thread, woken by a mission-0 trigger):
  - adv1/2: the proven trigger -> ADVANCE mission index 6597 from exe+0x2B95764, thread = the quest server's.
    The mission table (exe+0x71C1940: +8 count, +0x58 info* per index) has the key at info +0x08.
  - adv4: "Defeat the Helms at Batihar" and "Defeat the enemies around Deepwoods Beacon" 2 -> 5 from far away, with
    the consequences: UpdateQuestGaugeAck, DeleteFactionSpawnStageAck (the camp counts as cleared), experience
    (AddSubLevelExperienceAck).
  - adv5 (time 0): talk steps "Talk to the injured Blue Fang", "Talk to the chief of Tinkerton" 2 -> 5; a step
    already done is refused (error result, no change); a step NOT STARTED ("Visit all Greymane Shrines") goes
    0 -> 2 -> 5, so callers must only advance steps under way.
  - Built into live quests: src/mod/net/quest_advance.* (hooks: state setter for the manager, trigger handler to run
    requests; site bytes checked; installed in late init when [coop] live_quests is on). quest_live sends "step
    finished" (opcode 2360 + u32 key) when one of our steps goes 2 -> 5 (not while a save loads, not the partner's own
    step coming back); the receiver advances it only if it is under way there (waits 60 s for it to start).
  - lstep2 (peer simulator): ours -> partner `01 01 38 09 d6f4ffff`; partner's "Defeat the Helms at Batihar" ->
    advanced here 1.6 s later -> "finished here too" toast. lstep3: a partner's step not started here is dropped
    ("not under way here"), no change. Two-PC test still to come. Game state after: release asi a51453d2, shipped
    ini, SAVES OK.
- 2026-11-04 14:30-15:00 RELEASED 0.1.0-alpha+16df564d, co-op saves + live quests as an EXPERIMENTAL OPT-IN (user:
  "make the option to opt in very obvious and easy to turn on ... when they first launch the game"). src/mod/optin.*:
  a card on the title screen at the first launch of this build (also for players updating: [general]
  experimental_asked), Shift+F5 / View+D-pad Up = turn on (applies at once: save hooks, co-op worlds, quest server
  hooks), Shift+F6 / View+D-pad Down = not now; F8 > Settings first item switches it ([coop] coop_worlds, off by
  default; off = none of those hooks). Tested in game: card at the title and in the world (screenshots optin1_card,
  smoke16), turn on (log "turned ON", ini coop_worlds=1, hooks installed), not now (stays off, no hooks). Package
  (unit tests, install test all PASS, Defender clean both zips), installed into the game (asi aadcb83c..., player's
  ini kept; build/port3460/cdmp.ini.orig = it; old one kept as cdmp.ini.orig-2bd32050), smoke start OK, SAVES OK.
  GitHub release v0.1.0-alpha-16df564d (Latest, README/FEATURES refreshed). Discord: docs/release/discord-server-posts.md.
  Test scripts in build/port3527 now restore this release's asi.
- 2026-11-04 15:10-15:50 Reddit questions tested on one PC (build/port3527/session_test.sh, focus_test.sh):
  - Far apart: peer --offset 15000 (new peer option --far START,END,M moves the echoed partner away and back). BUG
    found: a partner 15 km away was drawn frozen next to us (frame "2 m": the game cannot place an actor on ground it
    has not loaded) and the T06 drift resync reloaded it every 15-20 s. FIX (coop.cpp): beyond 600 m the partner's
    character is removed, within 400 m it is loaded again where they are; the drift resync only runs while near.
    far5: "15000 m away" -> character hidden, frame shows 15000 m, our own character untouched (shot far5_a); back at
    2 m -> loaded again (far5). Connection steady (~20 ms, no loss), 1 harmless warning. Not released yet.
  - Focus (spirit slow motion; pad L3+R3, PC C+RMB per guides): could not trigger it by automation. C+RMB swung the
    weapon (an attack on this setup), X did nothing, a dev pad press does not reach the game without a real pad
    (that experiment reverted). The mod does not sync game speed, so Focus is local to the player who uses it.
- 2026-11-04 16:19 Focus tested by hand (user's PlayStation pad; the game was on monitor 2, captured from x 1920):
  focusman session (relay partner beside us), Focus on from ~16:19:20 for 35+ s (green tint, particles, "Cancel").
  During it: ~60 fps steady (overlay frames ~300 per 5 s), link rtt 12-16 ms, ~1.9 kB/s, clock offset steady, the
  partner's character drawn and never reloaded, no warnings. A dev pad press does not reach the game (Steam Input
  pad), so automated Focus tests are not possible here. Two-PC view (the other player's screen) still untested.
- 2026-11-04 16:25-16:48 Focus, the other way round (user's question: does the FRIEND's Focus slow my world?):
  recorded a session with the user using Focus (peer --record), replayed it as the stand-in (peer --mode replay;
  the stand-in arrives only after the recording's title minutes). Stand-in in our world 16:40:45 -> 16:48, Focus
  included: our screen never showed the Focus tint (269 frames every 1.5 s, build/port3460/capture_game.ps1), link
  steady, no resync. Limits: the stand-in was off camera (its animation not seen) and the game's time scale was
  not measured. So far: Focus is local to the player who uses it (it sends nothing to the game's server either).
  CORRECTION: the 16:25:53 game exit (first thought a crash from the user's lock-on press) and the 16:37:00 one were
  my own test scripts: stopping a background test (TaskStop) left its inner bash alive, which closed the game on its
  schedule (taskkill by name). Fixed in the kit: each test script closes only the game PID it started.
- 2026-11-04 17:14-17:38 Focus during a fight, one PC: NOT achieved. As host the game cannot open its UDP port next
  to the relay peer; a guest recording with [dev] zone_copy_test (the guest sends its zone enemies) replayed as the
  host gave copies 63 s out of step that loaded and were removed within ~30 s (plus character-load errors), and the
  user's character never moved (no fight). A faithful test needs two worlds = two PCs: added as PLAYTEST-1 §6d
  (guest Focus mid-fight, host Focus, far apart and back). Game state after: release asi, player ini, SAVES OK.
- 2026-11-04 18:05-18:20 RELEASED hotfix 0.1.0-alpha+cea405cf (far-away friend hidden beyond 600 m, back within
  400 m; user approved): package (unit tests, install test all PASS, Defender clean both zips), installed in the
  game (asi b4d50b37..., player's ini kept), smoke start (build CEA405CF, no errors, SAVES OK), GitHub release
  v0.1.0-alpha-cea405cf (Latest), Discord (docs/release/discord-server-posts.md). Test kit restores this asi now.
- 2026-11-04 19:00-20:00 Blocking slow motion in co-op (playtester poll: remove ability slow-mo for sync), step 1:
  find the game speed. New dev tools: dev script fscan / ffilter / fwatch (float narrowing, dev_memscan.*) and
  waitfile (a test driver signals steps); build/port3460/tintwatch.ps1 spots the Focus tint on the game window and
  drives the scan, so the user only presses Focus (X on keyboard / L3+R3; a simulated X press does not start it).
  Rounds fscan3 / fscan4 (user's Focus x3 each): 7 M floats of 1.0 -> 24-47 that dipped every Focus and came back,
  but while Focus was on they held unrelated values (0.96, 0.6, 0.2, 0.067 ...; a watch on four: exe+0x495C4E9 /
  0x495B542, per-frame animation blend history). No single global speed float. The packet recording shows no
  request tied to Focus (client-side). Next: watch the writer of values that sit at one designed factor during
  Focus (e.g. the 0.6 group) to find the code that applies the slow-down, then skip it in co-op.
  ALSO: savecheck --fix had restored the game's settings files in save\ (it reverted the user's new keybind preset;
  their files were put back from the kept copies); savecheck now compares only the save slots (depth >= 2).
- 2026-11-04 20:35-21:21 NO SLOW MOTION IN CO-OP, done and verified on one PC (user watching). Focus is a server
  "special mode": EnableSpecialModeAck (3126: u32 user | u32 mode 101 | f32 FLT_MAX | u32 | u32) on, DisableSpecialModeAck
  (2701) off; the client never asks. src/mod/net/slowmo.* drops the switch-on in the sniffer's receive filter while a
  co-op session is up ([coop] block_slowmo=1 default, F8 "Slow motion (Focus) in co-op"; [dev] block_slowmo_always).
  Found via: game names (ClientFrameEventTimeScale / TimeScalePreset _globalTimeScale _playerTimeScale; that event's
  handler exe+0x7A2C30 never fired for Focus), float and double scans of the speed (no global speed value), then the
  recordings (3126/2701 line up with the tint exactly). Automatic Focus: keybd_event X with scan code 0x2D, retried
  until the tint shows (build/port3460/focusprobe.ps1 -KeepS; tintwatch.ps1; the first press after a load often fails).
  Results: control run = normal Focus (user saw the tint); block run = Focus triggers, no tint, no slow motion (user
  watched); Spirit still refills: [dev] stat_log=1 shows stat 17 (Spirit, max 30000) 15915 -> 30000 within 5 s of
  Focus with the block on (after three Force Palms spent it). Two-PC test pending (PLAYTEST-1 §6d). Not released.
- 2026-11-04 21:22-21:40 RELEASED 0.1.0-alpha+69b9b13e (no slow motion in co-op; user approved): package (unit tests,
  install test all PASS, Defender clean both zips, shipped ini block_slowmo=1), installed in the game (asi 33f2cf0d...,
  player's ini kept), smoke start (build 69B9B13E, "Focus's slow motion is skipped in co-op sessions", SAVES OK),
  GitHub release v0.1.0-alpha-69b9b13e (Latest; the publish script's release step fails on quotes in -Notes, so
  gh release create --notes-file), Discord (docs/release/discord-server-posts.md). Test kit restores this asi now.
- 2026-11-04 22:20-23:25 PORT to game 1.0.0.3549 (Steam update; user: "make sure everything works for the newest
  update"). Details: docs/research/port-3549.md. Every mod address maps to itself (map_all.py, all +0, heads equal);
  offsets_3549.h = 3527's table with the new PE stamp / image size; installer, README and release texts say 3549;
  verify-port.ps1 follows the header include chain (all pass). AI battle-type jmp head re-read (protection moved).
  Live, one PC, devtest world, SAVES OK each run: session + partner (p49b/c), aggro hook, Focus block (3126/101
  unchanged, fb49/fb49b), live quests both ways (ls49c), far apart (far49), opt-in card (opt49).
  Found + fixed: (1) loads ~50 s slower since the slow-motion block (VirtualQuery per received packet; in-world ->
  LoadingCompleteAck 37-63 s since 20:58 vs 12 s before; the released 69b9b13e has it) -> direct read, 12 s again;
  (2) a transient crash-guard hook failure (1.3, trampoline out of range; retry succeeds) left the horse copy off
  for the session -> mount fix installed after the guard tries; (3) hook failures log the inner reason.
  Not released yet (needs the user's OK). Game state after: shipped ini, release asi 69b9b13e (off on 3549), no game
  running.
- 2026-11-04 23:20-23:40 RELEASED 0.1.0-alpha+0364c3f0 for game 1.0.0.3549 (user: "Yes, release it"): package (unit
  tests), install test all PASS, Defender clean both zips, installed in the game (asi 1d5bee23..., player's ini kept,
  installer accepted 3549), smoke start (build 0364C3F0 on 1.0.0.3549, game reader on, 1175 opcodes, SAVES OK).
  GitHub v0.1.0-alpha-0364c3f0 (Latest; README/FEATURES pushed by hand: publish-playtest.ps1 now stops at git's
  LF/CRLF warning under ErrorActionPreference Stop). Discord: docs/release/discord-server-posts.md. Test kit
  (build/port3527/*.sh) restores this asi now.
- 2026-11-05 00:00 Test save slot103 removed from the user's save folder AND Steam Cloud. It had been uploaded to
  the cloud by early tests (before the devtest world) and Steam re-downloaded it at every Steam launch; the release
  smoke start + savecheck --fix (which touches slot103 to be newest) left it as the newest save, so the title's Play
  would have loaded the test world. build/port3460/cloud_remove_slot103.ps1: game to title, delete slot103 while it
  runs (copy in build/test-saves/slot103-cloud-copy-2026-11-04), exit -> Steam synced the deletion; a second launch
  did not bring it back. SAVES OK, user's ini and release asi in place.
- 2026-11-05 00:25-00:45 MISTAKE + FIX: a New Game probe (build/port3527/newgame_probe.sh, empty co-op world
  "newgame-probe": the title shows Play/Load greyed, only New Game; a short Z press did not start it) ran right after
  the user had played (their slot2 saved 00:16:48, real progress). trial.ps1 deleted the user's session log
  (cdmp.log) and rewrote cdmp.ini from the 2026-11-04 14:40 copy (the user's opt-in answer is gone: the card asks
  again). savecheck.sh compared with the 2026-11-01 backup and its --fix would have rolled slot2 back at the next
  test (it was not run). Fixed in the test kit (build/, not in git): savecheck.sh --snapshot (player's slots + ini
  at test start; compare / --fix / --restore-ini against that), test_guard.sh sourced by every game test script
  (refuses to run while Crimson Desert is running; snapshots first), run_echo.ps1 / trial.ps1 never close a running
  game, trial.ps1 keeps the last log and builds the test ini from the snapshot. Reference now
  SaveBackups/reference-2026-11-05_00-30-00 (= the user's real slot2 of 00:16:48), SAVES OK.
- User request (00:20): on the first invite, choose "new save" (both start the game from the beginning) or "start
  from the host's progress"; solo saves must stay safe. Not built yet (FLAGS F17); a no-code way for tonight given.
- 2026-11-05 00:30-01:25 F17 "new save or the host's progress" on the first session: BUILT, NOT RELEASED.
  coop_worlds: the host gets a card (Shift+F5 new save / Shift+F6 the host's progress), the guest's game follows the
  host's choice (RelKind::World = 11, peer sim --quest 0101 --rel-kind 11). A new-save world is a COPY of the
  player's slots (an EMPTY folder left the title holding its old slot list: "failed to load save", no buttons) with
  coopplus-world.ini start=new; at the title the mod holds Z (New Game; a short press is ignored), the player
  accepts the agreement and picks the difficulty; new_game_started=1 once in the world (later sessions: Play).
  Guest test nw3/nw4/nw5: world made, New Game, new game running in the world, player's saves untouched (SAVES OK).
  BLOCKER: the partner's character copy crashes the game seconds after it loads in a brand-new game: its action
  layer has a chart but never an action (the opening character), and three different update functions read the
  missing action (0x251A1AD via CC tick, then 0x96918B via 0x9671E6). The CC tick guard now covers the player copy
  too (horse_diag.cpp) and a co-op world switch drops the partner's old character (coop.cpp; it was loaded ~1 km
  off in the new world and swapped a moment later), but the copy itself still crashes. Next: why a fresh
  character's copy gets no action (its spawn template is 10593 bytes against 12610 in a normal save).
  Also fixed: the co-op world's background story ask no longer shows "Story copy: not done" (user saw it on every
  test); peer sim answers "no" without --story; [dev] coop_world is now the tests' "own saves" (set_dev_home) so a
  switch can never reach the player's real folder (test nw1 had loaded the user's own save: nothing saved, SAVES
  OK); [dev] coop_world_as_host; worlds_configure keeps a dev test partner.
- 2026-11-05 02:15 FIRST TWO-PC PLAYTEST (friend Matikjr hosted, the user joined; 42 min): docs/tests/playtest-2026-11-05.md.
  Worked: Steam invite/join, connection (rtt p50 43 ms), hits on NPCs for both, co-op saves made for both (solo saves
  untouched). Broken: the host's game switching to the title by itself felt like a virus; revive; the host's NPCs
  missing in the first town; the host's UI gone after dying + checkpoint respawn (and no horse prompt); duel hits;
  the guest's co-op save made 1 s before the host's story arrived (different town states); challenges never end;
  laggy movement both ways (not the network). Guest log kept in build/playtest-2026-11-05/; host log asked for.
- 2026-11-05 02:40 Playtest fixes, not released: (1) the co-op save switch only on the player's answer (card: go now
  / I'll do it; banner while the mod drives the game; no keys at all when the player goes to the title); (2) a
  friend's revive is ignored while our own respawn is under way. The host's log is lost (friend uninstalled):
  the death/respawn/UI bug gets a one-PC repro next. Order agreed with the user: 1 switch, 2 death/revive, 3 lag +
  duel, 4 a fresh save for both on the first session (replaces "wait for the host's story"), 5 challenges end.
- 2026-11-05 02:35-04:30 (user asleep, autonomous) Playtest #2 FIXED: the lost HUD / pause menu / Ride prompt after a
  co-op death. Reproduced on one PC (death_test.sh: KillCheatReq on our own player, the mod's revive): EVERY co-op
  revive (checkpoint U and instant Q) left the HUD gone and the menus locked. Cause: our own NotifyHandleDeadAck
  (2985) puts the client into its death state; only the game's death screen (UIGamePlayControlReviveModal,
  vtable 0x5A13298, factory 0x10873F0) ends it, and that screen never opens once a copy was loaded (old finding).
  .data diffs (new dev gsnap/gdiff, ptrscan, watchstatus, key) narrowed it to client UI state, not a global flag.
  Fix (revive_prompt.cpp revive_filter_s2c): once a copy was ever loaded, our own death notice is not delivered to
  the client; the server still has us dead, so the mod's prompt, the friend's revive and the server revive work,
  and the client keeps its HUD and menus (dt9/dt11/dt12: HUD through the death, Esc opens the menu after). Also:
  the prompt now shows whenever a copy was ever loaded (a death after the friend left had no screen at all), the
  death screen replica looks like the game's (grey veil, dark red rim, no red bar). Dead end noted: re-addressing
  the notice to our body actor crashes the game (dt10).
- 2026-11-05 04:35 Playtest #3 (lag + duel) FIXED on one PC: (a) the duel's own HP came only from a VaryHPAck, so an
  unhurt player had none and EVERY duel hit on them was dropped ("our hp -0.0" in the playtest): seeded from the
  game's own reading (player_hits hits_note_own_health); (b) swings judged against the drawn copy (late) OR the
  partner's latest reported position, reach 3.5 m / arc 75 deg; (c) playout delay 300 -> 150 ms (the copy was drawn
  ~400 ms late; network loop p95 2 ms, rtt 43 ms in the playtest), migrated once in existing inis (delay_tuned).
  duel_test2.sh (scratch world): our hp 160 at the start, 9/9 swings HIT, 9 echoed hits applied, round lost "down".
- 2026-11-05 05:05 Playtest #4 / F17 DONE (user: "start both members on a fresh save before launching their co-op
  mode for the first time"): a pair's first co-op save is ALWAYS a new game (the host decides, tells the guest:
  RelKind::World; no "host's progress" option). Card "A New Journey with <friend>" (Begin now / Later), banner
  while the mod drives the game, then each player accepts the agreement and picks the difficulty. The journey only
  counts as begun when Kliff appears at the new game's start (-9910, -2807, 200 m): loading a copied save with Play
  instead says so and keeps it pending (nw10). The new-game copy crash is FIXED: a sixth containment guard
  (horse_diag kLayerSample, exe+0x968F50, CC = [[[[rcx+0x60]+8]+0x68]+0x40]) skips the function while the copy has
  no action; nw8/nw11: 45-90 s in the new game with the friend's copy beside us, no crash.
- 2026-11-05 05:15 Playtest #5 (challenges never end) FIXED: a challenge is one match: duel / race / hunt first to
  2 rounds, rock-paper-scissors and the wrestling table one match; both games end it on their own and show the
  result as the big title card ("Victory" / "Defeat", game and score, 6 s); banners say "first to 2". du2: two
  duel rounds lost -> "Defeat / Duel vs ... 0 - 2", challenge closed. Not done: the RPS hand animation (our
  challenge, not the game's minigame; known limitation).
- 2026-11-05 05:35 Co-op saves made by earlier versions (copies of the player's progress, no coopplus-world.ini; the
  user and Matikjr have one from the playtest): the host is asked ONCE ("A New Journey with X?": begin a new journey,
  the old co-op save kept as <name>-old-<date>-<time>, never deleted; or continue it, marked continue); the guest's
  game follows. The guest never enters a co-op save before the host's word for the connection (it had entered its
  old save at the title before the word, nw13). nw12 (host) / nw14 (guest): old save set aside, new journey begun.
- 2026-11-05 06:10 Regression on the final build, all green (scratch world, SAVES OK each): session (reader, 1175
  opcodes, guards, aggro hook, playout 150 after migration), Focus block 4/4, live quests both ways, far apart,
  co-op death (death screen replica, menu opens after the revive), duel 9/9 hits, continue an existing co-op save
  (host says continue), new journey host + guest (incl. earlier-version saves set aside). Test co-op saves removed
  from Worlds (devtest and the user's with-steam-...3693 kept).
- 2026-11-05 07:40 RELEASED 0.1.0-alpha+a82c1b48 (fixes from the first two-player playtest): packaged, install test
  PASS, Defender clean, installed on this PC (asi 3c07b730, smoke OK). GitHub ashersheets-jpg/crimson-desert-coop-
  playtest v0.1.0-alpha-a82c1b48 (Latest; README + FEATURES updated). Discord: new pinned download post, 0364c3f0
  relabeled and unpinned, announcement, #features messages 2 and 3 updated (docs/release/discord-server-posts.md).
- 2026-11-05 17:25 Workshop features (user: "get started with the smaller ones"). Most small ideas already existed
  (map/minimap marker, shared waypoints, revive with bleed-out, rematch, scoreboard + titles, photo quick message,
  race). New: the SESSION SUMMARY card (net/session_summary.*): a session runs from connecting until the friend is
  gone 20 s (a reconnect continues it); 2+ minutes -> "Journey Together / with X - 1 h 12 min", columns You / X:
  enemies defeated (kill feed both ways), bosses down together, revives each way, challenges won, quest steps
  finished together (live quests), distance side by side (our steps while within 150 m). Waits for the other cards,
  menus, cutscenes and loading (shows after a co-op world switch once back in our own save), 15 s, {Decline} closes.
  sm1 (scratch world, [dev] summary_test=1): kills 12/12, card on screen 3 s after the peer stopped, Shift+F6 closed
  it, SAVES OK.
- 2026-11-05 17:58 HIDE AND SEEK (net/coop_hideseek.*, Activity type 26, stage 0xD0E10004, last in the picker): the
  challenger counts first; the seeker sends the round start with its position = the area centre; 45 s veil ("Counting
  13", eyes closed) while the friend hides within 100 m (user mid-task: "within 100m or something like that ... or the
  game will never end"; a gold ground ring with 3 m posts marks the edge, red outside, 10 s outside = round lost);
  then 3 min seek: the hider's name tag, frame distance and map pin are hidden, Cold / Cool / Warm / Hot / Burning hot;
  within 4 m (and 5 m in height) = found (the seeker decides and tells the hider; no word 6 s after time = not found);
  roles swap each round, first to 2, scoreboard "hide". Ctrl+F4 (go to friend) is refused during the game.
  Tests (scratch world, echo peer, [dev] hideseek_test): hs1-hs4 we seek: veil, "Warm", tp near -> "Found! You found
  ... in 0:12"; hs5 we hide: "Well Hidden"; hs6 hider walked to 90 m (ring at the edge) then 112 m: "Back into the
  hiding area! ... 112 m", round lost (left the area). SAVES OK each.
  FINDING + FIX (pre-existing): on 1.0.0.3549 the game never asks the minimap hook for the copy's icon (no log of ours
  ever shows "the game asked for the other player's icon"), yet update_map_pin assumed an icon whenever [coop]
  minimap_icon was set and took the map pin away while the copy was loaded: a friend within 400 m was on NEITHER the
  minimap nor the map. Now the pin stays unless the game really asked (puppet_minimap_asked); hs4 shots: the gold
  diamond on the minimap and the world map with the copy 25 m away, gone during the search, back after.
  Not built: fishing contest (no catch signal known; needs a recorded fishing session). Regression: duel 9/9 HIT
  (rg9), death + checkpoint revive (rg10); SAVES OK; release asi back.
- 2026-11-05 18:55 FISHING RESEARCH (user: "record a fishing session then ... figure it out"). Findings so far:
  item keys from the live item table (strscan + ptr_multi, record = name object ptr | key at +8): FishingRod 11001,
  FishingRod_II 1000639, FishingRod_III 1000640; fish items are "Food_<species>" (build/port3479/fishkeys.txt):
  river 298xx (Ricefish 1001241, Carp 29808, Trout 29807, Catfish 29811...), sea 296xx (Salmon 29603, Mackerel
  29609...), legendary 295xx (Sailfish 29506, GoldCarp 29509, Coelacanth 29508, Golden Coelacanth 1001293);
  Ricefish 1001241 checked against the test save's inventory. AddItemToInventoryAck 3110 = every item into the bag:
  +5 u8 type (4 in game, 1 login lists), +6 owner, +10 u16 reason, +14 u16 count, element: +17 u64 item no, +25 u32
  ITEM KEY, +31 u64 stack, +55 u16 slot (seen: quest reward, use item). Keyboard: Equipment Quick Slot = 2 (not F2),
  Map M, Inventory I, WASD moves the inventory cursor, Space = Use. Rodbel Fishing Dock at (-10240, 548, -3959)
  (river bank at about (-10217, -3986)); the bandits blockade it in the test save (dev killall + a cutscene).
  The test save has no rod in the bag; make_rod_save.py put one in (bag slot 7): the inventory shows "Fishing Rod,
  Tool" but "Cannot be equipped" (client-side: no EquipItemReq is sent). The dock's NPC fishermen equip 11001
  (EquipItemOnlyAck), so the key is right. Next: the activated Mining Drill record turned into the rod (fishing2).
- 2026-11-05 19:40 FISHING SESSION RECORDED (bot-driven, test save fishing2/3 = the bandits save with the activated
  Mining Drill record turned into FishingRod 11001: a rod record made from a material record showed "Cannot be
  equipped"; a tool record with the tool fields works). How fishing works on PC (Default preset): Equipment Quick
  Slot = 2 (the rod sits in the ranged slot), Ctrl takes it out, HOLD left button = aim (a marker steered by the
  camera; RED = no fish spot, BLUE = fishable), RELEASE = cast; our client then summons a fish at the bait
  (GimmickExecuteSummonCharacterFrameEventReq 2969; the summoner only fires after arriving from outside the area);
  the bite = InteractionDoStepReq 3073 on the fish (the prompts vanish, a white splash); RIGHT button hooks it
  (CatchActionResultAck 2308, a fight stage 0x31D/0x31F starts); the fight: steer A/D away from the splashes, reel
  with the mouse wheel when calm (reeling all the time loses the fish); landed -> a card "Bass 58.4 cm / New size
  record!  Keep F / Release T"; F -> CatchActionResultAck (B_EnemyCatch_00) + ReleaseCatchResultAck 2237 +
  AddItemToInventoryAck 3110: item 29816 Food_Bass_I, stack 1, REASON 0x0D0F (the catch). The size is not in any
  packet. Tools: build/port3460/act.ps1 (input steps), castfight.py (cast until blue, wait for the bite, hook,
  splash-steered fight, frames). The catch signal for the contest: 3110 type 4, reason 0x0D0F, a fish item key.
- 2026-11-05 20:00 FISHING CONTEST (net/coop_fishing.*, Activity type 27, stage 0xD0E10005, the picker's last
  entry): one 6-minute round (a draw plays another); fish_observe_s2c counts AddItemToInventoryAck type 4 with the
  catch reason 0x0D0F and one of the 31 fish items (kFish: river 298xx + Ricefish 1, sea 296xx 2, legendary 295xx +
  Golden Coelacanth 5); "You caught a Bass +1" / "<friend> caught a Trout +1" toasts, banner with both scores, the
  big title card, scoreboard "fish". Tests: fish7/fish8 ([dev] challenge_test + fishing_test, round 150 s): picker ->
  Fishing contest, accepted, rounds 1-3 drew 0-0 -> next round each time; the bot hooked fish but lost them ("The
  fishing line snapped because of poor control"; the echo copy beside us also stands in the line's way: the one
  landed fish was without the echo peer); the recorded catch replayed (dev script s2c 3110, scripts/fishcatch.txt)
  -> "Fishing: we kept Bass (item 29816)", toast, banner "You 1 (1 fish)", round 4 ends 1 - 0 -> "Victory /
  Fishing contest vs Your friend 1 - 0". SAVES OK each run. LESSON: never edit a bash script while it runs (the
  fish6 cleanup broke mid-file; cleaned up by hand: SAVES OK, ini, release asi). hold_session.sh now archives the
  packet recordings in build/port3479/recordings instead of deleting them.
- 2026-11-05 20:30 NATIVE SERVER (user: "a pretty big priority"): ByJunter's CrimsonDesertMultiplayer-NativeServer
  cloned to upstream/cdmp-native (branch coopplus-3549, git-ignored until vendored), builds unchanged, 84/84 tests.
  Scope measured: 481 game addresses for build 2976; 38 found uniquely in 3549 by their byte heads. Plan and
  numbers: docs/research/native-port-plan.md (two-copy testing, core-first port, then the Co-op+ layer on top).
- 2026-11-05 21:00 Native port, session2 core table (offsets_s2.h): 28 of 30 addresses found in 3549 with evidence
  (docs/research/native-port-3549.json; tools in tools/native-port/): vtables by RTTI (7), functions by heads +
  vtable slots + neighbours (SessionSend 0x161ACC0, NwSession ctor 0x161A4A0 whose prologue changed, server
  OnRecvPacket 0x29AB7F0 via the run loop 0x29AAF80, allocators 0x4B00AA0/0x4B00B68, queue push 0x3A7A30, net loop
  pump 0x3A5464), globals by region shift + the instructions that use them (ServerNet 0x71BE3E0 stored by the server
  module init, ServerActorLookup 0x71BE3F8, ClientNet 0x71BDED0, _tls_index 0x70D5D50 from the PE TLS directory).
  Left for a live check: the ServerActorManager slot, the title's Play read. Struct fields of NwSession unchanged.
  ByJunter (via the user) stopped working on it and asked us to continue; questions sent to him.
- 2026-11-05 21:30 Native port, build wiring: game/offsets.h (23 values + head arrays re-read from 3549) and
  session2/offsets_s2.h ported (tools/native-port/apply_port.py re-reads each constant's byte arrays at the new
  address; only displacement bytes differ). Every session2 module whose addresses are not ported yet compares
  against kPeTimestampUnported (2976), so on 3549 it stays off by its own build check
  (tools/native-port/pin_unported.py; PORTED = offsets_s2.h, server.cpp). Safety added to the native base: the HOST
  role now redirects saves into cdmp_save_sandbox at start-up (the game ends if the redirect fails), and
  CDMP_SANDBOX_ADD copies a test slot into the sandbox only. Builds clean (native repo commit 15173f6, local).
- 2026-11-05 22:00 NATIVE BASE RUNS ON 3549 (first live check, run np1): one copy, CDMP_ROLE=host, saves redirected
  into cdmp_save_sandbox from the start, test slot copied into the sandbox only (CDMP_SANDBOX_ADD, dated now so
  Continue loads it). Loaded into the world; the ported session2 core reads live: ServerNet, session 1 (vtable
  NwVirtualAsyncSession matches), the host user (ServerUserActor vtable, key 0x90100000, back-pointer == session),
  the host character key 0xA0100001, the server FieldTime ticking. ServerActorManager slot found live:
  exe+0x71FC598 (the only .data pointer to an object with the SAM vtable 0x5EECE88; 5 code refs). One warning in the
  log (the redirect notice), no errors. SAVES OK. Also ported: the title's [WorldSystem+0x88] read (0x72B3F8) and the
  slot refresh test made build-relative (84/84). Harness: tools/native-port/test/probe.{sh,ps1}.
  Next: the ~33 addresses of the join modules (report_guard, clock_gate, identity_fix, field_share, guest_save_host,
  world_anchor, guest.cpp), then the two-copy join.
- 2026-11-05 22:20 Native port, join modules (native repo commits 5f438af..cef5d66): report_guard (Sentry reporter
  0x14F75E0 via its sentry_* imports), clock_gate (FieldMover::SyncFieldTimeOnMove is INLINED on 3549 into
  0x2E61990: the gate is now a mid hook on its owner check 0x2E61FB8 that copies the field time into user 2's
  character and resumes after the game's SetFieldTime), identity_fix + identity_probe's shared hooks (lobby load =
  ServerLobbyActorComponent vf 0x50, Status, login manager, LoadComponentsFromFile, phases), field_share
  (AcquireField 0x2DA08E0, EnterField 0x2E625B0, TryAddRef, the vector push AddActor itself uses). STRUCT LAYOUTS
  CHANGED on 3549, found in code incl. the Denuvo section (read only): FieldMover at TS+0x508 (was 0x4F0), its busy
  / moving fields -0x10, ServerField key/users/level entry +8. Checked live (run np2): the host's field found through
  TS+0x518, owner = host key, users 1, user actors 1 -> 2, TS vtable ServerTransformSync. SAVES OK.
  ByJunter's answers (via the user) in docs/research/native-port-plan.md: the core module list (kit 39 inis), use
  signature scans (planned after this hand port), field_share is required (AddActor rejects a 2nd new user).
  FLAGS F19: the 2976 exe from the user's own Steam would speed the rest up a lot.
- 2026-11-05 23:00 Native port, first TWO-COPY run (tc1, host + guest on this PC, harness
  tools/native-port/test/twocopy.{sh,ps1}): both copies start, the host loads into the world, the guest reaches its
  title (its saves in save_cdmp_guest, the host's in the sandbox); SAVES OK. Option D stayed off on both sides for
  two reasons, both fixed since (native commits a4cbe4f, 7f33727):
  1. the "real code" safety check only accepted 2976's section name .rsrc; 3549's code section is .xtext (and the
     Denuvo one .xpdata), code end 0x5595000;
  2. the game RENUMBERED its packet opcodes: only 36 of 1108 shared messages kept their id. The 3549 table is read
     from the exe itself (tools/native-port/troc49.py: the static initialisers' vtable stores into the Troc
     instances -> RTTI name + opcode; 1175 messages, docs/research/troc-messages-3549.csv; checked against what we saw
     live: AddItemToInventoryAck 3110, InteractionDoStepReq 3073). The source was rewritten by message name in
     one-shot passes (opmap / oplit / oplit2 / opmulti: ~980 constants and literals, every literal only where its
     line names the message or sits in opcode syntax, never twice). 33 unit tests still carry 2976 opcode fixtures
     (to migrate file by file; a blind pass also hit millisecond values, so it was reverted).
  Also ported: user2_funnels (rank permit, ExecuteTroc, field vf40/vf48, owner character, loot/dead, FindUser) with a
  new matcher (tools/native-port/fuzzy.py: rel32 / RIP displacements wildcarded), guest.cpp's framework mode.
  Next: the second two-copy run (lower graphics in both test copies per the user), then user2_stages.
- 2026-11-05 23:30 NATIVE BASE: TWO PLAYERS IN ONE SHARED WORLD ON 3549 (two-copy runs tc2..tc4, the user's OK).
  tc2: the guest's tunnel connects and its LoginUserToFieldServerReq reaches the host's server, but C2 identity
  logged it out ("lobby loaded off the server thread"): the TLS server-realm byte is +0x1EB on 3549 (2976 +0x1EC;
  ServerRunThread 0x29AAF36 sets it) and the allocator test byte +0x1FE (2976 +0x1FD; 807 uses). tc3: login goes
  through, user 2's lobby gets its own characterNo, then "Status unreadable": Status keeps characterNo at +0x90 on 3549
  (2976 +0x70; FindByCharacterNo 0x2D12B95; its byte check now covers that read). tc4 (both copies on the same test
  save): identity invariant ok, user 2 loads its own field, then TRAVELS TO THE HOST'S LEVEL AND IS GIVEN THE HOST'S
  FIELD (E3b H1/H2: "users 2, user actors 3"), clock gate keeps the host's world clock. ~10 s later the guest game
  exits by itself (no dump, no event, log simply stops); the host carries on and logs user 2 out after 15 s.
  Likely the guest-side guards are missing: child hold (NotifyAppear), the character-update containment, the guest
  layer/update guards and crash dump are in modules still pinned to 2976. SAVES OK every run.
  Next: port those guards (child_hold, guest_actor_diag, guest_s3, the CC update), then rerun with exit codes.
- 2026-11-06 00:15 Native base, two-copy runs tc5..tc9 (the user's OK): ported child_hold (NotifyAppear 0x2D96BB0,
  StartActionReq vf2 0x2BE08C0, its callers), HorseDiag's client character-control functions (update 0x944E70, post
  0x946060, restore 0x949B40: the only triple with 2976's spacing), the guest layer guard + remote update guard
  (guest_actor_diag) and the guest crash capture (guest_s3; now armed at once and logging each first-chance access
  violation site immediately, first in the handler list). Crashes found and fixed:
  - tc6/tc8: null read at exe+0x91CC5E3 (.xpdata) 5 s after the guest enters the host's field = ByJunter's documented
    field-change crash (p8-cierre-campo.md: the guest client looks up the field owner's user, the host, and finds none).
    His fix (user 2's MoveFieldAck names user 2 as the owner) was skipped on 3549: MoveFieldAck records are 0x38
    bytes now (live: 73 = 17 + 56; owner still @9). Fixed; tc9 log: "user 2's copy names user 2".
  - tc9: the guest now lives ~10 s in the shared field, then a null read at exe+0x7F6876 (func 0x7F66A0, normal code):
    an actor's component [table+idx*8+0x88] has [+0x10] = 0. Probably the area of guest_s3's update guard (the
    CharacterControl null-action consumer / StartAction points, 2976 0x232D5C0 / 0x863530 / 0x48B120 / 0x48AD40),
    not ported yet. Next: port those points and identify 0x7F66A0's actor (the capture logs rcx).
  tc7 showed the guest's Continue sometimes needs a second press: the harness now presses again after 10 s.
  SAVES OK in every run; the player's ini and the release asi restored each time.
- 2026-11-06 01:35 NATIVE BASE, TWO COPIES ON ONE PC: THE GUEST IS IN THE WORLD (run tc16; screenshots
  docs/research/native-shots/tc16-*.png). Host at Hernand Farmhouse, guest at Nas River (same shared field, the
  host's clock Day 27 11:14), both alive to the end, SAVES OK. What it took since tc9 (learned, for reuse):
  - Guest crashes in the host's field = remote actors whose action layer has no current action ([CC+0x88+8i]+0x10
    null; S3). Fix that works: ByJunter's HOST-side holds (child_hold + observer_hold + observer_load_defer +
    observer_hold_family + observer_hold_acks + character_hold, child_settle_ms 4000, child_reannounce) so such
    actors reach the guest only after their first action. Guest-side guards (guest_update_guard on the null-action
    consumer 0x251DFC0, a new Co-op+ guard on the movement update's read 0x7F6872 -> 0x7F6CC3) are contention only.
  - The guest's loading screen hung in state 0x11 of the field-change machine 0x855A00 (P in r13, state at
    P+0x15C): it waits for the field owner's character's stage manager ([[pj+0x68]+0xA8]+0x18) byte +0x259 >= 2
    (3549; 2976 +0x231). Only PlayAllPreLoadSequencerAck's handler writes 2 (3549: 0xC9CB9B after vf48 at
    [rbp-0x48]); the host's server sends it for the host's character. ByJunter routes it with guest_seq_route
    (not ported yet); for now guest_reload_wait sets the own manager's +0x259 to 2 after 4 s in 0x11 (game thread).
  - guest_actor_diag waited 2 min for unported diag points before installing its state hook: now only the wait
    list code gates it. The harness presses Continue twice when the first only refreshes the slot list.
  - Packet-only host modules worth enabling (no addresses): observer_seq_copy(+_2322).
  Next: get the two characters together (they spawn apart), movement sync (remote_interp/remote_motion), then the
  scene/NPC modules and guest_seq_route; then the Co-op+ features on top.
- 2026-11-06 02:05 Native base: GUEST'S OWN SAVE works on 3549 (tc17): guest_save_host ported (LoadSlot 0x19B09D0,
  SavePlayerToSlot 0x19AF6D0, FlushPendingSaveData 0x19B3020, GetOrCreateSlot thunk 0x2548E40, AddFile 0x254E2D0;
  call sites UserLogin 0x2EBB21B / 3093 lobby 0x2EE1F31 / 3093 character .xpdata 0xE3CCACF, autosave .xpdata
  0xB7F1385, 2910 .xpdata 0xE37C231, logout phase 0x2B5C0FA, flush timer .xpdata 0xE6FC3C8 (found by scanning BOTH
  .xtext and .xpdata for E8 rel32); error globals in 2976's pattern: busy 0x7149DF4 = save fail 0x7149DF0 + 4,
  invalid 0x7149A24 = absent 0x7149940 + 0xE4). Live: the guest uploads its slot (SAVE_OFFER, SHA-256 verified,
  staged), the host remaps user 2's three LoadSlot calls to slot204 in its sandbox, "user 2 plays YOUR save".
  LEARNED: the guest still spawns ~316 m from the host even with the same save: both clients pass field 3 at
  (0,1000,0) and then field 1; the server places user 2 elsewhere (an occupied-spawn rule?). Players meet by
  travelling, as ByJunter's testers did. For tests: build/native/walk_to.py walks a copy to a point (holds W,
  heading from its own MoveActorReq rows: actor u32 @0, position 3 floats @4, quaternion @16; steering gain adapts;
  avoidance when stalled), pos.py reads a copy's last own position; twocopy.ps1 -Walk walks the guest to the host.
  First walks: the guest walked 130 m to Muckroot Ranch (tc18), then climbed Hillside Manor's roof on the straight
  line (tc19) -> avoidance added (no jumping). LESSON: the native packet logs (cdmp_packets.<role>_*.log) are the
  easiest live position source; MoveFieldAck's record holds the spawn position at +25.
- 2026-11-06 02:40 Native base: the two characters SPAWN TOGETHER (tc21). The "316 m apart" of tc17 was not a server
  rule: the host's Continue loaded the newest slot in its sandbox, slot2 (a copy of the player's own character),
  not the test slot103 the guest uses. Fix: save_hooks' new CDMP_SANDBOX_ONLY=<account>\slotNNN removes every other
  slot folder from the SANDBOX copy only (refuses anything outside the sandbox; the real folder is never written);
  twocopy.ps1 sets it for the host. tc21: both MoveFieldAck spawn records (+25) at (-10349.4, 535.5, -3919.7),
  distance 0 m, Nas River; each copy received the other character's MoveActorAck (host A0100001 on the guest,
  guest A01001C5 on the host); SAVES OK; both alive. LESSON: when a two-copy result looks like a game rule, first
  check which slot each copy actually loaded (Session2 guest save probe: "slot 103 -> pass 103 (the host)").
  ui_pause ported (native ec45ddc): scale global 0x389FB30, the two UI-update calls 0xBDE82C / 0xBDE8C6
  (E8 FF 12 CC 02 / E8 65 12 CC 02, returns +5), trivial vf 0x7002A0 (C5 FA 10 05 A8 FB A3 05 C3), frame clock
  from FrameClock_NowMs 0x15F9B20: local 0x70D9E3D, base 0x70D9E60, frac 0x70D9E70 (remote_motion uses the same
  base/frac and delay global 0x71081C8; its other points are still unported, it stays pinned).
- 2026-11-06 03:10 Native base, MOVEMENT SYNC guest -> host WORKS (tc22, ui_pause_block=1 + remote_interp=1 on both):
  the guest's character (A01001AB) sent 6658 MoveActorReq; the host's client received 6558 MoveActorAck for it,
  last position identical (-10299.3, 537.9, -3921.4). Host -> guest not exercised (the run stopped before the host
  walked: actpid's `shot:` step splits on ':' so a C:\ path broke it, and the harness runs with
  ErrorActionPreference Stop). Fixed: `shot:NAME` -> build/native/shots/NAME.png; the walk steps are in try/catch;
  the walk is now straight W presses (walk_to.py's steering wandered at a 12 m target).
  UiPause installs on both copies once the player is in the world and goes LIVE at user 2's login ("menus no longer
  pause the world"). RemoteInterp installs its 4 hooks but resolves nothing: "no CC" on every call -> the movement
  controller's layout around its buffers moved in 3549 (2976: buf = mc+0x3D0 / +0x488, [mc+0x60] = CC with
  [CC+0xC8] == mc, [mc+0x68] = actor). The set/update functions are reached only through script bindings
  (lea r9,[rip+fn] at 0xD626DC.. / 0xD62911..), so the offsets come from a live probe (remote_interp layout_probe,
  log only, 3 probes). The four sites all have the buffer in RDI on 3549 too.
  Also found: observer_seq_copy's trip works on 3549 (user 2's MoveFieldAck into the host's field = OBSERVER trip);
  the host's PlayAllPreLoadSequencerAck (3549 opcode 2384) is sent to user 2 30 s after its 2144 (user 2's own
  never comes); the guest drops it until guest_seq_route is ported. guest_seq_route's 2322 point is ported in the
  source (handler 0xC9C9B0, point 0xC9CB9B, field [rbp-0x48], lookup found [rbp+8] / actor [rbp]; vtables
  ClientSequencerStageManager 0x59572E0, ClientSequencerPlayDataActorComponent 0x594D930) but the module stays
  pinned until its other points are ported or verified to refuse cleanly. hold_scene_order's MoveFieldAck /
  LoadingFieldDataAck record size is now 0x38 (a live 3549 MoveFieldAck is 73 = 17 + 56).
  LESSON: ctest's "Tests failed (N)" from build.ps1 is ctest's exit code, not the count; the count is the
  "x tests failed out of 84" line (33 = the known 2976 opcode fixtures).
- 2026-11-06 03:50 Native base, tc23 (two copies, straight walks + host menu): three features verified on 3549.
  1. MOVEMENT SYNC BOTH WAYS: the host's 650 own MoveActorReq -> 648 MoveActorAck on the guest, the guest's 375 ->
     373 on the host; each side's last received position equals the sender's last sent (-10364.8, 534.4, -3917.8)
     and (-10357.9, 535.4, -3918.8).
  2. UI PAUSE BLOCK (ui_pause_block=1): the host's ESC menu asked for UI timeScale 0; blocked for 1449 frames, the
     game clock ran +5018 ms in 5015 ms of wall time, back to 1 when the menu closed (shot tc23-37: the inventory
     open over a live world). Goes LIVE at user 2's login on both copies.
  3. Each client HAS the other player's character with a movement controller (RemoteInterp layout probe, both sides):
     3549 layout = buffer mc+0x430 (2976 0x3D0), rotation mc+0x4E8 (2976 0x488), [mc+0x60] = CC, [CC+0xC8] == mc,
     [mc+0x68] = actor (unchanged). Confirmed statically too: UpdateInterpolation (2976 0x88E6A0) is 0x976320
     (`lea rbx,[rcx+0x430]; cmp byte [rbx],1`), the rotation script setter 0x965AE0 writes [rbx+0x4E8]=1 and count
     +0x4F8. Fixed: remote_interp kPosBuffer/kRotBuffer, mount_sync kMcEmptyFlag, remote_motion kInterpPosBuffer.
  OPEN: neither window SHOWS the other player (shots tc23-34 / tc23-36, zoomed): the actor exists and gets the
  positions, so the next run logs where the remote actor stands on each client (RemoteInterp [remote character],
  every 10 s: TransformSync [[actor+0x68]+0x1A0] +0xB4 / +0xC0, unchanged in 0x5AC810).
  Mount port, addresses found (not applied yet; the module is layout-heavy): handlers via TrocTr vtable slot 2
  (2120 InteractionPreemptionStepSuccessAck 0xCD89E0, 2794 InteractionDoStepSuccessAck 0xCD94B0, 2552
  StartActionAck 0xC64250; prologue bytes equal), CommonVehicleActorComponent vtable 0x5CEFC28 vf53 0x23FD080
  (kEntry7 equal), ClientVehicleActorComponent 0x5944B90 vf53 thunk 0xB4D8D0 (E9 7B 20 D5 08) / vf54 0xB4D8F0
  (kVf54 equal), the RideOff thunk 0x23FDCC0 (E9 5B 2A 94 0A, right after vf54 as on 2976), RideOn::Enter 0x74FBB0 /
  RideOff::Enter 0x750B80 (RideOn/RideOffMovementMethod vtable slot 7, kRideEnter equal), the action request
  0xB4DD20 (2 callers, one at vf54's end), the hold update 0x976320, the gate call site 0x948E66 (-> 0x251A350, a
  real function on 3549, not a thunk; return 0x948E73), net-or 0x207A756.
  METHOD that worked: a Troc message's handler = slot 2 of the vtable of `.?AVTrocTr<Name>@pa@@` (vt49.vtable +
  slot); a movement method's Enter = slot 7 of its vtable.
- 2026-11-06 04:05 Native base, tc24: WHY THE OTHER PLAYER IS NOT SEEN, narrowed. Each client's actor for the other
  player stands where that player is and follows it (RemoteInterp [remote character], every 10 s): on the host the
  guest's 0xA01001DD TransformSync local (-349.5, 535.5, -919.6) sector (-10, -3) = world (-10349.5, -3919.6), then
  (-10357.9, -3918.8) after the guest's walk; on the guest the host's 0xA0100001 the same, then (-10364.9, -3917.9).
  Positions reach the actor's transform; the actor is simply not DRAWN (shots tc24-34 / -36: the spot 8 m ahead of the
  camera is empty). Next run dumps the remote character's header bytes beside a visible NPC's (login state on 2976:
  u16 +0x5E 8 -> 0x10, +0xCF 1; copias-grupo-invisibles.md: actors left in state 8 are never drawn).
  Unit-test fixtures: tools/native-port/fixfails.py maps 2976 opcode literals to 3549 ONLY on lines a test reports
  failing and only where the line is unchanged since a base commit (--base 12fff5f in the native repo: a 3549 number
  is often also some other 2976 opcode, so a second pass over a mapped line would map it again; caught before
  applying). One pass: 58 lines, 33 -> 29 failing tests. The rest need real work: raw packet fixtures (hex payloads
  with 2976 opcode bytes), recorded traces (replay), expected log text, and 5 fixture-driven crashes (hold_scene_order,
  scene_level_share, seq_copy, user2_shops: 0xC0000005; npc_probe_client / user2_road exit 1).
  LESSON (again): python inside a bash heredoc loses backslashes ('\r', '\'); write such code with Write/Edit.
- 2026-11-06 04:35 Native base, tc25/tc26: ROOT CAUSE of the invisible other player. Both copies: the other player's
  character is logged in (u16 +0x5E = 0x10, +0xCF = 1, the 2976 values), placed right, moving, but its MODEL ALPHA is
  0.00 because its Status flag word [Status+0x3B8] holds 0x100000 (bit 20). 3549 layout: Status = [[actor+0x68]+0x20]
  (vtable ClientStatusActorComponent 0x591A710), flags are two u64 at +0x3B8 / +0x3C0 (2976: u32 +0x390 / +0x394);
  slot 79 (0x6E99D0, 2976 vf80 0x6293A0) writes alpha [[[comps+0x40] CC +0xE8]+0x100] = flags ? 0.0 : 1.0 for actor
  types 1 and 3..9. The only writer is the setter 0x18CC450 (2976 0x1795490): (Status, bits, set, fade, second word).
  Bit 2 (CC vf18 body .xpdata 0x94AF5C0, set at load) is cleared at the end of the login as expected (guest:
  0x100004 -> 0x100000); bit 20 is set by 0x22153B8 (fn 0x22150C0) or 0x231E018 (fn 0x231DEE0: a NON-local actor
  linked to another actor's [comps+0x110] component, checked with vf178) and never cleared for the other player.
  Also: remote_interp resolves every call now (tc24: set pos 401/401, update pos 5776/5776; the set-pos hook runs on
  the client's packet thread). FIX (tc27): a mid hook at the setter's entry (7 bytes) strips bit 0x100000 from any SET
  aimed at the other player's character (key class 0xA, local mark actor+0x96 == 0) and adds it to any CLEAR on it,
  so the game's own next flag change shows the model natively; logs the caller of each change.
- 2026-11-06 04:55 Native base, tc27: THE TWO PLAYERS SEE EACH OTHER on 1.0.0.3549 (shots
  docs/research/native-shots/tc27-34-host-sees-guest.png: the guest's character ahead of the host's camera after its
  walk; tc27-36-guest-sees-host.png: the host's character walked past the guest). The visibility guard stripped bit
  0x100000 once on each copy, both times from exe+0x231E01D (the call at 0x231E018 in 0x231DEE0); afterwards the other
  player's Status flags 0x0 and model alpha 1.00 on both; the login bit 2 still clears natively (guest: 0x4 -> 0x0).
  Both copies alive, SAVES OK. Verified on 3549 so far: two copies on one PC, join with the guest's own save, spawn
  together, movement sync both ways (with remote_interp smoothing, every call resolved), the other player visible,
  menus never pause the world (ui_pause_block). Next: what 0x231DEE0's link is (which component at [comps+0x110]),
  so the guard's scope is understood; then mounts, the scene/sequencer routing and the Co-op+ features.
  Scope of the hide (04:58): [comps+0x110] is the MercenaryClanActorComponent (0x23143D0 = slot 23 of the
  Client/Common/ServerMercenaryClanActorComponent vtables; 0x23144B0 / 0x231CF30 / 0x231DEE0 are its helpers):
  0x231DEE0 hides (Status bit 0x100000) a NON-local actor the clan links, and the other player's Kliff gets linked
  like a mercenary. The guard touches only non-local 0xA keys (player characters); mercenaries are 0xB NPCs and keep
  the native hide. See ByJunter's companeros-mercenarios.md for the clan's 2976 side.
- 2026-11-06 05:35 Native base: mount_sync, fast_travel and remote_motion ported and unpinned (native 9b49a85,
  c547362). fast_travel: entry 0x721B90 (edx point, r8d index, r9d actor; 0 = a point), point sender 0xBA5B50
  called at 0x721FC2, which calls 0xD329F0 (writes opcode 3074 ReloadStageStartReq); ServerSequencerStageManager
  vtable 0x5EDE980. remote_motion: PointClimbMovementMethod 0x5924730 slot 7 = Enter 0x760A10 (same prologue),
  control-mode fn 0x976560 (same 212-byte body, table 0x976634), is-local 0x19318C0 (table 0x193190C), the wall-up
  (2851) sender 0x965C90 called at 0x96426C (site 0x96424B), its enable byte .data 0x71CC470 (zero-filled tail),
  the client clock bias 0x71BE198 (setPos 0x1386C1C). LAYOUT: the movement controller's active-method byte is
  mc+0x13C and its method array mc+0x140 (23 methods; 2976 0xF0 / 0xF8), found in 10 `movzx eax,byte [mc+0x13C];
  cmp al,0x17; mov rcx,[mc+rax*8+0x140]` sites; the wall-up gate's mc+0x168 (2976 +0x120). Unchanged: method+0x18 =
  CC, CC+8 = actor, mc+0x60/+0x68, the vehicle component's seats/reserves/count. Not re-verified: RideOff's cached
  horse key method+0xD4 (state lines only).
  tc28 (-Ride): a tap of H called no horse on 3549 (no request at all); the 3479 horse tests used H / E (Ride) / R
  with the same save. tc29 holds H 1.5 s. Both copies alive through it, SAVES OK.
  weather_sync (parked, 05:45): its 3549 points are NOT found yet. The 2976 update 0x3DC4730's 18-byte prologue
  matches only 0x969B40 on 3549, which is another function (no X+0x31 test, no +0x124/+0x128 accumulators); no
  function with both `[X+0x31]` and float writes to +0x124/+0x128 exists on 3549, so the weather system's fields
  moved. No RTTI name for the system class (2976 vtable 0x5D51268). Next attempt: from the 2976 getter vf60
  `mov rax,[rcx+0x60]; ret` and the client path [[[slot]+0x38]+0x38] (the 2560 handler 0x9ADF20 on 2976:
  find its 3549 handler via the TrocTr vtable slot 2 method, then the X it reads).
  weather (05:55): 3549's SyncWeatherStateReq (2689) sender is in .xpdata (0x9D91970, `mov ecx,0xA81` at 0x9D91AF5),
  the protected section: never hooked, so the 2976 route (find X from that sender) is closed. Next idea: the weather
  system X from a .xtext reader of the same globals; or sync through the packets the clients already send (2689).
- 2026-11-06 06:10 tc29 (mount_sync, fast_travel, remote_motion unpinned; -Walk -Ride): both alive, SAVES OK.
  INSTALLED on 3549: MountSync on both (vf53 / thunk / RideOff thunk / vf54 / RideOn+RideOff Enter / the three
  handlers / the gate / the riding action request all hooked); RemoteMotion (PointClimb::Enter hook, wall-up byte
  0x71CC470 0 -> 1, the dismount probe tracking the other player); GuestFastTravel (probe). Their log texts still
  print the 2976 addresses (cosmetic).
  NOT WORKING YET: (1) the horse: H (tap, or held 1.5 s) sent no FrameEventCallMercenaryReq with the guest in the
  world (tc30 tries H with the host alone). (2) HorseFollow: every MoveActorAck is "bad size": on 3549 its payload is
  239 bytes AND reorganised (captured, cdmp_packets.host_20261106-033517.bin): key 0x00, pos 0x04, quat 0x10, scale
  0x20, the parent u64 (all ones = none) at 0x44 (2976 0x8C), world quat ~0xB8 / pos 0xC4, a u32 at 0xD4 (ts?).
  The rider fields need a capture of a MOUNTED player (parent = horse) before horse_follow's layout can be ported.
  (3) user2_funnels refused its fast-travel funnel: "the 2283 handler's vf48 call 0x2A5F7F9 and its call 0x2896120
  ... not the expected code" (the server's ReloadStageStartReq handler; to port).
  The packet capture format (CDPK) and reader: upstream tools/scripts/analyze_capture.py.
- 2026-11-06 06:30 Native base, tc30 (guest_seq_route=1 on the guest): GUEST_SEQ_ROUTE WORKS on 3549. Hooked:
  PrepareSequencerAck 2380 (0xD33D2E), StageBranchStateAck 2920 (0xD35854), PlayAllPreLoadSequencerAck 2384
  (0xC9CB9B); DeleteSequencerAck 2292 refused (my patch length: its first instruction movzx ecx,byte [rbp+0x80] is
  already 7 bytes, so the hook patches 7, not 11; fixed). Live: the host's 2384 copy (actor 0xA0100001) was ROUTED to
  that character's own ClientSequencerStageManager X on the guest (load byte 1 -> 2). The guest's own load still
  needs the unstick (fired at 03:45:42, the routed copy came 03:46:05): the server never sends user 2 ITS OWN
  2384, and the 0x11 state waits on the guest's own manager (open: which host-side piece makes the server send it).
  METHOD for the handler points (2661/2981): where 3549 reuses the wrapper register before the point, read r8's home
  slot ([rsp+0x18] at entry = rbp + (pushes + frame) + 0x18).
  Horse: H with the host ALONE calls no horse either (tc30) -> not a co-op problem. tc31 tries the F1 wheel (the 3479
  dragon tests summoned Blackstar from it; the horse is the bottom slot). FLAGS F21 asks the user.
  user2_funnels F3 (user 2's fast travel on the server): the 3549 ReloadStageStartReq server handler is 0x2CED5E0
  (TrocTr vtable 0x5EE8E58 slot 2), its first vf48 at 0x2CED87B (return 0x2CED885), apply fn 0x2AF3A40 (2976
  0x2896120); the second vf48 site (2976 0x2BC4C49 in 0x2BC4950) is NOT found yet -> not ported.
- 2026-11-06 06:55 tc31: guest_seq_route all four points ON (2380, 2920, 2292, 2384; none refused). Horse: holding
  F1 opens NO wheel on 3549 either (shots tc31-11/-12), so neither 3479 way of calling a mount works in this build
  through simulated keys; FLAGS F21 updated (asks the user how they call the horse now). Next run (tc32): COMBAT at
  the bandit camp (Rodbel Fishing Dock ~(-10238, -3958), the bandit save's start is ~110 m west): both walk there,
  the host attacks 15 s; the harness gained -Fight.
  observer_milestone / observer_stage (the server sending user 2 ITS OWN 2384, which would retire the 0x11
  unstick): 3549 addresses found so far: writers Prepare 0x2CDA580 (op store 0x2CDA72F), Delete 0x2CDAB60
  (0x2CDACAC), Branch 0x2CDB980 (0x2CDBB36), the 2384 emitter 0x2C142A0 (op store 0x2C14397; same TLS +0x250 /
  +0x2B0 / +0x2B8 layout), its only caller 0x2ADB304 in 0x2ADB090 (2976 0x287F1D4 in 0x287EF60) with the same
  ToTarget recipe at 0x2ADB2C4; ToTargetSendTargetFinder vtable 0x5EEC840 (slots 0x2D04530 / 0x2D045E0 / 0x2D04640),
  base ISendTargetFinder 0x5EEC860 (slot 0 0x2D04490), its constructor 0x2D04590 (identical logic); the 2144 (3549
  2340) serializer 0x2C146A0 called at 0x2E6A36D in 0x2E69D70; MoveFieldCompleteCore 0x2E6A700 (2976 0x2BC7670),
  its 3066 (3549 3029) serializer 0x2C14B40 called at 0x2E6B225; the scope lookup 0x2BD5700 (2976 0x2961CA0).
  Not finished: the remaining verified regions (the 2144 site differs: lea r9,[r13+0x450]) and observer_stage's
  entry point in MoveFieldCompleteCore (2976 `mov rax,[r13+0x2D0]`, the level entry = +0x2D8 on 3549).
- 2026-11-06 07:25 tc32 (-Walk -Fight): both alive, SAVES OK; the packet-log walker stopped both players at a cliff
  ~70 m short of the bandit dock, so no fight happened. The guest still saw the host there (shot
  tc32-53-guest-sees-fight.png: the host's Kliff with its horned helmet at the cliff) -> visibility holds after
  long walks. tc33 uses the 3479 camera walker (tools/native-port/test/camwalk: position + camera forward read from
  the copy's memory, CD_PID picks the copy; the 3527/3549 old-base addresses).
  observer_stage + observer_milestone PORTED and unpinned (native 20d9050): writers Prepare 0x2CDA580 / Delete
  0x2CDAB60 / Branch 0x2CDB980 / the 2384 emitter 0x2C142A0, every context point at the SAME offset as on 2976 (all
  bytes checked); MoveFieldCompleteCore 0x2E6A700 entry 0x2E6B675 `mov rax,[r15+0x2D8]` (the field is R15 on 3549:
  mov r15,r8 at 0x2E6A72D; 2976 R13, level entry +0x2D0), predecessor 0x2E6B66C `mov rcx,[rdi+0x18]; call
  0x2A8A410`; ToTarget ctor 0x2D04590 / vtables 0x5EEC840 + base 0x5EEC860 (slots 0x2D04530/0x2D045E0/0x2D04640,
  0x2D04490); 2340 site 0x2E6A324..0x2E6A372, MoveFieldCompleteCore call 0x2E6A3AB, 3029 site
  0x2E6B1DE..0x2E6B22A, scope lookup 0x2BD5700 (+0x1A region), scope flush 0x2BD5C80 (+6), realm mark 0x29AAF36;
  field key +0x2D0, TLS realm 0x1EB. The host ini now has observer_milestone(_send, _reload, _reload_any)=1
  (ByJunter's), so the server should send user 2 its own 2384 and the guest's 0x11 unstick should stop firing.
- 2026-11-06 07:50 tc33 (-Walk -Fight, the camera walker): SHARED COMBAT WORKS on 3549. Both players reached
  Rodbel Fishing Dock (host 16.3 m / guest 18.7 m from the target); the host fought the Bleed Bandits (shot
  docs/research/native-shots/tc33-52-host-fought.png). Every VaryHPAck about the three bandits the host hit
  (0xB0100472, 0xB01005E9, 0xB010043E) reached the guest with the SAME values within 0.1 s, including two kills
  (B01005E9 -> 0 at +24 s, B0100472 -> 0 at +37 s); both players' own HP changes reach the other too. Both alive,
  SAVES OK. (Capture analysis: the CDPK .bin files; VaryHPAck payload u32 key, u32 max 1000000, u32 hp.)
  The camera walker (tools/native-port/test/camwalk, CD_PID) works for both copies on 3549.
  ALSO tc33: observer_stage + observer_milestone WORK on 3549 and the guest's 0x11 unstick did NOT fire. The login
  trip (E1.T1): user 2 owns its own field, the server sends its own 2384 natively ("native 2322", actor
  0xA01001CB). The trip into the host's field (E1.T2): ObserverStage saw the observer entry (field key 0x81020001
  owned by 0x90100000), ObserverMilestone EMITTED user 2's own 2384 through the game's ToTarget + emitter (0.017 ms,
  "IS in user 2's queue"), the guest's load ended normally (LoadingCompleteAck 12 s later). guest_reload_wait_fix
  stays on only as a fallback.
- 2026-11-06 08:30 Co-op+ LAYER on the native base, step 1 (docs/research/native-coopplus-layer.md, FLAGS F22): the
  Co-op+ source (upstream/cd-coop-3446) builds a second DLL with -DCDMP_NATIVE_LAYER=ON (build tree
  build-native-layer): coopplus.asi, files coopplus[.<role>].ini/.log/_scores.ini (CDMP_ROLE as the native base),
  src/mod/native_layer.h forces OFF after reading the ini: the sniffer, the puppet (no_copy), enemy copies + relay +
  zone, horse/equipment copies, damage mirror, guest hits, co-op worlds, live quests, story sync, the slow-motion
  block, the time sync, the stage cinema, its save guard. With the sniffer off the whole old co-op block (puppet
  hooks, crash guards, horse_diag) is never installed; the co-op link is made ready at once (coop_set_ready) so the
  layer's own traffic (activities, chat, waypoints, the party frame state) flows. online_guard's compatibility list
  skips coopplus.asi. The released build (cdmp.asi on the old base) is unchanged.
  Harness: twocopy.ps1 -CoopPlus copies coopplus.asi, writes coopplus.host/guest.ini (udp to cdmp_peer --mode relay
  on 37115; the native tunnel is 37015), starts the relay; twocopy.sh removes the DLL, its inis/logs and the relay
  afterwards (the player's game must never load coopplus.asi) and keeps the logs as <name>.coopplus.<role>.log.
- 2026-11-06 08:55 tc34 (-CoopPlus): coopplus.asi LOADS next to the native cdmp.asi on both copies: its native
  layer line, overlay hooks (Streamline's swap chain, ExecuteCommandLists), the link ready without the sniffer;
  the two Co-op+ instances linked (host "connected to peer 'Guest'") and the native session ran normally (both
  alive, SAVES OK). Problems found and fixed: (1) Co-op+'s UDP host binds peer_port itself (its guest connects to
  it): the cdmp_peer relay I had started on the same port kept the host from binding for 6 minutes (no relay now);
  "WSA error 0" because the error is read after closesocket. (2) HARNESS BUG: `$name` in the -CoopPlus loop
  overwrote the test's `$Name` (PowerShell variables ignore case) -> no tc34.pids -> the cleanup could not close
  the two copies, so the release cdmp.asi could not be copied back while they ran. Closed them by hand (only the
  two started at 05:00/05:02), put the release cdmp.asi back (checksum = release), removed the coopplus files,
  player ini restored, SAVES OK. twocopy.sh now (a) closes every CrimsonDesert started after the test began when
  the pids file is missing, (b) warns when a copy still runs, when coopplus.asi is still there, or when the game
  folder's cdmp.asi is not the release one. (3) A first-chance fault in guest_seq_route's guarded read (address
  0xFFFF...) showed up in Co-op+'s crash log: harmless, now skipped before the read.
  LESSON: in PowerShell never reuse a parameter's name in any case for a loop variable.
- 2026-11-06 09:15 tc35 (-CoopPlus, no relay): THE CO-OP+ LAYER WORKS ON THE NATIVE BASE. The two coopplus.asi
  linked directly (host udp 37115, "connected to peer 'Guest'" 25 ms after the guest's start); on the host's screen
  the Co-op+ party frame shows "Guest" with its HP bar and distance (8 m) over the native world, and F8 opens the
  Co-op+ panel with live link data ("connected to 'Guest' (5.2 ms) loss 0.0%", both HP / stamina, the distance,
  enemy health x1.5 for 2 players). Shots docs/research/native-shots/tc35-60-host-coopplus-hud.png, -62-...panel.png.
  Both alive, SAVES OK, cleanup clean. To do in native mode: hide the panel options the native world makes
  meaningless (co-op saves / live quests, story copy, guest finishes the host's enemies, loot of shared kills);
  decide party scaling (the host's server now owns enemy health: the host-side multiplier applies to both players'
  enemies, the guest's does nothing); then each activity (duel, race, fishing, hunt, hide and seek, chat, revive,
  kill feed) in the harness.
- 2026-11-06 09:40 tc36/tc37 (-CoopPlus acts): over the native world the Co-op+ layer's own features WORK: the
  quick message "Go here" + map pin (Ctrl+F3 twice), chat ("Chat: we said "hi"" on the host; the guest's screen shows
  the "Co-op / Host: hi" notice and the chat log "Host: Go here / Host: hi"), the activity notices ("the other player
  is in a menu" / "left a menu", cutscene start/end), the party frame with the partner's state ("Host - in a menu -
  8 m"), the challenge picker (Duel, RPS, hunting, race, hide and seek, fishing) and a DUEL: the host sent it, the
  guest accepted, both logged "Duel: round 1 at (-10361.5, 534.9, -3918.0), radius 20 m" and "fight"; the host's
  screen shows "Guest accepted: a duel!...", "Duel vs Guest: round 1", the opponent bar and the ring posts. Shots
  docs/research/native-shots/tc37-64-guest-got-chat.png, tc37-68-host-challenge-on.png. Script lessons: chat cannot
  open while the quick list is open (8 s); the picker sends with Shift+F5 (not Enter).
  Packet bridge (built, testing in tc38): the native sniffer forwards every client packet it feeds co-op to
  coopplus.asi's export coopplus_observe (resolved by module name, SEH-contained); the layer passes them to its own
  observers only (revive, party journal, contests, kill feed, activity notices, duel swings, weather).
  Open: duel HITS on the old base came from swings + injected damage (the sniffer, off here): to check how a duel
  is decided in native mode (the server's PvP rules).
- 2026-11-06 10:05 tc38 (-Walk -Fight -CoopPlus, packet bridge): the bridge is live on both copies ("Sniffer: the
  Co-op+ layer (coopplus.asi) gets the client's packets from now on"); both reached the dock; the host's swings hit
  nothing this time but the bandits brought the host down (Co-op+ Local hp 108 -> 0 in 20 s) and the Co-op+ REVIVE
  prompt appeared on the host's screen: "Guest can revive you (30 s)" (docs/research/native-shots/tc38-52-host-
  fought.png): the death reached the layer through the bridge. Both alive, SAVES OK (the death is in the sandbox).
  Second bridge direction (built, testing in tc39): the native cdmp.asi exports cdmp_inject_c2s (its
  sniffer_inject_c2s queue) and the layer's sniffer_inject_c2s sends through it in native mode (revive, duel hits,
  give-backs). On the guest that request goes the native way, to the host's server, for user 2's character.
- 2026-11-06 10:40 Revive on the native base, tc39-tc41: (tc39) the guest pressed ReviveFriend while the host still
  had 96 HP (it fell 30 s later, 8 m away); (tc40) the harness now waits for the host's Co-op+ status line "hp 0.0/",
  then walks the guest to where the host lies: it arrived 0.6 m away in seconds, but tapped the key; Co-op+'s
  revive is a 3 s HOLD within 3 m inside the 30 s bleed-out (coop_revive.cpp kHoldMs / kReachM / kBleedOutMs);
  (tc41) the camera walker raised a Python traceback and, under ErrorActionPreference Stop, PowerShell turned its
  stderr into a terminating error that ended the script (the cleanup still ran: SAVES OK); the walkers now run
  through cmd /c (stderr merged inside cmd) and print their last lines.
  Found by reading: revive_in_place needs capture_own_player_key, learnt by capture_observe_s2c from the actor
  loads; the bridge now sends the server's packets through the whole capture_observe_s2c (the world-simulating
  observers in it are inert: their features are off in native mode). tc42 tests revive with the hold.
- 2026-11-06 ~06:00 tc42-tc44 ran on a NEW GAME BUILD: Steam updated the game to 1.0.0.3576 (PE 0x6AED2E85) at 05:53
  during the native tests (the camera walker read garbage: its globals moved). tc44 stopped, cleanup ran (SAVES OK,
  release asi back). The released Co-op+ (a82c1b48, for 3549) is off for players on 3576. Exe archived.
- 2026-11-06 06:20-07:25 PORT of the released Co-op+ to 1.0.0.3576 (details: docs/research/port-3576.md, tools in
  build/port3576). The code MOVED this time (+0x250 .. +0xD50, vtables ~+0x2100, data +0x2000 .. +0x2220); every value
  mapped from both exes (map_all.py), opcodes unchanged (1175/1175). The 227 template copies are matched by their ORDER
  (the 64th / 66th copy in both builds), not by nearest bytes nor by callee (both wrong).
  FOUND (pre-existing since 3527): the aggro "CheckAIBattleType" hook sat on AICondition_IsKeepAggro's slot 1 (both
  are bare jmp stubs, so the head check matched); now from the RTTI. verify-port: ALL OFFLINE CHECKS PASSED.
  Live, one PC, scratch world, SAVES OK each: session/partner (p76b), aggro hooked, Focus block (fb76 4/4), live
  quests both ways (ls76), far apart and back (far76), opt-in card (opt76b), duel 10/10 HIT (du76), death +
  checkpoint revive with HUD back (dt76). Harness: the scratch world holds only the test save now (the player's
  Nov 5 slot2 is newer inside the save, so Play loaded it in p76a); optin_test resets the player's answered opt-in;
  walkto.py picks its globals by the game's PE stamp. Installer / README / release texts say 3576.
  Not released (needs the user's OK).
- 2026-11-06 07:25 Release candidate 0.1.0-alpha+fcc6e968 for 1.0.0.3576 PACKAGED (not published): unit tests pass,
  test-install-fake all 13 steps PASS, Defender clean on both zips. Waiting for the user's OK (FLAGS F24). The game
  folder keeps the released a82c1b48 (off on 3576) until then.
- 2026-11-06 07:30-07:50 NATIVE PORT 3549 -> 3576 (upstream/cdmp-native b724145, local only per F23; bundle
  refreshed in build/native-backup). Tools (tools/native-port): scan3576.py takes every hex literal in the CODE part
  of lines the 3549 port added since ByJunter's base 3cbb03c (1002) and maps it with build/port3576/portmap.py;
  judge3576.py accepts a code match only when its shift agrees with the unique neighbours (several hits: the one at
  the neighbours' shift); filt3576.py drops literals already present in the base file (742: ByJunter's 2976 values on
  lines the port touched for opcodes, e.g. owner_fanout_writers.h, whose addresses were NEVER ported: keep in mind);
  apply3576.py rewrote 298 literals in 33 files; heads3576.py re-read 41 byte arrays at the new addresses (91 the
  same, 0 not found). Hand-mapped (OVERRIDES): protected 5-byte jmp stubs through the vtable slot (RTTI name) or the
  callers that reach them (stubs.py in build/port3576): vehicle vf53/vf54, RideOff thunk, save refresh-slots,
  owner-user stub, GetOrCreateSlot, ServerStatus vf10; is-local fn +0xC40 and its jump table; the mode-fn jump
  table +0x370; two identical funnels +0xC70; kCodeEnd 0x5595000 -> 0x5597000 (section table). Unit tests: the same
  29 fixture failures as on 3549; optd_slot_refresh hardcodes the stub's protection target (3576: 0xB8AB410).
  Lesson: a protection stub's jmp target changes every build; tests should not pin it.
- 2026-11-06 07:50-08:05 tc45 (first native two-copy run on 3576): nearly every hook REFUSED. Two causes:
  (1) the guards found "the real code" by SECTION NAME (.rsrc on 2976, .xtext on 3549); 3576 renamed them again
  (.text1 = code at 0x1000; ".rsrc" is now a small other section; Denuvo is ".tls$"). The section layout is the same
  in all three builds, so in_real_code and the code/Denuvo lists now use the section at rva 0x1000 plus Denuvo's
  per-build name (guest_actor_diag_logic.h is_real_code_section / is_denuvo_name). (2) two more build stamps outside
  game/offsets.h (session2/offsets_s2.h, scene_level_share_logic.h) still said 3549: option D was off.
  Lesson for the next update: grep every "kPeTimestamp =" and never trust section names.
- 2026-11-06 08:05-08:20 tc46 (-Walk -Fight -CoopPlus) on 3576: every ported hook installs (mount_sync all true,
  ui_pause, remote_motion with 18 forced climbs, observer_stage/milestone with OUR 2322, guest_seq_route), both
  copies see each other with the Co-op+ party frame (shots tc46-34 / -36), walkers work again (3576 globals by PE
  stamp), and the layer's chat, quick message, ping and a full 2-round DUEL (guest won 2-0) work through the bridge.
  Revive not reached (the guest's walker fell 8 m on the way). Two misses of the mapping, fixed: (1) byte lists
  written INLINE in struct initialisers ({addr, {bytes}}): remote_interp's four delay reads (inline3576.py);
  (2) guest_save_host's callers that sit INSIDE the protection (above 0x7A41000, outside scan3576's range):
  found again by scanning every executable section for E8 to each target (build/port3576/dcalls.py), one-to-one
  in both builds, same order. The "GuestS3 first-chance AV at cdmp.asi+..." line also shows on 3549 runs (a
  diagnostic logging guarded reads): not new.
- 2026-11-06 08:20-08:30 tc47 on 3576: the ERR/WRN set of both copies now equals tc38's on 3549 (only the known
  first-chance diagnostic line differs); RemoteInterp ON (4 mid hooks, the other player alpha 1.00); SAVES OK.
  Revive still untested on 3576 (the host's walker got stuck, so the host never fell). Native repo ec4681c, bundle
  refreshed.
- 2026-11-06 08:30-08:45 tc48 (-Kill, new twocopy.ps1 switch: the host's layer gets [dev] script=selfkill.txt and
  the harness presses Ctrl+Shift+F6 after the fight): the KillCheatReq went through the INJECTION BRIDGE
  (coopplus -> cdmp_inject_c2s) and the host fell (hp 0.0/300); the guest walked to 1.7 m and held ReviveFriend;
  the host's layer logged "Guest revived us" and sent 3139 / 2390 / 2294 / 2957, the server answered each (3137,
  2352, ReviveAck 3053) ... for the GUEST's character: the ReviveReq carried 0xA01001B7. On the native base there is
  one server, so the host's client also receives the guest's user-owned load and local_capture's "last user load is
  ours" rule took the guest's key. Fix: in native mode capture_own_player_key() returns the game's controlled
  actor's key (PlayerReader::controlled_key(), actor + kActor_Key), the learnt one only as a fallback. Also seen:
  the client got the death notice (2985) this time (the layer only observes packets in native mode, so its filter
  that kept the client out of the death state is not in the chain): to check whether the HUD survives the revive.
- 2026-11-06 08:45-09:00 tc49: REVIVE WORKS ON THE NATIVE BASE. The ReviveReq now carries the host's own key
  (01 00 10 A0), the server answers NotifyHandleReviveAck + ReviveAck, the layer logs "we are alive again", hp
  300/300 (Local line). But the host's minimap / date / region text stayed hidden after it (shot tc49-56): the
  client had processed our NotifyHandleDeadAck (2985). Second bridge direction for packets added: the native
  sniffer's OnRecvPacket hook asks the layer's new export coopplus_filter_s2c(packet, size) (resolved like
  coopplus_observe, SEH-contained) and returns through a `ret` stub when it says 1 (the hook is on the function's
  first instruction, as on the old base). The layer answers with revive_filter_s2c (our own death notice while a
  friend's copy was seen). tc50 checks the HUD.
- 2026-11-06 09:00-09:15 tc50: the filter export was found ("may now hold back server packets") but never dropped:
  revive_filter_s2c required puppet_ever_spawned(), and the layer never spawns a copy on the native base. Now
  "a friend is with us" = a copy was seen OR (native layer) the revive tick's connected && partner state present
  (coop_revive.cpp revive_partner_present). tc51: "our death notice is kept from the client", the guest's revive,
  "we are alive again", hp 300/300, and the HUD is there afterwards (minimap, date, region, quest tracker; shot
  tc51-56). REVIVE ON THE NATIVE BASE: DONE (kill through the injection bridge, revive, HUD kept).
  Harness note: in tc51 a game "Do you want to exit the game?" dialog was open from the fight onward (a stray Esc
  of the menu step; tc49 had none): the host's attacks then do nothing. Not the mod.
- 2026-11-06 11:55-12:20 RELEASED 0.1.0-alpha+f3517194 for game 1.0.0.3576 (user: "go ahead and push the update for
  people who want to play"). Repackaged from HEAD (fcc6e968 was older; the later commits only add native-mode code):
  unit tests pass, test-install-fake 13/13 PASS, Defender clean on both zips. Installed in the game (asi 84585e42...,
  player's ini kept, saves backed up by the installer). Checks with this exact asi in the scratch world, SAVES OK each:
  rel76 session (build F3517194, 1.0.0.3576 offsets, 1175 opcodes, packet hooks +0x3A6310/+0x3A66A0, CheckAIBattleType
  hooked, partner copy visible; only the usual game-clock WRN); hs76b hide and seek (count, friend hidden from the
  minimap, found, shown again); fish76 fishing contest (round on, replayed catch "we kept Bass (item 29816)").
  hideseek_test.sh: the picker needs TWO Lefts since fishing became the last entry (LEFTS=1 picks fishing; hs76 had
  started a fishing contest by mistake). The test kit (29 scripts) now restores the f3517194 asi.
  GitHub ashersheets-jpg/crimson-desert-coop-playtest v0.1.0-alpha-f3517194 (Latest; uploads' sha256 match:
  35479fd6... player zip, 2a224c3c... mod-manager; README + FEATURES pushed; notes dist/notes-f3517194.md).
  Discord (after the user's "You finish Discord", 12:30-12:45): download post with both zips sent and pinned, the
  a82c1b48 post unpinned (not deleted), announcement posted, #features 1 (version) and 2 (hide and seek, fishing)
  and #how-to-install (version) edited. Details and the editing method: docs/release/discord-server-posts.md.
- 2026-11-06 12:50-14:40 LOW LATENCY (user: "player lag at an absolute minimum ... under 30 ms between player screens";
  top priority). Measured with a new probe (tools/lag, [dev] lag_probe=1, echo peer): the released build drew the
  other player 352 ms behind (150 ms playout + the game's own 200 ms render delay). Now 26 ms on one PC (24 per
  window), path just as smooth (residual 0.05 m); 72 ms with a simulated 30 ms +-10 network. Changes: playout buffer
  in the timestamps, adaptive ([coop] buffer_ms, default 10; the old delay_ms is no longer read); the fast lane thread
  (movement in and out without waiting for the ~30 ms network loop); writer wakes on our packets; smoothed own-clock
  estimate; the pose stream (our transform every frame, [coop] pose_stream); halfway prediction ([coop] predict).
  Details and the numbers per step: docs/research/latency.md. Checks on this build, SAVES OK each: duel 9/9 HIT
  (du77), death + revive (dt77), live quests both ways (ls77, peer --quest 0101380996480F00@20 + advance1.txt), far
  apart and back (far77). hideseek_test.sh: two Lefts for hide and seek (LEFTS=1 = fishing).
  LESSONS: the echo peer hides clock problems (the echoed packet carries our own stamp): measure lateness against a
  smoothed clock. The network loop's own work (enemy relay etc.) is ~30 ms a pass: nothing latency-critical may wait
  for it. Not yet: two real games (two copies) and horse riding / climbing with the new pipeline.
- 2026-11-06 14:40-15:45 LOW LATENCY, round 2. (1) Two copies of the RELEASED mod on one PC now work (CDMP_ROLE role
  files, whole save folder to a per-role scratch world, full GPU priority for the copy without focus; harness
  tools/lag/twocopy_cp.sh, SAVES OK each run). (2) The game's clock found again: exe+0x7233A78 (dev clockscan); read
  smoothly it fixes an idle receiver's drifting estimate (two copies: late positions 42 % -> 11 %) and revives the
  trusted-clock paths. (3) A stutter measure showed the earlier 26 ms was choppy (copy standing still on 32 % of its
  steps); the buffer now also covers the game's wait before it takes our packets (p90): 52 ms, smooth (0.4 %), two
  copies 52-54 ms. (4) Direct writes into the copy's position buffer (mc+0x430 / 0x4E8 on 3576) work but gave no gain:
  off ([coop] direct). Checks on this build: duel 10/10 HIT (du78), death + revive (dt78), live quests both ways
  (ls78). LESSON: measure smoothness with latency; a lower delay that makes the copy stop and jump is worse.
  Notes: docs/research/latency.md.
- 2026-11-06 15:50-17:20 CO-OP MENUS (user: a Co-op section in the game's Start menu; a co-op main menu before playing).
  (1) Co-op page (render/coop_page.cpp): a "Co-op" tab after "Others" in the game's own tab bar (1920x1080 positions
  scaled by height, centred: x 964, y 50; only in the world), F8 / pad chord / click opens it: sections Session (status,
  invite, leave, go to friend, bring friend, resync), Saves & Story, Combat & Loot, Reviving, World, Display, Challenges
  (every former F8 setting with a one-line explanation, settings_menu.h page_rows), Controls, Status (the old panel's
  lines; the old panel on/off). Keyboard (LL hook), pad (masked on Xbox), mouse (WH_MOUSE_LL, clicks swallowed while
  open). Shots ui1-*. (2) Title lobby (net/coop_lobby.cpp + the page module), redesigned on the user's spec in the
  look of the game's difficulty dialog: Start Solo / Invite Friends / Start Co-op (only with a friend) / Co-op
  Settings; the party top right with a crown by the host; Start Co-op asks: Continue Our Co-op Save (greyed until
  one exists) / Start From My Solo Progress / New Co-op Save (user's order). New save: the host's game presses New Game
  and the host picks the difficulty; the guest sits on "Waiting for <host>" (opaque) and its game is driven behind it
  (New Game, the agreement if shown, the host's difficulty: W to the top, S down, Space) once the host's run enters the
  world (capture_template_generation moves on: New Game makes the character already at the difficulty dialog, so
  in-world cannot tell). Continue / solo progress: both press Play after a 3 s countdown. Difficulties reported both
  ways (ChangeGameDifficultyReq, 0 Easy 1 Normal 2 Hard); different ones in a session bring up a card.
  Two-copy tests with co-op saves (twocopy_cp.sh -Lobby -CoopSaves): cpl1 (ready/start/play), cpw4 + cpw5 (host new
  save, Normal; guest followed on Normal, New Game started), SAVES OK every run. FLAGS F25: the user allowed accepting
  the EULA in test copies only; in real play the mod accepts it for the guest. In the sandboxed copies the game did
  not show the EULA at all (New Game went to the difficulty).
  LESSONS: the race lobby_choose/tick (prepare the save before the phase); PowerShell variables ignore case ($Worlds
  vs $worlds); python heredocs eat backslashes ("\a" -> BEL): write edit scripts to files.
  Not yet: two real PCs (Steam invite / join through the lobby), the guest arriving exactly at the first cutscene (it
  follows ~15 s after the host's run enters), a release.
- 2026-11-06 17:50 PLAYTESTER REPORT (Plitos, via the user): progress broke when one player's cutscene ended earlier
  than the other's; the starting quest's arm wrestling at the tavern broke too. They fixed both in their own copy (with
  Codex) by a joint check and synchronisation of cutscenes and a joint speed-up; separate save files worked and the
  original was kept. They played an offline copy over RadminVPN (UDP), not Steam. TODO: cutscene end sync (stage_cinema
  / live quests hold until both finished), the tavern arm wrestling in the opening quest; ask for their patch (GPL).
- 2026-11-06 17:40 Geonode's CDCO (GPL-3.0) is a fork of our 0364c3f0 / a82c1b48 releases plus a Tauri launcher (host/
  join and settings outside the game, written to the ini, then steam://rungameid; nothing automated at the title) and an
  experimental in-world panel made of the game's own modal UI (CommonModalOpen hook, its own uibinary/css served via the
  resource loader). Barber (Khione, 9.1.36): look stored in the save as list indexes into its own data; both players
  need the identical Barber; gender/race/height chosen at game start (probably not on our copy); no API.
- 2026-11-06 17:55 PLAYTESTER REPORT (Borque): could not talk to Oongka in the first missions; some crashes; did not
  understand how to start (asks for a video). TODO: the Oongka talk in a new co-op save (the copy beside him?), their
  cdmp.log / Diagnose output for the crashes; a short "how to start" video or card (the title lobby answers most of it).

- 2026-11-07 00:50 Plitos's patch (CrimsonDesert-Coop-Radmin-3576-F351A182, on our f3517194) reviewed: Radmin/direct
  IP transport (binds the 26.x address, drops datagrams not from peer_host, protocol 13, game restart after Leave),
  a fixed co-op world made fresh at startup with a queued story copy (backup first, refuses a host with fewer
  missions), quest sync reduced to "waiting for X" texts when native progress is on, cutscene skip vote / shared
  fast-forward by synthetic Esc/Space, tavern arm-wrestling diagnostics (our versus off in that step), horse slot 1,
  save_io hardening, an installer that backs up both save sets. Likely cause of his own playtest breaks: his
  quest_live sees connected=false whenever no player snapshot arrives (cutscenes, minigames, loading) and clears the
  pending steps. MERGED NOW (small, safe): quest_advance drops requests from before a co-op world switch (epoch) and
  re-checks on the quest thread that the step is still under way, connected, in the world, not loading
  (quest_live_can_advance; the dev "advance" step skips the check); stage_cinema handles the 12-byte DeleteSequencerAck
  (min size 13 -> 12, branch acks need 13). Worth merging later, with changes: direct-IP transport (peer filter on the
  fast lane too, no journey layer), the story backup + "fewer missions" guard in our lobby's flow, save_io hardening.
  Not taken: his quest sync and cutscene layer (they give up shared progress / trap Esc). Built, unit tests pass.
- 2026-11-07 00:55 Test kit: dev script step `ridehorse [timeout s] [hex mount vk, default E]`: after a call-horse key
  step, waits until our horse (capture_horse_present) is in the world and stands still, steps ~1.8 m from it (a
  teleport on our side), presses the mount key and checks our Moves name the horse as parent (capture_riding); 3
  tries; logs "ridehorse: MOUNTED" or why not. The call key on 3576 is still F21 (H called nothing on 3549). Built,
  not run (Steam closed).
- 2026-11-07 00:55 Checked before building: boss / enemy health by party size already exists and is on by default
  (party_scaling, [scaling], bosses from scaling.toml, curve up to 4 players). Shared XP stays BLOCKED (BLOCKED.md T10:
  KillCheatReq gives no XP; the lead, VaryStatCheatReq to 1 HP + our collision on the guest's twin, needs the twin's HP
  and a collision from up to 400 m away, so a live test comes first). Trading stays BLOCKED (T18: no request that adds
  an item found). Guest save button: in our design each player saves their OWN copy of the co-op world (the guest's
  progress lives only there), so hiding the guest's save would lose it; only meaningful on the native base.
- 2026-11-07 01:00 PLAYTESTER (Borque): both crashes came right after accepting the friend's Steam invite before a
  save was loaded; after loading his save first, joining worked. So: the Join path at the title / from Steam's launch
  (+connect_lobby, Kind::Join -> apply_role_settings while at the title, co-op world switch). TODO: two-copy repro
  (guest at the title accepts a join), and his cdmp.log from the game's bin64 folder.
- 2026-11-07 01:50 Live (Steam started with the user's OK; two copies on this PC, SAVES OK every run):
  - cpm1: STANDALONE MENU input block PASSES: with the co-op menu over the title, the host pressed the title's own keys
    (E Play, Z New Game, O Options, F, E again); the title never reacted (no load, no dialog, shots cpm1-31/-32).
    (Its start step pressed the old menu order and only selected Invite Friends: harness fixed.)
  - cpm2: Start Co-op -> Play Together -> 3 s countdown -> input back -> each game pressed Play -> both in one world.
  - ENEMY FAST LANE PASSES (cpm2): "Fast lane: 100 enemy events applied as they arrived"; the host's bandit copies exist
    in the guest's game (113-118 m away).
  - Borque's crash, not reproduced: cpj1 ([dev] join_switch_s: a copy hosting at the title turns into the guest 30 s
    later, as an accepted invite does): no crash, joined, played. cps1 (a copy started with Steam's
    "+connect_lobby <id>", transport=steam, role=auto as in the release): no crash in 90 s. Left: a real invite between
    two Steam accounts. Asked for his cdmp.log and the Windows "Application Error" entry (faulting module).
  - hk1-hk4 HORSE: on 3576 the starter horse loads with kind 4 (slot 1), the mod only knew kind 5 (slot 2), so it never
    saw the horse ("Capture: horse template" missing in every 3576 run). Fixed: kind 4 or 5 (not a player), and the
    horse copy accepts slot 1 (Plitos's kFirstMountSlot = 1). hk4: "horse template (0xB0100002)" and its position every
    5 s. H (tap or 1.5 s hold) does NOT call it on 3576; the kit then walked toward it (300 m) and got stuck on rocks.
    Kit ready (build/port3527/horse_kit.ps1, -HostScript): call, watch it come, walk there for real (walkto.py, now
    per PID with CDMP_PID), E, "Capture: riding our horse" confirms. Needs the call key (FLAGS F21).
  - hk3 DIRECT PLAY over loopback: both copies transport=direct, connected, joined, played.
- 2026-11-07 01:50 DIRECT PLAY built in (Plitos: integrate Radmin so he need not patch each release): [net]
  transport=direct (radmin, lan), role host/guest, peer_host = the friend's address on both sides, bind_host optional.
  Binds only the adapter whose subnet holds the friend (else the Radmin 26.x adapter), drops every datagram not from
  the friend's IP (transport level, so the fast lane is covered too), co-op save key "direct-<ip>", Steam name if
  Steam runs (8 s wait) else the ini's. Guide: docs/direct-play.md. Not taken from his patch: protocol 13 / journey
  string (ours stays independent), the 26/8-only rule.
- 2026-11-07 02:30 HORSE CALL (user: "I hold up on the d pad or hold H"): hk5 held H 3 s, then d-pad up 2.5 s through the
  mod's simulated pad (dev script `pad 0001 2500`, Ctrl+Shift+F6); hk6 the same with the packet recording on
  (build/port3527/rec/cdmp_packets_20261106-191835.*). The horse (0xB0100002, wyvern test save, ~300 m away, idle) never
  moved; the recording has NO call message at all (no FrameEventCallMercenaryReq 2643, CallSpecialVehicleByQuickSlotReq
  3036, CallVehicleMercenaryAck 2720, nothing named Call/Vehicle/Summon) and our player started no action during the
  hold (only its idle every ~15 s). So the game ignored the simulated hold; only a brief "menu" pause (~0.2 s) showed
  2.4 s into the H hold in hk5. Open: whether the call needs something this save lacks (horse too far, a story flag) or
  the hold must come through another input path; the simulated pad may not count without a real pad at startup. The
  kit's walk-to-it fallback got stuck on rocks over 300 m. Asked the user what they see when they call.
- 2026-11-07 02:55 HORSE CALL, more (user: "Kliff does nothing on screen, the horse runs in from behind; it can take a
  while"): hk7 held H the way a keyboard does (key-down repeated every 33 ms for 3 s) with the recording on: horse did
  not move in 60 s, no Call/Vehicle/Summon message recorded. hk8/hk9 (-Solo, ONE copy, co-op off; hk9 network on so the
  horse's position is logged): same, the horse stood at (-10666.2, -3701.0) for 2 min and walking there got stuck. So
  not the mod: either this test save's horse cannot be called (it never moves at all, not even idling) or the game
  does not take a simulated hold for this action. Next: a sandbox copy of a save where the user's call works (asked).
  Harness: -Solo (host copy alone) added; horse_kit.ps1 tries the repeating hold first.
- 2026-11-07 03:15 HORSE CALL on a sandbox copy of the user's own newest save (user OK; build/test-saves/slot103-usercopy,
  git-ignored, never pushed; the real slot only read, hashes equal): hk10 (-Solo, co-op off) H repeating 3 s and H 3 s:
  the horse did not come; its position IS live (it moved 46 m while the kit walked toward it). The same save calls the
  horse for the user, so the game most likely ignores the SIMULATED H hold for this action. Paused until the user can
  help (user: work on other things meanwhile).
- 2026-11-07 03:40 LAG re-check on today's build (enemy fast lane, direct play, Plitos fixes in): two copies, host walks
  W/S/A/D for 60 s (lagc1): SCREEN TO COPY 50 ms (3 s windows 38-64, median 49), path residual 0.03 m, network out and
  back 0.6 / 1.3 ms; the two-copy baseline was 52-54 ms (cp3). No regression. The one-copy echo harness (lag_test.sh /
  port3460 run_echo.ps1) pressed E under the new co-op menu, which now holds the title's keys, so it never loaded
  (lag20): run_echo presses Esc (Start Solo) first now.
- 2026-11-07 03:40 Player README (kit/coopplus/README.txt): QUICK START for the co-op menu (invite, accept, Start
  Co-op and its three choices, Start Solo, "Diagnose Co-op+" for crashes) for Borque's "how to start"; a DIRECT PLAY
  section (Radmin / LAN settings).
- 2026-11-07 04:30 SOLO PROLOGUE (user chose "co-op after the prologue" after Borque: as host, after the first dialog
  with Oongka neither player could talk to Marius, the camp horses could be ridden out, a new save had no Oongka; and
  Plitos's cutscene-end break). net/prologue_hold.{h,cpp}: while either player is in the prologue, co-op works as if
  far apart: no partner character, no shared cutscenes / skip vote, minigames, duels, live quests, story copy, time,
  weather, enemies, revive, travel, resync (mod.cpp: `linked = connected && !prologue_held()` for those; CoopLink
  drops incoming gameplay events / reliable kinds except Spawn (kept), Despawn, World; Activity passes only menu,
  notices, chat, kill feed, titles). Chat, the party frame, lobby and map pins stay. "In the prologue": the newest save
  in the save folder in use has < [coop] prologue_missions (60) completed missions, or no save yet, or a New Co-op Save
  run until its saves say otherwise; read on each load and every 30 s; held until the first read of the game.
  AUTOMATIC end (user: no button; "one of the first things you do after the prologue is ride into town with an old
  man", no riding in the prologue): riding anything for 5 s (our Moves name a parent; capture_carried_for). States
  exchanged as RelKind::World type 7. [coop] solo_prologue=0 turns it off. The party frame says why co-op waits.
  Tests (two copies, SAVES OK): pr1 guest forced into the prologue (threshold 1000): both held, no copy either side,
  57 / 40 partner events set aside; dev "prologuedone" -> both copies loaded within 15 ms. pr2 (normal saves): held
  ~50 ms for the first save read, then normal. NOT tested live: the ride trigger (no ride possible in the test saves
  yet; see the horse call) and the 60-mission threshold (no prologue save; each read is logged for tuning).
- 2026-11-07 05:10 PROLOGUE LENGTH: np1 (a New Game in a test copy, EULA accepted in the test copy as allowed): the
  first saves have 0 then 1 completed missions ("Unfamiliar Lands", Day 0, Hills of No Return); kept as test save
  slot103-newgame (1 done). The prologue's END count is still unknown; the 60 cutoff covers its start and the ride
  trigger its end. The solo-prologue hold switched on by itself in that run.
- 2026-11-07 05:10 SHARED XP, tried (xp1-xp4, two copies, bandit camp): the host's kills reach the guest as copy deaths;
  the guest picks its own hidden bandit of each kind and sends VaryStatCheatReq (HP to 0) + AttackByCollisionReq of
  its own player (xp4: 5 of 5 sent, 113-118 m away). Result: NO experience message (AddExperienceAck /
  AddSubLevelExperienceAck, now counted and logged) and the bandits did not die from it (KillCheatReq retries
  followed). Likely the server ignores a collision from 115 m or on an actor its client does not have. [coop]
  shared_xp now defaults to 0 (the code stays). BLOCKED.md T10 updated. Test notes: copies wait for the guest's own
  game clock, which needs the guest to act once (xp1/xp2 stood still); Ctrl+Shift+F6 needs a hold longer than one
  network-loop pass (700 ms).
- 2026-11-07 05:10 HORSE TEST KIT PASSES with the user calling (hk11, sandbox copy of the user's save, one copy): the
  user held H; the horse came 323 m (in 5 s), the kit saw it stop, walked Kliff there for real (walkto.py, 1.1 m),
  pressed E: "MOUNTED (try 1, 76 s after the call)"; "Capture: riding our horse 0xB0100003"; the solo prologue's
  ride trigger fired ("we have ridden 0xB0100003 for 5 s ... our prologue is done"). SAVES OK. The simulated H hold
  is still ignored by the game (hk4-hk10), so the call itself needs a person (or a real input path); horse_kit
  -KitCalls manual waits 3 minutes for it.
- 2026-11-07 05:20 HOW TO CALL THE HORSE on 1.0.0.3576 (the user, after hk11): HOLD D-PAD UP (opens the mount
  wheel) and push the RIGHT STICK straight DOWN to pick the horse mount; it then runs in from behind. H did NOT work for
  the user either. So: do not test H again. A simulated call needs a pad hold of Up plus the right stick down (the
  mod's dev pad press only sets buttons today; a stick axis would have to be added), or a person calls it
  (horse_kit.ps1 -KitCalls manual).
- 2026-11-07 05:50 SHARED XP, why it fails (xp5, guest recording rec/cdmp_packets_20261106-232348.*): the guest's
  VaryStatCheatReq mode 1 value 1 took the hidden bandit 0xB010006D from 63.9 to 63.0 HP (VaryHPAck: hp x1000 at +8,
  the old value at +0x18): mode 1 is NOT "set to"; HP is in each VaryHPAck, so an exact delta is possible. The
  AttackByCollisionReq became a PROJECTILE of our player (CreateProjectileAck) that OUR CLIENT flies and judges
  (OnTimerProjectileReq, OnAirFinishProjectileReq, RemoveProjectileReq: hit nothing): the client decides what a
  projectile hits, and the hidden native is not in the guest's client. That is why the same blow works on the host
  (the enemy is drawn there). Upstream research: no request grants sub-level XP (skill points; KillActor; 10/15/25,
  elite 140, boss 1000; needs Mission_Intro_Abyss_Tutorial done). Way left: give the hidden native back to the guest's
  client for a moment (at the copy's corpse, below 1 HP after an exact VaryStat) so our projectile can hit it.
- 2026-11-07 06:40 GHIDRA (12.2 + Temurin JDK 25, in build/tools, git-ignored; user downloaded both). Headless, no
  auto-analysis (327 MB exe): build/tools/scripts/Decomp.java decompiles chosen RVAs of the 3576 exe; settings/cache
  in build/tools/ghidra-settings|-cache (launch.properties VMARGS; the AppData felix cache failed). Usage:
  analyzeHeadless build/tools/ghidra-proj cd3576 -process CrimsonDesert-1.0.0.3576.exe -noanalysis -scriptPath
  build/tools/scripts -postScript Decomp.java <out.c> <rva...> (JAVA_HOME = the JDK 25). Request handler RVAs on 3576
  (TrocTr vtable slot 2): DoFrameEventReq 0x2BE1C10, VaryStatCheatReq 0x2C3FFB0, AddExperienceByGmReq 0x2C0F800,
  AttackByCollisionReq 0x2CC95C0, OnAirFinishProjectileReq 0x2CC6BA0 (pe.py now takes CDMP_EXE).
  FINDINGS for shared XP: DoFrameEventReq -> 0x2B5A200 dispatches by type byte (0 -> 0x2D19830, 1 -> 0x2E857C0,
  3 -> 0x2D19E90). Type 0 (melee) checks the attacker's CURRENT action hash and frame (a fake melee hit only works
  while the player really swings). Type 1 (a projectile's hit) only checks that the projectile exists in the
  attacker's table (id +0x10, index +0x1C) and its data entry (+0x18 key, +0x20 hash), then queues the hit on the
  listed targets: NO visibility or distance check seen. Plan: after our AttackByCollisionReq the server makes our
  projectile (CreateProjectileAck); inject a type-1 DoFrameEventReq naming the hidden native (at 1 HP) before our
  client removes the projectile. Needed: a real type-1 report recorded (a host-side collision kill on a visible enemy).
- 2026-11-07 07:10 SHARED XP, projectile route, first live checks (one copy, bandit save, recordings in
  build/port3527/rec): ft1: VaryStatCheatReq mode 0, delta -62 on a visible bandit 0xB010006D: HP 63 -> 1 (VaryHPAck
  exact). Then AttackByCollisionReq with 0xF2EE2678: server CreateProjectileAck; OUR CLIENT sent the hit report
  itself: DoFrameEventReq first byte 04, 1335 B, attacker + point + target entries (kFrameEventFixed / target entries
  at the end, as wire_layout.h) -> PostProcessAttackResult_AttackTargetAck + OnPostBuffDamagedAck + ApplyBuffAck
  0x3FF on the bandit, but NO HP change, no death, no XP: that collision projectile applies a buff, not damage.
  ft2: the arrow 0xF653A168 (1.5 m before, +1.2 m: the upstream M1 kill, +15 XP): NO CreateProjectileAck at all
  (probably needs a bow equipped). So the finishing blow itself must be found first (a projectile that deals damage
  for an unarmed / sword player), then the type-4 hit report can be injected for the hidden native.
- 2026-11-07 07:40 SHARED XP, bow recording with the user (bow1, sandbox copy of the user's save; rec/
  cdmp_packets_20261106-235658.*): a real arrow = CreateProjectileFromFrameEventReq 2644 (236 B, from the bow's action
  frame event) -> CreateProjectileAck with a real projectile (0x0ED8D272 | 0x000F4F01 | data 0x0BB2BA9B | homing target)
  -> OnHitProjectileReq 2860 (u32 player | u32 arrow n | u32 owner | u32 target | vec3 point) + DoFrameEventReq (04,
  1335 B) -> AttackTargetAck + VaryHPAck (damage) -> death -> AddSubLevelExperienceAck (sub-level 521, +15). Both kills
  +15. The collision projectile's CreateProjectileAck is all zeros (a dummy: buff, no damage). ft3: AttackByCollisionReq
  with the arrow's key 0x0ED8D272 gets NO answer (refused, like 0xF653A168): the collision request only takes collision
  keys, so it cannot make a damaging projectile. NOTE: the same collision on a visible bandit at 1 HP did not kill it
  either (ft1), so the host-side "guest lethal blow -> normal kill with XP" (guest_hits, T17) should be re-checked live.
  Routes left: (a) the guest's OWN real hits on a copy rewritten to also hit its hidden twin (DoFrameEventReq target
  rewrite; the upstream "option A"): real damage, real kill, XP for kills the guest takes part in; (b) nothing found
  yet for kills the guest has no part in.
- 2026-11-07 08:40 SHARED XP WORKS (both ways, two copies, xs1; sandbox gx2 first). Found with Ghidra: the XP message
  writer (exe+0x2CF56D0, `mov word [r15], 0xBB3`) is called by the experience routine exe+0x2EB4180(sub-level manager,
  out, actor, u16* index, u64 amount, u8 flag), which the server's own timer handler exe+0x2ED9000
  (ProcessSubLevelIncreaseExperienceOnceTimer 2069, TrocTr vtable 0x5EFDF30) calls under the game's lock after turning
  the sub-level KEY into its index. Its message: u32 actor at +3 (0 = the user), u32 key at +7, u64 amount at +0x0B,
  u8 flag at +0x13; its context [0] = the user = our player's server actor, [3] = the message. The mod
  (quest_advance.cpp run_xp_grants) runs on the quest server's thread inside MissionTriggerInOutReq's handler (woken by
  the harmless mission-0 trigger), looks our actor up as that handler does (exe+0x2D11AA0(manager=[[exe+0x71C0610]+0x50],
  handle, key), released via exe+0x1618660 with tag exe+0x5AC58E0), builds the 20-byte message and calls the timer
  handler under SEH. gx1 (the trigger's r8 as the user) faulted at 0x2ED91E1 (caught, no harm); gx2: "done" twice and
  AddSubLevelExperienceAck byte-identical to a kill's (521, +15). net/xp_share.{h,cpp}: every skill-point gain
  (sub-levels 521-523) our server reports goes to the partner (RelKind::World type 8: u32 key | u64 amount); the
  partner's mod grants it; our own grants of the partner's gains are never sent back (expected list, 10 s); not while
  the solo prologue holds. [coop] shared_xp=1 by default; the loot-twin XP route is retired. xs1: host +15 and guest
  +25 (dev grantxp, twice each): each side granted the other's amount, no echo, totals equal. SAVES OK.
  Not yet checked live: a real kill on one side (the ack is the same; the dev grant stands in for it).
- 2026-11-07 09:00 PLAYED TEST (xs2, two copies; the user played the GUEST and killed a bandit copy): the guest's
  blows reached the host's enemy 0xB0100073 (Piece 4, lethal at 1 HP); host: "survived the finishing collision:
  KillCheatReq" -> killed with NO XP for anyone (the collision 0xF2EE2678 deals no damage on 3576, as ft1 showed), so
  guest kills give no XP to share. FIX NEEDED: when the guest finishes a host enemy, the host grants the kill's XP
  itself (quest_advance_grant_xp, tier amount) and xp_share passes it on. Then the guest's game CRASHED 1.5 s later,
  40 ms after two newly re-sent enemy copies (0xB01F0006/7, "1 actions", placed by a Move) were injected:
  exe+0x2519762 reading 0x28 (stack exe+0x56759A0, 0x468C36D, 0x3E4433). Memory at 90 % (2.8 of 31 GB free, page file
  1.5 GB free) with two games on one PC. Open: copy-load crash vs memory pressure. SAVES OK.
- 2026-11-07 09:30 Fixes after the played test:
  - Kill counts ("Journey together" said host 2 / guest 4 when the guest made both kills): the host counted the guest's
    kills (the mod's finishing blow names the host as killer) and the guest counted each kill twice (the copy, then
    its hidden loot twin). kill_feed: a death the mod finished for the partner (guest_hits_finished_for_partner, 30 s)
    or of our hidden twin (suppress_is_hidden / loot_is_twin) is not ours; duplicate death notices dropped first.
  - Guest kills now give XP: when the partner's blow is finished here (KillCheatReq, no XP), the host grants the kill's
    experience itself (xp_share_partner_finished): the amount learnt per enemy kind from the host's own natural kills
    (the AddSubLevelExperienceAck within 500 ms of the death), else 15; xp_share passes it on, so both get it.
  - Guest crash (xs2) analysed: exe+0x2519762 `mov rcx,[rax+0x28]` with [rsp+0x48] = 0, in exe+0x2518850 (action
    playback; the same record's +0x28 -> +0x66 frame number the DoFrameEvent check uses): a re-sent enemy copy with
    no current action reached a code path the action-layer guards do not cover. One crash in 5 similar runs, at 90 %
    memory. Next: reproduce, then a guard (not mid-function blind).
- 2026-11-07 10:00 PLAYED TEST 2 (xs3, the user as guest; SAVES OK, no crash this time). WORKS: kill counts (guest's
  kills "we took down", hidden twins "not our kill"), guest-kill XP (host: "the other player's kill (kind 54104)
  finished here: 15 experience granted", guest: "the partner gained 15; granted here"), 50 guest blows / 46 applied.
  The user saw: (1) bandits that did not fight back at first; (2) a "joined the host's world" popup, then "all the
  bandits respawned and fought back"; (3) some bandits with "unlimited health"; (4) little damage taken as guest;
  (5) the host's blows stagger bandits more than the guest's. Causes from the logs:
  - (2) a FALSE RESYNC: the guest kept 5 dead copies for loot; the sync digest counted them (5 here, 0 sent) -> after 3
    digests "resync: leaving and joining again" -> link lost -> zone OFF -> all 24 hidden natives given back at once
    (some already killed as twins) + the host's respawned bandits as copies = doubled, natively fighting enemies.
    FIXED: enemy_copies_present skips g_kept_corpses.
  - (1)/(4) the host's enemies TARGET the guest's copy ("now targets the other player's copy", reason 0x18) but never
    ATTACK it: CheckAIBattleType "copy 0 (made to attack 0) | attack slots on the copy 2" all session; no "our enemy hit
    the other player's copy" line in any run, no "the other player's enemy hit us" on the guest. The guest takes no
    damage from the host's enemies (copia-objetivo-sin-ataques.md; the attack-slot fix does not work on 3576). OPEN.
  - (3) likely the doubled natives / copies after the false resync. Re-check after the fix.
  - (5) the guest's blows are applied as HP loss (VaryStatCheatReq) plus a hit reaction only when one was learnt for
    that attack ("no reaction learnt yet"): fewer staggers than native blows. OPEN.
- 2026-11-07 11:10 ENEMIES ATTACK THE GUEST + GUEST CRASH FIXED (two copies, ag1-ag4, bait test: [dev] attack_probe,
  Ctrl+F10 bait, guest and host walk into the bandit camp; SAVES OK each):
  - Guest crash (xs2? / xs3 / ag3, exe+0xA12E4E reading 0x115): exe+0xA128C0 processes a character's queued animation
    events ([rcx] = its CC); it reads each layer's current action [layer+0x10] -> +0x111/+0x115 with no null check on
    one path. The enemy copies came with L0 AND L1 holding a chart and no action; the tick guard only checked the MAIN
    layer. horse_diag: on_action_events skips that function for copies while any of layers 0-3 has a chart and no
    current action (events wait). ag4: guard fired at the copies' load on both sides, NO crash (ag3 crashed there).
  - Enemies never attacked the guest's copy: (1) CheckAIBattleType's context keeps its target at +0x268/+0x270 on
    3576 (2976: +0x260/+0x268) and the asking enemy is reached via a handle (exe+0x192EF10, released exe+0x1618660 tag
    0x5AC58E0): the F5 override never recognised the copy ("copy 0"); fixed. (2) with the copy targeted the AI never
    even asked (ag2: 0 calls); the copy's battle byte (CommonTarget +0x90 -> +0x164) is now kept at 1 on the host while
    it is loaded and alive ([coop] copy_in_battle=1, aggro_keep_copy_in_battle). ag4: CheckAIBattleType on the copy
    12808 times, made to attack 5527, 11 enemy blows on the copy relayed, the guest took all 11.
  - Test harness: bait_kit.ps1 (-HostScript), walks the guest (second PID) then the host into the camp.
- 2026-11-07 12:50 GUEST STAGGER FIXED. Measured first (user asked): played test xs3, 0 of the guest's 46 blows had any
  reaction ("no reaction learnt yet"). Three causes: [coop] guest_hit_stagger=0 by default; reactions were only copied
  from the HOST's own blows (session-only), none yet; and on 3576 ApplyActionRestrictionAck is 69 B with instance
  +0x1C / source +0x41 (2976: 77 B, +0x24, +0x49), so no stagger was ever learnt, not even from the host. Fixed: the
  3576 layout; built-in templates from the user's recorded sword hit (kDefaultAta 219 B, kDefaultAar 69 B,
  kDefaultBuff 77 B buff 0x452) used until learnt, for the measured staggering attacks (kSeedStaggers); stagger on by
  default (code and ini template; existing installs keep their ini value). Test st5 (dev "guesthit 0F357AC7 14" x8 on
  the guest: a GuestHit as from a real blow, because the simulated mouse clicks and E did not make the guest attack in
  st1-st3): 7 applied, all "+ stagger"; no crash; SAVES OK.
- 2026-11-07 12:50 Kill XP learnt per enemy kind is kept across sessions (<game>/bin64/cdmp_kill_xp.txt, "kind amount"
  per line), so the 15 default only applies to a kind the host never killed. Built, not run live yet.
- 2026-11-07 12:50 Disk cleanup (user's OK): deleted the old packet archive (22 GB), test screenshots (6.6 GB), game
  folder recordings (6.9 GB), old exes (3479/3527/3549), old release builds and symbols (kept f3517194), installers;
  C: 50 -> 83 GB free. Kept: source, test saves, the f3517194 release, the bow recording, Ghidra, the real co-op world.
- 2026-11-07 13:30 RELEASE CANDIDATE 0.1.0-alpha+c606602d (NOT published; waiting for the user's OK). The packaged
  cdmp.ini ships guest_hit_stagger=1. FOUND: an update keeps the player's old cdmp.ini, so updaters would have kept
  the old shipped 0; the installer now switches exactly the old shipped block (the "0 is safer" comment + =0) to 1,
  once, and says so (a player could not have picked 0 under that text: it was the default). LESSON: in .NET (?m) mode
  `$` does not match before "\r\n" (the first try missed; a lookahead `(?=\r?$)` fixed it). test-install-fake now
  checks that migration: 14/14 PASS. Defender: no threats on the three zips. CHANGELOG entry for everything since
  f3517194. Mod code identical to fd3f8f1 (the live-tested build). Notes + Discord text: dist/notes-c606602d.md.
  Removed the superseded candidates 2020f177 / 357cdef8 / 1b4608d2 from dist.
- 2026-11-07 13:45-14:15 RELEASED 0.1.0-alpha+c606602d (user: "Yes, publish it"). Before upload, a two-copy session
  with this exact asi (rel77: linked, the guest's character arrived, no ERR/WRN, SAVES OK; short walk: lag sample
  too small to compare). The test kit now restores the c606602d asi (the game folder has it; the player's ini kept).
  GitHub ashersheets-jpg/crimson-desert-coop-playtest v0.1.0-alpha-c606602d, Latest; uploads' sha256 match
  (590baf57... player zip, 085b95eb... mod-manager); FEATURES.md and README pushed (co-op menu, shared XP, enemies
  fight the guest, stagger, prologue, lag, direct play). publish-playtest.ps1 fixed twice: git's CRLF warning on
  stderr stopped it (ErrorActionPreference Stop) and Windows PowerShell 5.1 split --notes at embedded quotes (now
  --notes-file). Discord: download post pinned, announcement, #features 1 and 2 edited
  (docs/release/discord-server-posts.md). NOT done: unpinning the f3517194 post (refused by the permission check).
- 2026-11-07 14:20-15:30 RADMIN VPN / DIRECT IP IN THE CO-OP MENU (Plitos's handoff, CrimsonDesert-Radmin-Asher-Handoff.zip:
  RADMIN_UPSTREAM_HANDOFF.md + radmin-transport-reference.patch; their transport change = what we merged as direct play
  in cd116c4, so only the menu and the invite were new). Built: a "Radmin VPN / Direct IP" card in the title menu (host
  and guest views) opening Host / Join / Back to Steam Invite / Back. Host: our Radmin (26.x, else VPN/LAN) address,
  port 37015, a 64-bit random secret (BCryptGenRandom) -> invite "coopplus1/<ip>:37015/<16 hex>" put on the clipboard;
  Invite Friends becomes Copy Invite while hosting. Join: reads the clipboard (invite or bare address). The session
  restarts on the new transport through a new SessionEnd::Kind::Direct (run_coop); a failed menu session goes back to
  Steam with the reason on the page; Leave / Back to Steam return to Steam; nothing is written to the ini. Security
  (handoff: "an encoded address alone provides no authentication"): UdpTransport::open_direct(local, only_ip, token,
  knock) -- the guest sends a 16-byte knock "CDKNOCK1"+secret before each datagram until the host answers; the host lets
  in only an address whose knock carries the secret, drops the rest unread (counted). Codec header-only
  (net/direct_invite.h) for unit tests: test_direct_invite + test_direct_knock (loopback: wrong secret dropped, right
  admitted, knocking stops after the answer) pass. [dev] direct_loopback=1 hosts the invite on 127.0.0.1 for two copies.
  Two-copy test dr2 (harness: new -LobbyScript 'h=SEQ|g=SEQ|s=N'): Host -> invite on clipboard -> guest Join ->
  connected -> Start Co-op -> Play Together -> both in the world, lag 106 ms, SAVES OK. dr1 FOUND A MENU BUG: a mouse
  resting over a card snapped the highlight back every frame, so W/S did nothing; now the mouse only selects when it
  moves. Known (old): "the other player's horse is not a plain mount load (slot 1)" in every run with the bandits save.
  Not tested: two real PCs over Radmin (needs Plitos or the user).
- 2026-11-07 15:30-15:45 RELEASED 0.1.0-alpha+096d9c3f (user: "republish it once that is complete"): package + unit
  tests, test-install-fake 14/14, Defender clean; GitHub v0.1.0-alpha-096d9c3f Latest (sha256 match: ac0126f2...
  player zip, c767d700... mod-manager); FEATURES / README pushed; Discord: download post + a pinned zips message (the
  first post lost its attachments), announcement, #features line. The game folder now has the 096d9c3f asi (the test
  kit restores it); older local builds (c606602d, f3517194) deleted from dist (on GitHub still).
- 2026-11-07 15:50 DECOMP PASS started (user: "begin decomping the game using Ghidra ... then address any issues ...
  completely independently ... detailed notes ... then a concise list of what changed and why"). Full auto-analysis of
  the 3576 exe in its own project (build/tools/full_analysis.ps1 -> build/tools/ghidra-full; FullOptions.java turns
  off Decompiler Parameter ID / aggressive finder; 20 GB heap, 14 CPUs), then ExportIndex.java (functions, calls, RTTI
  classes + vtables, strings with users) and ExportAllC.java (every function's C, sharded per MB, in build/decomp/c).
  Meanwhile targeted reads with the small project. FIRST FINDING (items, docs/research/reverse-engineering-3576.md
  "Items"): the retail exe switches cheat requests off by a flag at handler+0x21 (the handler answers success and does
  nothing) -- the reason AddMinusItemReq "got no answer" in T18. Its worker 0x2E08220(inventory [[user+0x68]+0xB8],
  &err, item key, count) adds or removes a chosen item with the game's own client update: callable on the server thread
  like the XP grant. Unblocks T18 gifting, T45 stash, T26 penalties.
- 2026-11-07 16:20 CORRECTION to the item finding: 0x2E08220's message is 0xBB6 = MinusItemDataListAck and the tally
  it changes (inventory+0xF8) is a "minus item" list, not the bag -- AddMinusItemReq would not move real items. The
  real bag notifier is 0x2C75730 (writes AddItemToInventoryAck 3110 and RemoveItemToInventoryAck 2421); its caller
  0x2E05550 looks like the add worker; to be confirmed with the full index. Two fixes built (not yet live-tested, the
  analysis holds the RAM): (1) remote_puppet: the starter horse (slot 1) loads as kind 4, the copy check demanded 5
  ("the other player's horse is not a plain mount load (slot 1)" in every two-copy run: the friend's starter horse was
  never shown); (2) horse_diag: guard for the game bug at exe+0x2519762 (action playback reads [rec+0x28] before its own
  `if (rec)`; crash xs2): at 0x251975D with rec 0 the frame word is set to 0 and the reads skipped (0x262BAD0, called
  next, handles rec 0 itself); installed only on the exact 18 bytes.
- 2026-11-07 16:35 User: "I want the most in depth [decomp] that you can get". Plan: the base pass (running) is kept;
  then build/tools/deep_analysis.ps1 on the same database: (1) DeepPass.java -- every analyzer on, including
  Decompiler Parameter ID (real parameters/types) and the Aggressive Instruction Finder, thorough sub-options, full
  re-analysis; (2) Ghidra's RecoverClassesFromRTTIScript (C++ classes, methods in their classes, inheritance);
  (3) ExportIndex; (4) ExportAllC (every function's C). Each step a separate headless run (a failure keeps the rest).
- 2026-11-07 08:15 THE FIRST FULL GHIDRA RUN FAILED: java.lang.OutOfMemoryError after 222 min (20 GB heap; in the
  Address Table analyzer's address-set subtract), and headless saves nothing on a failed import, so all of it was
  lost. Cause: the exe's `.tls$` section (RVA 0x7A44000, 0xBDE1F34 = 190 MB, RWX) is the protector's code (junk flow
  into non-existing memory, the "More than 100 address segments" warning); the game's own code is `.text1` (85 MB).
  Fix: build/tools/scripts/StripProtected.java removes `.tls$` in a separate import run (saved before any analysis),
  then the analysis runs on the saved import with a 24 GB heap (full_analysis.ps1 v2); build/tools/run_all.ps1 chains
  base -> deep steps unattended. Restarted 08:25.
- 2026-11-07 08:16-08:22 LIVE TEST hz1 (two copies, bandits save, this build): THE FRIEND'S HORSE NOW APPEARS. Both
  copies: "Puppet: horse prepared ... loaded with its first action" -> "copy 0xB010F002 made visible"; the warning
  "the other player's horse is not a plain mount load (slot 1)" that every earlier run had is gone (the kind-4 check).
  "HorseDiag: action-playback guard installed" in both (the 18 bytes match on 3576; it did not need to act). No ERR /
  WRN, SAVES OK, lag 54 ms screen-to-copy.
- 0x2E05550 (thought to be the add-item worker) is an item exchange / conversion: its bag add is 0x29EA7C0 with 200-byte
  item records from 0x25FED70 and a long commit chain (docs/research/reverse-engineering-3576.md, Items). Gifting not
  built: too many unnamed steps before the full index.
- 2026-11-07 09:00 SECOND FULL RUN: the analysis without `.tls$` finished in 36 min, then OutOfMemoryError in
  ProgramDBChangeSet.endTransaction (one transaction's change set covering all 85 MB of code). Fix:
  build/tools/scripts/AnalyzeSliced.java -- the analysis in 4 MB slices of `.text1` (then each other block whole),
  each slice its own transaction, saved, undo dropped (heap stays ~300-420 MB of 24 GB); progress in
  build/decomp/sliced-progress.txt, a rerun goes on from the next slice. Base slices 0-22 (all of `.text1` + Headers)
  done 09:02-10:26, ~203,100 functions; slice 11 (.text1+0x2C00000) alone took 48 min (a function-overlap repair).
- 2026-11-07 10:57 Slice 23 (first data block after Headers) threw, and the three retries all stopped there. The real
  exception was hidden: the script had ended GhidraScript's own transaction (the "probe" trick), so GhidraScript's
  end() threw "Transaction not found" over it. Fixed: FlatProgramAPI end(true) at the start / start() at the end, and
  each slice catches its own exception (logged with its stack, "FAILED <exc>" in the progress file, pass goes on).
  Restarted 11:00 from slice 23.
- 2026-11-07 FULL DECOMP FINISHED. Chain after the sliced pass (build/tools/deep_analysis.ps1, logs in build/decomp):
  - Out of memory in AddressTableAnalyzer on `.arch` (a 40 MB data block): data blocks are now sliced to 4 MB too, and
    "Create Address Tables" is off for data slices. Deep mode's WindowsResourceReference looped forever: off.
  - RTTI found nothing at first. Cause: this exe's section names are not the usual ones. Ghidra's RttiAnalyzer looks
    for TypeDescriptors in ".data*" and the type_info vftable in ".rdata*"; here `.rodata` (RVA 0x6CD2000) is really
    the writable data with the TypeDescriptors and `.arch` (0x5597000) is really the read-only data with the
    vftables. Also the analyzer skips itself once the "RTTI Found" program option exists. Fix:
    build/tools/scripts/RttiFix.java renames the blocks by RVA (.rodata -> .data, .sdata -> .data_s, .arch -> .rdata),
    removes "RTTI Found", runs RttiAnalyzer again (326 s): 15,068 vftables. RecoverClassesFromRTTIScript also refuses a
    program not marked analysed (GhidraProgramUtilities.markProgramAnalyzed).
  - Class recovery 14:10-18:30 (4 h 20 min): 16,550 classes, 73,091 member functions assigned, 12,841 constructors,
    730 destructors, 185,273 virtual functions. 13,462 classes are the game's own `pa::` namespace (the rest Havok
    `hk*`, ICU, ...), e.g. pa::NwSyncSession(Pool), pa::VHousingManager, pa::CommonBuffProcessor, pa::ItemInfoManager.
  - Index (ExportIndex.java, 1 min): build/decomp/functions.tsv 204,597 functions (58,346 now with class-qualified
    names; 0 before RTTI), calls.tsv, strings.tsv 78,828, classes.tsv 14,628 vftables with every slot's RVA.
  - C export (ExportAllC.java, 10 threads, 14 min): 191,917 functions in build/decomp/c/000-093.c (415 MB), 91
    failed ("Decompiler process died" on a few huge ones; decompile those alone with build/port3479/dec.sh).
    Each function starts with `// ===== exe+0x<rva> <name> (<bytes>)`, so `grep -n "exe+0xA12C00 " build/decomp/c/*.c`
    finds one.
  - Disk: Ghidra project 6 GB + C 415 MB; 142 GB free on C:.
- 2026-11-07 19:30 CRASH SURVEY with the full index (every "Crash log" in 628 test logs). Caution learned: a crash
  address is only meaningful against the game version it ran on (log line "Host version"); only 3576 is archived,
  so 11-02..11-05 addresses (3479 / 3527 / 3549) cannot be read in the 3576 decomp (my first pass did, wrongly).
  - Current features, 3576: A12E4E (fixed today, c0858a8), 2519762 / 251A1AD (FUN_142518850), 96918B, 976921: all
    guarded already.
  - Dev experiments only: qs-t1 and the qs-* set (3479, save-data load tests), dt10 (3549, re-addressing the death
    notice; a dead end noted 11-05), fscan1/2 and dtscan (3527 scans).
  - late1 (3479, 11-03): a game crash (exe+0xBA8D2A, reading 0x8) 71 ms after the guest started the copy of the
    host's boss cutscene. late2 / late4 / late5 ran the same sequence (same stage, same timing) without it; not
    seen since. NOT exercised on 3576 yet: no cutscene copy in any 3576 log. Left as a test to do.
  - dragon7 (3479, 11-02): a game crash in the inventory push path while testing the dragon; not seen since.
  - wr1 (3549, 11-05 03:30): the one crash inside cdmp.asi (+0x3B4591 reading 0x1400160) in 628 logs, right after a
    load, with the PC out of memory (94 % used, page file 0.46 GB free, two copies running). Rebuilt that commit
    (0364c3f) for its PDB: the stack's return address is in puppet_visibility's g_watch.clear() (under its mutex),
    but the crash address itself lands in a destructor funclet, so the rebuild's layout is not exact; no conclusion.
    197 later logs: no crash inside the mod.
- 2026-11-07 18:55 RELEASE CANDIDATE 0.1.0-alpha+4effc5c0 PACKAGED, NOT PUBLISHED (publishing needs the user's OK):
  dist/CrimsonDesert-SeamlessCoop-0.1.0-alpha+4effc5c0.zip (+ mod-manager zip, private symbols zip); unit and
  roomcodec tests pass. Over 096d9c3f: the FPS fix (c0858a8), the A12E4E guard where it really is, the enemy
  hidden-flag watch off by default, horse copies of kind 4 or 5 in any mount slot (ba41644), the starter horse and
  the exe+0x2519762 guard (588df1b). Not live-tested: a slot 2/3 horse (calling a horse needs a person: dev pad
  presses do not reach the game) and the cutscene copy on 3576.

## Bug reports 2026-11-07 (user: "Frame drops seem to be a rather big issue now")
Reports kept in build/reports/ (not in git): borque-2026-11-07_13-28-50 (Borque's CoopPlus report: build 096D9C3F,
Steam, Hernand plains) and plitos-C606D401-FPS-Test (Plitos' "C606D401 | FPS fix" test build on c606602d: a
200 KB patch with many features; its FPS parts: packet replay 16 packets / 2 ms per real packet, the enemy relay's
watch_hidden() scan only with diagnostics, enemy retries every 33 ms, DX12 overlay skips its draw instead of waiting).
- Borque, FPS from the overlay's present counter (presents per 5 s): ~90 fps solo; the session starts 13:10:34 and
  by 13:11:34 the game runs 27 fps, ~25 for minutes, 3-4 fps 13:24-13:26, 55 after. Our network loop at the same
  time: normally ~1000 loops / 10 s (6 ms waiting for input, 3 ms work); from 13:11:30 ~30 loops / 10 s, 300-450 ms
  of work each, "waiting for input" 0 -- busy all the time. Its sections grew together: coop-update 0 -> ~180 ms,
  state (local.sample + HUD) 0 -> ~60 ms, activity 3 -> ~40 ms; already while our own player was still loading (no
  copies yet). Borque's "+150 latency" is this: rtt 300+ ms because the loop runs every ~330 ms.
- Lead: every game::safe_read calls VirtualQuery (231 call sites). VirtualQuery takes the process's address-space
  lock that every VirtualAlloc/VirtualFree of the game's threads takes too; while the game streams the world, our
  reads and its allocations wait on each other (slowmo.cpp's note: one VirtualQuery per packet made a load arrive
  ~50 s late). A loop that reads more each second makes the game's frames wait more: both sides slow.
- Borque's crash: exe+0xA12E4E reading 0x115 again (thread 20044), with the action-events guard installed and working
  (5500+ waits). 1.4 s after the enemy copy 0xB01F0008 was reloaded "with no action": a race -- the guard's check and
  the game's read are on different moments; a copy reload in between leaves the action null.
- 2026-11-07 14:15-14:35 FPS FIX built and measured (two copies, bandits save, same settings as hz1; Ghidra's class
  recovery running in both measured runs):
  1. game::safe_read (safe_memory.cpp): no VirtualQuery per read any more -- a per-thread cache of 32 ranges already
     found committed + readable (1 s life); the copy stays under __try (a range freed since: caught, forgotten); a
     guard page met anyway gets its guard back. New line every 10 s: "Timing: guarded reads, 10 s: N from the range
     cache, M VirtualQuery calls, K faulted". fps1: ~1,000,000 reads per 10 s from the cache, ~3,200 VirtualQuery,
     0 faults (the old build made ~1,000,000 VirtualQuery calls per 10 s, many on the game's own animation threads:
     the HorseDiag guards read the CC of every character every frame).
  2. Enemy relay: watch_hidden() (a test aid: two actor lookups per known enemy every 2 s, log only) runs only with
     [dev] enemy_relay (as Plitos' C606D401 did).
  3. Crash guard: exe+0xA12E4E (Borque's crash) is in exe+0xA12C00, called by the CC tick 0x9451E0 -- NOT in
     0xA128C0 where the earlier guard sits (the full index showed it). copy_tick_would_fault() mirrors the game's own
     condition in that loop (layers 0-1: flag byte +0x108 without bit 2, bit 1 or the layer's queued entry
     [cc+0x80]+i*0x78 valid, current action [layer+0x10] null) and skips that copy's tick only then. Did not fire in
     fps1 (no false waits).
  Results, the focused copy late in the session: old build (control ctrl1, same conditions) host 21-26 fps, network
  loop 94-100 ms of work per loop; new build (fps1) host 92-94 fps, guest 90-96 fps, loop ~2 ms (coop-update 70 ms
  -> 0.0). hz1 (old build, this morning) had the host at 30-44. The unfocused copy is now ~13 fps (old: 25-67): one
  GPU for two games -- the focused copy no longer starves itself, so it takes most of it (total ~105 vs ~90 frames
  a second); not an issue with one game per PC. fps1's lag line (136 ms) was measured on that background copy;
  ctrl1's 56 ms. SAVES OK both. No WRN/ERR in fps1; player, horse and enemy copies loaded.
- 2026-11-07 14:45 More from Borque's log: (a) "the other player's horse is not a plain mount load (slot 3 / slot 2 /
  slot 1)": his build (096D9C3F) predates the kind-4 fix, but it demanded kind 5 for slots 2-3 and refused those too
  -- so owned horses there are not kind 5 either. Now kind 4 or 5 in any mount slot (slot range, keys and "nothing
  of ours" still checked); a refusal logs the kind. Not testable here (the test saves have the starter horse only).
  (b) The partner's character was sent 11 times in 13 min: each after a reconnect (the session dropped 4 times
  while our loop took 300+ ms); 5 refusals ("not a plain player load") fell in the 3-4 fps stretch. (c) 13:16:42 ->
  13:18:29 both games acted as host after a reconnect (Borque's host got 1500+ EnemyAct/EnemyPos "only the other role
  sends") until he joined the other world by hand. Left as is: a consequence of the drops; more data needed.
- 2026-11-07 ~19:20 RELEASED 0.1.0-alpha+4effc5c0 (user: "go ahead and publish what we have now"): test-install-fake
  all steps PASS, Defender clean; GitHub v0.1.0-alpha-4effc5c0 Latest (sha256 1481163e... player zip, 44d536f5...
  mod-manager, match the local files); Discord #download-here post with both zips, pinned; #announcements "Update
  0.1.0-alpha+4effc5c0: frame drop fix" (texts: docs/release/discord-update-4effc5c0.txt). CHANGELOG entry added.
- 2026-11-07 19:30-20:10 BIG UPDATE B01 (native shared world), first runs on 3576 with today's Co-op+ (FPS fix etc.):
  - tcB1: the native host installed NOTHING ("another process of this role runs the mod"): Co-op+'s two_copy.cpp
    (added 11-06 for the old-base harness) takes Local\cdmp_role_<role>, the native cdmp.asi's own role mutex, and
    coopplus.asi loads first in the same process. Fix: the layer's lock is Local\coopplus_role_<role>
    (two_copy::extra_process(role, kNativeLayer)); and on the native base the layer no longer needs [dev] coop_world
    (the native save redirect covers both roles' saves; the layer's save systems are off there).
  - tcB2: the guest went REMOTE (in the host's world), but neither game loaded: Co-op+'s co-op title menu
    (coop_lobby.h, added 11-06) covers the title and takes the harness's Continue key; later presses hit "Leave the
    Party" on the guest (its layer then tried to host on 37115, taken by the host: "Cannot open UDP socket: WSA
    error 0" -- the error number is lost, to fix). Harness (build/native/twocopy.ps1, copy in
    tools/native-port/test): with -CoopPlus, Esc closes the layer's title menu first.
  - tcB3: WORKS on 3576: guest REMOTE, both walk (positions both ways), host to the dock and fights, revive step,
    the Co-op+ party frame / nameplate / chat ("Host: hi") on both (shots build/native/shots/tcB3-34, -64), SAVES OK.
  - Design facts for B01's join flow: the guest's client decides remote or local per connection, at its first
    packet (ConnectCryptogramToServerReq 2493), i.e. at the title before a save is chosen; guest and host stages are
    process roles set at start (cdmp.ini [session2] guest=1 / accept_tunnel...). The native host never writes the
    real save folder (save_guard=redirect sandbox; host_save sandbox|export, "real" refused): where co-op progress
    lives is the open design item (Co-op+'s co-op worlds: one save per friend). User 2026-11-07: joining only from
    the main menu is fine (B08 dropped).
- 2026-11-07 20:15-20:25 B01 co-op saves on the native base (design docs/design/native-join-and-saves.md):
  native (upstream/cdmp-native, local branch coopplus-3549, bundle refreshed): [session2] coop_save_dir (relative =
  under %LOCALAPPDATA%\Pearl Abyss\CD) + coop_save_seed keep|copy|new: the host's redirect sandbox and the guest's
  redirect go there and are KEPT between runs (copy/new set an existing folder aside as <dir>_old_<time>, never
  deleted; new = without slot folders); also on a real PC (no CDMP_ROLE) with [session2] enabled=1; only the first
  process of a run seeds (a mutex per folder: the game's short-lived second process keeps). tcB4 (-HostCoop
  coop_test\host -HostSeed copy -GuestCoop save_cdmp_guest -GuestSeed keep): "copied from the save folder (15
  files, 7 folders)", the whole run redirected there (writes 0 to the real folder), guest kept + REMOTE, SAVES OK.
  Co-op+: net/native_mode_text.h (the native cdmp.ini per mode, header-only, unit-tested: Solo / Host / Guest,
  Steam or udp, co-op folders coop_worlds\with_<id> / guest_of_<id>, hostile values cannot add lines) and
  net/native_mode.{h,cpp} (write the ini UTF-16, keep a hand-made one as cdmp.ini.before-coopplus, restart the game
  through a hidden helper: Steam, or the exe itself with CDMP_ROLE). Not wired into the lobby yet.
- 2026-11-07 20:30-20:47 B01 JOIN FLOW (title screen, restart into the native modes), two-copy harness -Lobby:
  - tcB5: the host wrote its native Host ini and closed, but never came back, and the guest never saw the host's
    message: the new lobby messages 7/8 collided with prologue_hold.h (7) and xp_share.h (8), which coop_worlds.cpp
    routes first. Now 20/21. The restart helper logs to bin64\coopplus.restart.log and tries to break away from the
    game's job (the game IS in a job; breakaway is refused, the helper survives anyway).
  - tcB6: WORKS up to the start: host Start Co-op -> Start From My Solo Progress -> host ini (udp, co-op save
    coop_worlds\with_127_0_0_1, copy) -> host restarts (3 s) -> its layer reconnects (resume) -> tells the guest -> the
    guest writes its Guest ini and restarts -> reconnects -> its native client goes REMOTE (in the host's world) ->
    "both games are in co-op" -> 3 s countdown on both. Play was NOT pressed: the press waits for the game window in
    front, and the user's Discord window was in front (the user is at the PC: in-game tests paused). SAVES OK.
- 2026-11-07 21:00 tcB7: THE TITLE-SCREEN JOIN FLOW WORKS END TO END (two copies): Start Co-op -> host restart ->
  guest restart -> guest REMOTE -> countdown -> Play on both (each window in front: the harness brings them forward;
  a player's game is in front anyway) -> both in the shared world ("in the shared world; the next launch is solo
  again": cdmp.<role>.ini back to Solo for the next start), shot tcB7-L5 (the guest's game, party frame "Host 0 m").
  SAVES OK. Also: Radmin VPN / Direct IP keeps its knock token and port across the restart ([native_resume] direct*),
  and over udp the native tunnel takes the Co-op+ port + 1 (Co-op+'s direct play already uses 37015). Not tested:
  Steam (needs two PCs / two accounts), Radmin across two PCs.
- 2026-11-07 21:10 tcB8 (the latest build: native tunnel on the Co-op+ port + 1): the same full flow passes; both
  inis back to "solo (the player's own game)" once in the world. SAVES OK.
- 2026-11-07 21:10 BUG REPORT (user, from playtester Rahat on Discord, released 0.1.0-alpha builds): "the quest right
  after the prologue: you go to the tavern to arm wrestle a guard but the guard doesn't appear" (both players got that
  far). Same family as the first playtest's "host's NPCs did not load in the first town" (never reproduced: the host's
  log was lost) and T21 (the arm-wrestling table not offered when the friend's copy arrived with us). Likely cause on
  the old base: the friend's copy is a player-type actor in each world; the quest's stage / trigger setup counts it
  (or binds it) and the guard is never spawned. Not reproduced: no test save is at that quest (build/test-saves has
  none between the prologue and Hernand's inn). The shared-world build (B01) has no copies, so it should not have it.
  Old-base mitigation to test once a save exists: no copy of the friend near a running story stage.
- 2026-11-07 21:15 B01 packaging: tools/package-native.ps1 (the shared-world zip from the normal package's stage:
  native cdmp.asi, coopplus.asi, the player's settings as coopplus.ini, a Solo cdmp.ini, source-native.zip);
  installer: the shared-world payload installs coopplus.asi/.ini, moves an older Co-op+'s cdmp.ini settings to
  coopplus.ini, cdmp.ini is Co-op+'s launch file (replaced on update); vanilla parks coopplus.asi too; uninstall
  removes coopplus*.log. The layer's Steam link uses channel 68 on the native base (the native tunnel has 67).
- 2026-11-07 21:25 B01 packages built (not published): dist\CrimsonDesert-SeamlessCoop-0.1.0-alpha+da1436e9.zip (the
  normal Co-op+) and ...-da1436e9-shared-world.zip (11.7 MB: native cdmp.asi + coopplus.asi + coopplus.ini + Solo
  cdmp.ini + source-native.zip). test-install-fake: ALL STEPS PASS on both (the shared-world one also: coopplus.*
  installed, Solo cdmp.ini, settings kept in coopplus.ini on update, the stagger migration there, coopplus.asi
  parked in vanilla, uninstall leaves bin64 exactly as before). Not covered yet: an old Co-op+ install updated to
  the shared-world zip (cdmp.ini settings -> coopplus.ini). Steam transport and two PCs: untested (one SteamID here).
  ROADMAP B01 -> [~]. Test leftovers under %LOCALAPPDATA%\Pearl Abyss\CD: coop_test\, coop_worlds\with_127_0_0_1*
  (copies made by the tests, not the real saves).
- 2026-11-07 21:30 tools/test-install-upgrade.ps1 (fake folder): the normal da1436e9 zip installed, the player's
  join=invite set in cdmp.ini, then the shared-world zip over it: "settings moved from cdmp.ini to coopplus.ini",
  coopplus.ini keeps join=invite, cdmp.ini is the Solo launch file, coopplus.asi in, uninstall leaves bin64 exactly
  as before the first install: ALL PASS. (A first try refused: "Crimson Desert is running" -- the tcB9 copies; the
  installer's own guard, as intended.)
- tcB9 (-HostVariant rps): the rps save also starts at Nas River (its tests teleported to Hernand). Harness:
  -HostScript / -GuestScript run a Co-op+ dev script (Ctrl+Shift+F6) after the join (tcB10: both teleport to Hernand).
- 2026-11-07 21:30 tcB10 (both run live_tp_hernand.txt, Ctrl+Shift+F6): the dev script ran in both layers but nobody
  moved: on the native base the layer has no path to put a server message INTO its own client
  (puppet_inject_s2c_batch queues for the copy machinery's injector, which is off there; only the c2s bridge
  cdmp_inject_c2s exists). Affects every Co-op+ feature built on s2c injection: dev teleports, "go to my friend" /
  "bring my friend" hops (DoTeleportAck 3052), the revive's move to the partner. To do with B19 (party travel) /
  B17 (revive): a cdmp_inject_s2c export from the native base's client delivery (the guest's OnRecvPacket path),
  or travel through the native fast-travel funnels. B02 (NPCs) not compared yet: no town test save; ByJunter's
  README lists "NPCs, schedules and travellers in the shared field" as validated.
- 2026-11-07 21:45 HOTFIX for the playtester's report (Rahat, second message: "my friend's game would stop and the
  cutscene to end the prologue wouldn't play and would get skipped and then he would be stuck with the go to hernand
  quest"; the missing inn guard follows from that). Reading of the code: the skip vote cannot skip a cutscene alone
  (both must press; reset per cutscene). The stage cinema relay (D38) starts a CLIENT-ONLY copy of the host's stage
  cutscene in the guest's game when the guest is within 100 m and not in a cutscene of its own; at the end of the
  prologue the host reaches the final cutscene first, the guest's game plays the host's copy, and the guest's own
  server never runs its own ending: the guest's story stays at "Go to Hernand". The prologue hold should have kept
  the relay off, but its "out of the prologue" test is a mission count (60, never measured against a real prologue
  end). The relay was never tested on 3576 either. Fix: [coop] stage_cinema off by default (config.cpp / .h), FEATURES
  and CHANGELOG say so. Not confirmed with the players' logs (asked through the user). Native base: the relay is off
  there anyway (native_layer.h).
- 2026-11-07 21:40 tcB11: with cdmp_inject_s2c the layer's teleport WORKS on the native base (the host's dev script:
  "Local: pos (-10611.27, 608.70, -3873.23)" = Hernand, from Nas River). SAVES OK. So "go to my friend" / "bring my
  friend" hops and the revive move can work there too (to check in B19 / B17).
- 2026-11-07 21:50 Hotfix 0.1.0-alpha+862b4256 PACKAGED, NOT PUBLISHED (needs the user's OK): unit tests pass,
  test-install-fake all steps PASS, Defender clean. Texts: dist/notes-862b4256.md, docs/release/discord-update-862b4256.txt.
  Regression run hf1 (old base, two copies) started.
- 2026-11-07 21:45 hf1 (old base, two copies, build 862B4256): connected, both copies visible, no WRN/ERR beyond the
  known clock line, guarded reads 0 faulted, SAVES OK. The hotfix is ready to publish (waiting for the user's OK).
- 2026-11-07 22:00 RELEASED hotfix 0.1.0-alpha+862b4256 (user: "Once the regression run passes, publish the fix"):
  GitHub v0.1.0-alpha-862b4256 Latest (sha256 13c9e2d4... player zip, 0b886165... mod-manager, match); Discord
  #download-here post with both zips (text typed first, zips attached last, 2 checked before sending), pinned;
  #announcements "Hotfix 0.1.0-alpha+862b4256: the end of the prologue". The game folder has the hotfix asi; the test
  kit restores it. publish-playtest.ps1 note: notes with double quotes must be passed in-process (& script -Notes),
  a nested powershell -File splits them at the quotes ("got 'to'" as the repo).
  User's extra facts: the inn-guard report was a Radmin session, the cutscene one a Steam invite. The relay does not
  depend on the transport; if the Radmin session's prologue ending was NOT skipped, the guard is a separate issue
  (T21-like: a copy arriving with us at the inn) that this hotfix does not touch.
- 2026-11-07 21:58 tcB12 (native base): the host's dev teleport to Hernand, then the GUEST pressed Ctrl+F4 ("go to my
  friend"): "Co-op: to the other player at (-10611, -3873)" -> hops through cdmp_inject_s2c -> "Teleport: arrived"
  in 7 s; shot tcB12-39 (the guest's game: Hernand Town, the party frame "Host 6 m"). SAVES OK. B19's "go to my
  friend" works on the shared world. Cosmetic: the layer's status line still says "other player not loaded" (no copy
  on the native base). Harness: -GuestKeys (steps joined with _, bash splits PSARGS at spaces).
- 2026-11-07 22:08 tcB13 (shared world, Hernand inn): the host's dev teleport to the arm-wrestling table
  (scripts/tp_inn_table.txt), the guest's Ctrl+F4 to it (arrived 1 m away), the host held R + turned 16 times.
  - B02 FINDING: the HOST sees the inn full (two guards beside it, patrons, "Stranger: Am I that good-looking?"),
    the GUEST 1 m away sees NOBODY, still a minute later (shots tcB13-38/39 host vs guest). The host's native log:
    "Session2 observer hold family: held member 0xB0100A55 (type 7, parent 0xB0100A54; host gets it now) from
    session ... (user 2 ...): its parent is withheld there" -- the native base's observer holds ([session2]
    observer_hold / observer_hold_family / observer_hold_acks / character_hold, on in the host profile) keep actor
    loads from user 2. Next: which hold withholds the inn's NPCs and why (ByJunter's notes, Spanish:
    upstream/cdmp-native/docs/research/opcion-d-*.md), and whether a guest that WALKS in gets them (a client-side
    teleport may skip the field / region steps that load NPCs for user 2).
  - B14: no InteractionDoStepReq in the host's packet log (cdmp_packets.host_20261107-215955.log); inconclusive:
    on 3576 the interact prompt shows E (R was 3479's), and the turn steps may not have faced the table.
  - Reading child_hold.h: observer_hold keeps an NPC from user 2 only while its restore byte is 0 (no first action);
    the host's client gives every NPC it controls its first action ~0.16 s after it appears, then a native re-announce
    goes to user 2. So a permanently empty inn points elsewhere: the family hold logged "its parent is withheld
    there" (parents never announced to user 2). Lead: the guest arrived by a CLIENT-side teleport (Co-op+'s DoTeleportAck
    hops); the native base moves user 2 through the server (its fast-travel funnels), so the server's announce of
    the inn sector to user 2 may never have happened properly. Next test: the guest walks into a busy place
    (no teleport), and/or Co-op+'s "go to my friend" uses the native fast travel on the native base.
- 2026-11-07 22:20 More on the empty inn (tcB13 logs) and tcB14:
  - The host's server DID announce to user 2 after its arrival ("to user 2 with the host's key: 2005x1373", "user 2
    move phase STABLE"), yet the guest drew nobody; and from the arrival on, the guest's layer logged "Local:
    unavailable (physics object missing)" (29 times): the guest's own character is in a broken state after the
    CLIENT-side teleport (Co-op+'s DoTeleportAck hops into the guest's client). On the native base the server owns
    user 2's position: moving the guest must go through the server (the native fast-travel funnel / a server move of
    user 2), not through its client. So "go to my friend" works for the HOST's own move (its client and server are
    one) but needs a native path for the guest. Same for B02's check: a fair NPC comparison needs the guest to
    arrive natively (walk / fast travel).
  - tcB14 (the guest walks into the inn from 7 m out): camwalk's walker climbed the outside wall (stuck at y 621.7):
    no answer. Harness: -GuestWalkTo x_z.
- 2026-11-07 22:30 tcB15: tried a SERVER move for "go to my friend" on the native base: AITeleportReq 3040 (template:
  the host client's own AITeleportReq for its NPC 0xB01006FF, recorded: u32 actor | ..., the position at +196) with
  our player key. The server answered AITeleportFailAck 3150 (key, key) every time: that request is only for AI
  actors (the handler needs the actor's AI component, [actor+0x68]+0x58), not for a player. Reverted (no code kept).
  State: the host's own move works with the client hops (tcB11/12); the GUEST's breaks its character
  ("physics object missing", no NPCs drawn: tcB13). Open: a native way to move user 2 (its fast travel to the
  nearest unlocked point, then a short hop; or the server-side move the fast-travel funnel uses).
- 2026-11-07 22:45 NATIVE PORT with the decomp: user2_funnels F3 (user 2's fast travel on the host's server) was still
  on 2976's code ("the 2283 handler's vf48 call 0x2A5F7F9 ... is not the expected code of build 2976; REFUSED" in
  every 3576 run). 2976 byte patterns did not match; the decomp did it: TrocTrReloadStageStartReq's handler (classes.tsv
  slot 3) = 0x2CEE250, its `call [rax+0x48]` at 0x2CEE4F2 (ctx 0x2CEE4EB, return 0x2CEE4F5) then `call 0x2AF4690`
  (the apply function, 2976 0x2896120; r13 = [rcx+0x18] at +55); the second vf48 is in 0x2E680F0 (2976 0x2BC4950;
  calls.tsv: called by 0x2AF4690) at 0x2E682E3 (return 0x2E682ED; the cleared field moved +0x6A0 -> +0x6D0). Native
  commit 161c23a. tcB16: "User2Funnels: user2_fast_travel 1 LIVE" (first time on 3576).
  - Tried for "go to my friend" (guest): a bare ReloadStageStartReq 3074 (a 0, FieldInfoKey 1, actor 0xA0100001 = the
    host's character, pos/rot/scale) from the guest's layer: the server took it (F3: "user 2's manager 2") but the guest
    got no MoveFieldAck: in a real fast travel the CLIENT starts its own loading (its fast-travel entry, 2976 0x655110,
    sends the 2283 and then 2437...). Reverted (a bare one leaves a destination set on the server). Next: call the
    client's fast-travel entry on 3576 (find it with the decomp: the caller of the 3074 writer) with the host's actor.
- 2026-11-07 23:00 CORRECTION: "Local: unavailable (physics object missing)" is in EVERY native guest log (tcB3 79,
  tcB9 51, tcB12 51, tcB13 59 lines, no teleport in tcB3/tcB9): the native base's own reader, not a sign that the
  client hop broke the guest's character. The empty inn (tcB13) is still unexplained by it; the hold logs remain the
  lead. tcB17: the guest's "go to my friend" through the client's own fast travel (native ui_pause detour runs
  0x7221F0(ctx = [[[0x71C0158]+0x90]+0x1240], -1, -1, 0xA0100001) on the game thread, export cdmp_guest_travel_to;
  the layer asks it): the layer said "asked", but the native side logged nothing and the guest did not move;
  tcB18 adds a line where the request arrives.
- 2026-11-07 23:05 tcB18: the native cdmp_guest_travel_to request arrives and runs on the game thread, but the client's
  fast travel 0x7221F0(ctx, -1, -1, 0xA0100001) returns al 0: its target check 0x2320500 (decomp) refuses a remote
  player (it validates the target actor's class data +0x4B, its field, its user ...; meant for the player's own mount /
  companion). Reproducing the rest of the success path by hand (the loading UI 0x854B40 with 0x905, then 0xD32E10's
  3074) was judged too risky. Kept: the native export (logs, harmless). The layer's guest move went back to the client
  hops, which DO move the guest (tcB12; the "physics object missing" sign was a false lead). Open: the guest's empty
  inn (tcB13) and a native move for user 2.
- 2026-11-07 23:10 The empty inn for the guest (tcB13), with its packet log: during the hops (t 174-182 s) the guest got
  ~900 ActorLoadDataAck and ~1100 RemoveActorAck; after arriving at the inn almost no loads (5+22+2+4 per 30 s) and
  steady removals. The host's game drew the guest AT the inn (RemoteInterp: local (-660.5, 617.0, -830.0)), so the
  server knew where it was; Nas River and the inn are in the same sector (-10, -3), so the server's sector-change
  announce (OnActorSectorChanged -> NotifyAppear for the new sectors) never ran for the inn's actors, and the observer
  holds logged parents "withheld there". Next for B02: the native announce of nearby actors to user 2 within one
  sector (ByJunter's opcion-d-campo-compartido.md / observer_hold), e.g. a re-announce when user 2 moves far inside
  a sector.
- 2026-11-07 23:15 tcB19 (host alone at the inn, E held + turns; the guest at Nas River): on 3576 the interact key is
  E: 14 InteractionPreemptionStepReq + DoStepReq, all on the inn's fire / lantern objects (0xB010088D, interactions
  0x000F4266/7) and three 0xB01005xx objects, none on the arm-wrestling table (3479's interaction 0x000F431C): the
  teleport spot (pub.sh's -10661.5, -3830) does not face the table on 3576. The host's inn is full of NPCs (shot
  tcB19-38c). B14 needs the table's spot and facing on 3576 (or a walk to it) before the shared-world check.
- 2026-11-08 00:00 THE GUEST'S NPCs (top priority, user: "we can't ship a co-op mod that strips the guest from having
  their NPCs showing and interact-able"; ship rule: nothing of the big update ships until every pick works):
  - tcB20 with the host's observer holds OFF (observer_hold / _family / load_defer / _acks = 0, harness -HostSet): the
    GUEST CRASHED ~1 min after joining, before any teleport: AV at exe+0x2519FC4 (read of 0x28) on an NPC 0xB01002AE
    controlled by the host with "L0 chart 1 cur 0 play 0" (a chart, no action yet) -- the very state the holds exist
    for; 0x2519FC4 is in 0x2518850, the function Co-op+ already guards at 0x2519762 / 0x251A1AD on the old base.
  - The empty-inn run's hold counters (tcB13): observer hold "NPCs held 1198 ... released 0 ... dropped 1198": NO held
    NPC was ever released to the guest on 3576. Logged drop reasons (first lines only): despawned 121, observer out of
    range 42, observer gone 2. Checked with the decomp: the actor map walk (0x98/0x9C/0xA8/0xB0, ServerActorManager
    vfunction6 0x2D11AA0) and the sector observer list (+0x68/+0x70, 16-byte entries, in 0x2D8FA00 which calls
    NotifyAppear 0x2D97820) are unchanged on 3576; actor key +0x60 unchanged. Native: counters of every drop by reason
    in the summary ("drops by reason"); tcB21 runs with it.
- 2026-11-08 00:20 FOUND AND FIXED the guest's missing NPCs (native base on 3576): tcB21's new per-reason counter:
  "drops by reason: despawned x23, observer out of range x1028" (1051 held, 0 released). A one-time live probe of the
  first "out of range" NPC (tcB22): "TS +0x540 sector 0x0: observers none | FOUND the observer in the list of TS+0x548
  (2 observers)": on 3576 the TransformSync's current sector is at +0x548 (2976: +0x540, the only use of it:
  child_hold.h kTsSector). Every held NPC looked "in no sector, observed by nobody" and was dropped instead of
  released, so the guest never got the NPCs around it. Fixed (native commit). tcB23: "NPCs held 291 ... released 262
  in 137 NotifyAppear (mean 5.8 s, max 12.2 s) ... drops by reason: despawned x25", no guest crash, SAVES OK. (The
  harness then lost focus; tcB24 re-checks the inn with screenshots.)
- 2026-11-08 00:30 With the NPCs released (tcB23/tcB24), the HOST crashed on its own teleport into the inn, the same way
  both times: AV at exe+0x249500A reading 0 (0x2495000, a component's serializer: *param_1 of a null entry) from
  0x22E32A0 (an actor component's array of 0x38-byte entries, entry +8 null) from NotifyAppear (stack exe+0x2D979D4)
  called by the native base: a HELD NPC's release re-announcing an actor that the host's teleport was tearing down
  (the harness then "lost focus" to the crash reporter). Before the fix no release ever ran, so it never showed.
  ByJunter's own answer exists: [session2] notify_thread_rule=1 (releases call NotifyAppear only on a thread the game
  uses for NotifyAppear, so they cannot race the server's own actor work); off by default. tcB25 runs with it.
- 2026-11-08 00:40 tcB25 (notify_thread_rule=1): the same host crash (0x249500A) -- not a thread race. With the decomp:
  0x22E32A0 is slot 25 (+0xC8) of three vtables, the Client/Common/ServerInteractionActorComponent ones (0x594E6F0,
  0x5CCB990, 0x5EF4338): the interaction component's serializer; it walks its 0x38-byte entries and passes entry+8 to
  0x2495000(slot, writer), which writes a "present" byte (*slot != 0) and the slot's data. An entry with +8 = null
  crashes it before the byte. 0x3DEEF0 (next) only indexes a static table. Native fix: an inline guard at 0x2495000
  (host, prologue checked) writes a null slot as the game writes an empty one (the byte 0): the stream to user 2 keeps
  its shape. These actors only reach that serializer for user 2 now that held NPCs are released (before the sector
  fix none were). tcB26 checks it.
- 2026-11-08 00:45 tcB26: THE GUEST SEES THE NPCs. With the sector fix (+0x548) and the slot guard: "Session2 slot
  guard ON"; the host's teleport into the inn did not crash; observer hold "NPCs held 1017 ... released 993 ...
  drops by reason: despawned x17"; the guest's shot tcB26-39 shows the inn's people (the masked guard, the man at
  the fire, the man with the headband: the same ones in the host's shot). SAVES OK. Next: the guest INTERACTS with
  them (talk, the table) and a longer town session (no crash over time).
- 2026-11-08 01:00 User, on the guest's shot: "the NPCs ... are all standing in the same A-Pose, not actually
  detecting the player at all". Confirmed: (1) the guest's packets carry the inn NPCs' actions (tcB26: the 58 NPCs
  loaded last got 8-19 StartActionAck each) but its native remote update guard held them: "update skipped 247152 ...
  actors held 623" (CharacterControl Update/PostUpdate return at once = no animation = the bind pose). Its test (a
  layer with a chart and no current action/playback, 2976) is cruder than the game's own per-frame layer rule on 3576
  (0xA12C00: skip flag-2 layers and not-started layers with an empty entry; only a layer it really reads needs an
  action) -- the rule Co-op+ already ported for the A12E4E crash. Native remote_without_action now uses that rule
  (tcB29 checks: poses, held count, no guest crash). (2) tcB28: the guest pressed E ten times at the inn: no
  Interaction request at all and no prompt on its screen (the host's shows "E Turn on" at the fire) -- next.
- 2026-11-08 01:05 tcB29 (the game's layer rule in the guest's guard): "update skipped 0 post-update 0 | actors held 0"
  (tcB26: 247152 / 623) -- every remote NPC is updated (animated) on the guest now; no guest crash, SAVES OK. The
  interaction check is still open: Ctrl+F4 put the guest in a corner facing a wall (shot tcB29-38b), so no prompt
  was expected; needs the guest placed at the fire / an NPC (the host interacted with the fire from the dev spot in
  tcB19) -- next.
- 2026-11-08 01:12 tcB30 (both players teleported to the inn table spot, the guest pressed E 12 times, turning
  between presses): the guest's NPCs are posed and animated (no bind pose: shots tcB30-38/39 guest), and the guest
  gets the game's own interaction prompts: "Fire / E Turn off" at the hanging lamp (shot tcB30-38b). On the host,
  the hearth is dark at 10:56, lit at 11:08, dark at 11:22 (tcB30-38/39/40 host) while the host pressed nothing:
  most likely the guest's E toggled it through the host's server. Not proven yet: the host's user 2 Req summary
  shows only the top 8 opcodes; native user2_funnels now always prints user 2's interaction Reqs (3008 / 3073 /
  2887) -- tcB31 checks. SAVES OK.
- 2026-11-08 01:25 tcB31 (tcB30 again, with the new counter): the host's server got EVERY guest E press as a native
  interaction: "interaction 3008x12 3073x12 2887x0" (InteractionPreemptionStepReq + InteractionDoStepReq from user 2,
  no reset) -- the guest's interaction path to the host's world works. Guest "actors held 0", SAVES OK. The guest's
  prompt is the hanging lamp ("Fire / E Turn off"); 12 presses = an even number of toggles, so no visible change is
  expected at the end. tcB32: the same turns with no E, then ONE E on the lamp, both screens compared.
- 2026-11-08 01:40 tcB32/tcB33 (the same turns, then ONE guest E at the hanging lamp): the native guest trace
  (guest.cpp interaction_trace, one line per interaction packet) shows the whole exchange WORKS:
  out 3008 PreemptionStep (guest character 0xA01001B4, target 0xB0100879) -> in 2120 PreemptionStepSuccessAck
  (33 ms) -> out 3073 DoStep -> in 2794 DoStepSuccessAck (33 ms). But no 2980 DoStepDoInteractionSuccessAck ever
  came, and the lamp stays lit on both screens ("E Turn off" still offered). The server's DoStep handler
  (0x2C70EA0) hands off to an obfuscated thunk; the real interaction runs from a server timer
  (TrocTrInteractionDoStepDoInteractionOnceTimer 0x2ED8DD0, on the interacting actor's interaction component
  +0x68 -> +0x98). tcB34: the sniffer logs the same packets on the HOST's client (its own E at "Stranger / Greet")
  to compare the host's sequence with the guest's.
- 2026-11-08 01:55 CORRECTION (user, on the guest's shots: "The NPCs are still in A-poses for the guest"): right --
  my tcB30 note "posed and animated" was wrong. Zoomed (tcB33-38b guest): the man in the long coat stands with his
  arms out (bind pose); at the hearth the man in white stands straight on the guest while the host's shot shows him
  bent over the fire. The guard fix (tcB29: held 0) only stopped the guest SKIPPING the update; the game's own layer
  rule (0xA12C00) then skips those layers itself because they were never started: the guest's NPCs have no action.
  Both native guest guards skip nothing (RemoteUpdateGuard "update skipped 0", puppet layer guards "skipped 0").
  Lead: an NPC that began its action before user 2 arrived gets no StartActionAck later; its state must come with
  the appear (restore byte 1 + the action load data -> the client's RestoreActionsFromLoadData). On 3576 that
  function is 0x949EB0 (2976 0x8646D0; RestartLayerAction 0x949080; the layer's chart is +0x118, not +0x110), so
  the guest diag (guest_actor_diag) was REFUSED; it is ported now (companion_probe.h) to count per remote NPC:
  restore byte 0 / restored / chart failure / other.
- 2026-11-08 02:00 tcB34 (sniffer interaction lines on BOTH clients; the guest's E at a lamp, then the host's E at
  "Stranger / Greet"): the host's own exchange is the guest's exactly: C>S 3008 -> S>C 2120 -> C>S 3073 -> S>C 2794,
  no 2980 either (2980 is not part of a normal interaction). The host's one E did two (the Stranger 0xB01005DC,
  interaction 0xAD, then the lamp 0xB010088F, 0x0F4267) and the Stranger spoke (subtitle, shot tcB34-38c). So the
  guest's request path is complete and accepted; what the guest misses is what follows (the NPC's reaction /
  dialogue; its NPCs have no action at all, see the correction above). Next tcB35: the restore diag on 3576 + the
  guest's E right after its teleport (facing the Stranger like the host).
- 2026-11-08 02:20 tcB35 (the restore diag ported to 3576): on the guest RestoreActionsFromLoadData ran for 842
  remote actors: 821 "restored" (both layers then have a current action + playback: those animate) and 21 FAILED
  with eErrNoInvalidActionChart, all with layer 0 hash 0x837487D4 (+ layer 1 0x35DD392F): ByJunter's
  seq_bodyanim scene actions (cdmp.2inst-guest.ini: "an NPC of the host announced in the middle of a scene action
  cannot restore it here: the scene's chart exists only while it plays"); keys in runs (0xB0100718-722,
  0xB010051D-521): groups in a scene (the inn's staff / drinkers). Those 21 keep a chart and no action: the bind
  pose. His fix seq_actor_restore (0 | probe | 1, guest) restores the wait 0x26107D3C instead (the scene gives the
  real action when it plays); it was 2976-only. PORTED to 3576 (native guest_seq_restore.cpp): the restore is
  0x949EB0 (2976 + 0xE57E0 throughout), the point 0x94A0EF, RestartLayerAction 0x949080, ActionGraph_Find
  0x2627350, the entries' constructor 0x207B060; changed on 3576: the layer's chart +0x118 (was +0x110) and an
  action counts only with its byte +0x111 set (the find and the triple check test it) -- sar::chart_find does
  too now (test added; optd_seq_restore all passed). Harness: -GuestSet k=v (like -HostSet). tcB36: seq_actor_restore=1.
- 2026-11-08 02:40 tcB36 (seq_actor_restore=1): two findings.
  (1) It did not install: its build gate still compared the exe stamp with kPeTimestampUnported (the base's
      deliberate block for unported modules): now kPeTimestamp (3576), since its points are ported.
  (2) THE GUEST CRASHED (02:01:48, exe+0x251AF7D reading 0x28; NPC 0xB010012D, both layers a chart and no
      action/playback). Every CharacterControl::Update (0x9451E0) calls 0x9496C0, which first calls 0x251AF40: it
      reads the SELECTED layer ([[CC+0x30]+0x1B]) -> playback +0x18 -> +0x28 -> +0x7E with no null check. My tcB29
      guard rule (the 0xA12C00 layer rule) no longer held such an actor (the old crude test did). Fixed: the guard
      also holds a remote actor whose selected layer / its playback / playback+0x28 is null (it has no animation
      to show anyway; no visual change). The regression came from my change of 4a60393 -- my mistake.
  tcB37: both, with a minute more in the inn.
- 2026-11-08 02:55 tcB37 (seq_actor_restore=1 + the selected-layer guard): NO guest crash in ~5 min, SAVES OK.
  GuestSeqRestore ON (3576 point); restores 999, ALL restored (CHART FAILED 0, tcB35: 21); seq_bodyanim restores 22:
  20 SUBSTITUTED with the idle wait (the native 0x949080 then succeeds), 2 skipped (the native byte-0 path); "actors
  held 0". The guest's teleport landed it on the inn's ROOF this time (11 m from the host), so no visual check;
  tcB38 again for the shots.
- 2026-11-08 03:10 Plitos's C606D401 handoff (Discord, via the user; reference patches against 862b4256, unpacked
  to the scratchpad, hashes match his list). User: use nothing made irrelevant by the switch to native. Triage
  against native_layer.h (apply_native_layer turns OFF in coopplus.asi: sniffer, enemy relay, co-op worlds, live
  quests, story sync, stage cinema, save guard, puppet):
  NOT taken (copy-based, off on native): packet replay budget (sniffer), enemy relay pacing, save_io ready guard
  (the native base's save hooks own saves), Direct quest credit / quest_live, tavern local credit, scene-skip
  pairing (stage cinema; B09 is native), persistent World ID + title-screen checkpoint transfer (co-op worlds).
  TAKEN as references for our own port (still live on native): (1) the overlay's present path skipping a frame
  instead of waiting on its fence / the render lock (dx12_hook, overlay_fence_slots.h); (2) the installer reusing
  an existing ASI loader (winhttp.dll ...), keeping occupied proxy names, hash-verified backups of solo AND co-op
  saves, EXE identity check (Setup-Direct.ps1 -> kit/coopplus/install.ps1); (3) Direct link polish: the timeout
  message (LAN/VPN, IP, port, build), role checks on Direct too, the VPN route choice off-subnet. Credit Plitos.
- 2026-11-08 03:10 User on tcB39: "Even with the guest's camera under the floor ... the NPCs are still in A-pose";
  tcB40 (the guest inside, on the floor) agrees: stiff on the guest while the host's are bent over the fire, even
  with 897/897 restored. So the restore is not the whole story. AnimProbe (native, [session2] anim_probe=1, read
  only) counts per 5 s over the NPC keys: updated / layer-0 action / playback / playback CHANGED, host vs guest.
  tcB41 running.
- 2026-11-08 03:40 AnimProbe tcB41/42 (host and guest, every 5 s over the NPC keys): the SAME numbers on both sides
  (~320 NPCs updated, ~310 with a layer-0 action and playback) and the same NPC has the same action hash on both
  (0xB0100641: 5DCADFA4; 0xB01006B9: F9C42A4A). So the action layer is not what differs: the guest's NPCs have their
  actions and are updated, yet stand stiff (user, tcB39/40). Teleports into the inn are unreliable for both players
  (roof / upstairs / under the floor: the tp's height snap); tcB42's street spot faced walls.
  ROOT CAUSE CLASS: ByJunter's guest ran a whole town-NPC stack on 2976 (his last guest ini s46: guest_schedule_load=1,
  guest_sched_bind=1, guest_sched_route=1, guest_seq_play=1, guest_seq_foreign_*=1, seq_actor_restore=1, ...): the
  townsfolk are in schedule / sequencer scene actions whose body animation comes from the scene, which must be
  routed, bound and TICKED on the guest. On 3576 that stack is still blocked (kPeTimestampUnported): guest_sched_load,
  guest_seq_play, guest_seq_foreign, guest_seq_present, guest_x_veto, docked_gimmicks (+ the host's user2_stages);
  guest_seq_route is half ported (its 2661/2981/2810/2322/2524 points; the schedule points 2335 0xC0B5C1, 2126
  0xC0BA4E, 2958/2909 0xC0C761/0xC0CC91 and the 0xA9E55B lookup are still 2976 and refuse themselves). This is B02's
  core. Next: port guest_seq_route's schedule points, guest_sched_bind, guest_schedule_load, guest_seq_play to 3576
  (bytes from the source + the 3576 decomp's handler/class names, as for seq_restore), then the two-copy check.
- 2026-11-08 04:15 ByJunter's own note (cdmp.2inst-guest.ini, guest_schedule_load): "the guest's pa::NPCScheduleManager
  container is empty (only its own server fills it, which does not run in option D): 477+ schedule NPCs without a scene
  (children in A-pose, sweepers, fauna)" -- the user's symptom exactly. guest_schedule_load loads, before each host
  2005 / 2335 / 2874 is delivered, the schedule files it names, with the game's own loader.
  PORTED to 3576 (native guest_sched_load.cpp / _logic.h): the loader 0x19A64E0 (2976 0x1864140; found through the
  "sequencer/binary__" string: hashes the name, finds it via 0x19AC5F0 in [mgr+8], else the reader), the reader
  0x19A6760 ([mgr+0x90] vf40), the container lookup 0x19AC460 (users +0x68, lock vf40/18/20), the release 0x3D7FA0
  (its body's FNV equals 2976's exactly), the node lookup 0x19ABC70 (reads [0x70DBFA0]+8), the key lookup 0x19A4B90,
  the 2335's call into it 0xBABB73 (in 0xBABA90 = 2976 0xA9FEE0), vtables NPCScheduleManager 0x5B7CD28 /
  NPCScheduleData_File 0x5B7CD38, the manager slot 0x70DBFA0, the client / server roots 0x71C0488 / 0x71C0490;
  body hashes from the 3576 .pdata ranges; the build gate now kPeTimestamp. The context table layout and the name
  hash are checked at run time by its self-check (schedule_load_selfcheck=1). tcB43.
  guest_seq_route's schedule handlers on 3576 (for the next step): CreateNPCScheduleStageAck (2976 2335 -> 2989)
  0xD3A1B0, Delete (2126 -> 2796) 0xD3AEE0, ChangeMiseenscene (2958 -> 2616) 0xD3BBB0, DeactivateMiseensceneSchedule-
  EvadeObstacle (2909 -> 2297) 0xD3C300, InitializeNPCSchedule (2874 -> 2769) 0xD3CA50; the frames moved (the 2335
  point is now 0xD3A951 lea rcx,[rbp-0x28] ; mov [rsp+0x60],rcx after vf48 on [rbp]; find 0x98FC20 found [rbp+0x80]
  actor [rbp+0x78]; kind [rbp+0x1100]; the stage call 0xBABA90).
- 2026-11-08 04:20 User (from Plitos): "if you add nothing else that he said, to please add this part to our final
  package by the time you release it" -- save protection and installer. Added to ROADMAP as B22a (a release blocker):
  loader reuse (winhttp.dll ...), occupied proxy names kept, hash-verified backups of solo AND co-op saves, the EXE
  identity check, and co-op refused when a required save-redirection hook fails (on native: the native base's save
  guard). Credit Plitos.
- 2026-11-08 04:35 tcB43 (guest_schedule_load=1 on the 3576 port, seq_actor_restore=1; both players on the inn floor):
  THE GUEST'S NPCs ARE POSED. Shots tcB43-38 / -38b (guest): a man SITTING at the left table, the guard with his spear
  in a natural stance, a man LEANING over the table, a woman READING a paper, the man at the hearth bent over the fire
  (tcB40, without it: all standing stiff). GuestSchedLoad: manager / client root / server root one object (vtable
  pa::NPCScheduleManager), context table 4408 keys, names hash back to their key 64/64 sampled (the layout and the
  hash hold on 3576); before delivery: 2005 x1885 (NPC records 2098, key hits 868), 2335 x78; LOADED 60 failed 0,
  late 0, 24.5 ms in all (max 3.3 ms); no guest crash, SAVES OK. (ScheduleSelfCheck, a separate read-only thread, is
  still gated: unported, not needed.) Native 3df1909 (local) + bundle. Next: guest_sched_route / guest_sched_bind
  (NPCs whose 2335 comes after their 2005; the handlers are mapped above) and guest_seq_play (scene ticking); then
  the guest's interaction with NPCs (talk) now that they are real.
- 2026-11-08 04:55 B22a, the installer part (after Plitos's Setup-Direct.ps1; our own code): kit/coopplus/install.ps1
  (1) checks the EXE identity (version 1.0.0.3576, PE stamp 0x6AED2E85, SizeOfImage 0x13CC4000; a note on another
  build, as before: the mod switches itself off there); (2) finds the ASI loader: an Ultimate ASI Loader already in
  bin64 under any proxy name (winmm, winhttp, dinput8, version, dsound, xinput1_3/1_4, d3d9, d3d11, wininet,
  msacm32; marker 'IsUltimateASILoader') is used as it is and the bundled winmm.dll is not installed; two loaders,
  or a winmm.dll that is not a loader (and not ours), STOP the install before anything is touched; (3) backs up the
  solo saves AND the co-op ones (coop_worlds, save_cdmp_guest) to Documents\...\<date>\{save,coop_worlds,...}, every
  file checked by SHA-256 against its original, a linked (reparse point) save folder refused. tools/test-install-
  fake.ps1 -RepoScripts: new cases (a reused winhttp.dll loader + uninstall leaves it; a foreign winmm.dll; two
  loaders; the co-op save in the backup). ALL PASS on the 862b4256 zip (18 steps) and the shared-world zip (20).
  Still open in B22a: co-op refused when a native save-redirection hook fails (native code).
- 2026-11-08 04:55 tcB44 (the guest turned toward the hearth, E twice): poses fine (a man leaning on a pillar with his
  arms crossed, the man in white bent toward the fire), but NO "Greet" prompt on the guest for any NPC (the host had
  "Stranger / Greet"); both presses went to a lamp (0xB0100862) and the host's server answered PreemptionStepFail
  3100 this time. User (tcB43): the man by the fire faces away from it on the guest (host: warming his hands): the
  scene that turns and animates him is not played on the guest yet (guest_sched_route / bind / seq_play to port).
  Next: the host's slot guard now counts every interaction slot it writes (present / empty / NULL holder) to see
  whether user 2's NPCs reach the guest with their interactions emptied.
- 2026-11-08 05:20 tcB45 (the host's slot guard now counts every interaction slot it writes): in the whole session
  only 73 slots: present 45, empty 0, NULL holder (guarded) 28 -- the same component is serialized for both users,
  so the NULLs are not user-2-specific, and so few calls mean most NPCs' interactions do not come through it. Still
  no "Greet" on the guest (tcB45-g1; the man at the hearth faced the fire this time: his facing on the guest depends
  on when he was announced). ByJunter's notes never reached NPC conversations for the guest (only "ShowAIDialogAck
  may not include the guest"). Lead: the client's candidate collector 0xAC0150(player side, target, out, flag)
  (3576 decomp: gates 0x9857C0 / vf188 / 0xABF090 / field 0x215B790, then 0xABF3E0 must give 0, then the target's
  entries from [[target+0x68]+0x98] via 0x22EAE00, each tested by 0xABF5A0). Native InteractProbe
  ([session2] interact_probe=1, read only, 4 mid hooks: 0xAC0150 / 0xAC027B / 0xAC02E4 / 0xAC0335): per target
  asked, refused by 0xABF3E0 (code), entries, kept -- host vs guest. tcB46.
  The scene port (for the man by the fire) found on 3576: the stage manager grew by 0x28 (its schedule container
  +0x148 -> +0x170, the ready byte +0x231 -> +0x259); the Delete point is 0xD3B10B lea rdx,[rbp+0x88] ; mov rcx,
  [rax+0x170] (14 bytes) ; call 0x4E1740 (2976 0x43C750); its slots found [rbp+8] actor [rbp] packet key [rbp+0x80]
  key [rbp+0x88]. The Create point 0xD3A951 (9 bytes): found [rbp+0x80] actor [rbp+0x78] packet key [rbp-0x40]
  key [rbp-0x30] resource [rbp-0x34] kind BL; the stage call 0xBABA90 (13 args now).
- 2026-11-08 05:50 tcB46 InteractProbe (host and guest, the same NPCs): the guest RECEIVES each NPC's interaction
  entries -- 0xB01005AF / 0xB01005AC / 0xB0100524: 25 entries per query on BOTH sides -- but the client's per-entry
  test 0xABF5A0 keeps one on the host (426 of 426 queries: the "Greet") and NONE on the guest. (Some NPCs, e.g.
  0xB01004CF-4D4, are refused earlier by 0xABF3E0 code 0x741F3E3F on both sides: not talkable.) So the data is there
  and the slot-guard suspicion is out; the guest's client refuses the entries. 0xABF5A0's refusal paths each store
  their own code (globals 0x714C018 via 0xABCD30, 0x714AF0C via 0x8D0FE70, 0x7137FC8 the target's +0x120 flag,
  0x714BE54 the local user's current interaction, 0x714AF14 the target's current action needs +0x111 and flag
  +0x116 & 4) plus 0x22E6FC0 first. The probe now records the guest's refusal code per NPC. tcB47 (+ the 3576 port of
  guest_seq_route's schedule points, in probe mode: Create 0xD3A951, Delete 0xD3B10B (14 bytes), Change 0xD3C047,
  Deactivate 0xD3C797; manager container +0x170).
- 2026-11-08 06:10 tcB47: (1) the guest's per-entry refusal is 0x6467532C for EVERY entry of a talkable NPC
  (0xB0100524: host keeps 1 of 25 per query, its other 24 also refused with 0x6467532C; guest keeps 0) -- one condition
  of the entry check fails on the guest for the Greet entry too; the code globals were still 0 at the probe's start
  (runtime-filled); tcB48 prints them once set, to name the path. (2) The guest_seq_route schedule port in probe mode:
  Create (3576 2989) and Change (2616) HOOKED and decoding right: "scene 0x200001BC of character 0xA0100001 (the
  host's), resource 0xF9E1E4B3, kind 4 | vf48 gave M (this client's 0xA01001D0, its container counting 1, 2, 3...)
  | X (the host character's manager on the guest) | WOULD route to X". Census: M holds 87 host stages, X none -- the
  routing ByJunter built is what puts them where the NPCs look. Every stage's +0x28 is M (the stage -> manager link
  holds on 3576); "0 active (+0x178), 0 nodes (+0x138)" across 87 is suspect: tcB48 dumps a stage's first 0x200
  bytes. Fixed: Delete's patch is the first 7-byte instruction (the second reads the RAX the hook sets, in place);
  Deactivate's lookup call bytes (E8 20 35 C5 FF).
- 2026-11-08 06:30 tcB48: (1) the 3576 stage layout (a live stage of M dumped): +0x28 the manager (M), +0x30 the scene key
  (0x200001BA), the node vector +0x140 / count +0x148 (2976 +0x138 / +0x140), active +0x180 (2976 +0x178; it is 1),
  resource +0x1C0 -> kStage_* fixed in guest_seq_route and guest_sched_bind; tcB49 dumps a node (stride 0x168, NPC
  +0x28 to check). All four schedule points hooked now (Delete / Deactivate fixed). (2) The refusal code globals,
  filled: 0x714AEEC (0x1940AD0's) = 0xCBCF84AC, 0x714ABB8 = 0x378A2A74, ... -- none is 0x6467532C: the guest's
  refusal comes from a deeper callee. The condition evaluator 0x1940AD0(target, filter = the player's vf178(), &key,
  out) re-gathers the target's entries through 0x22EAE00: filter 0, or an empty server list (comp+0x98), takes them
  from the NPC's AI ([[actor+0x68]+0x58]+0x90: its current state node) -- the Greet may come from the NPC's AI
  state, which the guest (not the NPCs' controller) may hold stale. tcB49 logs the evaluator's inputs per target.
- 2026-11-08 07:00 tcB49/50: (1) the 3576 node layout: the NPC key at node +0x28 (0xB01003A1, next 0xB01003A2), stride
  0x188 (2976 0x168) -> fixed. Census with the 3576 offsets: M (the guest's own manager) holds the host's 82 stages,
  ALL active, 1541 nodes, 554 bound to an NPC; X (the host character's manager on the guest) none. ByJunter routes
  them to X, but only guest_seq_play ticks X: routing on without seq_play would put the stages where nothing ticks
  them, so guest_sched_route stays in probe until guest_seq_play is ported (next, with guest_sched_bind).
  (2) The condition evaluator's inputs: "server list 0" on BOTH sides for every NPC -- every interaction entry is
  derived from the NPC's AI state (0x22EAE00's AI path), and the guest's NPCs do have AI components and states; no
  difference seen in what the probe reads. tcB51 scans .sdata/.rodata for the guest's refusal value 0x6467532C to
  find which check stores it.
- 2026-11-08 07:30 tcB51 NAMED the guest's refusal: 0x6467532C lives at 0x714AF5C, registered with the string
  "eErrNoInteractionOwnerConditionCheckFail" (0x5CD8140). It is stored by 0x25FA200(condition, PLAYER actor, target,
  mode, out) at its failure exit 0x25FA3DA (a second exit 0x25FA552 stores another code); the only caller 0x22E7A90
  runs it for each owner condition of the entry with the INTERACTING PLAYER as the subject (interaction side + 8).
  So the guest's Greet is refused because a condition on the guest's PLAYER CHARACTER fails (not the NPC). tcB52
  logs each distinct failing condition (its vtables and flag bytes) on both sides: the one that fails only on the
  guest names what the guest's character lacks.
- 2026-11-08 08:00 tcB53 (the failing owner conditions per NPC, their classes from the exe vtables): failing on BOTH
  sides (normal: entries that do not apply) NotContentsLogicFunction 0x5D83C30, ConditionData_CheckActionAttribute
  0x5D09190, ConditionData_CheckGimmickState 0x5CFD3B0 (the conditions are data, deterministic: the same heap
  addresses on both PCs); failing ONLY on the guest: one pa::ConditionData_Macro (vtable 0x5D09E08; target
  0xB0100588) -- a named macro condition from the data (condition_macro): ConditionData_Macro::vfunction2 0x2417930
  looks the macro up by its u16 index in [0x71C2150]+0x58 (0x3F4F10, loaded on demand) and evaluates the macro's
  inner condition [info+0x10]. tcB54 dumps that macro (index, info, inner class, strings) to see what the guest's
  character lacks.
- 2026-11-08 08:30 tcB54: the guest-only failing macro (index 270) wraps a pa::ConditionData_GameEventParam (vtable
  0x5CF7958, named from its RTTI: not in classes.tsv), which only yields a real result for a type-9 (game event)
  context; here it gets CompareTargetParameter type 5, so it is not the real cause -- the branch that reaches it is.
  0x25FA200 picks its branch with the PLAYER's vf178 = 0x5B3AA0 ("controlled by this client": [actor+0xA0] ==
  [[exe+0x71C0100]+0x30]+0x58 and byte [actor+0x94] != 0; slot 47 of ClientNormalInGameActor 0x5914848 /
  ClientChildOnlyInGameActor 0x5914D58), and 0xABF5A0 makes the same test inline several times. Hypothesis: on the
  guest, the guest's OWN character fails that test (its +0xA0 owner object is not the client's local user object),
  so every player-side interaction check takes the "not my character" branch. tcB55 logs both values on both PCs.
- 2026-11-08 09:00 tcB55: the "controlled by this client" test is TRUE for the guest's own character on the guest
  (player 0xA01001B0 +0xA0 == the client's local user 0x901001AF, +0x94 1) -- hypothesis out. In 0x25FA200's
  branch for a local player the owner condition is evaluated on the TARGET (the NPC; the player is the compared
  parameter), so the guest-only failing macro tests the NPC's state as the guest sees it. GameEventParam's default
  case returns 2 (passes), so the failure is elsewhere in the macro. Experiment instead: interact_owner_override=1
  (guest, TEST ONLY): an owner condition that fails for an NPC another user controls resumes at 0x25FA55F (success).
  If the Greet prompt appears and the host's server accepts it (the NPC answers), only this client check is wrong.
  (Over-broad by design: it also passes conditions that fail on the host; not a fix.) tcB56.
- 2026-11-08 09:30 tcB56 (interact_owner_override=1, test only): the owner-condition override passed 14770 checks; the
  lamp then showed 3 prompts on the guest (over-broad, as expected) but NPCs still none: the next refusal is
  0x2569C661. NAMING THE CODES: they are lookup3(name, initval 0xC5EDE) of the eErr names (calibrated on two known
  codes; tools/native-port/errhash.py hashes every eErr string of the exe):
    0x6467532C eErrNoInteractionOwnerConditionCheckFail   (0x25FA200's exit 0x25FA3DA)
    0x2569C661 eErrNoInteractionTargetConditionCheckFail  (0x25FA200's exit 0x25FA552; ALSO what the host's server
                                                           answered the guest's lamp PreemptionStep with)
    0x741F3E3F eErrNoInteractionTargetIsHidden            (0xABF3E0's refusal of some NPCs, on both PCs)
    0x7B1E861C eErrNoGimmickNotInteractableState
  So the guest's Greet fails BOTH the owner and the target condition lists, and both evaluate the NPC with the
  player as the parameter (the guest-only failure: a ConditionData_Macro). The NPC's state as the guest sees it is
  what differs -- and the townsfolk's state comes from their schedule scenes, which on the guest sit in the wrong
  manager (M), unbound and unticked. The scene stack (guest_sched_route mode 1 + guest_sched_bind + guest_seq_play)
  is therefore needed for NPC interaction too, not only for the scene poses (the man by the fire). Porting it next.
- 2026-11-08 09:45 tcB57 (guest_sched_route=1, routing ON alone): no crash, SAVES OK; all 88 host schedule stages
  ROUTED to X (M 0); but X's stages show "0 bound to an NPC" (in M, unrouted, the natives had bound 554) and the
  Greet is still refused (owner / target condition codes). So routing without guest_sched_bind leaves the NPCs
  unbound from their stages: ByJunter's bind step (0xC0B626 -> 0x9F2FA0 per NPC of the routed scene) is needed, and
  guest_seq_play to tick X. Bind on 3576: the InitializeNPCSchedule handler 0xD3CA50 calls 0xAF91F0 / 0xAF9480;
  0xAF9480 has 0x9F2FA0's path-A test ([[actor+0x88]+1] == 7 on [comp+8]) -> the bind candidate. The guest's tp
  landed upstairs (no pose check): tp_inn_guest.txt now jumps twice (4 s apart) like the host's.
- 2026-11-08 10:15 Two corrections from the 3576 decomp:
  (1) ROUTING IS NOT NEEDED ON 3576. The 3576 bind 0xAF91F0 (2976 0x9F2FA0; it stores the stage in [comps+0xB0]+0x318
      and links the node via 0x4E0480 = 2976 0x43B4B0) looks the NPC's stage up in the LOCAL player's manager
      (0x99EA20 attaches [user ctx +0x50], then [[comps+0xA8]+0x18]+0x170), not in the owner's X as on 2976 -- which
      is why M (unrouted) had 554 NPCs bound natively and X (routed, tcB57) none. guest_sched_route stays OFF on 3576
      (the port of its points stays, probe-only). Other 3576 equivalents found: the locked actor lookup 0x997570
      (2976 0x8AE1A0), the holder release 0x1618660 (2976 0x1434880, the same 156 bytes), ScopeAttacherBase vtable
      0x5AC58E0 (2976 0x5725E48).
  (2) The owner conditions are NEGATIVE: in 0x25FA200 a sub-condition that PASSES jumps to the failure exit (0x25FA491
      -> 0x25FA3B5 -> 0x25FA3DA). GameEventParam's parameterised evaluate (slot 4 = 0x522390) returns 3 = true, so
      macro 270 "true" blocks the Greet on the guest. Which conditions apply comes from the NPC: 0x22E7A90 takes the
      list of the target's control state ([[target+0x68]+0x30], u16 index +0x48 -> 0x3DE240 -> 0x194DEA0). Lead: the
      guest's NPC is in a different control state than the host's (e.g. seq_actor_restore's substituted wait vs the
      host's scene action). tcB58 logs each talkable NPC's control state on both PCs.
- 2026-11-08 10:45 tcB58: the NPC control state (+0x48) is IDENTICAL on both PCs for every NPC seen on both (the
  talkable townsfolk 0xFFFF); for townsfolk (type 1) 0x22E7A90 takes the conditions from the interaction's own static
  list (the same data on both) -- so the condition list does not differ. CompareTargetParameter (0x240E9D0) stores
  the player at +0x828 = 0x25FA200's local_38, so both PCs take the same "with context" call. Remaining lead: the
  guest is offered a different entry under the same prompt (the 25 entries come from the NPC's AI state node).
  tcB59 (per NPC and entry index: kept / refusal) ran out of its 160-line budget on other actors (only 2 gimmicks
  differ: host 0x3E2AD36C, guest 0x2569C661); tcB60 logs only the townsfolk (20+ entries), 700 lines.
- 2026-11-08 11:10 tcB60 (per NPC and entry, townsfolk only): THE GREET IS ENTRY INDEX 0x0055 -- the host keeps it for
  0xB01003B7 / 0xB010058F / 0xB010060A; on the guest the same NPCs' 0x0055 is refused 0x2569C661 =
  eErrNoInteractionTargetConditionCheckFail (a TARGET condition, the second exit of 0x25FA200). Other codes named:
  0x3E2AD36C eErrNoInvalidInteractionDistance (the host's usual refusal of far entries), 0x669DBA09
  eErrNoInteractionShowWhenTargeted. tcB61 dumps the target condition that fails for entry 0x0055 on the guest (a
  hook on the collector's call 0xAC0330 records the entry index; the dump hook at 0x25FA549).
- 2026-11-08 11:40 tcB61: the Greet's (entry 0x0055) failing TARGET condition on the guest = macro 270 (its inner
  GameEventParam: fields 0x11E / 0x60F / 0xF43A3) AND an AndContentsLogicFunction (0x5D83B40, named from RTTI) -- the
  same data on both PCs, so runtime state differs. Not the guest's story progress: its own save has 492 completed
  missions (the host's "other player is in the prologue (0 completed missions)" is Co-op+'s prologue message sent
  before the guest read its save: the count defaults to 0 -- a Co-op+ artifact; prologue_hold is copy-mode logic
  still running on the native layer: to switch off there). The host's server ALSO refused the guest's lamp with
  TargetConditionCheckFail, so the failing state is probably visible to the server too. Practical test tcB62:
  interact_owner_override=1 now covers both exits (owner 0x25FA3DA and target 0x25FA549), only for HUMAN NPCs (type 1)
  another user controls: does the Greet show, and does the host's server accept it (dialogue)?
- 2026-11-08 12:10 tcB62/63 (the test override on both condition exits for NPCs another user controls; NPCs = an AI
  component, not actor type 1 -- 0 matched by type): owner 2237 / target 26615 overridden on the guest, and the guest
  then requested interaction 0x0C on the NPCs 0xB01005F7 / 0xB01005F8 -- the HOST'S SERVER refused each one:
  InteractionPreemptionStepFailAck code 0x2569C661 = eErrNoInteractionTargetConditionCheckFail. So it is NOT only a
  stale client copy: the server judges the same target condition failing FOR USER 2. Working theory: the townsfolk
  are in schedule scenes owned by the HOST's character ("scene 0x200001BC of character 0xA0100001"), and a target
  condition (macro 270, GameEventParam, ...) treats an NPC in another player's scene as unavailable. ByJunter's
  user2_stages is about user 2's OWN stages far from the host (>= 64 m), not this case; but his rule "the guest
  follows the host's progress" (U-a2: user 2's quest context redirected to the host's on the server) is the pattern
  of fix. Next: the host's InteractProbe catches the server-side target-condition failure for the guest's character
  (the same Common code 0x25FA200 runs in the host's server) to see what the macro reads for user 2 vs user 1.
- 2026-11-08 12:40 tcB65 FOUND THE SERVER-SIDE BLOCK: the host's server refuses the guest's NPC interaction (the guest's
  character 0xA01001CA, NPC 0xB0100618, interaction 0x0C) on a target condition whose first part is
  pa::ConditionData_CheckKnowledge (vtable 0x5CFDE68, named from RTTI; fields ...0024 / 0x1FFFF14E8: knowledge entry
  0x14E8?) AND an AndContentsLogicFunction. NPC conversations are gated by the PLAYER'S KNOWLEDGE, checked per
  character on the server (the condition classes evaluate through the CompareTargetParameter visitor: 0x2417610 ->
  param vf+0x20). The guest's character lacks that knowledge on the host's server. (tcB64's lines were the host's
  own character: my "local character" test missed; the host's is 0xA0100001 in every run.)
  This is B04 ("knowledge learned by one counts for both"): the fix direction is for knowledge checks of user 2's
  character to see the host's knowledge too (as ByJunter's rule "the guest follows the host's progress" for quests),
  on the server and on the guest's client. The game has native knowledge sharing (ShareKnowledgeDataListReq/Ack,
  ShareKnowledgeSeedAck, CommonShareKnowledgeInteractionProcessor) and per-actor knowledge (LearnKnowledgeListAck
  0xCFEFA0 resolves the actor by key, 0x98FC20). Open: is user 2's knowledge on the server EMPTY (its save's
  knowledge not loaded) or only missing this entry; which knowledge is 0x14E8.
- 2026-11-08 13:10 CheckKnowledge's evaluate is slot 21 (+0xA8) of its vtable 0x5CFDE68 = 0x242A270 (the class table
  listed only 11 of its 22 slots): the subject actor (CompareTargetParameter +0x828) -> its USER (+0xA0) -> the user's
  main character (+0xD0) -> knowledge comp ([+0x68]+0x150) -> 0x2308FC0(comp, &key, level): comp+8 its actor, +0x18
  an array of 0x18-byte entries by knowledge key, the level learned at +0; "has" = level >= the asked level. The
  Greet's condition asks key 0x14E8 at level 1. TEST user2_knowledge_share=1 (native, HOST): an inline hook on
  0x2308FC0 asks the host character's knowledge again when the server (TLS +0x1EB) gets "no" for another player's
  character -- read only (B04 "knowledge learned by one counts for both"). tcB66: with the guest's client override.
- 2026-11-08 13:50 tcB66-68 (user2_knowledge_share=1 on the host + the guest's client override): the server's "no"
  answers for the guest's character (27) ask keys 0x0F9A-0x0FD2 at level 1, and the HOST's character has them at
  level 0 too (its comp known) -> shared 0: knowledge sharing is not the fix for these checks (CheckKnowledge's
  evaluate returns NOT has ("^ 1") on that path, so these are "has not learned yet" checks that come out the same for
  both players). The Greet's key 0x14E8 never reached 0x2308FC0: that condition takes CheckKnowledge's other path
  (flag +0x20 set: the knowledge key of the NPC itself, [[npc+0x68]+0x20] -> info +0x64). The guest's E kept landing
  on interaction 0x0C, not the Greet 0x55. tcB67's guest keys aborted on "game window is not in front" (focus).
  Next idea (an experiment first; a design choice for the user): on the SERVER, evaluate user 2's interaction
  conditions with the HOST's character as the subject ("the guest follows the host's progress", ByJunter's rule for
  quests), so any NPC interaction the host can do the guest can too; the guest's client keeps the prompt override.
- 2026-11-08 14:20 tcB69/70 (TEST user2_condition_subject_host=1 on the host: on a server thread, 0x25FA200's subject
  -- another player's character -- becomes the host's server character, looked up in the SAM by key each time; the
  guest's client override limited to the Greet entry 0x0055): the server swapped the subject 3-4 times, but the
  guest's E never asked for the Greet (tcB69: interaction 0x0C, refused; tcB70: the lamp). The Greet's target
  condition was overridden 1428 times on the guest's client and still no Greet prompt: more of 0xABF5A0's checks
  stand after the conditions (e.g. eErrNoInteractionShowWhenTargeted, the distance).
  GOOD: tcB70 -- the guest TURNED OFF THE LAMP (0xB01008C8, interaction 0x0F4267): PreemptionStep and DoStep both
  succeeded on the host's server and the lamp is dark on the guest's screen afterwards (shots tcB70-g1 lit, -g3
  dark). Object interaction works for the guest.
  Next for NPC talk: send the Greet (the host's greet interaction 0xAD, tcB34) from the guest to the server directly
  (a one-shot test of the request path) with the host's subject swap on, to see whether the server accepts and the
  dialogue reaches the guest -- before more client-side work.
- 2026-11-08 14:50 tcB71 (TEST test_greet=1 on the guest: the host's own greet request, interaction 0xAD -- the bytes the
  host's client sent in tcB34 -- sent by the guest's character 0xA01001C0 to the talkable NPC it looked at most,
  0xB0100587; the host's subject swap on): THE HOST'S SERVER ACCEPTED IT -- InteractionPreemptionStepSuccessAck
  2120 for 0xAD on 0xB0100587. The guest's client then sent InteractionResetReq 2887 (it had not started that
  interaction itself) and the server acked the reset. So the server path for a guest greeting an NPC works (with the
  swap: tcB72 checks without it); the remaining block is the guest's CLIENT (the Greet is not offered: the target
  condition and further checks of 0xABF5A0).
- 2026-11-08 15:10 tcB72 (the same greet request WITHOUT the host's subject swap): the host's server REFUSES it
  (InteractionPreemptionStepFailAck, eErrNoInteractionTargetConditionCheckFail). With the swap (tcB71) it accepts.
  So the server side needs the rule "user 2's interaction conditions are judged as the host's" (ByJunter's "the guest
  follows the host's progress"); the conditions read the subject's user / main character / knowledge. tcB73: the
  same swap on the GUEST's client too (guest_condition_subject_host=1: the subject becomes the host's character's
  copy on the guest's client), to see whether the Greet is offered.
- 2026-11-08 15:30 tcB73 (the subject swap on the guest's client too: 7943 swaps): the Greet (0x0055) is still refused
  by its target condition on the guest's client (18x) -- the host's character's COPY on the guest lacks the host's
  progress data the conditions read. BUT tcB70 (the narrow override: the Greet's owner/target conditions pass on the
  guest's client) had the Greet KEPT for 7 NPCs (the rest refused only by distance, eErrNoInvalidInteractionDistance):
  the Greet WAS available; E went to the lamp because the guest faced it (the targeting prefers what is under the
  crosshair). THE PATH FOR NPC TALK, tested in parts: (1) the host's server judges user 2's interaction conditions with
  the host's character as the subject (tcB71 accepted / tcB72 refused without); (2) the guest's client lets an
  NPC's (another user controls) condition failures pass for the entries the host could do, so the prompt shows; the
  server stays the judge. Next: the end-to-end run (the guest facing an NPC, Greet, dialogue on screen), then the
  proper versions: the host's character found through its user (not the key 0xA0100001), and the guest's client
  allowance limited to what the host's world allows (e.g. the host's knowledge sent to the guest, or a host-side list
  of the entries each nearby NPC offers).
- 2026-11-08 15:50 tcB74 (end to end: the narrow Greet allowance on the guest + the host's subject swap; the guest turned
  past the lamp to the man by the stairs, ~1 m): the guest's client KEPT the Greet for 0xB0100473 / 0xB01005B9 (the
  rest refused only by distance), but no prompt showed for the NPC in front and E sent nothing. So after the candidate
  collector the target choice / prompt step (FindManualInteractionTargetProcessor 0xB2C1E0, the UI's
  "ShowWhenTargeted" refusals) still drops it -- next. No crash, SAVES OK.
  STATE OF NPC TALK FOR THE GUEST: server side solved in principle (the subject rule, tested: accepted with, refused
  without); client side: the condition allowance makes the Greet a candidate; the prompt choice remains.
- 2026-11-08 16:30 THE USER'S CHOICE: the guest keeps ITS OWN progress (not the host's); a "wait for the host" overlay
  only if own progress turns out impossible. Their idea (the guest joins mid-world without a co-op save) checked
  against the logs: in every two-copy run user 2 ALREADY loads its own save on the host's server (ByJunter's
  guest_own_save=1: the guest's slot is uploaded and loaded as slot 204; tcB72: "user 2's save VERIFIED"), and that
  save comes from the same test save as the host's (the host's mailbox gen2, a user 2 autosave of an earlier run,
  staged instead of the upload). So identical progress, and the server still refuses the guest's greet: not a
  progress desync. Also: cond_dump read the macro index from +8 (it is the function type 0x10E 'Macro'); the index is
  the u16 at +0x10 (0x24178F0 -> 0x3F4F10, the table [0x71C2150]+0x58). Rewritten as a tree (RTTI names at run time;
  And/Or children +8/+0x10, Not +8, Macro -> info+0x10, leaves: u16 type at +8 named by the table 0x5D02C70).
- 2026-11-08 16:40 tcB75 (the tree): the Greet's TARGET condition (judged with the NPC as target, refused when TRUE)
  = And(Not IsManagementActor, Not CheckMaritalState, Not Macro#29(CheckSpawnReason | HasSummonTag),
  HasQuestDialog x3 (Or), Macro#82(CheckFriendlyLevel | Not CheckCharacterGroupKey), Macro#20(CheckFriendlyLevel),
  Macro#21(...)). The per-player ones: HasQuestDialog (quest state) and CheckFriendlyLevel (friendship with the
  NPC). Every leaf goes through the generic 0x2417610 (obj, compare param, invert flag): [obj+0x10] set ->
  param vf+0x20, else the class's vf+0xA8. tcB76: a read-only hook there logs each leaf's result for the Greet on
  the host's client and for user 2's requests on the server (exit 0x25FA561), to see which leaf differs.
- 2026-11-08 17:30 tcB76-78 FOUND THE GUEST'S NPC BLOCK, and it is NOT story progress. Logging each part of the Greet's
  condition showed the guest stopping at "CheckQuestDialogCategory" (on its client and on the host's server): each
  player has a per-NPC quest-dialog category table, and the Greet is refused when the NPC's category is 0. Same NPCs,
  same run (tcB78): the host's table has 200-300 NPCs at category 1 (one at 2); the guest's has 30-80, at 0 or missing.
  The server's quest-dialog replies (SetQuestDialogAck) are identical for both players, so both get the same dialogs;
  the category byte is set by the NPCs' own stage/AI step (SetQuestDialogAICategory), which runs only where the NPCs
  are controlled (the host's client) and only for its local player. So it is NPC state, not player progress: the
  user's choice (the guest keeps its own progress) stands -- quests, knowledge and friendship stay the guest's.
  FIX (native, local commit: npc_category + its pure logic and tests, [session2] npc_category_share=1 by default): the
  host reads its own table every second and sends it to the guest as tunnel control state 16 (on a change or every
  5 s); the guest's character (on the guest's client, and on the host's server) is judged with the host's category
  only where its own entry is missing or 0. Nothing in the game is written. tcB79: the end-to-end check, no test
  overrides.
- 2026-11-08 18:45 tcB79-81 (npc_category, no test overrides). tcB79: the host read 0 entries (wrong object: the table
  is reached through the player's user and its main character, as the game does) -- fixed. tcB80: WORKS: the host's
  table (185-264 NPCs) reaches the guest every second; the guest's character was judged 15860 times, 7025 with the
  host's category, 2748 answers changed; the Greet now passes the category check on the guest. No crash, SAVES OK.
  NEXT BLOCK (tcB80/81): HasQuestDialog -- for most NPCs the guest's table has NO entry, so "this NPC has a quest
  dialog for you" is false and the Greet is refused (the host's entries exist). The entries come from the server's
  answers (SetQuestDialogAck) to SetQuestDialogReq; on the host those requests come from the NPCs' own AI step
  (RequestNearbyQuestDialog), which runs only on the host. These ARE per-player (the server builds them from the
  asking player's quests), so the own-progress way is for the guest to ask for its own. tcB81 tried that (the guest
  asked SetQuestDialogReq for the host's NPCs it lacked): the server answered none of them, and that run's tunnel
  broke (the guest's net loop stalled 8.6 s; tcB79/80 never broke) -- turned off (kept in the code, never sent).
  Open: why the server answers the guest's native requests (~350 a run) but not these (the server finds the NPC by
  key through a manager first; maybe a per-user relevance check).
- 2026-11-08 20:30 *** THE GUEST TALKS TO AN NPC (tcB88), ON ITS OWN PROGRESS, NO TEST OVERRIDES. ***
  tcB82-86 (probes): the guest's quest-dialog entries DO exist (one dialog each, like the host's) and its user is
  valid; the HasQuestDialog search for the Greet's "Hello" dialogs gives up before reading them because it first
  asks the NPC's category from the same per-player table, and the guest's is 0. So the share moved from the
  CheckQuestDialogCategory condition to the category GETTER itself (npc_category: one inline hook, the guest's 0
  answers become the host's category, a non-zero own one is kept; the dialogs and their requirements read after it
  are the guest's own). tcB87: the Greet passes for the guest on 35 NPCs (keys aborted: game window not in front --
  actpid.ps1 now retries the focus 6 times). tcB88: the guest turned to a "Stranger": the prompt "E Greet" shows
  (shot tcB88-g1), E sent InteractionPreemptionStepReq + InteractionDoStepReq for interaction 0xAD and the host's
  server ACCEPTED both (2120, 2794); on screen "Stranger: Trust +5" and the NPC's line (tcB88-g2/g3). No crash,
  SAVES OK. The earlier TEST flags (owner override, subject swap, knowledge share) are not needed for this.
- 2026-11-08 20:50 tcB89 (the same, WITHOUT interact_probe: only the default npc_category_share=1): reproduced -- the
  guest's Greet (0xAD) sent and both steps accepted by the host's server; SAVES OK.
- 2026-11-08 21:40 MERCHANTS (user 2026-11-08: the guest must buy from merchants and start / finish their quests on
  its own; ROADMAP B02b). tcB90/91: the guest's Greet works on many townsfolk (a "Talk" prompt also seen, tcB90-s5).
  tcB92-95: finding the merchants on 3576 (teleports inside the inn land on the upper floor; the market square is at
  ground 612.4; the host's probe now logs each NPC's position). The guest receives the same StoreDataListAck /
  StoreItemSellDataListAck as the host (495 / 34) but NO shop scenes (PrepareSequencerAck: host 33, guest 0): a
  merchant's trade entry exists only while its cd_seq_funcnpc_* scene runs, and the host's server makes those scenes
  for the field owner (the host) only. ByJunter's S46 work (s46-tenderos.md) is the full design for this (user 2's
  own shop scenes, the U2 routing, T1/T2 ordering, R0 entry deletion), compiled and unit-tested on 2976 but never run
  in the game; its modules (user2_stages, user2_shops, shop_probe, shop_trade_probe, observer_hold_scene_order) are
  in the native tree. Next: check which are ported to 3576, then a live test as its 9.7 describes.
- 2026-11-08 23:30 QUEST SYNC DESIGN (user, after a Discord poll): option 3 as the base (each player plays its own
  quests and story in the shared world) + option 2 as an optional bonus (both on the same step: one completing it
  checks it off for both). ROADMAP B10 updated.
  MERCHANTS, groundwork (tcB96-100):
  * FIXED: on 3576 PrepareSequencerAck 2380 has a 92-byte header before its 11-byte bindings (2976: 91): every
    module that parses it (observer_seq_copy, observer_scene_copy, observer_stage, hold_scene_order, troc_script)
    rejected every scene, so the host's scenes never reached the guest. tcB97: 32 of the host's scenes copied to the
    guest, all ROUTED to X on the guest, 0 refused, no crash, SAVES OK. (Unit tests: 29 of 85 fail with or without
    this change -- the pre-existing 2976 test data.)
  * PORTED guest_x_interaction (ByJunter's C1: the guest's prompt check of a merchant's dynamic entry looks the
    entry's scene up in X when it is not in the guest's own manager): 3576 function 0x22EE2D0 (A 0x22EE3D9, R
    0x22EE465, F 0x22EE6F9, stack slot 0x8F8), scene lookup 0x21578E0 whose table moved by 0x28 (stage_lookup_logic.h;
    guest_seq_route's peek fixed the same); class vtables by RTTI. Hooks in; not exercised yet.
  * The Hernand merchants on 3576 (their spawn positions in the guest's capture = ByJunter's 2976 coordinates):
    butcher (-10677.6 611.4 -3905.4), grocer (-10653.6 612.3 -3879.6), blacksmith (-10636.5 613.4 -3848.3), bank
    (-10700.8 622.4 -3798.1); keys change per run. At the test save's hour the grocery stall is empty for BOTH
    players (tcB100) and the blacksmith gave the host no prompt either: next, a merchant actually at work in the
    test save (or another hour), the host's own shop prompt as the control, then the guest.
- 2026-11-09 00:30 MERCHANTS: THE CAUSE FOUND (tcB101-tcB116, host on the sandbox copy of the user's save "usercopy";
  the coop-test-bandits save has Hernand's shops locked).
  * Host ALONE (new harness switch -NoGuest; saves sandboxed as always): the butcher Renee stands behind her counter,
    prompt "R Shop" (HOLD R, a tap does nothing), and the shop opens: "Hernand Butchery - Buy / Sell / Leave"
    (tcB116). Same as ByJunter's 2976 capture: InteractionPreemptionStepReq 3008 (key 0x000F4321) -> 2120,
    InteractionDoStepReq 3073 -> 2794, then the HOST'S CLIENT asks StageBranchStateReq 2880 branch 6 of the butcher
    scene -> 2920. Present at 10:50 AND at 11:36-11:42 game time (tcB114/115): not a schedule.
  * With the guest in the world (tcB109/112/113): Renee is loaded on the host (2005, name "Renee", at her counter)
    but NOT shown, and the host's own interaction check refuses her every time: 0xABF3E0 -> 0x741F3E3F
    eErrNoInteractionTargetIsHidden (InteractProbe, tcB113: 808 of 808). So co-op HIDES the merchants for both
    players: the guest's presence, not the guest's own client, is the bug. Next: who sets the hidden state on the
    host's client when user 2 is in (the observer hold of user 2's copy released her after 4.9 s; whatever hides the
    funcnpc actors), then the guest's own shop.
  * Grocery stall goods are gimmicks ("E Buy (5)"; holding E picks the item up: Place / Throw / Drop).
  * Built, off by default until proven: shop_guest (guest_shop_check=1): the store check 0x23C7350 (3576 of
    ByJunter's 0x21F2780) takes the merchant's own linked stage as "current" for the guest's character only (A
    0x23C74BB, B 0x23C777B), so the guest's buy / sell is judged with its own money, stock and distance; unit test
    optd_shop_guest. guest_seq_route now reports the host's manager X to C1 (guest_x_veto is not ported to 3576).
  * Harness: -NoGuest, -HostWalkTo / -AfterWalkKeys / -GuestAfterWalkKeys, walkto.py --face=X,Z and
    --sweep=FROM,TO,STEP,VK,PREFIX[,HOLDMS] (turns the camera in steps, holds a key, screenshots each).
- 2026-11-09 23:00 MERCHANTS HIDDEN IN CO-OP: narrowed (tcB117-tcB131; the user checked by eye in tcB117/tcB120).
  * The user saw it too: with a guest in, NO merchant at the butcher, the equipment shop or the smithy, for host and
    guest; also WITHOUT the Co-op+ layer (tcB120, ByJunter's base only) and with the guest on the same advanced save
    (tcB122: not story progress) and with the guest far away (tcB131: not proximity).
  * The hide: [[npc+0x68]+0x20] +0x3B8 bit 9 (0x200), read by 0x22E6C70 (eErrNoInteractionTargetIsHidden). HideWatch
    (hide_watch=1, test builds: a DR0 write watch armed in the host client's 2380 handler point 0xD3414E for
    cd_seq_funcnpc_butcher, real unwind) catches the writes, all through the setter 0x18CD090 (write at 0x18CD138):
    BOTH solo and co-op set 0x200 ~2-3 s after the 2380 (protected code 0x9593D33 via the job 0x5B2320). SOLO then
    CLEARS it (0x221B680 <- 0x3791FE0 <- 0x3791CF0 <- 0x37AB860 <- 0xBDFBD0, the NPC's AI / action chart processing);
    in co-op that clear never comes. The host's own packets agree: solo Renee's AI goes on at +1.0 s (RotateActorReq,
    StartAIFunction a8094c03/e4ea17ed, the next StartAction 0x26107D3C -> 0x5762951C, SetQuestDialogReq); in co-op it
    stops after its first action 0x26107D3C. No server packet differs (the step is the client's own AI timer).
  * Ruled out by bisecting the host ini (each: still hidden): remote_interp/mount_*/ride_filter/remote_climb/
    wallup_send/dismount_probe/horse_follow*/ui_pause_block; clock_gate/npc_category_share/user2_fast_travel/
    child_reannounce; observer_seq_copy*/observer_milestone*; observer_hold_acks/observer_load_defer/
    observer_hold_family; (observer_hold/character_hold/child_hold off: the guest drops, inconclusive). The host's
    client-side session2 modules that act only with user 2 are all off by default. Lead: the game's own NPC AI with
    a second player (e.g. an AI check over all players tripping on the guest's remote character).
  * Host crash seen once (tcB117, 20:44, the user not touching the PC): 0xC0000005 in
    TrocTrSyncStageCandidatePostionReq's handler 0x2CEC840 (+0x3F0 writes [vf48()+0x668], vf48 gave null), right
    after ALL the host's sequencers were deleted (2292 burst) while the host's own client still sent a 3117. To fix.
- 2026-11-09 00:50 *** MERCHANTS: ROOT CAUSE FOUND AND THE HOST'S SHOP WORKS IN CO-OP (tcB148). ***
  * The chain (HideWatch / merchant_unhide=2 snapshots, solo tcB136/140/144 vs co-op tcB137/139/141/143/145):
    a scene NPC (shop or not) finishes its login (+0xCF, set by 0x9A1880 at 0x9A1D2A) only when none of its
    components is still loading; its ClientSequencerPlayDataActorComponent (vt 0x594FAF0) says "loading" (vf88
    0xAF4B20) while its byte +0x3E0 ("waiting for my scene", 2976 +0x3D8) is 1. The scene clears it when the client's
    stage manager ticks the scene (0x9A0540 -> 0x99C5A0 -> 0xAAABB0 = the FIELD OWNER's character's manager ->
    0xBA04A0 -> 0xBA2610, which processes stages only while the manager's state +0x259 == 3). Without the login: the
    NPC's AI stops after its first action, hide bit 9 stays, no interaction ("target is hidden").
  * Why co-op: when ANOTHER player's character logs in on a client (the guest's on the host, the host's on the guest),
    the character login 0x99BE90 calls 0x1928650 (jmp .xtls 0xB65B330) at 0x99C17A, which puts the manager it is
    attached to -- this client's OWN -- back to state 1 (write at .xtls 0x9AC803B; HideWatch mode 3 tcB145). For a
    client's own login that is the start of its field load (MoveFieldCompleteAck -> 1, PlayAllPreLoadSequencerAck ->
    2, the world update -> 3); for a remote character nothing ever moves it on: the host's manager stayed at 1 for good
    (tcB143: 3 until the guest's login at 00:08:42, then 1), so NONE of the host's scenes ticked again.
  * The fix (merchant_unhide=1, both roles; merchant_unhide.cpp): the LOGIN GUARD, mid hooks 0x99C17A / 0x99C17F: for
    another player's character ([char+0xA0] != this client's user) this client's own manager state is noted before
    the call and a 3 that dropped is put back to 3 (tcB147: "state before 3 after 1" -> restored; the host's manager
    stays at 3). With it the host's shop NPCs finish their login and unhide on their own like solo (+0xCF 1, hide 0
    at 8 s), and in tcB148 the HOST opened Renee's shop with the guest in the world ("Hernand Butchery - Buy / Sell /
    Leave"). The guest: its shop NPCs belong to the host's scenes (copies in X) that its client never ticks; after
    4 s their +0x3E0 is cleared (the scene-start step) and the hide bit cleared with the game's setter 0x18CD090:
    Renee shows on the guest. The guest's prompt (C1, now fed X by guest_seq_route) finds her 5 shop entries in X but
    each fails its target condition (0x2569C661 / 0x669DBA09): next.
  * Shop scenes by dictionary hash (lookup3 seed 0xC5EDE): butcher, smithy, grocery, general, bank, pub, clothes,
    leather, smuggler (merchant_unhide_logic.h, unit test optd_merchant_unhide).
- 2026-11-09 02:00 The user tested by hand (tcB150): with merchant_unhide=1 every shopkeeper shows for both players,
  and the HOST bought an item in co-op (money went down). The guest cannot interact with any shopkeeper. Traced:
  * The guest's copies of the shop NPCs finish their login after the release (+0xCF 1, hide 0; tcB151).
  * Their entries 0x06 / 0x07 (the shop, key 0x000F4321) pass every player / NPC condition leaf on the guest once
    npc_category has the host's table (tcB155; early refusals were CheckQuestDialogCategory before the table came).
  * The blocker: the dynamic entry's scene check 0x22EE2D0 (C1 redirects its lookup to X; 373 of 373 redirected,
    all result 0). Counted (tcB156/157): the scene is found, +0x168 0, the focus check 0x215B230 passes, its mission
    (0xC4FA) checks pass, then `mov r10,[r13+8] ; test r10,r10 ; je 0x22EE66A` (bl = 0): the ENTRY's data condition
    [entry+8] (CompareSequencerTargetParameter, which player the scene serves) is NULL in the guest's X copy; on the
    host it exists. So the prompt is refused for the guest at that step.
  * ByJunter's analysis (s46 nativo.md 1.4) adds: even past the prompt, the shop opens through the scene owner's branch
    (StageBranchStateReq 2880 branch 6 on the HOST's scene, broadcast ToNear: it would open the host's dialog too),
    and a buy checks the scene as the manager's current stage (shop_guest covers that). His plan: the guest's own
    shop scene (mgr2 twin) rather than the host's. Next: decide between that and a guest-local branch of X.
  * Probes added (test builds): per-entry verdicts for shop NPCs, their condition leaves ("client SHOP"), the scene
    check counters at 0x22EE47A / 0x22EE49E / 0x22EE515 / 0x22EE529 / 0x22EE5CA.
- 2026-11-09 03:20 *** THE GUEST'S OWN SHOP MENU OPENS (tcB164): "Hernand Butchery - Renee: Buy / Sell / Leave" on the
  guest's screen, with the host in the world; the host's own scene untouched. (User asleep: working down the list.)
  * Design picked by the user (2026-11-09): the guest's own shop scene. On 3576 the server's prepare starts in
    protected code (ByJunter's server twin was only ever a probe), so the twin is built on the GUEST'S CLIENT
    (shop_twin.{h,cpp}, shop_twin_logic.h, [session2] shop_twin=1 on both):
    - guest: each host 2380 of a shop scene (merchant_unhide_logic's 9 hashes) is followed by a twin 2380: actor,
      owner and the player binding = the guest's character, id = host id + 0x10000000; its first 2920 likewise; the
      host's 2292 is preceded by the twin's. The twin lives in the guest's own manager M (state 3), its entries carry
      the guest's own data condition (the X copy's were null: 0x22EE2D0 refused), its NPC logs in natively.
    - every client request whose id is a twin's (2880 / 2839 / 2752 / 2620 / 3117...) is WITHHELD in guest_send;
      a branch is answered locally exactly as the server answers the host (tcB116 bytes): branch N: 2708
      SetAvoidByCutScene(1), 2436, 2920 (1, N); branch 0: 2436, 2708(0), 2920 (2, 0, the scene's root from its
      initial 2920, 0). A chart event: its 2187.
    - the shop menu's choices (SetQuestDialogAck 3086 entries of group 0x000F4440, Renee 0x0A / 0x0B) are in the
      HOST's per-player table only (put there by the NPC's scene functions on the controlling client): the host
      collects them from its client's 3086s (sniffer tap) and sends them as tunnel control state 17; the guest adds
      them to its own 3086 for that NPC. Without them the dialog opened and closed itself (tcB161).
    - the interaction (3008/3073 with key 0x000F4321) goes to the host's server and is accepted (2120 / 2794).
  * Tests: optd_shop_twin (twin 2380 / 2920 / 2292 rewrites, local answers byte-exact, the dialog frame and merge).
  * Harness: walkto.py stops its sweep when the guest's mod writes bin64\shop_twin_open.flag (a twin past branch 0);
    an early shot 0.5 s after each key.
- 2026-11-09 03:45 *** THE GUEST BOUGHT FROM A MERCHANT ON ITS OWN (tcB166): "Butchery | Buy" opened on the guest's
  screen, "Purchase [Small Bag]? Price 60" confirmed, "Inventory Expansion" shown; on the wire the guest's own
  BuyStockFromStoreReq (user 2 0xA01001FA) and the host's server answered it exactly as for the host's own purchase
  (tcB150): RemoveItemToInventoryAck (60 coins from the GUEST's inventory), AddItemToInventoryAck (the bag),
  VaryExpandedInventorySlotAck. The host bought nothing; its scene untouched. Both alive, SAVES OK.
  * The block in tcB165: "Buy" brought the greeting back. The server keeps a state per scene (the type of its last
    2920) and answers the same request differently: branch 0 after the greeting = "the menu" (type 2), branch 0 from
    the menu = "the store opens" (type 3). The twin always got type 2. From 30 host captures (state = last type,
    initial 0): 0 -N-> 1 (2708 on), 1 -N-> 2 (+root, 2708 off), 2 -0-> 3 (Buy / Sell), 2 -N-> 7 (Leave), 3 -N-> 2,
    7 -N-> 0 (+root). shop_twin now keeps that state per twin (stl::next_branch_type); tcB166's guest went
    6 -> 1, 0 -> 2, 0 -> 3 (store), 0 -> 2, 3 -> 7, 1 -> 0, the host's own sequence.
  * Harness: walkto.py's stop file path had a literal backspace (python "\b" written by a script): fixed through
    PowerShell; the flag is now written at the menu (type 2) and removed by twocopy.sh after each run.
  * HOST CRASH FIX (tcB117's): stage_candidate_guard ([session2] stage_candidate_guard, default 1): a mid hook after
    the 3117 handler's vf48 call (0x2CECC2B); a null candidate holder (its scene deleted) skips the write and goes on
    at the handler's own exit 0x2CECCBF (r15 = 0, as the normal path). Bytes checked before installing.
  * Next: Sell, another shop (smithy / general store), the host shopping at the same merchant at the same time, the
    twin when the host walks away (its 2292) and comes back.
- 2026-11-09 04:15 *** THE GUEST SOLD TO A MERCHANT ON ITS OWN (tcB170) *** (and bought again in tcB167).
  * tcB168: "Sell" is branch 1 from the menu; the twin answered it as Leave (type 7) and the shop closed. Host alone
    (tcB169, -NoGuest) captured the server: menu (2) -1-> 4 (the sell window), back -0-> 2. So the menu's choices
    lead to the scene chart's nodes: Buy 0 -> 3, Sell 1 -> 4, Leave 3 -> 7 (an unseen choice N: N + 3 for now).
  * tcB170: the guest went 6 -> 1, 0 -> 2, 1 -> 4 (sell window), 0 -> 2, 3 -> 7, 1 -> 0; its own
    SellItemToItemStoreReq (user 2) answered by the host's server: RemoveItemToInventoryAck (the item from the
    guest's inventory) + AddItemToInventoryAck (the coins). Both alive, SAVES OK.
  * The host now logs its own shop scenes' transitions ("ShopTwin [host scene] ... state S branch N -> type T",
    flagged when the twin's table would differ) and writes the harness's stop flag at its menu, so other shops'
    charts can be checked host-alone (tcB171: the smithy).
- 2026-11-09 04:40 Other shops / host far (tcB171-172).
  * Every merchant's menu in the captures has the same shape: two shop choices (group 0x000F4440: Buy / Sell, index
    pairs 0A/0B, 06/07, 02/03, 0C/0E, 11/13, 0F/10) or one (0x00, 0x04), plus Leave; so the butcher's chart (Buy 0 ->
    3, Sell 1 -> 4, Leave 3 -> 7) very likely holds for all. tcB171 (host alone at the smithy, -10636.5 -3848.3): its
    scene 0xA7132561 is prepared, but the scripted aim never got the blacksmith's "Shop" prompt (the walk ended in
    his archway; a display sword "E Buy 2.51" is a gimmick). Not retried: the chart check is the host's own log
    line "ShopTwin [host scene]" whenever a shop is used.
  * tcB172 (host teleported 430 m east, guest at the butcher): the server DELETED the butcher scene when the host
    left (2292 of 0x194, and the twin with it), no scene came back, and the guest's market was EMPTY (no Renee, no
    townsfolk in the shots). So with the host far, the guest has no merchants -- and no townsfolk: the server
    evaluates the world's cells only for the field owner's stage manager. 3576's owner gate found: 0x2AE972A
    `cmp [r15+0x180],eax ; jne 0x2AE9CB7` (2976 0x288B87A), in 0x2AE9650. ByJunter's fix for this (user2_stages,
    "U-1": user 2's own manager armed >= 64 m from the host, its own cells, its NPCs controlled by user 2, drained
    when close again) is 8200 lines with ~15 hook points written for 2976 and never ported. Recorded as the B02
    far-world gap. (Also: the host's tp to (-10238 640 -3958) left it "Badly hurt" -- a fall; test artifact.)
- 2026-11-09 05:55 *** REGRESSION FOUND IN THE RELEASE JOIN (B01 lobby): the host never entered the shared world. ***
  * The release defaults: the layer's native ini (native_mode_text.h) now carries merchant_unhide=1,
    guest_shop_check=1, shop_twin=1 for both roles (B02b). tcB173 (no -Lobby) showed the harness writes its own inis,
    so the release path is only exercised with -Lobby; the harness's -Lobby now continues with the scripts / walks on
    the restarted copies ($LobbyGo).
  * tcB174-176 (-Lobby, usercopy) and tcB177 (-Lobby, the default save): the guest entered the world, the HOST's client
    stayed "loading (actor parked at 0,1000,0)" forever behind the Co-op+ title menu (tcB7/8 on 11-07: in 38 s).
    Bisected with the 11-07 package's native cdmp.asi (works, tcB179) and the native commits (tcB180-183): the cause
    is 0c78f10 (TransformSync sector +0x548: the NPC hold now really holds and releases NPCs instead of dropping
    them). In the lobby both games press Play at the countdown, so user 2's character logs in while the host's client
    is still loading; with the hold working, that load never finishes. (Without the lobby the host is in the world
    before the guest joins: tcB164-172 fine.)
  * FIX (layer, coop_lobby.cpp): on the native base the host's game loads first (HostSetup + Play) and sends the
    guest its "go" only once the host is IN the world (in.in_world); the guest's existing native path presses Play
    then ("the host's world is up"). The guest's waiting line: "<host> is loading the shared world. You join right
    after." tcB184: the check.
- 2026-11-09 06:15 *** THE RELEASE FLOW WORKS AGAIN, AND THE GUEST BUYS IN IT (tcB184/185, -Lobby). ***
  * tcB184: "both games are ready: our game loads the shared world first" -> host in the world -> "the host's world is
    up: Play" -> guest in 41 s. One stray E: the host's press thread waited for its window and pressed E 2 min after
    the host was in (E = interact): the press now stops when the game is already loading or in the world.
  * Harness: the lobby phase keeps each window in front until its game logs "in the shared world" (host, then guest).
  * tcB185 (usercopy, the release inis written by the layer, no test overrides): host in 06:08:23, guest 06:09:08; the
    guest opened Renee's shop, 6 -> 1, 0 -> 2, 0 -> 3, bought the Small Bag (money 114.54 -> 113.94, "Inventory
    Expansion"), closed, left. Both alive, SAVES OK. ROADMAP B02b -> [~].
- 2026-11-09 07:00 B22a, the mod part: CO-OP IS REFUSED, SOLO STAYS USABLE, THE REASON SHOWN (tcB188).
  * Native (local commit): in the release path (no CDMP_ROLE, co-op mode from the layer's ini) a failed co-op save
    redirect no longer ends the game: co-op is refused for this launch (net, coop and session2 forced off, the
    loaded config too), the save hooks have already rolled themselves back, so the game is a plain solo game on the
    player's own saves; the reason is exported (cdmp_coop_refused_reason). The test roles (CDMP_ROLE host/guest)
    stay fail-closed (the game ends: a test must never reach the real folder). Test aid: bin64\
    cdmp_test_refuse_coop.<host|guest>.flag refuses co-op in that role with the sandbox redirect still installed
    (harness -RefuseCoop host|guest; twocopy.sh removes the flags).
  * Layer: the lobby reads the export once the co-op launch starts; the menu says "Co-op could not start on this PC
    (<reason>). You are playing solo and your saves are safe. Restart the game to try again.", the next launch is
    solo, and the partner is told (lobby message 22): "<Host>'s game could not start co-op. Restart the game to play
    solo." (its next launch solo too).
  * tcB188 (-Lobby -RefuseCoop host): the host refused at its co-op start, the guest was told 15 s later, both stayed
    solo with the notes on screen; SAVES OK.
  * Open for B22a: nothing in the mod part; the installer part was done 11-08 (Plitos credited in the changelog at
    release).
- 2026-11-09 07:00 Dialog entries the guest lacks (B02a, open): with BOTH players on the same save (tcB186,
  -GuestVariant usercopy) 12 NPCs still give the host's client extra quest-dialog entries (groups 0xF435B, 0xF42B6,
  0xF44B6, 0xF4250...) and the guest's only the greeting (group 1). Not progress: per-player state on the server. The
  server's builder (SetQuestDialogReq handler 0x2CD9DD0 -> 0x2B72670) looks the NPC up in the ASKING player's table
  (0x1C35790: hash map +0x68/+0x78/+0x80; record +0x10 base category, +0x11 AI category, +0x12 sent in the 3086) and
  collects candidates in protected code (0x14BAEB240): the categories / dialog lists there are set by the NPC's AI
  (AIFunction_SetQuestDialogCategory / RequestNearbyQuestDialog) for the player whose client controls the NPC (the
  host). npc_category's getter hook (0x1C358E0) covers the condition checks, not this builder. Fix options: copy the
  host's record bytes into user 2's record before the builder runs for user 2 (server side), or share the host's
  entries with the guest's client like the shop choices (then check the server accepts the guest's choice).
- 2026-11-09 07:05 tcB189 (same save, npc_dialog_share=1, experiment, default 0): the builder hook gave user 2's
  existing records the host's category 67 times (354 builds for user 2, 226 with no record yet), and the guest's
  lists STILL lack the extra groups (19 NPCs differ, 0 same). So the category does not gate them: the groups come
  from per-player dialog lists the NPC's AI / stage fills for the controlling player (RequestNearbyQuestDialog,
  QuestDialog_Stage). Next idea, not started: the host's extra entries shared with the guest's client as for the shop
  choices (all groups but 1), then a live check whether the server accepts the guest's choice of such a dialog (it
  may refuse a dialog absent from user 2's own list). Native local commit (the hook stays, off).
- 2026-11-09 07:15 The guest's missing dialog groups ARE STAGE DIALOGS (decomp, B02a). QuestDialogEndReq's handler
  0x2CD9600 -> 0x2B70F00(the player's table, ...): the dialog key is looked up in the static dialog table
  ([0x71C6FA0], entry +8 = its stage key) and then 0x2228460(the player's user +0xA0, the stage key) must find that
  stage in the PLAYER'S OWN stage manager, else it fails (0x714ADA0). So a 0xF435B / 0xF42B6 / ... entry is a
  QuestDialog_Stage of a scene the host owns (like the shop menu in its funcnpc scene): sharing the list entries alone
  would show the guest choices the server then refuses. The way that works is the shop's: the guest's own copy of
  the scene (shop_twin generalized from the 9 shop resources to the NPC scenes with dialogs) + the host's entries for
  that NPC. Not started: it needs the per-scene branch charts (the shop chart came from captures) -- a bigger piece
  of B02a.
- 2026-11-09 07:30 Local packages (NOT published; publishing needs the user's OK, GPL native code): tools/package-native.ps1
  -> dist\CrimsonDesert-SeamlessCoop-0.1.0-alpha+7048ff23.zip and ...+7048ff23-shared-world.zip (11.8 MB; native
  8a696fd-era build + the layer with tonight's lobby / B22a / shop-key changes). tools/test-install-fake.ps1 on the
  shared-world zip: ALL STEPS PASS (install, vanilla on/off, uninstall leaves bin64 as before, loader reuse cases,
  mod-manager layout).
- 2026-11-09 07:40 tcB191/192: the host teleports 430 m away while the guest's own store window is open. The guest's
  store stayed open and normal (shots every 10 s for 2 min), then closed cleanly; both alive, SAVES OK. The host's
  butcher scene was NOT deleted (other scenes were: 0x1DE, 0x207, 0x208): with the guest standing there the server
  keeps the scene alive (in tcB172 the guest arrived after the host had left, and the scene was gone by then). So a
  host leaving mid-shop does not pull the guest's shop away. Harness note: Ctrl+Shift+F6 reaches BOTH copies' layers
  (each dev script ran twice, 21 s apart): harmless for tp scripts, keep in mind for others.
- 2026-11-09 (after the user's return) B02a, which stages the guest gets (tcB186 packet logs: host
  cdmp_packets.host_20261109-065804.log, guest cdmp_packets.guest_20261109-070012.log; tool scratchpad schedcmp.py):
  * CreateFactionSpawnStageAck: host 347, guest 0. CreateNPCScheduleStageAck: host 87 (21 deleted later), guest 39;
    48 schedule stages only the host gets, none only the guest. Every schedule stage in BOTH logs is addressed to
    user 0xA0100001 (the host's user): the server creates them for the field owner and the guest only sees the
    ones near it. UpdateStageListAck (2946): host 82, guest 81 (the quest stage lists match).
  * Format 2989: 04 | user u32 | stage key u32 (0x000F4A87...) | hash u32 | ... | position floats near the end.
  * The guest's missing dialog groups (0xF4250, 0xF42B6, 0xF435B, 0xF4440 shop, 0xF4490, 0xF44B6, 0xF4614) are NOT
    schedule stage keys (no overlap with either side's 2989 keys). Same key range, other table: next, the static
    dialog table entry's stage key (+8) for each group, then which message creates that stage for the host.
  * In that run the guest's 3086s carried no non-greeting group at all (only group 1), the host 7 groups.
- 2026-11-09 B02a ROOT: the guest's missing dialog groups are WORLD STAGES the server runs only for the field owner.
  * In tcB186 the host got 36536 UpdateStageStateAck (2349: user | stage key | state u32 ...), the guest NONE at all.
    The dialog groups the guest lacks are stage keys: 0xF4490 / 0xF44B6 / 0xF435B each get a 2349 (state 0x0A) for user
    1 right before the NPC's 3086 lists them; 0xF4250 also appears in ActorLoadDataAck / UpdateFactionBlockDataAck.
    No create message carries them (not 2989 schedule, not 2947 faction spawn, not 2380): they are the cell / region
    stages the server's world evaluator (0x2AE9650, owner gate 0x2AE972A) runs for the field owner's manager only.
  * So B02a's missing dialogs and the far-world gap (tcB172) are ONE problem: user 2 has no world stages of its own.
    The fix for both is ByJunter's user2_stages (user 2's own stage manager evaluated by the world cells), not a
    per-scene twin. Next: port user2_stages to 3576.
- 2026-11-09 B02 far-world: ByJunter's U-1 (docs/research/s25, summarised): the far guest's NPCs need user2_stages=1 +
  user2_timer_route=1 + user2_world_anchor=1 + user2_fast_travel / user2_revive_manager (funnels) and, in practice,
  user2_npc_control=1 with the S35 containment keys and observer_hold (else NPCs loop: walk, vanish, reappear). Live on
  2976 (Shadow, two PCs, kit 38/39): town NPCs with routines around the far guest, no duplicates, 0 ERR. Still broken
  there: shops far away (U-f skips sequencer scenes), a 4-9 cell dead zone, carts. U-1 creates NPC SCHEDULE stages
  only: no quest-dialog / world stage states for the guest (so it does not fix B02a's dialogs).
  * Port to 3576 started (build/port-u2s/map.json): matched by call offsets inside already-matched functions (heads
  alone mislead: the code changed). T 0x2ADA9B0, P 0x2ADBCE0, Arm 0x2ADC230, List 0x2AF5570, Interest 0x2ADC910,
  L 0x2ADA850, U 0x2ADA6C0, gate fn 0x2AE9650, gate 0x2AE972A / ctx 0x2AE9716. Register allocation CHANGED (the gate's
  default is r12d on 3576, esi on 2976): every mid hook's register reads must be re-derived, not only its address.
- 2026-11-09 12:35 tcB193 (same save, butcher, dialog_share_all=1 on both, native local build): the host now sends
  EVERY non-greeting dialog group of its NPCs (state 17), and the guest's own 3086s get them added: 0xF4490 (NPC
  0xB010006E), 0xF435B (0xB0100444 / 0x445 / 0x57C / 0x587), the shop groups; both alive, SAVES OK. Correction to
  07:15's note: QuestDialogEnd's handler 0x2B70F00 fails with 0x714ADA0 on the character / NPC scope (0x2228460 is
  the character scope attacher, 0x2D11EE0 = SAM::FindActor), and with 0x714BB68 when the dialog's condition
  (0x253F380) fails for the player's character -- it does NOT look for the stage in the player's own manager. So the
  guest's pick of a shared dialog may well be accepted. The grocery merchant (0xB0100421, scene cd_seq_funcnpc_grocery)
  carries shop 02/03 + 0xF435B 0x92: a merchant with an extra dialog. tcB194: the guest opens it.
- 2026-11-09 12:45 *** tcB194: the guest's own shop at a SECOND merchant: "Hernand Grocer's Shop - Delkin: Buy / Sell /
  Leave" opened on the guest's screen (shop twin of cd_seq_funcnpc_grocery), the Buy window with the grocer's stock and
  the guest's own money (114.94), and the guest BOUGHT (its BuyStockFromStoreReq -> RemoveItemToInventoryAck +
  AddItemToInventoryAck). Both alive, SAVES OK. So the butcher's chart holds for the grocer too.
  * Delkin's extra dialog (0xF435B 0x92, now in the guest's list with dialog_share_all=1) is not in the shop menu: it is
    reached through another prompt. Next: host alone at Delkin to read the prompts, then the guest picks it.
  * user2_stages port (far-world gap): every point mapped and applied (build/port-u2s/apply_u2s.py, map.json): C
    0x2A5D120, L 0x2ADA850, U 0x2ADA6C0, N 0x2A4BF00, T 0x2ADA9B0, P 0x2ADBCE0, O 0x2225930 (Logout thunk: 26 reason-25
    callers), gate 0x2AE972A (4th argument in RSI on 3576), gate 2 0x2B7E1DA (frame 0x1D2A8; return 0x2ADEA54), element
    0x2AE8700, Arm 0x2ADC230, Enqueue 0x29ACC70, List 0x2AF5570, Interest 0x2ADC910, vf40 return 0x2ADDC92, FindActor
    0x2D11EE0, scope release 0x1618660, data: ServerNet 0x71C05F0, period 0x715A778, 2043 Troc 0x7403090, zero cell
    0x58F4A30, lookup 0x71C0608; errors by NAME from the game's own registration table (1387 names,
    build/port-u2s/errglobals.json): eErrNoEmptyStage 0x714C070, eErrNoRequestLogoutBySequencer 0x714B93C,
    eErrNoUserAlreadyLogouting 0x712DFA8. Vtables by RTTI: StageMgr 0x5EE0B88, SchedMgr 0x5EDFA10, SchedStage 0x5EDFA58,
    desc NPCSchedule 0x5ECDB60 (+ the child descs). Manager layout: schedule mgr +0x170 (2976 +0x148), cells
    +0x6A8/+0x6AC, +0x6B9/+0x6BA, state +0x6D0. The controller write (U-e, docked_gimmicks_logic.h) 0x29CCBB1 (same
    bytes and registers), block 0x29CCB4C, owner read 0x29CCBA6. Not mapped (log only, left REFUSED by their byte
    checks): D, S, F, H. Fixed in passing: user2_funnels read the manager's +0x6A0, 3576's byte is +0x6D0.
- 2026-11-09 13:05 tcB195 (host alone at Delkin): the only prompt is "R Shop"; the bottom-right panel lists 5
  interactions for Delkin (cycled with the mouse wheel / CAPS): the extra dialog is among them. User's question about
  github.com/cranebeaker/coop-anything: a general agent playbook + sync test tools; its Crimson Desert page (10-21)
  is a 2976 ByJunter revival with quest-accept replay, behind us. Kept from it: an NPC's quest / hand-in option stays
  greyed out until the player KNOWS the NPC (NormalLearnKnowledgeListAck, from its meeting scene) -- relevant to the
  guest's shared dialogs; its `chaos` UDP relay (lag / loss between two copies) as an optional pre-release check.
  Native local commit 89fbc86 (user2_stages port + dialog_share_all); the user2_stages unit test: 19 failures, the same
  19 as before the port (fixtures still on 2976 opcodes), the two pinned constants updated.
- 2026-11-09 13:25 tcB197 (host 430 m away, user2_stages=1 + timer_route + world_anchor + npc_control): the ported
  points install (C L U N T P O, the gate, the element hook in; D S F refused as expected, log only), the manager's
  state reads 0/2/3 (the +0x6D0 offset is right). Mode 1 could NOT arm: user2_world_anchor refused itself (its build
  fingerprint, the 2245 handler's write 0x29C2915 / the collector's read 0x2C38E28, is 2976's). world_anchor writes the
  ServerField's "world observer" slot itself (2976: byte +0x2E4, key +0x2E8, under the field's WindowsRWLock<4,1>);
  on 3576 the slot is not found yet: the 2245 handler (TrocTrUpdateWorldObserverStateReq vtable 0x5EED4D8, slot 2
  0x2C43980 -> 0x2EE64D0) has no direct byte write, the scans for byte X / dword X+4 pairs (X 0x2E0..0x300) found
  nothing (the +0x2F0 read at 0x2EDBDFF is an actor component's type switch, not the field). Next for it: a live dump
  of the host's ServerField around +0x2D0..+0x320 while a 2245 is handled (a DR watch from the handler), or the
  collector's anchors from its caller. Parked; B10 first.
  * B10 built (native, not committed yet): quest_route.{h,cpp}, [session2] quest_own_progress / quest_shared_credit
  (host, default 0). On 3576 the quest component's game-event callback is the thunk 0x2B9C660 -> 0x2B7A7A0 (registered
  by ServerQuestActorComponent vfB0 0x2B744B0); it asks ServerField::vf40 0x2D98440 (return 0x2B7A7DB) for the quest
  manager, i.e. the field owner's: natively the guest's kills / zones / interactions advance the HOST's quests. Own
  progress: a mid hook at 0x2B7A7DB gives the event the acting character's own manager ([[[char+0x68]+0x138]+0x18],
  vtables 0x5EE8C80 / 0x5EE8DD0). Shared credit: the thunk (inline hook) runs the callback a second time with the
  other player's manager (the game's apply functions only advance steps that manager has active).
- 2026-11-09 13:00 world_anchor PORTED to 3576 (native, build + optd_world_anchor pass): the world-observer slot is
  ServerField +0x198 (u8 on) / +0x19C (u32 key) on 3576 (2976 +0x2E4 / +0x2E8): the 2245 handler's worker 0x2EE64D0
  writes it at 0x2EE653A (rsi = the field, lock [rsi+8]); ServerField::Reset (vtable 0x5EF2B10 slot 4, 0x2D8E290)
  clears +0x19C, +0x198 and the owner +0x180. Generation +0x2D0 (the 3549 port's "key"). The field lock
  pa::WindowsRWLock<4,1>: vtable 0x5B52BF8 (RTTI .?AV?$WindowsRWLock@$03$00@pa@@), Unlock slot 4 0x1554690, TryLock slot
  5 0x15546C0. Also fixed: world_anchor read the TransformSync's field at +0x500 (2976); 3549+ is +0x518.
- 2026-11-09 13:05 B04 (shared map / knowledge) research on 3576: fog is a per-player bitmap sent at login as 47
  FogDataAck (2196, 32705 bytes each; header u8 seq | u32 user), host and guest each get their own; no C>S fog
  message in play (the reveal is server-side). Visited zones: RegisterVisitZoneReq 2879 / Ack 2564 (handler
  TrocTrRegisterVisitZoneReq vtable 0x5EEE6F8, slot 2 0x2BFBB80: real logic). Knowledge: AcquireKnowledgeReq 2285,
  NormalLearnKnowledgeListAck 2711, ShareKnowledgeDataListReq/Ack 2996/2628 (native sharing), LearnKnowledgeByCheatReq
  2986 (handler 0x2CA2FD0: looks parse-only like the quest cheats). Direction: (map) merge the host's fog bits into the
  guest's FogDataAck at its login + replay each player's RegisterVisitZoneReq as the other's own Req; (knowledge) the
  game's ShareKnowledge path or AcquireKnowledgeReq replayed for the other player. Not started.
- 2026-11-09 13:04 B10 option 3 LIVE (tcB198, quest_own_progress=1 on the host): "QuestRoute: a game event of character
  0xA01001F6 (the guest) applied to its OWN quest manager 0x5A53E4EF500 (the field's was 0x5A4F219ED80)": the guest's
  game events no longer go to the host's quests. (Quest steps themselves not yet checked: needs a quest the guest has.)
  B04 built: fog_share (native, [session2] fog_share, host, default 0): the host's own FogDataAck chunks recorded from
  its sniffer tap; the guest's FogDataAck chunks OR-ed with them in take_frame (fog_share_logic.h, test passes). The fog
  is a plain bitmap (full captures of tcB186: runs of 0x01 / 0x80 / 0xC0 bits, 1,520,768 bytes in 47 chunks for both).
- 2026-11-09 13:08 tcB198: with world_anchor ported, user2_stages' arm check on user 2's manager is READY; the server
  positions read right after the teleports (host (-10252, -3960), guest (-10681, -3902): 6 cells of 64 m). It did not
  arm only because U-1 armed at >= 10 cells: ByJunter's own "4-9 cell dead zone" (PENDIENTES.md 4: the host's manager
  creates stages 1-3 cells around its character), exactly tcB172's empty market. Changed (his proposal): arm at >= 4
  cells after 3 s, drain at <= 2 after 5 s, the overlap left to U-f2's dup_skip. Native local 5bb2d51 (+ fog_share).
- 2026-11-09 13:09 tcB198 also: QuestRoute counted 400 game events in 3 min, 45 of the guest's given its own quest
  manager, 354 the host's (the same as the field's), 0 unreadable; the guest's mission-list updates came at the usual
  rate (103 in 342 s vs 79 in 262 s without the route): nothing broken. A quest STEP of the guest advancing on its own
  is still to be shown (needs a quest the guest has active; the usercopy save's are not mapped to places yet).
  Notes for the next B tasks: B09 on native = the host's scene stages already reach the guest (ByJunter: they took
  over the guest's camera / UI / input; his guest_seq_foreign is 2976-pinned) -> to test with a host cutscene first.
  B11: no player item-drop / give message in the 3576 Troc table (only shop / bank / exchange-event ones).
- 2026-11-09 13:10 tcB198 shots: the guest at the Hernand butcher with the host 441 m away (Rodbel Fishing Dock): the
  market EMPTY (no Renee, no townsfolk), as tcB172 -- the baseline for tcB199 (U-1 arming at >= 4 cells).
- 2026-11-09 13:12 Planning notes. B14: on native the merchant fix (merchant_unhide's login guard: another player's
  login no longer stops this client's stage manager) is the likely cure of T21's empty arm-wrestling table too (an NPC
  whose AI stalls); the table is in the inn's cellar (aw_route.sh: -10660, -3829, hold R "Try arm wrestling"): a
  two-copy check there is next after tcB199. B11 / B12: the bag's item transaction (reverse-engineering-3576.md
  "Items": stage 0x29EA7C0 / 0x29ECDE0, check, commit, notify) has a container-to-container move (0x29F3070, reasons
  0x18 / 0x13: tx+0x40 remove, tx+0xA8 add) -- a gift = that move from one player's bag to the other's; RE needed.
- 2026-11-09 13:20 *** tcB199: U-1 ARMS ON 3576 *** (host 7 cells away, arm at >= 4): "U ARMED", arms 1, the gate
  passed 11, the collector's guard on user 2's own manager 11, user 2's manager state 2 with 65 NPC SCHEDULE STAGES
  of its own (elements: schedule 65 created, sequencer scenes 17 skipped, faction stages 347 skipped -- the
  user2_stages_elements=schedule filter). world_anchor never set: "its field unreadable" -- its ServerField vtable
  check was still 2976's (0x5B29A60): fixed to 0x5EF2B10. Also ported: the NPC family / road descriptor vtables
  (npc_family_logic.h, user2_road_logic.h; RTTI: ICreateServerActorDesc 0x5ECDC48, NPCSchedule 0x5ECDB60,
  InstantGimmick 0x5ECDB08, child descs Simple 0x5ECDD60 CatchTarget 0x5ECDDE0 AutoSpawnGimmick 0x5ECDD40
  CombinationGimmick 0x5ECDD00 SavedAttached 0x5ECDCE0 LinkedVehicle 0x5ECDCA0 Docking 0x5ECDDA0 Rider 0x5ECDDC0
  NPCScheduleGroupActionMember 0x5ECDCC0); their descriptor field offsets are not re-verified. Tests unchanged
  (npc_family 0, user2_road 8 and user2_stages 19 pre-existing failures, world_anchor passes).
- 2026-11-09 13:35 tcB200 (anchor fixed): world_anchor SET and verified (field slot 01 / user 2's key; +0x198/+0x19C
  right). U-1 armed again (60 own schedule stages, their 2335s sent to user 2), but the guest's loads after the arm
  had none of the town's people (by name, from the full captures: near the host the guest loads Boy, Girl, Dancer,
  Hernandian Guard, Noble, Punter, Dalia, Grover, Haldwin ...; far with U-1: only wildlife and road travellers, as
  without U-1). Cause: "timer route: 2043/2045/2046 to its own manager 0/0/0" -- the 2045 / 2046 (2049 / 2050 on
  3576) schedule / faction updates of user 2's stages ran on the HOST's manager: funnels recognises the timer
  handlers by their vf48 return addresses in the protected section, still 2976's. 3576's, from the funnels' own
  vf48 caller table (x24 / x40 / x40 under 2047 / 2049 / 2050, each right after a call [rax+0x48]): 0xE68CDEA,
  0xE6911DF, 0xE6946CF. Fixed (user2_funnels.cpp timer_vf48_ret). tcB201 next, the guest in the open market.
  (The actor load / remove churn on the guest, ~100 each per 30 s, is normal: 400-800 near the host.)
- 2026-11-09 13:45 *** tcB201: THE FAR GUEST GETS ITS OWN TOWNSFOLK ON 3576 *** (host 430 m away, U-1 armed at >= 4
  cells, timer route fixed): user 2's timers on its own manager 34 / 281 / 281 (2043 / 2045 / 2046), 62 own stages
  sent to it, and U-e gave user 2 183 NPCs (161 alive at the end) -- the schedule spawns now run for the guest. Its
  loads after the arm include town people absent from tcB198 / tcB200 (Jester, Meat Porter, Bird Hunter...). Shots:
  the guest's teleport landed on a roof, but a townsperson is in the street below (tcB201-g2) and the minimap shows
  NPC icons. Both alive, SAVES OK. Open: a street-level visual check (better guest spot), NPC behaviour (the S35
  containment keys are not on), shops far away (U-f skips the funcnpc scenes: shop_twin needs the host's scene, which
  the server deletes when the host leaves -- ByJunter's user2_shop_trade_list), the host's far spot (Rodbel dock has
  bandits: the host dies in every far run; harmless, but pick a quiet spot).
- 2026-11-09 13:50 Harness note: the only scripted spots 300-900 m from the Hernand market are camps (Rodbel dock
  -10238 -3958, events1 / hostcamp -10300 -3690): the far host is attacked and goes down in every far run. Harmless
  for the guest-side checks so far; a quiet far spot still to find (tprel exists for relative teleports).
- 2026-11-09 13:55 tcB202 (B04 fog_share=1, host usercopy, guest rps): the host recorded its 47 fog chunks, the
  guest's 47 were merged (all matched, 37 bytes changed: these two saves have explored nearly the same area -- both
  maps fully revealed around Hernand in the M-map shots). Mechanism works, no errors, both alive, SAVES OK; a visual
  proof needs a guest save with much less explored (the earliest prologue save) -- to do.
- 2026-11-09 14:00 tcB203 (B14, both at tp_inn_table.txt, the host sweeping 360 degrees with R held): the teleport
  height 617.6 puts the host UNDER the inn's floorboards on 3576 (shots tcB203-sheet.png), so no table prompt; its R
  presses reached other objects (interactions 0x000F42B1 / 0x2B76). Both alive, SAVES OK. The table's spot on 3576 is
  unknown: put on the user's hand-test list (a minute by hand vs many scripted runs).
  Layer (953380c, pushed): the release native ini now turns on for the host quest_own_progress, fog_share and the
  far-world set (user2_stages, user2_timer_route, user2_world_anchor, user2_npc_control); quest_shared_credit stays off
  (the optional bonus, to become a setting).
- 2026-11-09 14:10 tcB204 (quest_own_progress=1 + quest_shared_credit=1, both in Hernand, same save): the thunk hook
  installed ("shared credit on"), the guest's events went to its own manager, 0 ERR, both alive, SAVES OK; too few
  events for the 200-event counter line, so the first shared passes are now logged (native c149dc4). Checklist for the
  user's next session: docs/test-checklist-2026-11-09.md. tcB205: the release flow (-Lobby) with the new defaults.
- 2026-11-09 14:15 tcB205 (-Lobby, the release flow with the new host defaults written by the layer): host in the
  shared world 14:05:55, guest 14:06:40; QuestRoute / FogShare / WorldAnchor / User2Stages all ON from the layer's
  ini; the guest's own butcher shop opened and reached "Purchase [Small Bag]?"; both alive, SAVES OK. No regression.
  Building the local shared-world package with today's work (not published).
- 2026-11-09 14:20 Local package (NOT published): dist\CrimsonDesert-SeamlessCoop-0.1.0-alpha+9e684aa9-shared-world.zip
  (11.9 MB; native c149dc4 + the layer with the new host defaults). tools/test-install-fake.ps1: ALL STEPS PASS.
- 2026-11-09 14:35 USER REQUEST (new list item, ROADMAP B16b): a player interaction menu -- hold R on the other
  player (the game's own HOLD prompt: the R in a circle that fills while held, as at shopkeepers / shop menus) opens a
  menu like the shop menu with every minigame challenge, later trading (B11) and other player-to-player features.
  Geonode (Discord) made a proof of concept: "Kliff / R Rock, Paper, Scissors" over the other player (a tap prompt).
  Our scratch copy of Geonode's CDCO (3549, GPL-3.0) predates it (no such prompt in its source). Plan: on the native
  base the other player is a real actor on each client, so the prompt can be the game's own interaction UI: find
  how a shopkeeper's hold-R entry is built (the interaction table entry with the hold type; shop_twin's entries) and
  add a co-op entry for the other player's actor, or draw our own hold ring in the overlay as a fallback.
- 2026-11-09 14:45 B17 / B16b built (layer, pushed):
  * B17 revive by interacting: holding E (the game's interact key) next to the downed friend revives them, as well as
    Shift+F7 / the pad chord; the hint reads "Hold E or {ReviveFriend} to revive X". tcB206 (bandits, -ReviveWithE):
    the guest's walk to the downed host stuck at a cliff (194 m) -- no revive tried. tcB207 (both at the butcher, the
    host's KillCheatReq): the dev script's wait caps at 10 s and Ctrl+Shift+F6 runs BOTH copies' scripts, so the host
    went down at +10 s and the guest's E came 38 s later, past the 30 s bleed-out. tcB208 retimed (4 x wait 10000).
  * B16b first version: the hold-R prompt drawn on the other player (render/hud.cpp + ui::hold_prompt in game_ui.cpp:
    the R in a dark ring, a gold arc filling over 0.8 s, "Challenges" beside it), within 3 m with the other player near
    the middle of the screen and OUR game window in front (two copies on one PC); full -> activity_request_menu() ->
    the challenge tick acts as Shift+F5 (opens the challenge list, or accepts a received challenge). Shift+F5 kept.
    Next: the menu itself in the shop-menu style (a list of challenges, later trading) instead of the old picker.
- 2026-11-09 14:55 *** B17: REVIVE BY INTERACTING WORKS (tcB208) ***: both at the butcher, the host down by its
  KillCheatReq, the guest held E 1.3 m away: host log "Revive: Guest revived us" / "the other player revived us: up
  where we fell"; both alive, SAVES OK. (Bleed-out timer, the hold progress line and the "X revived you" credit were
  already there; Q / U kept.)
  B16b menu built (layer f20face, pushed): the shop menu's look -- the friend's name under "Co-op", stacked choices
  (each challenge, "Trade (coming soon)" greyed, "Leave"), W / S or up / down choose, Space / Enter select, Esc leaves
  (the keyboard hook holds them from the game while the menu is open), closes by itself after 60 s idle; the prompt
  reads "Interact". tcB209 checks it on the guest (hold R facing the host).
- 2026-11-09 15:05 *** B16b WORKS ON THE GUEST (tcB209) ***: facing the host 1.3 m away, the "R  Interact" hold
  prompt drew over the host; holding R 1.3 s logged "the hold-R prompt on the other player opened the interaction
  menu" and the shop-style menu showed (Host; Duel, Rock paper scissors, Hunting contest, Race to the waypoint, Hide and
  seek, Fishing contest, Trade (coming soon) greyed, Leave); S twice moved the frame to Hunting contest; Esc closed it.
  Both alive, SAVES OK. Polish after the shots: more room between "Co-op" and the name, a brighter bold prompt label,
  the prompt hidden while the menu is open. Both layer builds (native, classic) compile.
- 2026-11-09 15:20 B11 (trading) lead on 3576: MoveItemInventoryToOtherInventoryReq 3111 (TrocTr vtable 0x5EEC6F8,
  handler 0x2C948C0 -> worker 0x2E0B2B0(the sender's character 0x22F1DC0, err, u32, u16, u64, u32* FROM, u32* TO,
  u16, u16, u32 type)): the worker resolves TWO actors by key with ServerActorManager::vfunction5 (FindActor) and moves
  an item from one's inventory to the other's, after a condition (CompareTargetParameter) of the "other inventory"
  type entry ([+0x38] table, stride 0xA0, by the u32 type index). In-game this is most likely the transfer between
  the player's own party characters (Kliff -> another playable character) -- the game's own item move between two
  characters, a natural carrier for trades (from = the giver's character, to = the receiver's) if its condition
  accepts another player's character. Field order from the handler: u32, u16, u64, u32 from, u32 to, u16, u16, u32.
  No capture of a 3111 yet: put on the user's checklist (move one item to another party character once, the sniffer
  records it), then a server-side test of from/to across the two players.
- 2026-11-09 15:35 B19 anti_grief built (native 6417c60, test passes): the host records every NPC its client gets a
  dialog list for (3086) and drops the guest's DoFrameEventReq 3030 whose targets include one (targets: 677-byte
  entries at the payload's end, key first). tcB210: the guest's 11 swings at the butcher targeted 0xA01001C8 (a player
  character, not Renee), so nothing to drop -- the check is not shown live yet (user checklist). On by default for the
  host in the layer's ini (it only ever drops hits on people the host can talk to). Both alive, SAVES OK.
- 2026-11-09 15:45 Local package 0fb316f6 (NOT published; native 6417c60 + the layer with B16b / B17 / anti_grief on):
  test-install-fake ALL STEPS PASS. tcB210 also showed the guest's swings on a PLAYER character do no damage on native
  (host HP 300/300 throughout): the Co-op+ duel's own hit relay applies duel damage -- to check by hand (checklist).
  Items expected to work natively (the other player is a real actor of the shared world) put on the checklist for a
  look / listen: B20 partner audio, B18 combat feedback, B06 mounted hits, B05 other mounts, B09 cutscenes, B03 events.
- 2026-11-09 15:55 FPS (pre-release item): the release ini already has [sniffer] record=0. The game's own counter in
  today's two-copy shots with the release settings (tcB205, -Lobby): 96-148 FPS in the world per copy (DLSS, windowed),
  55-58 in shop menus (no DLSS there); tcB204 80-158. No mod-side slowdown visible here; the user's 20-50 FPS (11-08)
  came from harness settings (packet recorder, diagnostics on) and/or a heavier scene -- a measurement is on the
  checklist. B12 shared stash: the bank exchange (ExchangeItemFromBankReq 2113, handler 0x2C8ED90) works on the
  player's character object (FUN_1422F1DC0(user)), which owns both its bag and its bank: no simple swap; needs RE.
- 2026-11-09 16:05 B10 option 2 is now the optional bonus the design asks for: Co-op+ setting [coop]
  shared_quest_credit (panel: "Shared quest credit (same step: done for both)", off by default); the lobby writes
  quest_shared_credit=1 into the host's native ini at the next co-op launch. Layer tests: the native ini text with
  the new host defaults (quest_own_progress, fog_share, anti_grief, user2_stages, world anchor) and the option -- all
  tests pass; both layer builds compile.
- 2026-11-09 16:25 Shops far from the host (B02b "without the host at that step"): ByJunter's chain is
  user2_stages_elements=schedule+funcnpc (U-f lets user 2's manager prepare the cd_seq_funcnpc_* scenes) + user2_shop_route
  (user2_shops) + shop_probe (its points). shop_probe PORTED (native ff4cc7e): E 0x2AE9650 (same head), G as
  user2_stages' gate (4th argument rsi), P 0x2AE8883 in the element 0x2AE8700 after call 0x2AD8E60 (3576: rcx = r14 the
  manager, rdx = r15 the result, the element in RBX; frame unchanged), C 0x2C70C39 in
  TrocTrInteractionPreemptionStepReq's handler 0x2C70910 (frame: requester rbp+0x80, NPC +0x88, interaction -0x48,
  result -0x80), manager state +0x6D0; its opcodes were already 3549's. user2_shops prepares shops at >= 4 cells (was
  10, with U-1's new arm distance). tcB211: far host, the guest at Delkin with R.
- 2026-11-09 16:45 Far-world shops, status (tcB211 / tcB212, host 6 cells away, user2_stages_elements=schedule+funcnpc +
  user2_shop_route + shop_probe):
  * shop_probe on 3576: E / G / P / C / S all hooked. user2_shops' 15 scene-handler vf48 sites: 11 from their TrocTr
    vtables' slot 2 (2880 0x2CEA189, 2396 0x2CEA570, 2098 0x2CEAC8F, 2752 0x2CE9CEA -- tcB209 caught user 2's 2752 there --,
    2178 0x2CE902A, 2620 0x2BEE31E, 2869 0x2CEC18F, 2683 0x2CEBAD9, 3113 0x2CEB2F9, 2676 0x2CEB6E9, 2783 0x2CE9943); 2839,
    3161 and the 2048 / 2059 timers jump into the protected section: matched by opcode for a vf48 called from it. tcB212:
    "U2-a at the vf48 call of 15 of 15 scene handlers", route live (native d0b473e).
  * tcB211 (before the sites): user 2's manager saw 29 sequencer elements, 9 of them shops, "SKIPPED (a shop scene:
    user2_shop_route cannot prepare it)". tcB212 (sites in): 20 sequencer elements, NO shop elements at all, user 2's
    manager prepared 0 shop scenes. Likely: the guest standing at the market keeps the HOST's shop scenes alive there
    (tcB191/192), so the cell's shop elements are not offered to user 2's manager. Open: confirm with the host far
    BEFORE the guest arrives at the market (as tcB172), then the guest's own shop scene; ByJunter's T1 trade list
    (user2_shop_trade_list) and shop_trade_probe (2976-pinned) are the next pieces if needed.
- 2026-11-09 16:55 *** tcB213: THE FAR GUEST'S OWN MANAGER PREPARES ITS SHOP SCENES *** -- the host teleported 430 m
  away first, the guest reached the grocer 40 s later: user 2's U-1 manager "LET THROUGH (U2: a shop scene of user 2's
  own manager)" 9, "user 2's manager prepared 9 (shop scenes booked 9)". So the shop elements are offered to user 2 once
  the host's own shop scenes are gone (tcB212: with the guest already at the market the host's stayed alive and none was
  offered). Both alive, SAVES OK. tcB214: the guest walks to Delkin and holds R in that state.
- 2026-11-09 17:05 *** tcB214: THE FAR GUEST'S TOWN IS POPULATED *** (host 430 m away at the Rodbel dock, the guest
  reaching the grocer after the host left): Delkin stands at his stall and townsfolk walk the square on the guest's
  screen (docs/research/native-shots/far-world-before-after-2026-11-09.png: tcB198's empty market vs now). user 2's
  manager prepared its 9 shop scenes. Open: the guest's R at Delkin gives no prompt (no InteractionPreemptionStepReq
  sent): the far guest's own shop scene is there but its shop entries / prompt are not yet usable -- next: what its
  client's interaction check refuses (the shop group 0xF4440 entries come from the host's client only, shop_twin's
  state 17, and the host is far: its client never had Delkin's list).
- 2026-11-09 17:15 tcB214 detail: the guest's client DID receive its own 9 shop scenes (2380 with its character
  0xA01001DC as the actor, 184-197 s), but no dialog list with the shop group 0xF4440 and no interaction Req at Delkin.
  ByJunter's s46-tenderos.md §1.1 had this case on 2976: the far guest's client REJECTS its own shop scene
  (0x3B331F28) because the scene's 2380 arrives before the shopkeeper's 2005 -- observer_hold (in our release ini)
  holds the 2005 ~350 ms; his T2 = observer_hold_scene_order=1 holds the scene until the NPC is announced. Also
  needed per §1.1: the store check on the guest's own stage (our guest_shop_check covers the near case; his U2-c =
  user2_shop_trade_route). tcB215: T2 on.
- 2026-11-09 17:30 tcB215 (T2 on): "HoldSceneOrder ... user 2's own trade 2661 0" -- nothing matched: its opcodes were
  already 3576's (the log text keeps 2976 numbers) and its 2380 prefix 92, but its trade list (funcnpc_trade_policy.h)
  held only ByJunter's two verified shops (smithy, butcher), not the grocer. Extended to the 9 merchant scenes
  merchant_unhide knows (hashes by lookup3 seed 0xC5EDE: grocery 0x8FA1605E, general 0x2CFAD71A, bank 0xAA015527, pub
  0xEA60A5DF, clothes 0x0591B85B, leather 0x6EA4BC4E, smuggler 0xE2EB7F12). tcB216 repeats with it.
- 2026-11-09 17:45 *** tcB216: THE GUEST SHOPS FAR FROM THE HOST *** (host 424 m away at the Rodbel dock): with the 9
  merchant scenes on T2's trade list, T2 kept 7 of user 2's own shop scenes until their shopkeepers were announced
  (released in order, max 376 ms); the guest held R at Delkin -> its own InteractionPreemptionStepReq / DoStepReq
  (interaction 0x000F4321) accepted (2120 / 2794), then ITS OWN scene 0x255's branches answered by the server natively
  (branch 6 -> type 1, branch 0 -> the menu, type 2) -- no shop twin involved: "Hernand Grocer's Shop - Delkin: Buy /
  Sell / Leave" and the Buy window with the guest's own money (114.94). Both alive, SAVES OK.
  The release native ini (layer, host) now adds: shop_probe=1, user2_shop_route=1,
  user2_stages_elements=schedule+funcnpc, observer_hold_scene_order=1 (layer test updated; all pass).
  Note: the guest must reach the market after the host's shop scenes there are gone (tcB212 / 213) -- the case of the
  guest standing at a market while the host leaves (the host's scenes kept alive by the guest) still uses the host's
  scenes + shop twin, which also works (tcB191 / 192).
- 2026-11-09 18:00 tcB217 (-Lobby, release defaults incl. the far-world shop keys): both joined, SAVES OK, BUT (1) U-1
  armed while user 2 was still loading in (its own saved place 5 cells from the host) and the 2322 of its field enter
  aborted it for the run ("[ABORT] an unexpected 2322 to user 2 after an arm"); (2) the guest's near-host butcher menu
  did not open (twins prepared, no branch; the release ini records no packets, so the cause is not visible). Fix for
  (1): U-1's distance counts only once user 2 has been in the world 10 s (native, committed). tcB218 repeats to see
  whether (2) followed from (1).
- 2026-11-09 18:20 tcB218: harness flake (the lobby focus loop threw "game window is not in front"; the guest never
  joined; SAVES OK). tcB219 (-Lobby, the 10 s settle gate in): U-1 armed 24 s after the guest was in the world (no
  2322 abort any more) but U-e then aborted ("more than 10 reason-42 logouts of assigned NPCs in a minute" -- the NPC
  loop ByJunter contained with his S35 keys, not in our build); and the guest's near-host butcher menu did NOT open
  again. tcB205 (the same flow before the far-shop keys) opened it, so the four far-shop keys are OUT of the release
  defaults again (layer committed) until the conflict is found -- tcB220 checks the near shop without them.
- 2026-11-09 18:35 tcB220 (-Lobby, far-shop keys out): the guest's near-host butcher menu OPENS again ("stopfile
  seen"); both alive, SAVES OK. So the four far-shop keys conflict with the near-host shop twin (to find; far shops
  stay a non-default option for now). Also: U-1 armed at the guest's own saved place (5 cells from the host) and U-e
  aborted 10 s later on ByJunter's S31 respawn loop: 212 NPCs given to user 2, its client drove 205, 27 despawned by
  their AI (reason 42) and put back by their stage within a minute -> "user2_npc_control stops assigning". His S33 /
  S35 containment (npc_activation, npc_action_evidence, npc_containment_guard, npc_degradation, publication_gate,
  npc_action_probe's 0x281CEE0 group roles) is what stops that loop; not ported yet. Checklist: far-world NPCs may
  flicker for the first seconds.
- 2026-11-09 18:50 The U-e abort (tcB219 / tcB220) was partly OUR port gap: all 27 reason-42 logouts were counted "with
  no slot" -- the NPC schedule descriptor's handle ({u32 data, u32 node}) is [desc+0x510] on 3576 (its vf 0x29E06E0
  reads it 9 times; 2976 +0x4E8): the loop check could not tell a respawn loop from ordinary despawns and, failing
  closed, aborted. Fixed (native 39efe43). The base descriptor's later fields look shifted +0x28 too (0x3C8 -> 0x3F0,
  0x3CC -> 0x3F4, 0x3CD -> 0x3F5, 0x3D0 -> 0x3F8 in ICreateServerActorDesc vf3 0x29CBEE0): those are the family / road
  modules' (off in the release ini), not changed yet. tcB221 re-checks the release flow.
- 2026-11-09 19:00 tcB221 (-Lobby, the descriptor fix): every assigned NPC now has its stage slot (56 reason-42 logouts:
  37 first in their slot, 19 repeats, none "with no slot") -- no reason-42 abort any more. U-e stopped instead on
  "an assigned NPC has no accepted StartActionReq 5 s after its birth" (the 5 s watchdog): the NPCs were given to the
  guest at its own login place, then the harness teleported it to the butcher at once, leaving them undriven -- a test
  artifact (a walking guest keeps them), handled by the design (the unaccepted ones are removed with reason 25 and come
  back with the host). The near-host shop opened ("stopfile seen"); both alive, SAVES OK. ByJunter's S35 containment
  (per-unit, instead of this run-wide stop) remains the proper fix -- not ported.
- 2026-11-09 19:15 The far-shop keys vs the near shop: tcB222 (non-lobby, the far keys, but a bare guest ini) did not
  open -- not comparable (the guest lacked guest_x_interaction_stage / seq_actor_restore / guest_schedule_load of every
  working non-lobby shop run). tcB223 (non-lobby, the far keys + tcB194's guest keys): the near-host shop OPENS
  ("stopfile seen"). So the keys alone do not break it; in the lobby runs (tcB217 / 219) U-1 had ARMED at the guest's
  own login place 5 cells off and (with schedule+funcnpc) its manager likely prepared its own copies of the market's
  shop scenes, which then coexist with the shop twins when the guest arrives next to the host. Next for making far
  shops default: shop_twin must not twin a host shop scene whose resource the guest already has as its own scene (and
  the drain must remove user 2's shop scenes when it comes near), then a lobby run with the far keys. (The lobby's
  release ini records no packets: a capture of that case needs a dev override.)
- 2026-11-09 17:16 (clock fix: the entries above dated 18:20-19:15 were 16:20-17:05) tcB224 (-Lobby, the far-shop keys
  back in the host defaults): the near-host butcher menu did NOT open (sweeps 0-14, no "stopfile"), and U-e aborted
  at 17:11:52, 35 s after the guest entered the world and BEFORE any teleport ("no accepted StartActionReq 5 s after
  its birth"; 212 tracked, 27 reason 42). The common factor of every failing far-world run (tcB217 / 219 / 220 / 221 /
  222 / 224) vs every working one (tcB194 / 201 / 214 / 216 / 223): the GUEST's keys. The release guest ini never had
  guest_schedule_load (the guest client loads the NPC schedules before the host's 2005 / 2335 / 2874 reach it --
  without it the guest cannot build / drive the NPCs U-e gives it), guest_x_interaction_stage (C1) or
  seq_actor_restore. Now in the layer's guest defaults (cd-coop-3446 tests updated); tcB225 re-runs the lobby flow.
- 2026-11-09 17:16 S33 / S35 NPC containment PORTED to 3576 (native, not committed until tested): ServerStartActionLogic
  0x2D08190 (2976 0x2A78CA0; identical head, H1b 0x2D08550 at the same +0x3C0, the frame / args / out flag / broadcast
  call bytes identical; the branch flag is block+0x279, was +0x281), the schedule node constructor 0x2A62280 (2976
  0x281CEE0, identical head). Its 9 callers (2976: 8) mapped by function and order: exact 0x2A5EB72 (0x2A5DD70 =
  2818AE0), 0x2A5F6E1 / 0x2A5FCF7 (0x2A5F1B0 = 2819E00 +0x52C / +0xB42), 0x2A631A5 (0x2A63090 = 281DBD0); group
  leader 0x2A5F855 / member 0x2A5F929 (2819E00 mode 3: same prologue, group in [rbp-0x78] = [rsp+0x88], loaded to
  r13 at 0x2A5F7AE, not rewritten between the calls); parent 0x2A66891 (0x2A66460 = 2820D80, its cmp [r12+0xB0],-1)
  and 0x2A62EAE (0x2A62D10 = 281D970, 0xA90 after the constructor as in 2976); a NEW 9th caller 0x2A68440 (ret
  0x2A68794) is Unknown (never vetoed). The cleanup functions (FindActor / scope dtor / Logout) were already ported
  (U-e's logouts work). Keys (ByJunter's S35 set, without family / road control whose descriptor offsets are not
  verified on 3576): npc_activation=1, npc_action_evidence=1, npc_containment_guard=1, npc_degradation=1,
  publication_gate=1. Also fixed a stale shop_c1 test (C1 lookup call 0x22EE424 -> 0x21578E0). The native optd
  suite still has 2976-numbered troc fixtures failing (optd_tests.cpp:1154-1231, known since 7f33727).
- 2026-11-09 17:35 B11 (trading), the 3111 worker 0x2E0B2B0 read in full (decomp): (char, err, u32 item key -> item
  index (0xFFFF = error), u16 b, u64 c, u32* FROM key, u32* TO key, u16 d -> an index through 0x488D4430 (0xFFFF =
  error; likely the inventory key), u16 e, u32 entry index). Both actors are found with SAM::FindActor (both must be
  logged in); the inventory definition 0x5560E0(idx) holds a table [+0x38] (count [+0x40], stride 0xA0) indexed by
  the u32: entry byte 0 = kind (3 = refused; 2 -> 0x2DF2560, else -> 0x2DF04A0, the move that writes the bag acks),
  +2 / +4 = two inventory ids (-1 = refused), [+0x78] an optional condition (CompareTargetParameter). Then
  0x2301D20(from, to, 0.0) is a DISTANCE check: both actors' positions (sector * 1000 + local) within a constant
  radius (a second, larger one when either actor's region id [+0xA6] is the current one) -- a "give to someone next
  to you" rule, which fits a player-to-player trade (the two players standing together). No code writes the 0xC27
  opcode literally (the Req is built from its vtable), so the client UI that sends it is still unknown (a horse /
  companion inventory is a guess). Next for B11: a 3111 capture (the game's own use), or a crafted one from the
  guest's layer (the dev script's c2s) with from = guest, to = host, after reading 0x2DF04A0's use of b / c / e.
- 2026-11-09 17:28 B11, 3111's fields from the move 0x2DF04A0 (the kind-1 path): {u32 item key, u16 b (passed to
  0x22FFCA0 on the RECEIVER's inventory with the entry's +4 inventory id: likely the receiver's slot / room check),
  u64 COUNT (checked against the entry's per-item limits at def +0x90 / +0x98), u32 giver key, u32 receiver key, u16
  inventory key (-> def index), u16 giver SLOT (0xFFFF = any; else 0x22F7FE0(giver, inv +2, slot) must hold that item
  with count >= 1), u32 entry index}. So a crafted trade needs only one valid entry index (kind 1, the two inventory
  ids of the bags), read at runtime from def 0x5560E0 ([0x71C1060] +0x58 table) -- next when B11 gets its turn.
- 2026-11-09 17:40 tcB225 (-Lobby, far-shop keys + the new guest keys): near-host butcher menu still NOT open; both
  alive, SAVES OK. Shots: the guest stands at the stall, Renee is NOT there on the guest's screen, while the host's
  view shows her next to the guest. Cause (logs): U-1 armed 17:23:25 at the guest's login place (5 cells off); user 2's
  own manager prepared its own copies of all the market's funcnpc scenes (butcher 0x1CD binding Renee 0xB010030B,
  bank, grocery, leather, smithy, general, clothes, pub). The guest then came to the host: the host's butcher scene
  0x241 (the SAME NPC 0xB010030B) reached the guest at 17:25:40 (ShopTwin prepared its twin) while Renee was still
  bound to user 2's own 0x1CD; the distance drain came only at 17:25:43-47 (5 s off timer), its DeleteSequencerAck
  2292 of 0x1CD reached the guest at 17:25:47 -- and Renee stayed bound to the deleted 0x1CD on the guest
  (GuestSeqRoute G5: scene +0x2E0 0x1CD, +0xCF 1 = waiting for its scene) for good. An ORDER problem: user 2's stages
  must be gone before the host's scenes reach it. ByJunter's user2_drain_at_host_prepare=1 (review 2: unload user 2's
  manager at the host's preparation near it, before the host's evaluation builds its stages; a distance drain waits
  for that preparation) is that rule: now in the host defaults; tcB226 re-runs the lobby flow. This is not a test
  artifact: any guest that logs in away from the host and then walks to it hits it.
- 2026-11-09 17:40 B16b on a pad: the hold prompt's ring also fills with the co-op chord + D-pad Up (the Challenge
  action); the label shows the pad's buttons ("{Challenge} Interact"); while the prompt is up, the chord's press no
  longer opens the menu at once (the full ring does). Layer built, tests pass; not seen in the game yet.
- 2026-11-09 17:50 B15 (duel extras) audit: titles and the per-friend scoreboard exist (T43, scoreboard.cpp, a title
  shown to the friend), the duel already plays round after round (kOverMs then the next round: the rematch). Left:
  wagers (need B11's item / money transfer between the players), arenas (a named duel ground, e.g. the Goldenfist
  Arena south of Hernand), a spectator camera (only meaningful with a third player, B21).
- 2026-11-09 17:37 Built (native 6aefe79, local): the SceneHold CONFLICT hold, [session2]
  observer_hold_scene_conflict=1 (host, with observer_hold_scene_order=1 + observer_hold_acks=1; default 0): every own
  2380 of user 2 (U-1's scenes) is noted with the NPCs it binds; a 2380 of ANOTHER owner (the host's) to user 2 that
  binds one of them is KEPT, with every later packet of its id, until the 2292 (actor @0 = user 2's character) of each
  such own scene went; then it is queued right after that 2292 (observer_hold_acks' route pushes it, under its lock).
  The 100 s bound and the field boundary end it as T2's holds. New unit suite optd_scene_conflict (3576 layouts:
  2380 92-byte prefix, 2292 id @4): passes. (The old optd_hold_scene_order suite crashes on its 2976 fixtures:
  2661 / 91-byte prefix -- known, not ours.) Used if tcB226 (drain at host prepare) does not bring Renee back.
- 2026-11-09 17:41 tcB226 (-Lobby, + user2_drain_at_host_prepare=1): menu NOT open; both alive, SAVES OK. U-1
  armed at the login place again (17:34:55, user 2's own butcher 0x1CA binding Renee 0xB01002AC); the host's butcher
  scene 0x238 went to user 2 at 17:37:11.98 through ObserverSeqCopy (a LIVE copy, server_push_extra -- it bypasses
  observer_hold_acks' route, so the SceneHold conflict hold would not have seen it either), the drain (distance) came
  at 17:37:14.998 with the 2292 of 0x1CA right after; on the guest Renee stayed bound to the deleted 0x1CA (G5: scene
  0x1CA, +0xCF 1). The prepare drain did not run before (the host's manager prepared nothing new there). Built (native
  78aba55): the conflict hold in ObserverSeqCopy too -- its Copier notes user 2's own scenes' bound NPCs (its own
  2380s) and keeps a copy pending while one of its required actors is bound by a live own scene, releasing it right
  behind that scene's 2292 (same key observer_hold_scene_conflict). Layer: observer_hold_scene_conflict=1 in the host
  defaults. tcB227 tests it.
- 2026-11-09 17:43 B11 tool: the layer's dev script gained "dumpitemmove" (read only): it walks [exe+0x71C1060]
  (+8 count, +0x58 definitions) and logs each loaded definition's entries (kind, the two inventory ids, condition, the
  first 32 bytes) -- the values a crafted 3111 needs (entry index of a kind-1 move between two characters' bags).
  build/port3479/scripts/tp_butcher_host_dump.txt = the butcher teleport + dumpitemmove (tcB228).
- 2026-11-09 17:54 tcB227 (-Lobby, + the conflict hold): menu NOT open; both alive, SAVES OK. The order was now
  right (user 2's own scenes deleted 17:47:59.20, the host's butcher scene 0x21F built on the guest at 17:47:59.89),
  yet Renee (0xB01002C3) stayed bound to the guest's DELETED own scene 0x1CA (G5: owner 0xA0100066, scene 0x1CA,
  +0xCF 1). So the guest's client never releases an NPC from its own deleted scene: the 2292 goes to its own manager
  (0xBA94E0 on the field's manager) whose scenes do not tick on the guest (the same stall merchant_unhide works
  around), and the host's scene, routed to the copy manager X, cannot take the NPC. Not a test artifact: any guest
  that shopped with its own scene and then meets the host at that market loses the shopkeeper until she streams out.
  Fix built (native 49adcd6): when user 2's own scene goes (its 2292 to user 2) and it bound NPCs that a live scene of
  the host requires, ObserverSeqCopy sends user 2 a RemoveActorAck 3067 for them right behind the 2292 and asks
  child_hold for a native re-announce (child_hold_npc_reannounce: an observer_hold NPC entry for session 2, released
  by its pass with NotifyAppear([session 2], [key]) = a fresh actor on the guest, no stale scene binding); the host's
  scenes binding them wait for that fresh 2005 (ObserverSeqCopy: the keys leave its announced set; SceneHold: the
  conflict keys become the scene's pending keys; the route now releases T2 scenes on any 2005, not only on one
  observer_hold_acks held). Unit suite optd_scene_conflict updated and passing. tcB228 tests it with the S35
  containment keys in the defaults and the host's dumpitemmove (B11).
- 2026-11-09 18:02 tcB228 (-Lobby; S35 containment + the NPC refresh + dumpitemmove):
  * S33/S35 CONTAINMENT WORKS ON 3576: "sondas entrada acción=true H1b=true constructor nodo=true" (all three ported
    hook points verified and in); U-e did NOT abort for the run this time -- late NPCs were rejected / abandoned ONE
    BY ONE ("contención abandonada ... sólo esta unidad", 3 units over 5 minutes, 215 records), the rest kept going.
  * The refresh fired for all 9 market shopkeepers at the drain (18:00:20.5, "RemoveActorAck sent, native re-announce
    queued") -- but my RemoveActorAck lacked the queue's 5-byte packet header (u16 op | 0xFF | u16 len, as
    clock_push's): user 2's link broke 0.6 s later ("link lost ... reconnection or overflow"), user 2 logged out
    (the guest game kept running, "host ready no"). Fixed (native, header added); tcB229 repeats.
  * B11: dumpitemmove read 21 item-move definitions (inventory ids: 1 = the bag; 8 / 5 / 6 / 7 other inventories;
    def 1 = from the bag: kind-1 moves 1 -> 8 and 1 -> 5, kind-0 moves to 5, 9, 10, 0x13, 0xC, 0xE, 0xF, 0x11,
    0x12, 7, 6; defs 5 / 6 / 7: back to 1). To map which inventory is what next (the shared stash candidates).
- 2026-11-09 18:03 B11 item-move map (tcB228 dumpitemmove, the host's 3576 client): definition N = inventory N
  (21 of them). Bag = 1. Moves OUT of the bag (def 1): kind 1 -> 8 (entry 0) and -> 5 (entry 1); kind 2 -> 8
  (entries 3-7, the df2560 path); kind 0 -> 5, 9 (with a condition), 0xA, 0x13, 0xC, 0xE, 0xF, 0x11, 0x12, 7, 6.
  Back INTO the bag: 5, 6, 7, 9, 0xA, 0xB, 0xC, 0xE, 0xF, 0x10, 0x11, 0x12, 0x13, 0x14 -> 1 (kind 0); 8 -> 5 / 6
  (kind 0) and 8 -> 0 (kind 2). No bag -> bag entry: a player-to-player trade would be two moves (the giver's bag ->
  an inventory X of the receiver's character, then the receiver's X -> its bag), X one both characters have (5 / 6 /
  7 look like party-member or mount bags, 8 a storage). The worker resolves BOTH actors by key and checks only the
  distance between them, so a crafted 3111 with giver = the guest's character, receiver = the host's is the next
  experiment (it needs an item key the guest holds: the bandits save has 22007 x9; the login AddItemToInventoryAck
  lists (type 1) carry every bag item -- to decode: element +17 u64 item no, +25 u32 key). build/port3479/make3111.py
  builds the payload.
- 2026-11-09 18:14 tcB229 (-Lobby, the refresh with the packet header fixed): no link loss this time, the
  host KEPT its butcher (host MerchantUnhide: exists, +0xCF 0), ObserverSeqCopy's conflict hold put off 1809 copy
  releases (it works), the refresh fired for the 5 shopkeepers of user 2's own scenes at 18:10:49 -- but the guest's
  Renee (0xB010049B) was bound AGAIN to user 2's deleted scene 0x1C8 right then (MerchantUnhide registered a new
  binding at 18:10:49; G5 at 18:11:09: owner 0xA0100066, scene 0x1C8). So the fresh 2005 carries the server actor's
  own sequencer state, and the SERVER's Renee still names user 2's (closed) scene after the drain: the guest rebuilds
  her as she was. The real fix is server-side (clear that binding when U-1's stages close, or have the host's scene
  take her again); not done. (tcB228's host-side butcher loss came with user 2's broken link / logout, not seen in
  tcB229.) Menu not open; both alive, SAVES OK.
  DECISION for the release defaults (layer): far shops OFF again (shop_probe, user2_shop_route,
  user2_stages_elements=schedule+funcnpc, observer_hold_scene_order, user2_drain_at_host_prepare,
  observer_hold_scene_conflict out) -- back to tcB220's behaviour where the near-host shop works in the lobby flow;
  KEPT: the guest's far-world keys and ByJunter's S33 / S35 containment (it worked in tcB228/229: no run-wide U-e
  abort). The conflict hold + refresh stay in the native code behind observer_hold_scene_conflict (default 0).
  tcB230 confirms the near shop with these defaults.
- 2026-11-09 18:15 Far-shop next step (research pointer): the actor's sequencer play data is the
  CommonSequencerPlayDataActorComponent (server; client ClientSequencerPlayDataActorComponent vtable 0x594FAF0, its
  scene +0x2E0 / owner as GuestSeqRoute's G5 reads them). Find what it serializes into the 2005 and what clears its
  scene / owner when a stage closes (reason 19), then clear it for the NPCs of U-1's closed funcnpc stages.
- 2026-11-09 18:24 tcB230 (-Lobby, the release defaults: far shops off, S35 containment on, the guest's
  far-world keys): the guest's near-host butcher menu OPENS ("stopfile seen", sweep 4); containment ready (all three
  probes), 4 late NPCs abandoned one by one, no run-wide U-e abort; both alive, SAVES OK. These are the defaults the
  package gets.
- 2026-11-09 18:25 Local package (NOT published): dist/CrimsonDesert-SeamlessCoop-0.1.0-alpha+58061ca8-shared-world.zip
  (11.9 MB; native 4b-series local commits + the layer with tcB230's defaults). tools/test-install-fake.ps1: ALL
  STEPS PASS.
