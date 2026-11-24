# Changelog

## Shared world playtest builds (0.1.0-alpha+<id>-shared-world; game 1.0.0.3732 from the 19 Nov port, 1.0.0.3663 from the build after cf8c7fa3, 1.0.0.3576 before)

One world for both players, on the native shared-world base. Tested with two copies of the game on one PC unless a
line says otherwise; two PCs, AMD / Intel graphics cards and long sessions are what the playtest is for.

Candidate 0.1.0-alpha+365e7b50, for game 1.0.0.3732 (native 52ef13e, layer c0fa5540's; checklist pp54 running; not
posted yet; text docs/release/shared-world-playtest/download-365e7b50.txt). From Borque's two-PC session of 24 Nov:
- **The host's network loop no longer stalls in towns**: npc_category's table read and the hide mirror's actor
  lookups used VirtualQuery-checked reads, which cost the host's network loop 0.5-2.4 s per pass while the game
  streamed a town, so everything to the joined player went out in bursts (the "big desync, NPCs teleporting"). Fast
  (SEH-guarded) reads now; each tick after the tunnel is timed ("Net: host tick <name> took N ms" over 100 ms).
  pp53-armwrest (before): 196 "one loop took" warnings, up to 1.1 s; pp54-armwrest (after): none, and no slow tick.
- **The host no longer freezes the joined player's horse**: horse_diag held user 2's actors on the host by the crude
  2976 test (a chart and no action), so the joined player's own horse never updated on the host. The game's layer rule
  now, as on the guest since 2026-11-08. cs114c: 0 held frames through a 38 m ride.
- **A refused step of the joined player's own scene no longer strands them** (Borque's taming): when the host's
  server refuses a branch of user 2's own scene and no accepted branch or delete follows within 6 s, user 2 gets that
  scene's 2292 naming its character (observer_seq_copy, "[own refused]"). Unit-tested; a taming is not scriptable.
- **Hops to the friend land on the friend's recent path** (T19, layer 9a16282 / c0fa5540): the newest point 1-4 m
  from the friend within 2 m up / down, else 1 m beside them (pp52-lowsave: the friend's own spot overlapped).

Posted 2026-11-24 01:53 as 0.1.0-alpha+2451dff5, for game 1.0.0.3732 (checklist pp51 over 28 steps, its three
failed steps re-run clean in pp51b with static as smoke: farfight and newsave (the Discord overlay had broken their
joins) and lagrun (one hold over its line under the random TEST lag); both zips checked file by file against the
tested folders, 25/25 and 10/10; message 1574679393542869023, text
docs/release/shared-world-playtest/download-2451dff5.txt; tags Brycecco):
- **The hide mirror: characters a host scene places no longer stay invisible on the joined player's game**
  (Brycecco's boss whose health bar showed but not the boss). Such an NPC gets hide bit 9 when the scene binds it and
  only the host's game, which runs the scene's AI, ever clears it. The host now sends the keys of those NPCs that are
  visible on its game (tunnel control state 18, on a change or every 6 s); the joined player's game clears bit 9 with
  the game's own setter on any still hidden there 4 s later ([session2] hide_mirror, default 1). Nothing hidden on the
  host is shown. pp50c / pp51-fresh: 6 and 3 critters of a start scene shown on the guest. Not yet seen on a boss.
- **Hunting contest: the joined player's game knows the host's animals** (T41): the variants of batched
  ActorLoadDataAck records count, so its kills of them score and the kill feed names them. The joined player's own
  bow kill is a hands-on check.
- Re-checked in the shared world (T19): summon from 4 km and following a far jump, both landing 1 m from the host.
- Test kit: a failed join stops the step (no title keys into a new world); the host window is put back at the
  screen's corner for up to 12 s when Discord's overlay moves it.

Posted 2026-11-23 16:42 as 0.1.0-alpha+ee4cf8e5, for game 1.0.0.3732 (checklist pp48 / pp48d / pp48e / pp48f: all 28
steps, one PC; both zips checked file by file against the tested folder, 25/25 and 10/10; message 1574540718498644048,
text docs/release/shared-world-playtest/download-ee4cf8e5.txt):
- **The host no longer freezes 4-15 s when the friend loads in or teleports** (T09b): merchant_unhide's per-tick hook
  read game memory with safe_read, whose VirtualQuery waited on the address-space lock while the game loaded the
  friend's area (FreezeWatch samples, csfz); it reads with fast_read now. csfz2: no host hitch over 0.5 s at the join
  and two teleports (it froze 4-5 s before; most checklist logs had ~15 s).
- **The joined player's crash under lag is guarded** (exe+0x37E45B1, an actor update reading a missing component;
  pp46 lagrun), on both games, by a vectored exception handler (no game code is changed at that spot).
- **Race to the waypoint works in the shared world** (T41; cs101c): the host's map destination reaches the race
  through the native base (cdmp_own_waypoint); both games race to the same point and agree on the winner. Also
  re-checked in the shared world: rock-paper-scissors (cs100) and hide and seek (cs102b), whole matches.

POSTED 2026-11-23 04:49 (0.1.0-alpha+7d0d63b2, for game 1.0.0.3732; checklist pp46 over 28 steps with the new
armwrest step, its two failed steps re-run clean in pp46b: revive (the Discord overlay had broken its join) and
lagrun; pp46c's lagrun missed one hold threshold (6 / 3); pp46's lagrun saw the joined player's game crash once at an
old game-code spot, guarded in the next build). Also in it: the HOST's game no longer crashes when the joined
player's arm-wrestling opponent is handed to its game right after it spawns (the hand-over waits 1.5 s; the host has
the movement update's null guard too). The rest:
- **The joined player can arm-wrestle at the Hernand inn table** (B14): its game gets its own copy of the table's
  scene (like the shops' copies), and the match's grip, which the host's server only makes for the host, is made on
  the joined player's game. One-PC run cs95b: the 20 copper fee, the live strength bar, a won match and its 30 copper
  prize, all on the joined player's own purse; back in the room after. Checked on 40de0201 with no test override
  (cs103): a lost match (no prize), "Play Again" (a second fee), a won match and its prize, Back, then the host's own
  match at the same table, won and paid.
- **New option, off by default: a death penalty** (T26; Co-op tab > Reviving, "Silver lost when you get up by
  yourself after a death": none, 10 %, 25 %, half, all; the host's choice applies to both players from the next Start
  Co-op). Getting up by yourself (instant or checkpoint revive) costs that share of your own coins, taken by the
  host's game with the game's own delete; your friend getting you up costs nothing; the death screen says what it
  costs. One-PC run cs97b (research switches, 10 %): the joined player and then the host each lost exactly 10 % of
  their own copper (1145 of 11454), once per death, no error.
- **The partner frame's stamina bar is back** on game 1.0.0.3732: the game moved its stamina stat (22 -> 0x19), so
  the friend's stamina was never read (cs94: 0x19 drains while sprinting and refills, 22 is no longer in the list).

POSTED 2026-11-22 15:52 (0.1.0-alpha+677c87bb, for game 1.0.0.3732; checklist pp44 over 27 steps, the re-runs
pp44b / pp44c / pp44d clean after test-kit-only fixes): everything in c7c22497 just below, plus:
- **Doors in a place the joined player reaches first now open** (layer 677c87bb, the native base's [session2]
  scene_level_share now on for the host): when the joined player's game loaded a building before the host's did, the
  host's server never marked its level ready, so its doors refused BOTH players until a fast travel (reproduced on
  3732 at the Hernand tavern, cs88a: every door request refused). With it on, the level is made ready for the joined
  player too and the door opens (cs88b). ByJunter's fix from the native base, now ported and tested on 3732.
- The friend menu no longer offers "Arm wrestling" in the shared world (layer 32e0d84): there it started the
  two-worlds versus (each player at a table in its own world), which cannot finish in one shared world, and the
  joined player cannot start a table match of its own yet (B14). The host plays the tables against the townsfolk as
  usual; wrestling each other waits for B13 (README: CHALLENGES and KNOWN ISSUES).
- The save hooks name their callers again on 3732 (native 72ebb0d): the three LoadSlot, two SavePlayerToSlot and two
  flush callers were still 2976's return addresses, so every one read "OTHER" (the guest save's manifests said
  "OTHER (ret ?)"); now 3732's, tied to the hooks' call sites by static checks. Behaviour: only an unidentified user
  actor at the real UserLogin is now let through, as designed (user 2 is identified there on 3732: unchanged).
