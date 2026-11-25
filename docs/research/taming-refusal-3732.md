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
