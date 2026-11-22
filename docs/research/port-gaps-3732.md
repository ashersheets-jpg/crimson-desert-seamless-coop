# Constants the ports left at an older build (audit, 2026-11-21)

A read-only audit (Read / Grep only) of `upstream/cdmp-native/src/mod/` for game addresses still at 2976's or 3576's
values on 3732, after the three found while working on B19 (guest_save_logic.h's LoadSlot returns). Log evidence
is from cs86f and pp42-shop (3732). Plan per item at the end.

| # | where | constants | used for | if stale on 3732 |
|---|---|---|---|---|
| 1 | session2/guest_save_logic.h | kRetUserLogin, kRetLobby3093, kRetCharacter3093, kRetAutosave, kRetManual2910, kRetFlushLogout, kRetFlushTimer (all 2976's) | LoadSlot / SavePlayerToSlot / Flush caller by return address | every caller reads "OTHER"; an unidentified actor at the real UserLogin is refused; manifests and the host-save journal say "OTHER (ret ?)". guest_save_host.cpp kCallSites already holds 3732's call sites (return = site + 5). |
| 2 | session2/user2_funnels.cpp (timer_vf48_ret) | 3 return addresses (3576's) | U-b: user 2's 2043/2045/2046 timer vf48 to its own manager | live (user2_timer_route=1) but never matches: falls back to user2_stages_timer_vf48 (own manager only inside its armed tick) |
| 3 | session2/user2_funnels_logic.h, user2_funnels.cpp | policy_caller Vf40Ret set, vf40_caller_allowed, kPolicyVtableCallRets (2976's) | quest_policy / quest_per_user caller match | off; a silent no-op if turned on (install logs "N call site(s) NOT as expected") |
| 4 | session2/mount_sync_logic.h | kVf178Actor 0x4FE260, kVf178User 0x8A67D0 (2976's) | mount_seat's "local user" judge | guest self-test: "vf178 exe+0x5DF500 ... NOT PROVEN (the reads disagree with 0x4FE260)": every rider counts as local, so seats are refused unless the 0xA45AC0 action-request veto is hooked (it is in cs86f) |
| 5 | session2/user2_stages.cpp | D 0x2808D80, S 0x2822D00, F 0x27FBD60, H 0x2881A19 / 0x2881A04 (2976's, byte-checked) | option U hooks | host: "D/S/F ... REFUSED, not hooked", "H 0x2881A19 REFUSED": a silent no-op |
| 6 | game/game_errors.cpp | kErrorLogger 0x35D8D0, kActorCreateFailedFrom 0x8BDF7C (2976's) | the game-error hook; the guest's child_reannounce failure feed | "Game errors: logger not recognised; not hooked" (no-op). The check is ONE byte (E9): a crash risk on a build where that byte happens to match |
| 7 | session2/guest_seq_route.cpp | the stage teardown guard 0x43B0B6 (+ context), kP2524Lookup 0xA9E55B (2976's, byte-checked) | a use-after-free guard; the 2524 inner check | "2524 scene check (inner hook not installed)"; guest_sched_route is off. A crash risk if it is turned on: its ported points install, the guard does not |
| 8 | game/offsets.h | kVtCamera 0x5D20718 (2976's; kCameraSlot was ported) | camera vtable check | the HUD camera always "no camera" (overlay off) |
| 9 | session2/user2_shops_logic.h | kProtectedBegin / End 0x7ACB000 / 0x137DB000 (3576's range; 3732's image is 0x13C7E000) | protected-section match for the 2200 / 3069 / 2048 / 2059 vf48 sites | wrong only if a 3732 return falls outside the range (not checked at install) |
| 10 | session2/guest_s3_logic.h, guest_s3.cpp | start_caller ranges, kVtClientHierarchy 0x5578BB8 (2976's) | GuestS3 counters; crash-capture register naming | mislabelled diagnostics |

Diagnostic-only, all off in these runs (2976's): host_scene_diag.cpp and scene_points.h (its g2_pass reads
scene_diag_logic.h's kManagerSlot 0x6C8CDA0 every pass even though its hooks are refused), guest_actor_diag.cpp
(FinishGroup points, error globals), identity_probe.cpp's extra points (kRestoreVersion is read for the log),
trade_probe.cpp's error-constant scan range (misses 3732's 0x71B8xxx-0x71D6xxx), host_save_logic.h (every host-save
manifest records pe_timestamp "0x6AB28F00"). 2976-only and refused cleanly by their byte checks: owner_fanout (with
owner_fanout_writers.h), observer_scene_copy, guest_sched_bind, companion_probe's extra points, enemy_damage_probe,
aggro_test, local_capture's equip / docking writers. Off by their own build check (kPeTimestampUnported, no log line
when off): guest_seq_play / present / foreign, guest_stage_diag, guest_bodyanim_diag, guest_scene_diag, p8_probes,
shop_trade_probe, door_trace, catch_seq, docked_gimmicks, remote_rope, rope_probe, tree_bend, guest_sched_load's
second installer. Already fine: the four non-address values the port had rewritten (kCatchInteractions,
kShopDialogGroup, trade_probe's 0xF4321, guest_s3's mod size) are back; anti_grief kCrimeRetHit and guest_save_host
kCallSites were fixed by hand. Many log strings still print 2976 addresses (LoadSlot 0x186F570, GuestSeqRoute
0xC0612A, MountSync 0x2228260, WeatherSync, UiPause 0x1416B80, GuestFastTravel 0x655110): cosmetic, misleading.

Refused / not installed in the 3732 logs: host NpcProbe ("build distinta de 2976"), Game errors, User2Stages D/S/F/H;
guest GuestXVeto ("unsupported game build"), Game errors, GuestSeqRoute's 2524 inner check, MountSync self-test NOT
PROVEN (GuestXInter's evaluator test not hooked in pp42-shop only).

## Checked after the audit (2026-11-22 ~00:00, read only)

- Item 2 is NOT stale: tools/native-port/pins73.py pinned the three returns to 3732's (0xE57536A / 0xE57791F /
  0xE5780FF, found by the 12-16 bytes before them); only the code comment still says 3576. pp43-shop's host log:
  "timer route (user2_timer_route=1): 2043/2045/2046 to its own manager 28/38/38".
- Item 9 is NOT stale: 3732's protection section (".text1") is 0x7ACB000..0x137DB000, the same range
  (docs/research/port-3732.md's table).
- Item 5 is diagnostic: D (DeleteStage), S (the 2045 schedule update), F (the 2046 faction update) and H (the cell
  evaluation) only count and log (on_delete, on_sched, on_faction, on_eval); option U arms and works without them
  (pp43-shop: "User2Stages [ARMED] ... the gate (U-a) is live").
- Item 4 is real: the 3732 actor's vf178 is exe+0x5DF500 (the self-test reads it, and its other two reads agree:
  +0xA0 IS the local user, +0x94 1). To port: disassemble 0x5DF500 to confirm it is 2976's 0x4FE260 (`[+0xA0] ==
  local user && [+0x94]`), and find the server user actor's vf178 (2976 0x8A67D0) from its RTTI vtable.

## The Co-op+ layer (second audit, 2026-11-22 ~00:43, read only)
Nothing the layer runs in native mode uses a constant left at an older build's value (XP, revive, the main menu, HUD /
camera, the player reader, photo mode, scaling, stash / give all show working in pp43's logs). Left at older values,
all dormant: the two-worlds puppet's rope lookup (remote_puppet.cpp: a 3446-era world global and ScopeAttacherBase
vtable; 3732's vtable is 0x5B45300), its tree-bend sites, the relayed-Ack sizes (3479's), the default-load filter's
kLoadPositionFromEnd (3460), the unused kClientClock (2976). Visible but minor: the stamina stat id (3424's 22) reads
0 on 3732 ("stamina 0.0/0.0" in every Local line), so the party frame's stamina bar stays hidden (the README promises
health only). Cosmetic: two log labels name 3576's numbers (revive_prompt "3071", dev_script "2491"). Weak install
checks (main_menu's kSeqDataApply / kMenuUpdate one byte, a few unchecked calls) matter only if a port maps a value
wrongly: the whole layer stays off on any game build it was not made for (mod.cpp's PE timestamp gate).

## Plan

1. Item 1: fixed for the next batch (scratchpad port_gap_rets.py: the seven returns = kCallSites + 5, static_asserts
   tie them, the callers test updated; the same text in the B19 worktree). 3732 logs agree: logins from exe+0x2F28800
   / 0x2F4F576 / 0xE274364, the host's saves from exe+0xB6A4AA5 and the flush after them from exe+0xE584ACD
   (pp40-newsave). User 2 is identified at the real UserLogin ("= user 2 | slot 103 -> REMAP 204", pp43-fresh), so
   the fix changes the decision only for a truly unidentified actor (passed at UserLogin, as designed).
2. Items 6 and 7 (crash risks on a later build or with an option on): fail closed. game_errors installs only on the
   build its addresses are for; guest_sched_route does not install when its teardown guard cannot.
3. Item 9: nothing to do (see above).
4. Item 4: port (static analysis of the 3732 exe after the pass); mount seats depend on the veto while the judge is
   unproven. Item 2: only its comment. Item 5: when its diagnostics are next needed.
5. Items 3, 8, 10 and the diagnostic-only list: when their option is next turned on; the log strings when touched.
