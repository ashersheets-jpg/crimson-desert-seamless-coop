# The joined player's taming: why the host's server refuses its last step (game 1.0.0.3732)

Status 2026-11-24: research. A release is built (native 52ef13e); the cause is not fixed.

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

## Hypothesis
The scene lives in the host's manager, so the branch condition is evaluated in the host's context (the manager's
owner is the host's character, who is not riding the horse). Branch 0 has no such condition, which fits it being
accepted. Not proven.

## Next
1. errglobal73.py: confirm 0x71BB2E8 (or another global) is eErrNoFailedStageChartBranchCondition and which path sets it.
2. If it is the condition call: what 0x25C82A0 evaluates (its arguments: the node, [r13+0x28], the scene, a byte).
3. A taming cannot be scripted: a log-only probe at the condition call for user 2's scenes would let a tester's
   session say which condition fails.
