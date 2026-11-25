# The joined player's taming: why the host's server refuses its last step (game 1.0.0.3732)

Status 2026-11-24: research. A release is built (native 52ef13e); the cause is not fixed. Evening: the likely cause
is a focus slot both players share in the host's stage manager (below: "The likely cause").

## What happened (Borque's two-PC session, 24 Nov, build 2451dff5)
- The joined player (user 2) mounted a wild horse; its game prepared its own taming scene 0x29D (resource hash
  0x3795CC07, one binding: its own character 0xA0100066) in the HOST's stage manager 0x51494321800 (User2Stages: user
  2's own manager was 0x5150BD90800).
- Branch "then 0" was accepted (13:03:30). The branch to state 2, the taming's success, was REFUSED at 13:03:45:
  the server's StageBranchStateReq handler got 0xFF9EA4EB (eErrNoFailedStageChartBranchCondition) from 0x2B56870 and
  answered with a Nak 1013.
- Nothing deleted the scene until the host logged out, so the joined player's game stayed in it: mounted, no input.
- The earlier, thrown-off attempt (scene 0x28F) was released by the copied delete ("[own 2292]", 20 Nov fix).

## The release (native 52ef13e)
shop_probe's B2 (the handler's result, 0x2D55B6A) tells observer_seq_copy each of user 2's branch results. A refused
branch of user 2's own scene that no accepted branch and no 2292 follow within 6 s gets that scene's 2292 naming user
2's character ("ObserverSeqCopy [own refused]"), once per scene. Tester logs so far: 5 branch answers for user 2,
4 accepted, the taming's one refused.

### Independent review of the release (2026-11-24)
- Fixed in native c63f8b4: (1) a refused scene that the server then moved on another way (a StageBranchStateAck of
  it to user 2's queue) was still released after 6 s: it is dropped now (Borque's log has no branch of 0x29D to user
  2 after the refusal, so his case still releases); (2) a push the queue refused (over half its budget) or no
  character key lost the release for good: retried every 0.5 s, given up after 20 tries.
- Open, minor: (3) B2 knows user 2's branches by sequencer id only (a ring of 32), so a host scene with the same id
  as a recent own scene of user 2 could be confused (unlikely); (4) the 2292's u32 | u8 tail comes from the last
  2292 the host's client saw, not from this scene (the "[own 2292]" copy uses the scene's own): the client's
  acceptance is to be confirmed in a two-PC log; (5) only user 2's client is released: the server's scene stays
  alive, still binding user 2's character, until the host logs out (a later taming may meet it); (6) own_ /
  own_deleted_ hold at most 4096 ids per server instance and are never pruned (an existing limit of "[own 2292]"
  too). It needs shop_probe=1 and observer_seq_copy=1: the host's co-op settings the layer writes (net/native_mode_text.h)
  set both.

