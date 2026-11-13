# Shared story cutscenes (the ready check)

The owner's pick (2026-11-13), from three options: **ready check first**. A story trigger waits until both players are
there and both have pressed interact; then the scene plays in each player's game. (Jay's idea in #play-testing-feedback.)

## What was measured (game 1.0.0.3663, New Co-op Save, one PC, two copies; runs cs1 - cs3, PROGRESS.md)

- **The trigger.** At the cabin of the first quest ("Return to your comrades", ~45 m from the start) the HOST's client
  sends `SequencerTriggerVolumeInOutReq` (2808): `u32 sequencer 0x1BC | u32 0 | u32 kind 0x20000003 | u32 actor =
  its own character 0xA0100001 | u8 in`, then `StageBranchStateReq` for 0x1BC. The server answers
  StageOnBranchEventAck, UpdateMissionUIStateListAck, DeleteSequencerAck 0x1BC, UpdateStageListAck,
  UpdateMissionListAck (the quest steps on). 0.6 s later the host's client starts the cutscene (33 s). The cutscene runs
  in the client; the server sees the trigger and the mission update only. Other 2808s name NPC gimmicks (0xB...) with
  kinds 0x20000006 / 0x20000022.
- **The joined player has no story of its own.** Walking through the same spot the guest's client sent no 2808 at all
  (0 of 17 395 requests), no StageBranchStateReq, no UpdateStageChartEventReq. The server prepares story sequencers
  (PrepareSequencerAck 2356) only for the host's character: 22 at the host's world entry, with no request of its
  client before them; the guest got 44 PrepareSequencerAcks, all the host's (the base's observer copies, filed under
  the host character's manager X on the guest and never played). The guest does get its own stage list
  (UpdateStageListAck x79 for its character). So the guest's client has no stage sequencers, hence no trigger volumes.

## Where the server decides (from the base's U-1 work, user2_stages.{h,cpp})

- Each character has a ServerSequencerStageManager. The manager's evaluation (prepare P, the 2043 tick T, the stage
  element E 0x288A970-equivalent) runs for the field OWNER's manager: the owner gate G `cmp [r15+0x180],eax`.
- The base's option U-1 (`user2_stages=1`, shipped) ARMS user 2's own manager, but only when user 2 is >= 10 cells
  (64 m) from the host for 10 s, drains it at <= 7 cells, and its element filter U-f lets only schedule elements (and
  `cd_seq_funcnpc_*` merchant scenes, `user2_stages_elements=schedule+funcnpc`) through. Its quest context is the
  HOST's quest manager (the base author's rule of 2026-10-13: "the guest follows the host's progress").
- Works on 3663: pp13-farfight2 "arms 1".

## The plan

1. Probe: the sequencer resource names the HOST's manager prepares at world entry (which names are story scenes, e.g.
   the cabin's 0x1BC), and the element kinds. [session2] key, probe only.
2. A "story" element class in U-f: user 2's armed manager prepares its own story sequencers, with ITS OWN quest
   manager (own progress per player, the owner's quest design), and the manager armed also near the host (story
   elements only while near; schedule elements keep U-1's distance rule).
3. Then both clients have the trigger. The ready check: hold each player's story 2808 (kind 0x20000003, own character)
   in the host's server until both are inside the volume and both pressed (a prompt on both screens), then let both
   through together.
4. What a story scene brings into the world (its actors: the comrades, a boss) exists once per player then: look at
   duplicates (two sets of comrades) before calling it done.

Risks: the base's guest_seq_play crashed / reloaded the guest on some scenes (the camp liberation scene sent it to the
title); U-1 near the host was never tried; story scenes that spawn the same NPCs for both.
