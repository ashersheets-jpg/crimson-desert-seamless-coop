# The joined player's own arm wrestling at the Hernand inn table (B14, 2026-11-21, game 1.0.0.3732)

## Why the joined player has no "Try arm wrestling"

The table in the Hernand inn's cellar is a scene of the resource `cd_minigame_armwrest_base` (dictionary hash
0x94FEA6AE: lookup3 hashlittle with seed 0x000C5EDE, the hash of the shop scenes' names too). The server prepares it
for the field owner, the host (cs83w2: host id 0x31F, 2380 of 180 bytes, prepared together with
`cd_minigame_seotda_base_hernand` 0x1D8 and `cd_minigame_rockpapersci_base` 0x1F6 when the cellar loads). On the
joined player's game the scene is a copy in the host character's stage manager X: the organizer's and the patrons'
interaction entries belong to the host, and the joined player's prompt check refuses them, exactly as it refuses the
host's shop entries (shop-twin-again.md). The shops were solved by a twin (ShopTwin: the joined player's own copy of
the host's shop scene, in its own manager M); the table has none, so its prompt has nothing it can use (cs76f, cs84a).

## The host's talk with the organizer, message by message (cs83w2, twin_watch on the three scenes)

| when | direction | message | what it is |
|---|---|---|---|
| cellar load | S>C | 2380 (len 180) + 2752 type 0, N -1, root 0x22B6EC91 | the scene prepared, its initial state |
| approach | C>S | 2808 x ~20, 2672 x 7 | trigger volumes in / out, per-element flags (no answers) |
| arrival | C>S 2200 / S>C 2105 | chart events (0,0,1) (0,1,1) and acks | |
| R on the organizer | C>S 2642, S>C 2308, C>S 2304, S>C 2363 | interaction 0x000F431C on NPC 0xB0100341 | the server's check (0x2126920) finds stage 0x31F in the FIELD OWNER's manager: accepted, 2663 / 2916 follow |
| menu opens | C>S 2644 branch 5 | S>C 2369 (4, actor, id, 5, 0) + 2752 type 1, N 5, WITH the root (len 29) | no 2698 (a shop's greeting has one) |
| | C>S 2672 x 14 | one per scene element, indices 0..0xD | no answers |
| | C>S 2200 every 30 ms | chart events (1,0,1) (1,1,1) | S>C 2105 acks |
| | C>S 2359 | ActivateMiniGameReq, minigame 0x4240 | the layer's "we started a minigame" |
| | C>S 2808 | trigger volume | |
| | (game) | SavePlayerToSlot slot 0 and 206 | the game saves the host's world |

The menu reads "Fancy giving it a try?" with "E Participate (20 copper)" and "Esc Back". In cs83w2 neither an E press
nor 20 s of E presses started the match (the shots show the menu throughout), so the capture ends at the menu.

## The match (cs83w3: "Participate" is E HELD about 2 s; the host's sniffer recorded every packet)

| t (s) | direction | message (3732 opcode, length) | payload |
|---|---|---|---|
| 340.47 | C>S | ActivateMiniGameReq (2359, 6) | minigame 0x4240, player 0xA0100001 (sent when the menu opens) |
| 353.57 | C>S | JoinMiniGameReq (2204, 20) | 4, minigame 0x4240, actor 0xA0100001 (the player), zeros |
| 353.57 | C>S | SpawnMiniGameCharacterReq (2281, 19) | 4, **scene id 0x31F (u64)**, minigame 0x4240, u32 0xD551, u32 0x20000006 |
| 353.57 | C>S | ApplyMiniGameReq (3033, 2) | minigame 0x4240 (the 20 copper, most likely) |
| 353.58 | S>C | JoinMiniGameAck (2920, 17) | player, player, zeros |
| 353.59 | S>C | SpawnMiniGameCharacterAck (2810, 24) | 4, player, **scene id 0x31F**, 0x20000006, the opponent 0xB0100A5C, 5 |
| 354.19 | C>S | JoinMiniGameReq (2204, 20) | 4, minigame 0x4240, the opponent 0xB0100A5C |
| 354.19 | C>S | RecoveryActorForMiniGameReq (2162, 6) | minigame 0x4240, player |
| 354.21 | S>C | JoinMiniGameAck (2920, 17) | player, opponent |
| 372.21 | C>S | LeaveMiniGameReq (2443, 23) | 4, the opponent, zeros, stage key 0x00F437EC |
| 372.22 | S>C | LeaveMiniGameAck (2784, 30) | player, opponent, ..., 0x00F437EC, 0x00243556 |
| 378.51 | C>S | MiniGameResultReq (2885, 19) | minigame 0x4240, 1 (won), the opponent, 1, 0, 2 |
| 378.51 | C>S | UnspawnMiniGameCharacterReq (2361, 15) | 4, **scene id 0x31F**, minigame 0x4240, the opponent |
| 378.52 | S>C | UnspawnMiniGameCharacterAck (2922, 9) | 4, player, the opponent |

The scene's own requests during that match (C>S StageBranchStateReq 2644 = id u64 | branch u32 | u16 flag; the
server's answers in order; 2698 = SetAvoidByCutSceneAck 4 | actor | id | u8 on | actor, 2369 = StageOnBranchEventAck
4 | actor | id | branch u32 | u8 (the request's flag), 2752 = StageBranchStateAck 4 | actor | id | type | branch | root
0x22B6EC91 | 0, always the 29-byte form here):

| state | branch (flag) | answers (then the 2752 of the new state unless noted) | new state |
|---|---|---|---|
| 0 | 5 (0) | 2369 | 1 (the menu: Participate / Back) |
| 1 | 1 (0) | 2698 on, 2369, 2752, 2698 off | 2 (Back) |
| 2 | 0 (0) | 2369 | 0 |
| 1 | 0 (1) | 2698 on, 2369 (flag 1); the 2752 and a 2698 off only after the client's StageBranchStateDelayEndReq 2654 (id), ~2 s later | 3 (Participate) |
| 3 | 0 (0) | 2698 on, 2369, 2698 off | 4 (the minigame requests follow) |
| 4 | 1 (0) | 2698 on, 2369, 2698 off | 5 |
| 5 | 0 (0) | 2698 on, 2369, 2698 off | 6 |
| 6 | 0 (0) | 2369 | 7 (the match) |
| 7 | 0 (0) | 2369 | 8 (won; Leave follows) |
| 7 | 1 (0) | 2369 | 9 (lost) |
| 8 / 9 | 0 (0) | 2369 | 10 ("Play Again (20)" / "Back"; Result and Unspawn follow) |
| 10 | 6 (1) | 2698 on, 2369 (flag 1); the 2752 and a 2698 off after the 2654 | 3 (Play Again) |
| 10 | 7 (1) | 2698 on, 2369 (flag 1); the 2752 after the 2654, a 2698 off ~4 s later | 11 (Back) |
| 11 | 0 (0) | 2369 | 0 |

(cs83w3's 2698 off before the (3, 0) answers was the tail of the held (1, 0) step: cs83w4 shows it ~20 ms after the
delayed 2752, before the client's next request.)

Scenes 0x321-0x323 (prepared with the table) get a branch 1 every ~17 s from the host's client (2752 of 37 bytes with
a counter): the spectators' loop, not part of the match.

cs83w4's minigame requests on the other paths: Back from the menu = DeactivateMiniGameReq (2584, the minigame id);
a lost match = MiniGameResultReq with 0 (no prize); Play Again = SpawnMiniGameCharacterReq + ApplyMiniGameReq again
(another 20 copper taken); a won match = the prize's AddItemToInventoryAck (0x1E = 30 of item 0x4413: the stake back
plus 10); Back after a match = LeaveMiniGameReq naming the player + DeactivateMiniGameReq.