## The code path (3732 addresses)
- 0x2B56870 is a protection stub: a 5-byte jmp to 0xE169180, a relocated but readable body (LESSONS: "a function that
  disassembles to junk after a jmp").
- 0xE169180 finds the scene in the manager (0x21C7370), then calls 0x2C0B8B0 (the scene's branch step) and stores its
  result; a non-zero result is reported through 0x2D4E7A0.
- 0x2C0B8B0 (about 1700 instructions) sets its result from error globals filled at start from hashed names. Two
  paths load the same global 0x71BB2E8 just before returning: (a) a flag computed from the target node's tables and a
  byte at [[x+0x88]+0x75]; (b) the condition call 0x25C82A0 returning false (2C0C02D). Which global holds 0xFF9EA4EB:
  scratchpad errglobal73.py (the name's string, the code that stores its hash, every load of that global, and the
  path from 0x2C0B8B0), to run after the checklist.

- CONFIRMED 2026-11-24 13:58 (errglobal73.py / errglobal73b.py): the game registers eErrNoFailedStageChartBranchCondition
  at 0x2443149..0x2443169 with the global 0x71BB2E8 (lea rcx,[0x71BB2E8]; mov [rsp+0x20],rcx; lea rdx,[the name];
  call 0x164E060). 0x2C0B8B0 loads that global at exactly two places: 2C0BFD7 (a node entry of kind 2 with the
  element's [+0x88]+0x75 set) and 2C0C03A (the condition call 0x25C82A0 at 2C0C02D said no).
- The condition call gets the target node, [scene+0x28] (the scene's owner actor: for this scene the JOINED
  player's character, per its PrepareSequencerAck "owner actor 0xA0100066"), a context object and the scene.

## Hypothesis
First thought: the scene lives in the host's manager, so the condition is evaluated in the host's context. The
condition call's actor argument is the scene's own owner (the joined player's character), which speaks against that.
More likely the condition reads a state the host's server never got from the joined player's game (the taming's
progress on the horse, whose AI the ride rule handed to the joined player's game). Not proven: the next batch's
log-only probe (scratchpad apply_branch_fail_probe.py; bytes checked in the 3732 exe) names which of the two places
refused and the target node's object types, from a tester's taming.

## The condition call, read (2026-11-24 evening; scratchpad cond73.py / cond73b.py on the 3732 exe)
- 0x25C82A0(node, X = [scene+0x28], context, scene, u8, ptr). X is the scene's MANAGER, not an actor: the mod's
  other modules name +0x28 kStage_Mgr, and 0x21CAF70 below feeds it to the scene lookup 0x21C7370 as a manager.
  (So the first BF probe's "owner actor" is [[scene+0x28]+0x60], a manager field; fixed in the next build's text.)
- First path: the target node's entry in the scene's processor list ([[scene+0x58]+0x10], u32 count +0x18, matched by
  u32 [entry] == [node+0x10]). If its element flag [[entry+0x88]+0x75] is set, 0x21CABF0(manager, scene) is asked and a
  yes REFUSES. 0x21CABF0: 0x21CAF70 takes the manager's current stage ([mgr+0xA0] when its low 29 bits are set, else
  [mgr+0x90]) and looks it up; none -> no; the same stage as the scene -> no; another live stage -> yes, unless its
  later checks (the two stages' +0x78 group through a table; a type-6 scene's list) clear it.
- Otherwise the node's condition object [[[node]+0x38]] is asked (vtable +8, or +0x10 when the context's +0x828 is
  set) with a pa::CompareSequencerTargetParameter (RTTI; it derives from pa::CompareTargetParameter); a true answer
  refuses. The objects are the game's pa::ConditionData_* classes (about 500 in the exe's RTTI, e.g.
  ConditionData_CheckWildVehicle, _CheckRide, _IsRidingVehicle, _BlockByExclusiveStage, _CheckOwnershipState).
- What [mgr+0x90 / +0x98 / +0xA0] are was already in ByJunter's S46 notes (native-base/docs/research/s25/s46-datos/
  nativo.md, "Qué son [mgr+0x90/+0x98/+0xA0]"): a branch into a node with that same flag sets [mgr+0xA0] = the scene's
  id and [mgr+0x90] = 0 (0x2045BC0); 0x1F6FF40 undoes it (+0xA0 = 0, +0x90 = +0x98); deleting the current stage
  clears it (0x1F709A0). It is "the focused stage of the manager's owning player". Shop buys (2789 / 2366) check it too.

## The likely cause: one focus slot for two players
The joined player's own scenes are prepared in the HOST's manager (0x51494321800 in Borque's log; its own manager
0x5150BD90800 holds none of them: the base's own-stages mode "U" is off and cannot arm on 3732). So both players'
flagged nodes write and check ONE focus slot. In Borque's host log, between the taming's accepted "then 0"
(13:03:30.5, which most likely made 0x29D the focus) and its refused "then 2" (13:03:45.1), the same manager prepared
the host's 0x29E (13:03:30.1), 0x29F ('cd_seq_faction_bleed_moru_slave_0300_01', 13:03:36.3) and 0x2A0 (13:03:36.6),
and the joined player's own 0x2A1 (13:03:42.3, its character plus three NPCs of scene 0x2BC74C06: an encounter of
its own, like 0x299 before it). If any of them took the focus, the taming's last (flagged) step had to be refused.
Not proven: the next build logs it ("ShopProbe [branch why]": the node's flag, the manager's +0xA0 / +0x90, whether
that stage is live, both stages' names, the condition object's class).
The same slot predicts more: a joined player's flagged step refused while the host has a focus stage of its own, the
host's refused while the friend's has it, and a shop buy of either refused (eErrNoInvalidStageInfo) when the other
holds the focus. cs121 (build/native/cs121.sh) is a one-PC repro of the second case: the friend's own butcher shop
open (the host far away first, so it is its own scene, not the twin) while the host talks to the inn's arm-wrestling
organizer, takes part and mashes.

## Fix directions (after the data)
- Proper: the joined player's own scenes in its own manager (the base's mode "U", user2_stages / user2_funnels'
  vf48 routing; not armed on 3732). Large; it is the base's unfinished design.
- Narrow: a focus slot per player for the host's manager: keep the host's +0x90/+0x98/+0xA0 and user 2's apart and
  swap the right one in around the server's work for user 2's scenes (its branch requests, its scenes' processor
  ticks, the deletes). Every writer of the three fields has to be covered (0x2045BC0, 0x1F6FF40, 0x1F709A0, vf1
  0x1F6B860) or the slot desyncs; to be designed once cs121 / a tester shows the slot is the cause.
- The current release (52ef13e) only ends the refused scene for the joined player after 6 s.
- NOT a fix: keeping the joined player's scenes out of the slot. Its shop buys pass only BECAUSE its own shop scene
  takes the host manager's focus (tcB185 / tcB194: the guest bought at the butcher and the grocer; 0x21F2780 checks
  the focus against the NPC's stage, S46).

## The focus slot's code on 3732 (scratchpad focus73.py / focus73b.py; ServerSequencerStageManager vtable 0x5F5FAF0)
| What (S46's 2976 name) | 3732 | Notes |
|---|---|---|
| reset at creation | 0x21C6490 (caller 0x2B3AAC3) | +0x90, +0x98, +0xA0 = 0 |
| vf reset (vf1 0x1F6B860) | 0x21C6A40 (no direct caller) | the three = 0 |
| undo (0x1F6FF40) | 0x21CAF10 (95 B; callers 0x21CBA36, 0x2271487) | +0xA0 = 0; +0x90 = +0x98 and +0x98 = 0 when +0x98 is set, else +0x90 = 0 |
| set at a flagged branch (0x2045BC0) | 0x2271150 (callers 0x226DC8F in the processor's vf 0x226DA80 = ServerSequencerStage_StageChartProcessor +8, and 0x2270B65 in 0x226F7A0, which the branch step calls at 0x2C0D200) | at 0x2271252 / 0x227125B: +0xA0 = the scene's id, +0x90 = 0, then vf +0x110; it first reads the current focus (0x21CAF70) and does NOT take it when another stage holds it (2271236 -> 2271496); leaving a flagged branch calls the undo (0x2271487) |
| clear at a delete (0x1F709A0) | 0x21CB970 (callers 0x21C8663, 0x2B3BCA8) | reads +0xA0 / +0x98, calls the undo (0x21CBA36) when the deleted stage was the focus |
| readers | 0x21CAF70 (the current stage; 0x21CABF0, the setter, ~40 more callers), 0x21C8DB0, 0x21C9480, 0x21C9AD0, 0x21CA5F0 | 0x21F2780's own read (shop buys) as in S46 |
So the request path writes the slot synchronously (2880 StageBranchStateReq -> 0xE169180 -> 0x2C0B8B0 -> 0x226F7A0 ->
0x2271150), and the processor's tick writes it for transitions it makes on its own.

## Per-player focus slot: design draft (to build only once cs121 or a tester log shows the slot is the cause)
- State: the host manager's own triple stays where the game keeps it; user 2's triple U (+0x90, +0x98, +0xA0) is
  kept by the mod (zero at the session's start).
- Swap in U, run, save U, swap the host's back, around every server operation done for user 2: (a) user 2's session's
  Reqs that read or write the focus (2880 branch -> 0xE169180; the shop Reqs that reach 0x21F2780: 2789, 2366,
  2458, 2308, 2940, ...), best at the one place the server dispatches a received Req of a session, so every handler is
  covered; (b) the processor's vf 0x226DA80 when its scene is one of user 2's own (the ledger's "own" ids, or the
  scene's owner key); (c) 0x21CB970 when the deleted scene is user 2's.
- The reset paths (0x21C6490, 0x21C6A40) also zero U. A user 2 logout zeroes U.
- Same thread only (the server's); a nested swap is a no-op (depth counter). Pure bookkeeping in a logic header with
  unit tests (swap, nested, a write while swapped saved to U, a reset while swapped), then hooks with their bytes
  checked, off by default behind [session2] user2_focus_slot, research first ("probe": log what each swap would
  change, change nothing).

## Next
0. Written, not built (native 418d116 + 9b19699, local): the "why" lines and a FOCUS PROBE (ShopProbe [focus]): every
   write of a manager's focus slot (the setter's store 0x2271252, the undo 0x21CAF10, the end of the focus stage
   0x21CB970) with the scene, its owner (user 2's or not), the slot before ("TAKEN FROM ANOTHER STAGE"), and whether
   it runs inside a request of user 2's session (FR: 0x2A10B89 / 0x2A10B9A around ServerRunThread's one call of
   ServerNet::OnRecvPacket 0x2A111F0); a summary each minute. Read only; its lines have their own budget.
1. Build the "why" lines; run cs121 (one PC). Post the diagnostics build with the next fixes so a tester's taming
   names the focus holder too.
2. If cs121 reproduces it: design the per-player focus slot (above) with every writer covered, unit-test the swap
   rules, then the checklist and a taming by a tester.
3. WRITTEN (native c2ec8eb, local, not built; off by default: [session2] user2_focus_slot=1, needs shop_probe=1):
   fsl::FocusSlots (session2/focus_slot_logic.h, tests/focus_slot_tests.cpp) and its hooks in shop_probe.cpp: the
   request window FR1 / FR2 (user 2's session: its triple in), inline hooks at the setter 0x2271150, the processor's
   tick 0x226DA80 and a stage's end 0x21CB970 (each 3 or fewer register arguments, no stack argument read: checked),
   which swap user 2's triple in only for a stage positively owned by user 2 ([[stage+0x48]+0x60] = its character)
   and pass everything else on as it is (a wrong owner read gives the game's own behaviour back, never a new mix).
   Server thread only (server_thread_id()). Order of use: the focus probe's lines first (do they mark user 2's stages
   "USER 2's"?), then cs121 with user2_focus_slot=1, then the checklist with it, then a tester's taming.
   Not covered: the base's two reset paths (0x21C6490, 0x21C6A40; harmless: a stale id is no live stage, so it blocks
   nothing), and user 2's focus work outside its requests and its own stages' setter / tick / end (none known).

## What the probe runs showed (24 Nov evening: cs121a, pp57, pp58)
- The setter runs for every manager: the host's SERVER manager on the server thread and the host's CLIENT manager on
  its own thread (the client mirrors the server's focus ~15 ms later). The fix's thread test keeps it to the server.
- The host's own talk / arm-wrestling match is a focus stage (0x34C in cs121a, 0x4DA in pp57), and its flagged
  steps are applied in the PROCESSOR'S TICK, outside any request: the per-stage hooks carry most of the fix.
- User 2's requests reach ServerRunThread's OnRecvPacket call with the HOST's session in r8 (the entry hook re-routes
  them by the buffer's tag): the request window now tells them by that tag (server_packet_is_user2).
- No owner field on a server stage: +0x98 and [+0x48]+0x60 read 0 on the host's own talk, and the +0x48 chain faults on
  some stages (pp57-armwrest's two FAILs, before the module's reads went through game::fast_read). User 2's own
  scenes are known by id from the PrepareSequencerAcks (2356) to user 2 naming its character.
- The joined player's own SHOP scene runs in its OWN manager (User2Shops' routing): its focus is already separate.
  The scenes of its own that stay in the HOST's manager are the unrouted ones: Borque's taming (0x28F / 0x29D) and
  encounters (0x299 / 0x2A1: its character plus 3 NPCs; deleted with the host as the target). pp58-chest had two
  such encounter scenes of the joined player (0x1C2 / 0x1C3) that never touched the focus.
- pp58 (the full checklist, 28 steps, 1212 checks, 0 fail; probe on, fix off): 181 focus writes, all in the
  processor's tick, none inside a request. Host stages took the focus on the host's server and client managers (the
  opening scene, the arm-wrestling match 0x4D1, loot's 0x203); the joined player's own shop 0x1EA took the focus of
  its own manager. No checklist step makes a scene of the joined player's own take the host manager's focus.
- Done 25 Nov (native 96d6c65, layer d871af4): the request window only for user 2's StageBranchStateReq (2644 on 3732,
  2880 on 2976) and chart-event Req (2200 / 2591) about a scene of its own (the request's u64 sequencer, in the ids
  learned from the 2356s); every other request of user 2 runs with the host's triple, as without the fix. Why not
  every request: the setter runs in the tick (pp58), and a request about a host stage should meet the host's focus.
  Not covered (widen only on evidence): user 2's interaction Reqs (2642 / 2304), in case one checks the host's
  focus. "ShopProbe [own scene]" names the manager each own scene is in at its 2356. The layer now writes
  user2_focus_slot=1 for the host (the package's default). pp59 (the full checklist with it on) started 01:39.
- Self-review 25 Nov 02:00 (for the next build, not d871af4f): FR1 stores the live host manager into g_host_mgr
  itself, so when the host's manager CHANGES (a new character / field) and a request of user 2 comes before the
  module's once-a-second refresh, the refresh sees no change and never resets the slots: user 2's saved triple of
  the OLD manager is swapped into the new one. A stale focus id there blocks only if a live stage of the new manager
  has the same id (ids look like one counter per server: unlikely); the host's triple is never affected. Fix: FR1
  compares with g_host_mgr (exchange) and asks the reset itself when it changed, as the refresh does.
- pp59 (25 Nov, the fix ON, d871af4f; 28 steps, 1211 pass, 1 fail = my uncommitted docs): "swaps 0 failed 0 ..." in
  every step but shop (4 of user 2's branch / chart-event requests about its own shop scene: 8 swaps, 0 failed, 0
  unbalanced; harmless, that scene is in its OWN manager). [own scene]: all 19 of user 2's own scenes (door 10, shop 9)
  were in its own manager. Posted as 614591d0 (the same DLLs plus the README notes).

## Which other server paths read the focus (25 Nov, scratchpad interact_focus73.py, revfocus73.py, exclusive73.py)
- Forward, direct calls only (depth <= 7): the server's interaction decision 0x2126920, InteractionPreemptionStepReq
  2642 (0x2CDBE20) and InteractionDoStepReq 2304 (0x2CDC3B0) reach NO focus reader; the control (the
  StageBranchStateReq body 0xE169180) reaches 0x21CABF0 at depth 3 (via 0x25C82A0) and the setter at depth 4.
- The 506 pa::ConditionData_* classes share generic vtables (BlockByExclusiveStage, ExclusiveStagePlaying and
  IsReservedNextExclusiveStage have the same slots): their checks run elsewhere, so no class reaches a reader there.
- Backward from the readers (0x21CAF70, 0x21CABF0, 0x21C8DB0, 0x21CA5F0; direct calls, depth <= 6): 323 ancestors,
  84 of them virtual functions. The SERVER's among them:
  - ServerInteractionActorComponent (and Common...) slot 46 0x2359460 -> 0x23607D0 -> 0x21CABF0 "blocked by another
    live stage": 0x23607D0 takes a manager from an actor's chain ([actor+0xA0] -> 0x2299FB0 -> 0x19A14B0 -> its object's
    vf +0x48), looks the interaction's stage up there by id (0x21C7370, [arg1+0x20]) and refuses when another live
    stage blocks it. The client's ClientInteractionActorComponent slot 46 and its interaction target finders
    (FindManualInteractionTargetProcessor) call the same 0x2359460: the talk prompt itself may grey out on it.
  - TrocTrActivateMiniGameReq slot 2 0x2CBE4F0 -> 0x2F54DD0 -> 0x2BB2190 -> 0x21CAF70 (starting a minigame).
  - TrocTrStartPlayerBehaviorSequencerReq slot 2 0x2D5A160 -> 0x2BA2B40 -> 0x21D05A0 -> 0x21CABF0.
  - TrocTrStartChangeFocusActorReq slot 2 (depth 4), TrocTrReviveLoadingStartReq slot 2 (0x2C23BD0 -> 0x21CAF70),
    ServerGimmickActorComponent slot 6, ServerQuestManager slots 6 / 8, the global-stage requests.
- So whether the joined player's TALKS and MINIGAMES meet the host's focus depends on whose manager that actor chain
  gives for a request of user 2: its own (fine) or the host's (then the friend cannot talk to an NPC or start a game
  while the host is in a talk or a match). Never tested: no checklist step has the host in a talk while the friend
  talks. cs123 (build/native/cs123.sh): the host talks to the inn's organizer and leaves the talk open, then the
  friend opens the butcher's shop 75 m away, twice; then the host closes its talk.
- cs123a-c / cs124a-b (25 Nov 06:21-07:26) could not answer it: each teleported the friend into the market AFTER the
  host's shop scenes ran, the known case where the friend's shop twins never take (PROGRESS 2026-11-21 13:05: its game
  refuses a twin's prepare while the shopkeeper is not loaded there; shop_twin_again ships off). Its interaction check
  (GuestXInter A) then never runs at all, with or without the host's talk, the table's twin or the host's distance.
  cs125 (build/native/cs125.sh) does it right: the friend at the butcher first (the host far), the host to the inn,
  then three opens: host far / host at the inn not talking / host in its talk. The friend's talk request is
  InteractionPreemptionStepReq (2642 on 3732; ShopProbe's "[2671]" lines are its handler 0x2CDBE20's result).
- cs125a (25 Nov 07:38): #1 and #2 open Renee's shop; #3 (the host in its talk with the organizer, scene 0x390 the
  host server manager's focus) does NOT: the friend gets InteractionPreemptionStepFailAck 2946 with the busy-NPC error
  0x58C8345D for RENEE, an NPC the host never touched (the C point still logs ACCEPTED: the refusal is later, the
  .xtls check 0xE401150). So the testers' BUG 2 (16 Nov: "no NPC interaction while the host is in a cutscene", read
  then as "the NPC the host is talking to") is the shared focus: while the host holds a focus stage, the friend's
  talk to ANY NPC fails. Fix (native, next build): the request window also takes user 2's 2642 / 2304 (its own
  triple), except at the NPC of the host's own current talk (its last accepted 2642's NPC while the host's focus is
  set), where the game's one-talker rule stays. Counted in the summary ("its talks among them", "left to the game").
- That fix did NOT cure it (cs125b: 4 of the friend's talks ran with its own triple, 16 swaps, none failed, #3 still
  refused with 0x58C8345D) and cs126a (the same flow WITHOUT the host's talk) opened all three: the host's talk is the
  cause, but the server's busy check reads some OTHER shared state than the manager's focus. Reverted (native 2nd
  commit of 25 Nov). Candidates from S46 1.1: the requester state vf160 0x211F930 (eErrNoAlreadyPreemptingActor when
  [compSol+0xD8] is set; eErrNoInteractionDelayTime) if the requester's component resolves to the host's for user 2's
  request, or the .xtls reserve 0xE401150. Next: a probe at the 2642 handler logging, for user 2's requests, the
  requester actor, its interaction component and +0xD8, and the host's component.

## THE SLOT RAN ON THE WRONG THREAD (25 Nov, cs125d's [u2 talk] probe)
- The request window (FR1 / FR2 around ServerRunThread's call of ServerNet::OnRecvPacket 0x2A111F0) runs on the
  NETWORK thread (cs125d: T30528). OnRecvPacket only hands the packet on: the 2642 handler (its C point) ran on
  T35304, and the server stage manager's focus writes (setter, undo) also on T35304, the server's LOGIC thread
  (T25068: the host's client manager). The talk probe saw the C point never inside the window ("not reached").
- slot_here() compares with g_built_tid (server.cpp:403, the thread that built user 2's session: the network side), so
  every detour call on T35304 was refused ("other threads" 222 in cs125d, 490 in pp59-armwrest) and the request
  window's swaps happened on T30528, where nothing reads the focus, while T35304 ran: a small race on the host's
  triple (8 swaps in the whole pp59, none failed or unbalanced).
- So the per-player focus slot posted in d871af4f / 614591d0 never acted, and the cs125b talk swap could not either.
  The layer stops writing user2_focus_slot=1 in the next build. To redo: the detours' thread test must take the
  LOGIC thread (learned from the setter / tick hooks on the host's server manager), and the request window must wrap
  the handler's own run on that thread (the dispatch of a queued request on T35304, not OnRecvPacket), with the
  request's user known there.
- Redo design (not built): ShopProbe already hooks INSIDE the StageBranchStateReq handler on the logic thread: B1
  0x2D55B56 (right before its call of the branch 0x2B56870, which runs the condition check 0x25C82A0 -> 0x21CABF0;
  rax = the manager, t_branch knows whose branch and the sequencer) and B2 (its result). So: (1) learn g_logic_tid
  at B1 / at the setter on the host's server manager, and let slot_here() take it (the setter / tick / end detours
  then act where the focus is written); (2) the branch window = B1 -> B2 for user 2's branch of its own scene in the
  host's manager (both on the logic thread, so no race); (3) the chart-event Req 2200 needs the same kind of point in
  its handler (to find). Nothing scripted reaches the case (a taming / an encounter of the joined player's own in
  the host's manager): the proof is "no harm" in a full pass plus the [branch why] lines of a tester's taming.
- Checked 25 Nov 08:47: the probe's [branch] (B1 / B2), [2671] (C) and the server manager's [focus] writes all come
  from ONE thread per run (cs125d T35304; pp59-armwrest T424; pp59-shop T6856): the logic thread. CORRECTION to
  pp58's reading above ("181 writes, all in the processor's tick, none inside any request"): the probe's "inside a
  request" flag (t_recv_u2 / t_recv_any) is set on the network thread, so on the logic thread it was always 0: the
  writes may as well have come inside a request's handling. pp58 did not show where they come from.

## The redo, built and run (25 Nov, native 81e3ed4; cs125e)
- B1 learns the logic thread and opens the branch window (B1 -> B2) for user 2's branch of a scene of its own in the
  host's manager; slot_here() takes that thread; FR1 / FR2 keep only their counters.
- cs125e (the cs125 shop flow with the slot on): the logic thread learned (T5484, the same thread as the 2642
  handler's C point); "user 2's branches of its own scenes in the host's manager 0 (other branches passed by 70)",
  swaps 0, failed 0, unbalanced 0. The friend's own shop scene runs in its OWN manager, so nothing there reaches the
  case. The focus lines' labels now work on that thread: the friend's own shop undo "inside USER 2's branch request",
  the host's talk's undo "inside a branch request (not user 2's)" (so the host's focus writes do come inside its
  branch requests too). #3 refused again (2946, 0x58C8345D) while the host talked: BUG 2, not the slot.
- The layer writes user2_focus_slot=1 for the host again. The proof that it helps is a tester's taming (its [branch
  why] lines and the slot's summary: "user 2's branches of its own scenes in the host's manager N" with N > 0).

## BUG 2 (the host's talk refuses the friend's talks): where 0x58C8345D comes from (25 Nov, reading S46)
- The FailAck 2946 is queued INSIDE 0x2E3C180 (3732's twin of 2976's 0x2B36EF0): 4 us before the C point, whose
  path result stays 0. In S46's map of 0x2B36EF0 (native-base/docs/research/s25/s46-datos/nativo.md 1.1) the one step
  there that answers by itself is the .xtls RESERVE 0xE3F84D0 -> 0xE401150(compNPC, sol, idx, p5): on failure
  eErrNoAlreadyPreemptedActor; on success 0x2123090 points the NPC at the requester and [compSol+0xD8] = the NPC's
  key. S46 item 4: "0xE401150 applies the exclusivity of the key's InteractionInfo (+0x13 and +0xBC). If the key does
  not exempt players, the second gets eErrNoAlreadyPreemptedActor." So 0x58C8345D is most likely
  eErrNoAlreadyPreemptedActor: to confirm with scratchpad errname73.py (the name's registration on 3732, its global,
  the global's loads near the 2642 path) or S46's P3 (read the globals at run time).
- Why the HOST's talk with ANOTHER NPC (the organizer, key 0x000F431C) makes the friend's shop talk (Renee, key
  0x000F4321) fail: the exclusivity is the InteractionInfo's (+0x13 / +0xBC: perhaps a group shared by both keys, or
  "one preemption per owner" where Renee's owner is the host through the host's shop scene that binds her), not
  Renee's own preemption state (she was free). To read: 0xE401150's 3732 twin, and at run time the two keys'
  InteractionInfo +0x13 / +0xBC (S46's probe P5) during cs125's flow.
- 3732, static (scratchpad errname73.py / errname73b.py, 25 Nov 09:39): the names are registered at 0x244D3B5..405
  (lea rcx,[G]; mov [rsp+0x20],rcx; lea rdx,[name]; call 0x164E060): eErrNoAlreadyPreemptedActor -> global
  0x71D6974, eErrNoAlreadyPreemptingActor -> 0x71D6978. Their loads:
  - AlreadyPreempting: 0x2359089, in the requester-state check (2976's vf160 0x211F930; before it 0x2359060 tests
    [[comp+0x1A0]+0x3DC], the "moving" byte, for another error): `cmp [comp+0xF0],0` (2976's +0xD8: the
    requester already holds a preemption); and 0xC958F21 (relocated code): `cmp [r14+0xF0],0; je -> mov
    [r14+0xF0],eax (the NPC's key)` = the reserve's "mark the requester", AlreadyPreempting when already marked.
  - AlreadyPreempted: 0x235BDB9 and 0x2A33ED0, each right after `call 0x235D030(compNPC = [[actor+0x68]+0x98], ...)`
    returned false: 0x235D030 is the NPC's reserve on 3732; and 0xB4CB50D (relocated: after 0x235E850 with
    [comp+0xE8] == 0).
  - So which one 0x58C8345D is decides the mechanism: AlreadyPreempting = the server judges the friend's request
    against a component that already holds the host's preemption (the friend's request resolved to the HOST's
    character's component?); AlreadyPreempted = Renee's reserve fails (an exclusivity of the key or of her
    binding in the host's shop scene). The next build logs both values once ("ShopProbe: the game's preempt
    errors: ..."), so any host log names it.
- S46 section 3 on the NPC's side (0xE401150 on 2976): with a player requester and InteractionInfo +0xBC set (and
  +0x13 != 3) the reserve always accepts; else by +0x13: 0 = one slot (+0x98 requester / +0x9C key), 1 = per key
  (+0x98/+0x9C, +0xD0, +0xE0/+0xE4, +0x118 or the key in the lists +0xA8 / +0xF0), 2 = by (key, p5). All of it is the
  NPC's own state, and Renee was free (cs126a: #3 opens without the host's talk), so the NPC side fits badly. The
  requester side does fit "ANY NPC while the host talks": [compSol+0xF0] (2976's +0xD8) holds the NPC the requester
  is preempting; if the friend's 2642 were judged with the HOST's component, the host's open talk would refuse every
  talk of the friend's with eErrNoAlreadyPreemptingActor.
- BUT the [2671] C lines already name the handler's requester actor: on_c's "user" is user_key_of(r13), and every one
  of the friend's reads "USER 2 asked: requester 0xA0100066 (user 0x90100065)": r13, the actor whose component the
  check uses ([[r13+0x68]+0x98]), is the FRIEND's character. So "judged with the host's component" is out unless the
  failing step takes another actor; AlreadyPreempting would then mean the friend's own +0xF0 is set during the host's
  talk (why?), AlreadyPreempted that Renee's own state is (a block the host's talk puts on the NPCs of its other
  scenes, e.g. +0xD0 / +0x118 in mode 1?).
- ANSWER (cs127c's host log, 25 Nov 09:54, the one-time line): eErrNoAlreadyPreemptedActor = 0xA956C7AF,
  eErrNoAlreadyPreemptingActor = 0x6DB9963D. The friend's refusal 0x58C8345D is NEITHER: no preemption state refuses
  it. Remaining candidates in the 2642 path (S46 1.1): the requester's state (dead, loading, QTE,
  eErrNoInteractionDelayTime), the NPC's state (eErrNoSequencerNotInteractableState: the NPC has a dynamic entry with
  a scene), the common validation (eErrNoInvalidDynamicInteractionData, ownership, the condition vf1E8,
  0x2126920's eErrNoInvalidStageChartInteractionCondition through the FIELD OWNER's manager, distance / angle).
  0x2126920 is where the host's manager's state meets the friend's talk; and cs125b's "the focus swap did not help"
  proves nothing: that swap ran on the network thread (cs125d), so the shared focus is back as the lead for BUG 2.
  To name the error: scratchpad errhash.py (the registrar's hash from the three known pairs, then every name of
  errtable73.csv), else the run-time values of those candidates' globals.
- cs125d's [u2 talk] probe cannot answer it: it read +0xD8 (2976's offset; 3732's mark is +0xF0) and never saw the
  requester actor (it ran on the network thread). Next probe (after this batch): at the C point 0x2CDC149, on the
  logic thread, for user 2's 2642: r13 (the requester actor) and its key, [[r13+0x68]+0x98] and its +0xF0, the host's
  character's component and its +0xF0, the err at [rbp-0x80]; then the 2946's error against the logged preempt values.

## BUG 2 FIXED (25 Nov, cs128)
- The scan of the error globals (the new one-time line) found 0x58C8345D in 0x71D5EA4 =
  eErrNoInvalidStageChartInteractionCondition (scratchpad errtable73.csv): the stage-chart interaction condition, the
  check 0x2126920 makes through the FIELD OWNER's (the host's) stage manager. While the host's talk held that manager's
  focus, every talk of the friend's failed it: the shared focus after all (cs125b's swap ran on the network thread).
- Fix: C0, a mid hook at 0x2CDC13D right before the 2642 handler's call of its check 0x2E3C180, on the logic thread,
  opens the per-player slot for user 2's talk (its own focus triple in the host's manager); C closes it. Not for the NPC
  of the host's own accepted talk while the host's focus is set (the game's one-talker rule stays).
- cs128 talkwhile: 28 / 0. #1, #2 and #3 (DURING the host's talk with the organizer) opened Renee's shop menu; never
  refused; "its talks 2 (... left to the game: 0; the C0 window hooked)"; swaps none failed or unbalanced. In the
  release checklist from this build (30 steps).
