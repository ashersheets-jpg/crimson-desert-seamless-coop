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

## Next
1. Build the "why" lines; run cs121 (one PC). Post the diagnostics build with the next fixes so a tester's taming
   names the focus holder too.
2. If cs121 reproduces it: design the per-player focus slot (above) with every writer covered, unit-test the swap
   rules, then the checklist and a taming by a tester.