The host won (shot h6: "E Play Again (20)", the purse 114.64 top right). The match is driven by the CLIENT's
minigame requests, which name the minigame, the player and the opponent; only the opponent's spawn and unspawn name
the scene (u64 at payload offset 1). The fee and the result are the player's own requests (ApplyMiniGameReq,
MiniGameResultReq), so a joined player sending them pays and is paid from its own purse.

## What a twin would have to do

Everything about the scene except the talk itself is a request a twin withholds and answers locally, as ShopTwin does
for the shops: the branches (the table above, held steps included), the chart events (2105, the shop's form), and
silence for 2672 / 2808. The talk (2642) names the NPC and the interaction, not
the scene: the server checks the entry's stage in the field owner's manager, finds the host's 0x31F and accepts, as it
does for the shop twins' talks.

Not known yet (cs83w3 records every packet of the host's game around "Participate"):
- what "Participate" sends, and whether the 20 copper are taken by a server-side scene function (then a locally
  answered twin would let the joined player play for free) or by a request of its own;
- how the match runs (the gauge is client memory, kMiniGameRoot) and ends, and how a win is paid;
- whether the server accepts the joined player's ActivateMiniGameReq while the host's scene owns the table.

## Plan

1. DONE cs83w3: the host's full match, every packet recorded; the fee (ApplyMiniGameReq) and the prize
   (MiniGameResultReq) are the player's own requests, so a twin does not stop them.
2. DONE cs83w4: the other paths (the menu's Back, a lost match, Play Again, Back after a match).
3. DONE native 892384b (built, unit-tested): ShopTwin generalized. [session2] twin_extra = resource list
   (guest, research, default empty): a host scene of a listed resource gets a twin like the shops; its branches are
   answered from a per-resource table (`stl::kArmwrestSteps`, `stl::table_answers`), the 2752 held for the client's
   StageBranchStateDelayEndReq when the request's flag is set; a (state, branch) the table lacks sends the scene back
   to its start (logged "NOT in the table"); SpawnMiniGameCharacterReq / UnspawnMiniGameCharacterReq of the twin go to
   the host's server naming the host's scene, the server's SpawnMiniGameCharacterAck to this client names the twin.
4. cs86a: the twin was prepared but never started, so no talk prompt ("E Greet" only): the host's initial state of
   the table reached the joined player 4.4 s BEFORE the table's prepare (the observer copy holds a prepare until its
   actors are announced; states go at once) and ShopTwin only took an initial state that came after the prepare.
   21 of 31 host scenes came state-first in that run, every shop among them (docs/research/shop-twin-again.md).
   Fix: an early initial state is kept and given to the twin right after its prepare ("ShopTwin [early state]").
5. cs86b (on that fix, guest_x_research=1): the twins now start (the table's twin added one entry, component index
   0x114), but the joined player's own scripted teleport into the cellar, 13 s after they were made, ended them all
   (a far teleport ends every scene of its own manager). Test order fixed: the joined player goes to the cellar
   first, the host comes 22 s later (cs86c with early_state_x, cs86d without).
6. cs86c (the joined player first, the host 1 s later): THE JOINED PLAYER'S OWN MATCH STARTED. Its talk was
   accepted by the host's server ("USER 2 asked ... 0x000F431C ... ACCEPTED"), its twin answered the menu, Participate
   (held, given at the 2654) and the steps to the match (state 7); the server took its 20 copper, spawned its opponent
   for the host's scene (the id swap) and joined both; its screen played the intro with the opponent. The match then
   stood at "E Push": the opponent's actions came from the HOST's game (S>C ChangeAIStateAck / StartActionAck only;
   in a host match the player's own client drives the opponent). Fix: AiHandoff's MINIGAME rule (native, [session2]
   ai_handoff_minigame, default 1): the host notes the opponent at the SpawnMiniGameCharacterAck to user 2 and hands it
   to the joined player's game until its UnspawnMiniGameCharacterAck. Test: cs86e.
8. cs86e / cs86f (the minigame rule in, native 9dbab60): the opponent goes to the joined player's game 0.6 s after
   its spawn and its client drives it; the countdown shows on its screen; but at state 7 its client never does what
   the host's did at once (CurrentTargetChangeReq on the opponent, EchoActionSessionIDReq of the opponent, the push
   action 0x63F9EFFD + DoFrameEventReq): its character keeps the grip pose (0x35DD392F, re-sent every 2 s), E taps,
   E held 4 s, Space and left clicks change nothing, its match camera sits behind a bystander, and Esc does not leave:
   THE JOINED PLAYER IS STUCK IN THE MATCH. So twin_extra stays OFF by default. Everything the twin answers matches
   the host server's (the state-7 2752, the chart events' 2105s). Hypotheses for the next round: (a) the hand-over's
   AI reset (0.6 s after the spawn, the host's client ran the opponent's AI meanwhile) leaves the opponent's
   minigame AI in a state the match does not accept: hand it over AT its creation instead -- UNLIKELY (cs86e's
   capture: the spawn Ack at 339.257, the hand-over's two ChangeAiControlAck 2152 at 339.756 / 339.780, the client's
   JoinMiniGameReq of the opponent and its RecoveryActorForMiniGameReq only at 339.891: control first, then the join,
   the order of a host match, whose client runs the opponent from the start); (b) the match needs the
   table's other people (bound to the host's copy of the scene, which never started on the joined player's game) --
   RULED OUT as the whole story: in cs86c (early_state_x on) the host's copy 0x321 got its early state again right
   after its prepare (so it started) and the match stalled all the same; (c) client minigame memory (kMiniGameRoot
   0x724B3A8 +0x90) compared between a host match and a joined player's; (d) two live copies of the table on the
   joined player's game (the host's, at state 0, and the twin, at state 7) binding the same people: whatever the match
   looks up through those people's sequencer component (it names the host's copy) finds the idle one.