- Mount seats: the "is this rider the local player" judge read 2976's function addresses, so on 3732 every rider
  counted as local and seating relied on a second guard (native 72ebb0d: 3732's, checked instruction by instruction
  on the game's exe).
- Two safety fixes for later game builds and options (native 72ebb0d): the game-error hook installs only on the build
  its address is for; guest_sched_route (off) routes nothing when its teardown guard cannot be installed.
- Lootr's shares kept across sessions (B19), switched off ([session2] lootr_persist=0) until its live test: in the
  build after five independent reviews (native b3fdea7, merged 7dcfc33).
- Research, off: [session2] twin_extra_only (B14: how the joined player's table copy is prepared), the layer's
  [dev] minigame_probe (the minigame gauge and E presses), shared quest credit v2 (B10: the other player's own quest
  component).

POSTED 2026-11-22 02:25: 0.1.0-alpha+c7c22497, for game 1.0.0.3732 (checklist pp43 1068 pass, 3 fail over 26 steps,
none of the 3 the package's: static's Windows Defender detection of a unit-test exe while the step ran, and crime's log
growth and one slow network tick, from a test script that walked the joined player out of the bandit camp into Serkis
Estate's townsfolk (each hop now starts from the dock); then pp43b 182 pass, 0 fail (static, fresh, solostart, crime,
leave with the test-kit fixes)): everything in cfaa3a69 just below, plus:
- **The joined player's shops in a town whose scenes the host already runs**: the host's copy of a scene reached the
  joined player's game in two parts, and in one run 21 of 31 town scenes (every shop among them) came "start first,
  scene second" (the scene waits until its people are known on the joined player's game, its start does not). The
  joined player's own shop copies were then made but never started: no "Shop" prompt at any shopkeeper. They start
  now (native a484b0a, checked: cs86b-d, every shop copy started and gave its shopkeeper's entries). A real fast travel
  into the host's town is still to be confirmed by hand.
- **A minigame opponent the server spawns for the joined player now runs on the joined player's game** (native 9dbab60,
  [session2] ai_handoff_minigame, on): it ran on the host's game, so the joined player's match could not go on; as in
  single player, the player's own game drives its opponent now.
- The joined player's own arm wrestling at the Hernand inn table (B14) is NOT in this build: its own copy of the
  table now gets it to the organizer's menu, its fee, its opponent, the intro and the countdown, but its match does
  not start pushing and it could not leave the table, so that copy stays off ([session2] twin_extra, research).
- Research, off: twin_watch (the host logs every message of listed scenes), twin_extra (the table's copy), early_state_x
  (the start-order fix for the host's own scene copies: no measurable effect yet).

POSTED 2026-11-21 17:54: 0.1.0-alpha+cfaa3a69, for game 1.0.0.3732 (checklist pp42 1064 pass, 7 fail over 26 steps, two new:
shop and leave; none of the 7 the package's: two in the static step (an in-progress edit applied to the native tree
while it ran; Windows Defender quarantining one unit-test exe at that moment) and five test-kit expectations of the two
new steps (the friend's game killed: the tunnel's stop, U-1's goodbye while armed; the host parked far: U-1's logging,
User2Shops' drain / nested-vf48 diagnostics, a teleport stopped 39 m short), then pp42b 155 pass, 1 fail (static, leave,
shop, fresh, solostart with the test-kit fix; the 1: Defender quarantining another test exe) and pp42c static 12/0): everything in 4394b014 just below, plus (Borque 20 Nov "client still not
able to interact with npcs"):
- **The joined player's greet / talk with townsfolk work again on 3732** (NpcCategory): the game asks the NPC's
  category from a per-player table before it offers a greeting or a talk; the joined player's is filled only on the
  host (the NPCs' own stage functions run there), so the mod sends the host's table across. On 3732 the table's fields
  are back at their 3576 places and the mod still read 3663's: the host's table always read empty and the joined
  player's greet / talk stayed greyed. Fixed (the 3732 getter's own offsets). Checked: the host's table 188-213 NPCs,
  the joined player's greeting of a townsperson accepted by the host's server, "Stranger - Trust +5" (cs80c).
- **Shops for the joined player, checked on 3732**: alone (the host 440 m away: its own shop scene) and with the host
  beside the shopkeeper (ShopTwin: its private copy of the host's shop scene), both accepted by the host's server and
  the menu shown (cs82c; the checklist's new shop step). Not checked yet: a fast travel of the joined player INTO a town
  whose shop scenes the host already runs. The test's cheat teleport loses its private shop copies there (cs75p5,
  cs75w2); a real fast travel ends with a field move, after which the host sends its scenes again, so they should come
  back by themselves (to confirm by hand).
- **When the other player's game closes or loads** (a crash, Alt+F4, fast travel, a respawn) the NPCs its game ran go
  back to the host's game (the departure sweep, native 18fdc52); an NPC the joined player talked to goes back to the
  game that ran it only when that player is within 60 m.
- Logs: each frame hitch over 0.5 s with its end time (UiPause), for "freeze at hh:mm" reports.
- Research, off in the release: GuestXInter's probes of the joined player's NPC prompts ([session2]
  guest_x_research), the evaluator change ([session2] guest_x_eval_fix: it would hide ShopTwin's entries), ShopTwin's
  re-making of lost twins ([session2] shop_twin_again: a second round reviewed, a third pending a real fast travel).
- Test kit: the checklist's shop step and leave step; step-specific error allowances ($ERR_ALLOW).

POSTED 2026-11-21 09:31: 0.1.0-alpha+4394b014, for game 1.0.0.3732 (checklist pp41 1017 pass, 2 fail over 24 steps, one new:
lagrun; the 2 were lagrun's own test switch announcing itself as a warning, allowed for that step in the test kit, then
pp41b 152 pass, 0 fail (static, fresh, solostart, lagrun again)): everything in 69edcc84 just below, plus (Borque / Jay, 20-21 Nov):
- **The other player no longer freezes mid-run** (the "running animation freeze", two PCs only). The other player's
  movement samples reach a game late for their own stamps (100-300 ms on one PC with simulated internet lag; the
  game reads them 200 ms back and does not extrapolate), so it stood still for a moment, often. New
  [session2] remote_interp_char=1 (the default, both ends): the other player's character gets a delay of its own,
  its samples' peak lateness (kept 10 s) + 60 ms, within [200, 600] ms, the game's while it rides. Tested with the
  new TEST sim_lag (30 + 0..60 ms, 400 ms stalls): short mid-run holds 25-53 -> 0-1 (cs74c-f); the checklist's new
  lagrun step keeps it so.
- **An NPC the joined player talks to stops for them** (the talk rule, [session2] ai_handoff_talk=1): in single
  player the talker's own game runs the NPC; the person goes to the talker's game while they stand within 6 m, then
  back. Seen in pp41's crime step on another interaction, not a talk (held E on a bandit at the camp,
  interaction 0x35: the NPC on the guest's game 113 ms after, back at 6.9 m). (Corrected 2026-11-21 09:50: it
  first said "a townsperson ... after the talk".)
  Not yet seen on screen: the scripted shop runs (cs75c / cs75d) never opened the butcher's shop (a load not
  finished; then the bowl on her table had the focus, so R sent nothing); the visible check is the testers'.
- Logs: the host's answer to the joined player's StageBranchStateReq (ShopProbe B: the taming freeze); each game's
  own frame hitches (UiPause); the other player's mid-run holds and its samples' lateness (RemoteInterp); each game's
  clock against the PC's (RemoteInterp [clock]); guest_clock_fix (default 0) measured.
- Test kit: TEST sim_lag ([session2] sim_lag_*), dev script "vehicles", tpvehicle [min key], faceto.py.

POSTED 2026-11-20 20:54: 0.1.0-alpha+69edcc84, for game 1.0.0.3732 (checklist pp40 971/971 over 23 steps):
everything in 9e777a23 just below, plus (Borque, 20 Nov):
- **The joined player on a horse that is not his own (a bandit's, a wild one) moves with it on the host's screen.**
  The host's client derived the horse's movement from its rider only for the rider's own horse; a mount the ride rule
  notes for him counts too now. Tested (cs69b: a bandit's horse, 387 derived, 0 refused).
- **The first mount of a wild horse is seated on the host's screen**: the host's apply 6 ms after the commit was
  refused (controller mismatch: the ride rule hands the horse to the rider's game ~1 s later); it now waits for that
  hand-over (until 3 s). Not seen live (wild horses do not spawn every run).
- **An animal the joined player carries stays in his hands on the host's screen** (attach_follow=carry, incoming: made
  at the host's client; the carried animal's own Moves from the catcher's game no longer end it). Measured (cs69a:
  attached, 13 poses), not seen on screen.

POSTED 2026-11-20 02:18: 0.1.0-alpha+9e777a23, for game 1.0.0.3732 (checklist pp39 963/971 over 23 steps + pp39b
152/152: pp39's 8 fails were lowsave's log-rate checks over midnight, a checklist bug fixed in tools/prepost.sh; 12d533ef
before it passed pp38 + pp38b and was never posted): everything in 26bf2c71 just below, plus:
- **An animal the host carries stays in the host's hands on the joined player's screen** (Borque, 18 Nov) instead of
  floating where it was caught. Its actions name the host's character as their parent, and the part that keeps a
  carried child on its carrier only took a player's own children. New [session2] attach_follow=carry, the default:
  only an animal / NPC nobody owns that the host carries. Measured (cs68f: 0.46-0.60 m from the hands, the positions
  made within 1-3 mm of the host game's own); not yet seen on screen.
- **Riding in an NPC's cart as a passenger no longer makes you wanted** (Borque, 18 Nov). Boarding sent frame events
  that named the cart and its driver; the host took them for the passenger's hits and gave cart and driver to the
  passenger's game, where the driver got hurt. While a player sits in a passenger seat (the vehicle checked beside them
  every server check) the fight rule hands nothing to them. Not seen live yet: no cart passed in the one-PC test.
- **Two more host crash guards** of the same kind as the 18 Nov host crash: a sequencer delete (2678) and a delayed
  stage chart event (3141) from the joined player, arriving while the host's world loads, used a missing stage manager.
  They now leave by each handler's own "not attached" exit. All three guards install on 3732 (checked in pp38); the two
  new ones saw no such request in the checklist.

POSTED 2026-11-19 17:04: 0.1.0-alpha+26bf2c71, for game **1.0.0.3732** (Steam's update of 19 Nov; every earlier
build switches itself off on it; checklist pp37 968/971 over 23 steps + pp37b 198/198: chest failed in pp37 on the
test kit's own walker, which read 3663's position global; mountfight's mounted swings missed, a combat outcome seen
before in pp19; both passed in pp37b): everything in e5fa32dc just below (the small-animal catch, the host-load crash guard), plus:
- **Ported to game version 1.0.0.3732.** Message numbers did not change; every game function the mod uses has the
  same code at a new address. The installer's version note now knows 3732 (it had kept an older size since 3663).
- The one-PC test host waits for its network port after the co-op restart (a test-only path; players use Steam).
- Fewer repeated NPC lines in the host's log (the first 40 of each kind, then 1 in 25).
- The checklist's far-fight test bot gets a revive after 50 swings (it never blocks, so five bandits could down it).

NOT POSTED (pp34 stopped after 16 steps; then the game updated): 0.1.0-alpha+e5fa32dc, for game 1.0.0.3663: everything in 569720a2 just
below, plus (from Borque's host report of 18 Nov):
- **Catching small animals works for the joined player.** The host's game accepted the catch but kept running the
  animal, so the catch never finished. The animal now runs on the catcher's game from the moment the catch is
  accepted, for 30 s. Tested with two copies on one PC: a partridge caught and still carried 40 s later; with this
  switched off (`[session2] ai_handoff_catch=0`) two catches in the same place both failed. Catching insects already
  worked.
- **A host crash while loading is guarded.** The game's own code for "a character entered or left a trigger area"
  could read a missing object while the host's world loaded with the joined player's character still in it
  (Borque's host crashed there). That moment's trigger is now skipped instead. Not reproduced here; the guard is
  checked to be in and idle.

NOT POSTED (replaced by e5fa32dc above): 0.1.0-alpha+569720a2 (checklist pp33: 970 of 971 checks; the one fail was
farfight's test bot dying among five bandits before its first kill): everything in 62f49d19 below,
plus (from Borque's session of 18 Nov):
- **The host sees the joined player on its horse.** Since the 3663 port neither game applied the other player's
  mount itself (a message size of the old game version); the joined player's game happened to show the host
  mounted anyway, the host's did not show the joined player mounted. Both now seat the other player's character on
  its horse. Tested: two copies, both mounted, each seated the other; the host still steers itself afterwards.
- **The dye vendor and the goblin trader show for the joined player** (they were missing from our list of
  merchants that the game keeps hidden in co-op); other service NPCs (craftsman, seat dealer, fence, church...)
  get the same unhide. Tested: both dye-vendor NPCs were hidden on the joined player's game and are now shown.
- A wild horse or a cart the joined player rides (the saddle or the driver's seat) now runs on the joined player's
  game (the horse-taming minigame and driving a cart); a passenger seat leaves the cart to its driver. Not tested on
  a wild horse or a cart yet.
- A log line a minute measures how often the other player's character stands still while it runs (for the
  "running freeze" reports; no change to play).

POSTED 2026-11-18 10:47: 0.1.0-alpha+62f49d19, for game 1.0.0.3663 (checklist pp31: 22 of 23 steps pass, 964 checks; crime failed in pp31 and pp31b and passed in pp31c on the same binaries -- test variance, see PROGRESS 10:2x-10:47): everything in 9e85b891 below,
plus (from JackyChan004's and D3vils' session of 18 Nov):
- **After the cabin, the host's story goes on.** When the friend's own cabin scene ended, its game sent a message
  that the host's world applied to the HOST's story manager and switched it off: the host never got the next
  scenes (the bear fight), and the friend's story stopped for the rest of the session too (and the NPCs around it
  stood still on the friend's screen). The friend's message now goes to the friend's own story manager. Tested:
  the switch is on and nothing else changed in our runs; the cabin itself has to be checked by you (in our
  one-PC runs the friend's game never sends that message there).
- A rare start-up check of the overlay waits longer on PCs with DLSS frame generation (it could create a
  temporary swap chain that frame generation is known to crash on; the check went off too early in a few of our
  starts, none crashed).

POSTED 2026-11-18 05:40: 0.1.0-alpha+9e85b891, for game 1.0.0.3663 (checklist pp30 + pp30b: all 22 steps pass, 952 checks; pp30's first farfight2 had one overlay-watcher warning, explained, fixed in the next build): everything in 1c6453e1 below,
plus (from Borque's two-PC session of 17 Nov):
- **The friend's progress is kept on a PC with several Steam accounts.** If the host's save folder had folders for
  more than one Steam account, the friend's own save was switched off and the friend played on a copy that was
  thrown away. Now the folder of the Steam account logged in is used. Tested on one PC with one account.
- **A failed horse taming no longer locks the friend.** The game sent "the scene is over" for the friend's taming
  to the host's character, so the friend's game stayed in it with no input. The host now passes it on to the
  friend. Not yet tested in the game (we cannot script a taming); tell us if it happens again.
- **Horses follow their riders on the other screen.** The horse relay was still reading an older game version's
  message layout, so it did nothing: the friend saw the host's horse away from the host, and the host could not see
  the friend's horse. Fixed both ways (tested: 1311 horse updates to the friend, 736 to the host in one ride).
- **An NPC the friend talks to stops walking** (on the friend's screen; the host's game keeps it moving). Not yet
  seen in a test.
- **The host's armour dye shows for the friend.** Not yet seen in a test (we cannot script a dye).

POSTED 2026-11-17 18:59: 0.1.0-alpha+1c6453e1, for game 1.0.0.3663 (checklist pp29: 907 pass, 0 fail, 22 steps): everything in ada1800c below,
plus:
- **"Enemy health with a guest" works in the shared world, for both players' blows.** The setting (Co-op Settings,
  Combat & Loot; x1.5 by default) makes enemies tougher while a friend is in. In the shared world it barely worked:
  the host's game took the friend's character for the host's own, so it counted only the friend's blows, and a
  check left over from the older two-worlds mode let about 2 in 10 of those through. Now every blow from either
  player on an enemy counts (an animal or a civilian never), so both of you need the same number of hits. Tested on
  one PC: a 13-point blow, then 4 given back, every time.

POSTED 2026-11-17 12:08: 0.1.0-alpha+ada1800c, for game 1.0.0.3663 (checklist pp28: 22 steps, the two test-kit
fails fixed and re-run): everything in 8a65cf2d below,
plus:
- **Enemies are where they really are, on the friend's screen too.** The friend saw every moving enemy about a
  second late: 2.5 m behind where the host's game had it (up to 9 m on a running bandit). So the friend's swings
  missed enemies that looked close, and enemy blows landed from 3 m away ("hits without touching"; reported by
  Borque). The host's game now sends the friend every enemy in a fight nearby 30 times a second: moving enemies
  are 0.5 m / 0.2-0.3 s behind now, standing ones exactly in place. Measured on one PC, two copies of the game.
- **Fixed: riding a camp horse.** When the friend rode a riderless horse the host's game controls (a bandit
  camp's), a safety check took the host's own horse AI for a fault and switched the dismount handling to its
  older mode for the rest of the session.

POSTED 2026-11-17 05:54: 0.1.0-alpha+8a65cf2d, for game 1.0.0.3663 (checklist passed: pp27 static/real/fresh,
pp27b 832/833 over 19 steps; its one fail, crime, was a self-check counting the host's own AI requests for a
camp horse the friend rode, listed as an open known warning, the step passed as pp27c 45/0; replaces f61f9fec,
whose checklist pp26 found the overlay hiding outside photo mode): everything in 74bb83c3 below,
plus:
- **Enemies fight the friend too.** Until now the enemies of the host's world mostly went for the host, even while
  only the friend was fighting them (a test: 27 blows on the host standing still, 3 on the friend doing all the
  fighting; reported by Borque). An enemy the friend is fighting now runs on the friend's game and fights back at
  the friend; when the host takes over the fight it goes back to the host. Tested on one PC: 9 enemies handed over
  in a fight, the way back too, nothing failed.
- **Damage: no difference found.** On the same character, the friend's swings take exactly as much health as the
  host's (12.0 and 12.2 per swing in our test). Each of you plays your own character, so a weaker character (gear,
  level) does less; that is the game's own rule.
  With a friend in, enemies have 1.5x health for both of you (Co-op Settings, Combat & Loot, "Enemy health with
  a guest").
- **Group photos.** The game's Photo Mode (pause menu, Others) shows both of you; our name tag, the friend's health
  panel and the "Interact" prompt no longer show in the photo (they were in the shot before; R, the photo's Capture
  key on a keyboard, no longer starts our hold prompt either).
- **Fixed: a co-op launch could stick.** If Co-op+ was switched off (or the game updated) right after a co-op
  session, the next launches still started in that session's co-op role. They start in solo now.

POSTED 2026-11-16 16:23: 0.1.0-alpha+74bb83c3, for game 1.0.0.3663 (checklist passed: pp25 863/864; its one fail, the mounted fight, was the check missing real blows: fixed in the test kit, the step passed as pp25c): everything in 430fe6be below,
plus:
- **Trade with your friend.** In your inventory, Discard on an item with your friend close now also offers "Offer
  in a trade". When you have both offered something, both of you get "Accept: your <item> for <their item>" or
  "Decline"; when you both accept, the two items swap bags (the host's game moves each one, as with Give). A decline,
  leaving, or 2 minutes without an answer calls it off. Tested on one PC: Ricefish for Ricefish, both moves done.
- **Giving works both ways on this game version.** The friend's "Give to <host>" is tested now too, and the item
  shows up in the other bag (before, only the host's give had been seen on 1.0.0.3663).
- **Wagers on challenges.** From the moment a challenge is sent, the same Discard question offers "Stake on the
  challenge". If you both staked, the loser's item goes to the winner when the match ends; if only one of you
  staked, or the challenge ends without a result, nothing changes hands. Tested on one PC with rock, paper,
  scissors.
- **Fixed: during rock, paper, scissors your inventory's buttons did nothing.** The match takes the pad's X / Y / B
  as hands and hid them from the game, even with the inventory open; it no longer does while a game menu is open
  (and a press there is no longer counted as a hand).
- The co-op menu's entry is now "Give or trade an item".

NOT POSTED (superseded by 74bb83c3): 0.1.0-alpha+430fe6be, for game 1.0.0.3663 (checklist passed: pp24 + the crime re-run pp24b; answers the testers' reports of
16 Nov on the experimental "Story scenes together"):
- **No black screen after a shared cutscene.** With story scenes together, the mod still copied the host's story
  scenes into the friend's game when the friend had not reached that step yet; the host's next cutscene, copied
  while the friend's own played, was what the friend's game fell into when its own ended (their report zips:
  'cd_seq_quest_intro_0100_dlc' sent 2 s before the friend's scene ended). No host story scene is copied now;
  each game plays its own (other host scenes, weather and the like, still are). Tested on one PC: both cabin
  scenes play, every host story scene is skipped.
- **NPCs in the friend's cutscene**: the host's cabin scene sat in the friend's game beside its own; gone with the
  fix above. Screenshots inside the friend's cabin scene show its people moving. Not proven to be the testers'
  case (theirs may have been a later scene).
- **A message when the friend is busy with an NPC.** The game's server lets one player at a time talk to a person;
  the friend's talk to the person the host was talking to was refused 11 times without a word (their zips). Now
  "They are busy with <friend> right now. Try again when <friend> is done". Not seen in a test (scripted input
  cannot open a talk).
- **The friend's health panel moved to the top left corner**: at 55 % of the screen height it covered the game's
  own region panel ("Balthazar, the Wyvernflame 100.00%" at Fort Windridge) on both screens.
- Dev: the npcs test command counts every NPC the server announced (it missed the batched announcements the
  joined player gets, which made one fort test look like 4-18 people against the host's 50-63; by key both games
  have the same people).
- Built but OFF (not in the release settings): knowledge one player learns is learned by the other (B04); no test
  has made the game teach a knowledge yet.

Posted 2026-11-15 20:43 as 0.1.0-alpha+0245f27d, for game 1.0.0.3663 (checklist pp22: all 22 steps, one PC,
862/864; the two fails, both bandit fights, passed on re-runs of the same package: mountfight pp22b, crime
pp22c + pp22d; the crime step depends on a bandit not yet fighting standing by the guest):
- **The friend's time of day follows the host's.** The joining player's sky clock fell behind the host's by the
  length of its own loading (about 37 in-game minutes in one test). The shared world's clock sync was there but
  switched off and still pointed at an old game version's memory; it is moved to 1.0.0.3663 and on. Tested on one
  PC with two copies of the game: both HUD clocks read the same minute.
- **The friend's weather follows the host's** (clouds, rain, wind, fog), within 2 km of the host. Before, each game
  ran its own weather: sun on one screen, clouds on the other. Tested on one PC: the same cloud cover and light on
  both screens. Not seen yet: rain falling during a test.
- The shared-world base could not read the local player's position on 1.0.0.3576 and 1.0.0.3663 (one outdated
  check); every feature waiting for it never started (the weather above among them). Fixed.
- The clock and weather sync switch themselves off on a game version they were not made for.
- **Three settings never showed on the Co-op page** (title Co-op Settings and in game): "Story scenes together
  (experimental)", "Shared quest credit" and "Items in the world" had no section, so the page skipped them; the
  2e5db87a post pointed players to one of them (tester report, 15 Nov). They are under Saves & Story and World now,
  and a setting without a section shows under Session instead of vanishing.
- **Give to the friend on a New Co-op Save**: the host's Give aimed at the joined player's character by a fixed
  number that is only right in our test world; on a New Co-op Save the friend's character has another one. The mod
  now finds the friend's character in the world (on a New Co-op Save in the checklist: 0xA0100125, not the old
  fixed one). Tested: Give from the host reached the friend on 1.0.0.3663 (first test since the move to that game
  version).
- **The friend's name tag stays over their character** while running: it followed the network position, which runs
  about 200 ms ahead of where the game draws the friend (tester report, 15 Nov). Measured with a sprinting friend:
  the body ran 1.4-1.8 m behind the network position; the tag now follows the body.
- Steam status ("Join Game" for friends): in 4 of 16 real Steam test launches Steam faulted inside its own code on
  the mod's first status call (the mod recovered and set it 10 s later). The status is now set through a freshly
  fetched Steam friends interface, which is what always worked on the retry.

Posted 2026-11-15 00:13 as 0.1.0-alpha+2e5db87a, for game 1.0.0.3663 (checklist pp18: all 23 steps, one PC):
- **Story scenes together (experimental, off by default; Co-op Settings, host)**: each player gets their own story
  scenes and steps, and a story scene waits until both players are there. When one of you reaches a story trigger,
  both screens say so: the friend can hold Interact to go to you, and once you are both there each holds Interact to
  start it; then BOTH games play the cutscene at the same moment. The waiting player can also hold Interact to go on
  alone (the friend then plays that scene later, when they get there, without a wait). Tested on one PC with two
  copies of the game, at the prologue's cabin: starting together (both cutscenes began within a second), the host
  going on alone, the joined player going on alone. Not yet: any story scene after the cabin, two PCs.
- The host's "your friend could not join" card no longer stays up when the friend gets in after a slow join.
- Steam presence: one Steam error no longer counts three times (the presence switched off too early).

Posted 2026-11-14 01:42 as 0.1.0-alpha+66985463, for game 1.0.0.3663 (checklist pp14 / pp14b-e: all 23 steps, one PC):
- **A part of the move to 1.0.0.3663 was wrong**: the server stage manager's scene table moved by 8 bytes in that
  game version and the mod still read the old place, so every "is this scene the joined player's?" check found
  nothing and fell back to the host's manager (the joined player's own shop scenes far from the host, the host
  scenes filed on the guest). Found by the shared cutscenes work.
- Research switches for shared cutscenes are in the code, all off by default (nothing changes for players).

Posted 2026-11-13 16:53 as 0.1.0-alpha+5a81a327, for game 1.0.0.3663 (checklist pp13 / pp13b: all 23 steps, one PC):
- **Works with game 1.0.0.3663** (Steam's update of 13 November 2026). The build before, cf8c7fa3, is for 1.0.0.3576
  and switches itself off on the updated game. Moved to the new game: every address and message number the mod
  uses (1066 of the game's 1175 messages were renumbered), the item and field tables' new readers, the structures
  that moved (stage manager, character control, movement, the loading machine, the inventory manager) and the two
  routines in the game's protected code that "Leave the session" and the menu's tab key use.
- **The title's Co-op menu no longer lets a controller press the game's own menu under it** (a tester on a
  PlayStation pad: a save started while "Invite Friends" was highlighted). Measured: the old hold never stopped a
  pad. While the Co-op menu is up the game now gets the same "your window is not active" message it gets when you
  click away, and an idle pad. Tested with a simulated Xbox pad on the game's own title (the Load screen opens
  without the hold, stays closed with it). Not tested: a real PlayStation pad.
- Steam's invite list still takes the mouse under that hold (opened from Invite Friends, closed with its X, the
  menu's keys work afterwards). Tested on one PC; no invite was sent.
- Calling your horse as the joined player works in the test world on a far-along character (called, mounted,
  ridden). The tester's "can't summon horse or wyvern" was in a host's new game, where the joining player used to
  arrive with their own character; since cf8c7fa3 both start fresh, as in the solo game.
- Known and unchanged: the host's story cutscenes, and what they bring into the world (a story boss), are not shown
  to the joined player. That is the next big piece of work.

Posted 2026-11-13 00:13 as 0.1.0-alpha+cf8c7fa3 (checklist pp11 / pp11b / pp11c: all 23 steps, one PC):
- **A New Co-op Save is a new save for BOTH players.** The joining friend used to arrive with their own solo
  character (a tester's 1,858-mission character in a world at mission 0). Now the friend's own save is checked and
  set aside, and they start as a copy of the host's new game, once its first real save exists (about a minute after
  the host stands in the world): same place, same first objective. Continue Our Co-op Save brings that character
  back, whatever save the friend has of their own. Start From My Solo Progress and worlds made before this build
  are unchanged (the friend brings their own character). `[session2] coop_world_character=0` turns it off.
- **Two different builds are refused, and both players are told.** Two testers played a whole session with one
  build each and the mod said nothing: its "same build" check compared a number that had not changed in any
  package since 10 November. It now compares a hash of the installed mod files, and both title menus say "Can't
  play together: <name> has another Co-op+ build ... Both install the same zip, the newest one."
- **Going to your friend is the game's own travel now, beyond 250 m.** It no longer drags you across the map in 40 m
  hops (testers: two to three minutes for 3 km, sometimes stopping short): it asks the game for a travel to that
  spot, as the world map does. That is a loading screen of about ten seconds whatever the distance (500 m and 1.3 km
  in our runs). Under 250 m the hops remain, they are quicker there; they are also the way out when the travel does
  not take. The move to the host's side when you join uses it too.
- **The host's game could crash when its world reloaded with a friend in it** (a tester: down in a fight, Q, crash).
  A timer of the game's NPC schedules ran in the moment the world's owner was gone and read through a null pointer.
  Such a timer is now put off and run when the owner is back. Tested with timers put off on purpose (the schedules
  go on) and with the host reviving at the checkpoint beside the friend; the rare moment itself did not come up.
- A crash inside Steam's own library while the mod set the player's status no longer switches Steam invites off for
  the whole launch: Steam is asked again after 10 seconds (three faults in one launch switch it off, as before one).
- The report tool removes Steam IDs inside folder names too (the co-op save folder is named after the friend's ID;
  two testers' reports still had it).
- **Start Co-op did nothing after a world had been loaded in the same game session** (played solo, went back to the
  title, then invited a friend; or came back from a co-op session): the host's game never restarted and the friend
  waited for ever. In every build up to 47135b83. Tested for a host coming back from its co-op world; the solo case
  is the same code and was not run (a solo test copy would load the developer's real save).
- **A start that does not work no longer needs both games closed.** A friend who is not let into the host's world is
  told so on its waiting screen after two and a half minutes, and the host gets a card in the world after four. The
  host uses Menu > Co-op > Leave the session and chooses Start Co-op again; the friend stays in the party at its
  menu meanwhile ("<host> ended the session. You are still in the party ..."), and both games restart.
- A second Start Co-op in the same launch restarts both games again (the friend's game too, although it already is
  a guest). Before, the host's game refused the friend's restarted game for the rest of the launch (base: a
  connection on which nothing was said is now simply replaced by the new one).
- The waiting screen said "Joining the start of the story..." on Continue and From My Solo Progress. It now says
  "Joining <host>'s world...".
- The "no Co-op menu" fix of 47135b83 could set itself off on a PC that did not need it, when the game's window was
  behind another window in the seconds after start-up: it then hooked the same drawing function twice (every frame
  passed the mod twice; nothing visible). It now never hooks a function twice, and where DLSS frame generation's
  plugin is loaded it only looks while the game's window can be seen.
- The report tool ("Diagnose Co-op+") also packs the two runs before the current one (the co-op start restarts the
  game, so the title part of a session was missing from every report so far).
- The base's log no longer shows an error line for a thread that ran into a page while a hook was being written
  (the hook library retries it; it was never an error).

Posted 2026-11-12 as 0.1.0-alpha+47135b83:
- **No Co-op menu at all on some PCs (reported on an AMD Radeon RX 7900 XTX, frame generation off)**: on those PCs
  the game draws its frames through a different path than on ours, and nothing of the mod was ever drawn (no menu, no
  waiting screen). The mod now notices within a few seconds that no frame reaches it and hooks the other path too.
  Tested here only by simulating it on an Nvidia card; not on AMD hardware.
- The base no longer writes a line to its log for every NPC schedule group the game loads for the whole session
  (the first 300 only).
- After "your friend has no character yet", Start Co-op does nothing more until the game is restarted (it only
  repeated the same refusal).
- A downed player now lies on the ground, on their own screen and on their friend's (before, the character stood
  upright at 0 health on its own screen). The death screen, the HUD and both revive choices are as before; the
  friend's hold-Interact revive and the Q / U choices stand the character up again. `[coop] downed_pose=0` turns it
  off.
- New Co-op Save works: the guest used to stay on "Waiting for Host" for ever (its login came before the new world
  had a save, so its own save was never loaded). Its login now waits for the host's new game.
- A guest now starts at the host's side. Before, a guest started wherever its own save was, which could be more
  than a kilometre away, and had to use "Go to my friend". `[coop] join_beside_host=0` keeps the old start.
- The downed friend's name sits by the body instead of floating at head height, and the "Interact" prompt on your
  friend no longer shows over your own death screen.
- The title menu's bottom line ("Invite a friend to begin a co-op journey, or start solo.") was cut off halfway.
- A misleading log line about shared loot ("set enemy_auto=1") is gone in the shared world.
- A guest more than 80 m from the host can fight: it now takes damage and its blows count there (the game sets all
  damage to 0 that far from the world's owner, so the guest could neither hurt nor be hurt). Tested 108 m from the
  host with the host's quest bandits: the guest lost health, killed six, and the bandits fought back.
  `[session2] damage_range_players=0` in the host's cdmp.ini turns it off.
- With the mod installed, playing solo or sitting at the title recorded every network packet to files in the game
  folder (`bin64\cdmp_packets_<date>.log` / `.bin`) with no size limit. It no longer records. You can delete those
  files; they are not needed.
- The host's log no longer gets two lines every second during co-op.
- Correction to the far-fight line above: until build d5c06748 it only worked while the fight was within 80 m of the
  host along the map's north-south axis (the test spot happened to be). From d5c06748 on it is by real distance to
  the nearest player; tested 484 m from the host (see PROGRESS, of1-of3).
- A friend who has never played (no save at all) could not join: their game waited on "Waiting for Host" for ever.
  Now their game says so, goes back to its normal title by itself and shows what to do ("You have no character yet.
  Start Solo, begin a New Game, and join again once you stand in the world."), and the host is told why. A new
  character made inside the host's world is not possible yet.
- Checked, no change needed: a player on horseback can fight (the guest's blows count and it can be hurt in the
  saddle).
- Known: enemies that came for the host do not go after a guest far from the host until the guest attacks them.

0.1.0-alpha+3c46bec5 (2026-11-11):
- The Co-op menu, prompts and party frame now show with Intel XeSS and AMD FSR Frame Generation on (found by
  D3vils). Tested with XeSS; FSR could only be run on an NVIDIA card here.

0.1.0-alpha+0797bafd / 21f0f6d0 / ed31f10e (2026-11-11):
- First launch: the "NEW: co-op saves + live quests" box could not be answered and stayed in the middle of the
  screen (found by Borque). It is gone in the shared world.
- The title menu no longer closes on the first key press; F8 (pad: View) brings it back.
- Going down in the shared world shows the death screen with Instant Revive / Revive at Checkpoint, and a downed
  player can no longer move or attack.
- The "co-op starts after the prologue" hold is off in the shared world.
- Enemy loot: what one player loots from a body, the other player nearby (30 m) gets too.
- Diagnose Co-op+ now collects the Co-op+ log.

0.1.0-alpha+2f4b0dd4 (2026-11-11):
- Enemies no longer ignore the guest's attacks; the guest is no longer a criminal for helping the host.
- An item one player dropped can be taken once, by whoever picks it up (it could be duplicated).
- Two crashes fixed (a guest crash on entering the host's world, a host crash after looting).
- New: give an item to your friend (inventory, Discard, "Give to <name>"); chests and gear lying in the world are
  for each of you; the shared stash (the host's warehouse, untested with an unlocked one).

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
