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

## cs4: the story scenes by name (native [session2] stage_names_probe=1, read only)

- At the host's world entry (18:16:06.7, one burst, the manager already known as the host's) its manager evaluated: sequencer
  elements `cd_seq_quest_intro_0300`, `cd_seq_quest_intro_trigger`, `bgm_cd_seq_quest_intro_0300`,
  `cd_seq_quest_intro_0310_01 .. _15`, `cd_seq_quest_intro_0300_01_dlc`, `_0340_dlc`, `_0340_01_dlc`, `_0360_dlc`,
  `_0360_01_dlc`, the weather scenes `cd_seq_contents_weather_*`, and many schedule / class2 elements (the 400-line cap
  was spent on class2).
- So the story class for U-f is the base name prefix `cd_seq_quest_` (and `bgm_cd_seq_quest_`), as the merchants' is
  `cd_seq_funcnpc_`. `cd_seq_quest_intro_trigger` is the likely owner of the cabin's trigger volume (sequencer 0x1BC).

## cs5a - cs5o (2026-11-13 / 14): the joined player's own story steps on its own quest

What it took (all native, off by default, keys in [session2]):
1. `user2_stages_quest=1`: U-f lets user 2's armed manager prepare `cd_seq_quest_*` (its own story scenes and their trigger volumes).
2. The port fix (shipped in 66985463): the stage manager's scene table is +8 on 3663, without it no lookup found user 2's scenes.
3. The shop route's safety latch accepts story scenes (it aborted at the first non-shop scene).
4. `user2_stages_own_quest=1`: Q1/Q2 build user 2's stages on its own ServerQuestManager; Q3 0x226F3CA: a stage event of user 2's manager takes its own quest manager; nested vf48 of user 2's routed scene Trocs answer its manager; ServerField::vf40 inside any Troc of user 2's own scene (the 2059 deferred delete included) answers its own quest manager.
Result (cs5o): at its own trigger every Ack names user 2's character and its screen shows the next objective. Open: the scene itself on its screen, the cutscene flag, U-1 arming only when far (the players near each other is the normal case), the ready check, duplicate story NPCs.

## The ready check: the plan (2026-11-14, after cs6a)

With story mode on, each player's own story trigger works, near or far. The gate:
1. Recognise a story trigger: a SequencerTriggerVolumeInOutReq (2808) of kind 0x20000003 naming the sender's own character, for a sequencer whose resource is a story scene (cd_seq_quest_*), followed by its StageBranchStateReq (2644).
2. Hold it: user 2's on the host's server (its Reqs pass the native ingress), the host's own in the host's client before it is sent (the layer / sniffer).
3. Both screens: "<friend> is not here yet: wait / go to them / continue alone" (the defaults to propose: wait with a prompt; the waiting player may continue alone; the other gets "travel to your friend").
4. When both are inside their own trigger (or one chose to continue): release both held Reqs in the same moment.
Open questions for the owner, with proposed defaults: what if the friend is far (proposal: a prompt, never a silent block); a time limit (proposal: none, the prompt stays); story NPCs of the scene twice (each player's scene spawns its own comrades: to look at with both triggering).