10. Next round, one build (written 2026-11-21 late as scratchpad patch scripts, applied after checklist pp43):
   (d) [session2] twin_extra_only=1 (guest, research): a twin_extra scene of the host's is given to the joined
   player's game only as its twin, so the table's people can bind to the twin alone (cs87: cs86f with it). The
   premise, from cs86f's own GuestSeqRoute [G5 actor] lines (22:02:37, 5 s after the twin 0x10000322 was made): the
   table's 7 people 0xB01003B8-0xB01003BE (the organizer 0xB01003B9, whose talk the server accepted, among them) all
   had their sequencer component on the HOST's copy (scene +0x2E0 0x322, owner 0xA0100001), none on the twin; and at
   22:02:16 GuestSeqRestore swapped THEIR scene's seat animation (seq_bodyanim_action 0x837487D4, the seat pose both
   seats play in a match) for an idle wait on three of them (0xB01003BB / BC / BD: "not found" in their chart): the
   host's copy they belong to never ran on the joined player's game;
   (c) the layer's [dev] minigame_probe=1: the game's minigame holder and gauge ([[exe+0x724B3A8]+0x90]+0xD30) in the
   log twice a second, so cs87 shows whether the joined player's match ever goes live, and cs83v (the host's own
   match, versus_test) gives the host-match baseline.
   Reading the probe: no "a minigame holder came" line = the client never set up the match's gauge; a holder with the
   gauge fixed at 50.00 while "E presses" climbs = the match is set up but the presses do not push (cs86's symptom);
   the gauge moving with E = the push works (then the end of the match, its result and prize decide B14). In cs83v the
   host's own match gives the same lines with versus_test pushing too.
   If cs87 still stalls, the next place to read (3576 decomp, build/decomp): the arm wrestling has no
   MiniGamePhaseData_* classes of its own (the card and dice games do); its client side is the script class
   ::pa::gameClientScript::MiniGame_ArmWrestling (registered in FUN_140d60dc0 with 0x1459650) and the UI controller
   UIGamePlayControlRootMiniGameArmWrestlingTimeLimit (vf2 0x11E7B80, 5139 B), which takes the UI events
   ArmWrestling_ChangeStateStartCount / ArmWrestling_ChangeStartCountProgress / ArmWrestling_ChangeStatePlay. The
   joined player's screen showed the countdown, so StartCount came; whether ChangeStatePlay came (and from what: the
   scene's state-7 track or a server message) is the question.
   RESULTS (2026-11-22 02:42-03:02, af1843c2): cs87 (=1) and cs87b (=2) both end like cs86f: the joined player's talk
   accepted, the fee, the opponent spawned and handed to its game, the twin to state 7 ("E Push") and no further; the
   table's people's sequencer components name the host's OWN scenes 0x32C-0x338 (theirs, not the table's 0x322 / its
   twin), so (d)'s premise does not hold. The probe settles (c): in the host's own match (cs83v) the gauge at
   [[exe+0x724B3A8]+0x90]+0xD30 is NaN through the intro and countdown and goes live 14 s after the match begins
   (51 -> 88 with E); in the joined player's matches it stays NaN all match: its client never enters the push phase.
   Next: cs92 (cs83v with the host's capture) to find the message that starts the push phase on the host's client,
   against cs87's joined-player capture (no minigame message after the opponent's JoinMiniGameAck).
   cs92 RESULT (2026-11-22 04:58, the host's own match with its capture; scratchpad mgdiff.py against cs87): in the
   host's match the table's state 7 (363.541) is followed by the client's two UpdateStageChartEventReq 2200 for the
   scene (events 7/0 and 7/1, 363.587) and 2 ms later by the server's CatchActionResultAck 2562 (catcher 0xA0100001,
   target the opponent 0xB0100AF7, action hashes 0x18F128E3 / 0x64528294): the server's grip. Only then does the host's
   client run the push: EchoActionSessionIDReq of the opponent, ReleaseActionPointReq, StartActionReq 0x35DD392F ->
   0x63F9EFFD (host) / -> 0x18F128E3 (opponent), DoFrameEventReq; CatchNak and PostProcessAttackResult follow, the
   quick-time results (RequestQuickTimeEventResultReq) 4 s later, LeaveMiniGameReq at +23 s, MiniGameResultReq at +30 s.
   The joined player's capture (cs87) has none of it after its opponent's spawn: its TWIN answered the two chart events
   locally (with the 2105 the server would send), so the server never ran the catch. Hypothesis (e): the twin's chart
   events of state 7 must reach the server for the host's scene. Native 2b2eaf2: [session2] twin_extra_chart (guest,
   research, default 0): 1 = a twin_extra scene's chart events in state 7 go to the host's server under the host's
   scene id (shop_twin_rewrite_out), and the server's 2105 for them comes back to the twin (matched by scene and the
   9 event bytes, 5 s); 2 = every chart event of the twin. Test: cs95 (cs87 with it, both captures). Open: whom the
   server makes the catcher (the requester, or the scene's own player binding: the host).
   Hypothesis (f), native 9fb4fa4: twin_extra_chart=3 forwards nothing; the twin's answer to the match's first chart
   event (7/0) carries a CatchActionResultAck made on the joined player's game (cs92's 55 bytes: u32 catcher | u32
   target | u32 2 | ... | f32 0.1 @27 | target action 0x18F128E3 @31 | 0x64528294 @35 | ... | catcher action
   0x63F9EFFD @50 | 0x17), catcher = its character, target = the opponent of its own SpawnMiniGameCharacterAck (+17).
   Test: cs95b. The server never hears of that grip: whether the push, the quick-time results and the match's result
   (prize) then go through is what cs95b shows.
   If cs87 or cs87b pushes: before twin_extra goes on by default, (e) BOTH players at the table at once (the host's
   own match in its copy, the joined player's in its twin, the server asked for two opponents at one scene id: does it
   spawn a second patron, refuse, or hand the first one over?), and (f) the host's match seen from the joined player's
   screen with twin_extra_only=1 (no host copy there: the host and its opponent play their actions without their
   scene; how it looks). (e) also answers B13's open "two matches at one table".
9. (old step list) cs86d / cs86e: the joined player talks to the organizer (variant 0x000F4B31 at (-10661.5, -3832.5)),
   Participates, plays and leaves the menu. Open questions: does the server spawn user 2's opponent for the host's
   scene, take its fee and pay it; does the joined player's game play the intro, the countdown and the match from its
   twin. Still open after it: the twin loss on a far teleport (in play: a fast travel or respawn in the same field,
   unless the host re-sends its scenes then).
