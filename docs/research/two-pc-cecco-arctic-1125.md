# Two-PC session, Brycecco + Arcticwolf, 25 Nov 2026 (614591d0, game 1.0.0.3732)

The first full two-PC run of the game's start: a New Co-op Save, Arcticwolf hosting, Brycecco joining, ~50 minutes
(13:03-13:53 on their clocks) up to the Graymane camp outside the castle. Their report zips (#play-testing-feedback,
18:59 / 19:00 UTC): each the standard Diagnose files (host cdmp.log 15.0 MB, joiner 4.8 MB). Read 25 Nov 15:03-15:17;
replied 15:17 (message 1575243949344432129). Kept in the session scratchpad (dl-cecco-1125, untrusted, read only).

## What worked

- The title-screen join (New Co-op Save, both fresh), the joined player's intro cutscene, in the shared world at
  13:06:20, 13 m from the host.
- Both long trips of Go to my friend by the game's own travel: 3322 m in 0.6 s, 2677 m in 0.7 s.
- Same-step shared credit through the whole prologue: every step "(ours too)" on the joined player: Head to the
  cabin, Speak with Ross, Speak with Marius, Enter the cabin, Unfamiliar Lands, Annihilate the Black Bears, Help
  Oongka, Help Naira, ..., Into the Fog.
- Rock-paper-scissors (a full match), Lootr (each took their own), two deaths and revives of the joined player.
- No crash, no freeze, no server stall (0 "has not taken our buffers"), no talk refusals (BUG 2 did not show).

## 1. The sky island: the joined player fell through the world (a place of the host's story)

- 13:26:19-13:27:42 the host's cutscenes; 13:27:19 "Travel: we travelled 3325 m": the host's story moved it to an
  Abyss island (the prologue's Abyss tutorial, (-10503, 2007.4, -6178)). The joined player saw no cutscene (its
  game had no story trigger there) and stayed at (-9938, 618, -2901).
- 13:27:54 the joined player's Go to my friend: far travel 3322 m, "took 0.6 s", arrived at (-10503.3, 2007.9,
  -6174.2). The game held the character there 25 s (the height did not change), then 13:28:25 it fell straight down
  (1976, 1738, 1540, ... 444 m: ~30 m/s, x / z unchanged for the first 10 s) while the host stood at 2007.4.
- 13:29:08 death 2 (the landing, ~355 m). The checkpoint revive put it at the server's checkpoint (-10504.0, 2007.3,
  -6180.1), the island: the same fall again (1998 at 13:29:20 ... 353 at 13:30:15), alive this time.
- Go to my friend at 13:29:30, 13:30:12, 13:30:35: "stopped 161 / 40 / 39 m short (hops did not take)": DoTeleportAck
  refuses points far above the ground (the host 1650 m up). Stuck below until 13:35:30, when the host had travelled
  away (the camp) and the far travel there worked.
- Why: the island's levels on the host are SceneLevelShare "[owner native]" (AbyssIsland_0210_Phase00_00 and its
  subs, 36 lines), loaded by the host's own story stage; never "[ready for user 2]". The joined player's game never
  loads them, so it has no ground there.
- Fix (layer, revive_prompt.cpp, the fall guard): after a move of ours arrives, for 120 s: 30 m below the move's
  goal, falling >= 12 m/s over the last second, within 7 m of the goal (a straight drop: not a walk off an edge, a
  dive or a lift), the friend standing up there (within 200 m of the goal, no more than 15 m below it) = no ground in
  our game: the move back to where it started (far travel / hops), a note, and that spot is remembered for 5 minutes:
  moves to within 150 m of it, not more than 60 m below it, are refused with a note (until we stand there 5 s
  ourselves). TEST ONLY [dev] fall_guard_test=1 skips the friend check (one PC: a far travel to a point in the air
  above the host). cs130.
- Not fixed: the joined player onto the island itself. Ways: share the host's story-phase levels near user 2
  (SceneLevelShare for "owner native" levels), or the story scene together (B09) moving both. (Their session also
  shows "Phase00" levels made "[ready for user 2]" when the joined player's own game asked for them with a 2720, e.g.
  DemenissGate_GoldenPlains_0001_Phase00_00, House_Her_0003_Phase00_00: for the island its game never asked, so the
  gate is in the joined player's own story state, not on the server.)
- Next improvement (not in 9e10811c): the guard's 120 s window counts from the arrival; cs130a showed the game holds
  the character in the air until the host says the load is done (8 s there, 25 s for the testers, minutes on a
  stalled host): the window should count from the end of that hold (extend it while the height still equals the
  goal's).

## 2. One NPC hand-over faulted on the host's server (caught, no crash)

- 13:36:41-44 at the camp the joined player did interaction 0xF4263 with NPCs 0xB0101FAD and 0xB0101FAE (2.5 s
  apart). The talk rule handed 0xB0101FAD to the joined player's game (13:36:41.998); for 0xB0101FAE (13:36:44.985,
  the server's logic thread T6344) the hand-over faulted: "exception 0xC0000005 at CrimsonDesert.exe+0x6287580
  executing 0x146287580" (exe+0x6287580 is data in .sxdata), "the hand-over did not go through".
- Where: hand_over() writes the controller (+0x90), runs the two session bumps, then the action reset
  kFnActionReset 0x229D880 (as the game's own handler 0x2F52CD6..0x2F52CF4 does). The reset first takes the AI's
  action object *(*(ai+8)+8) (0x3E5780: lock, vtable[3]), later its inner 0x229D960 reads it AGAIN and calls through
  it (0x229D9DA -> 0x3E4FB0: ref++ then vtable[3] at 0x3E5007): the second read gave a stale object (the first call
  had worked). Likely the other player's interaction step replaced the NPC's action in between (it was mid-interaction).
- Effect before the fix: the SEH returned "false" after the controller was written and the sessions bumped, so
  neither game was told: the host's packets for that NPC were refused (old sessions), the joined player's game did
  not drive it: it stood still until the talk ended (and the 4-byte ref-count increment at the stale object's +8
  happened).
- Fix (native, ai_handoff.cpp): hand_over returns No / Done / Faulted; a Faulted hand-over (the key written) is told
  to both games like a done one, logged with the fault address ("the game's AI restart faulted at ...; both games are
  told the new controller all the same"), counted in the summary. All five rules (far, ride, talk, fight, departure).
  Across all logs: 1 fault in 31 talk hand-overs (this session 7 ok / 1, Borque's 24 Nov 8 / 0, pp55-pp60 16 / 0).

## Their own notes (Brycecco, #play-testing-feedback 20:19 UTC) and what the logs say

- "can still interact with other player when in cutscene, not allowing for fast forward": the hold prompt on the
  friend (render/hud.cpp B16b) had no cutscene check: in a cutscene the friend stands within 3 m near the middle of
  the screen, the prompt came up and, on a pad, held the interact button back from the game (mask_from_game) and
  opened our menu on a hold. Fixed (not tested yet): no prompt and no held button while game::cutscene_playing().
  Note the separate skip vote (D32, coop_activity.cpp): a cutscene both are in skips when BOTH press Skip (Esc, pad
  Start / B; a banner says so) - by design.
- "Npc's t posing": not in the logs; the video should show where.
- "joined client not able to interact (in intro scene) can interact (i.e. pet)": no refusal in the joined player's
  log (no busy / refused lines); the video should show what was tried.
- "Host player does not take any damage from environment or npc. Can be stunned from npc attack": the logs agree.
  The host's AntiGrief tally counts 381 NPC>host blows from the host's own client by 13:28 (the prologue's fights:
  the Black Bears, Myurdin) and its HP stayed 300 / 300 in every 5 s sample 13:03-13:29, while the joined player in
  the same fights dropped to 258, 269, ... and died once. The host's HP did drop later, on the Abyss island (287.9,
  278.6 from 13:29:56). Not DamageRange (field owner out of range 0), not the scaling ("gave back N HP" is the
  enemies' toughness). First guess: the game's own protection of the hero in the prologue's tutorial fights, run by
  the host's story only. To verify: a solo prologue fight (does Kliff lose HP there?) or the video.
- "Enemies on host screen do aggro on joined client, and will periodically attack and do damage": expected (the
  enemies run on the host's game and fight the joined player too).
- 11.25.2 (a second session on the earlier build): "shops not able to have both players at 1 time if host initiates
  the interaction": BUG 2 (fixed in a03a63b9) or the same shopkeeper at once (the game's rule); asked.

## 3. The stuck pickup of Shia's pendant (a03a63b9, Arcticwolf hosting; reported 20:41 UTC)

- Their third session, on a03a63b9 (15:22-15:38): "got stuck in the animation of picking up Shia's pendant and had to
  forceclose the game". The host's log: 15:34:22 the host's story scene 0x352 prepared (bindings: the host,
  0xB01039A5, 0xB01039A4, 0x20000008 -> 0xB020093B = the pendant; ObserverSeqCopy sent the copy to user 2, whose game
  put it in its own manager); 15:34:23 the level 'Imp_Banquet_Boss_Battle_Gimmick' loaded by the host's stage ("owner
  native"); 15:34:35.9 branch 1 and 15:34:36.5 that level deleted by the host's own client (a 2834); 15:35:05.4 branch
  2; 15:35:06.8-15:35:33+ the host's 2642 pickup (interaction 0xF4244) of 0xB020093B refused 23 times (2946 FailAck).
- The refusal code 0x7B1E861C = **eErrNoGimmickNotInteractableState** (global 0x71D7024; named by the error-globals
  scan in 9e10811c's first host log, pp61, mapped with errtable73.csv). The joined player's interaction 0x275F with
  0xB010312B was refused with the same code at 15:33:11-15:33:20.
- 0x7B1E861C appears in no earlier tester log (614591d0 and before): only in this a03a63b9 session. a03a63b9 turned
  the per-player focus slot on (+ the C0 talk window): its counters show nothing wrong (21 talks, 42 swaps, failed 0,
  unbalanced 0, unwound 0: no window was ever left open), no user-2 request reached the logic thread in 15:34-15:35,
  and the seq-copy filter saw no user-2 request about the host's scenes. Not explained yet. Next: the server code that
  returns that error for a pickup (scratchpad xref73.py 0x71D7024) and what gimmick state it checks; whether the
  deleted level held the pendant's gimmick data.

## 4. Their fourth session's notes (Brycecco 23:10 UTC, with both zips; analysis of the zips to follow)

- **Duel hits without a stagger** ("hits do not register stagger or a hit visually but health is affected"): two
  causes in the layer, both on the native base only (read from the code, 25 Nov evening):
  - the duel's hit reactions ([coop] player_hit_react, on by default) are switched on by `hits_set_react` only on the
    old base's path of mod.cpp's late init; the native branch returns before it, so `g_react` stayed false and no
    duel hit was ever given a reaction there (our pp62 guest log has no "reacts to" line at all);
  - a duel hit carries the stagger its attacker last took from an enemy (`hits_make_pvp`), learnt from the server's
    batch for a blow on us (AttackTargetAck + ApplyActionRestrictionAck + hit buff), but each blow is keyed by the
    client frame event that named it (`pending_key_locked`), and the native base hands the layer no client-to-server
    frame event (coopplus_observe feeds only activity / duel / weather; the enemy's attack is the host client's
    anyway): nothing was ever learnt. And the receiver named our old puppet key (0xA010F001) as the source, which is
    no actor in the shared world.
  - Fix (scratchpad duel_stagger_fix.py, test build/native/cs131.sh): the switch on the native path; a blow on us
    with no frame event is kept under one key and the latest complete staggering batch is the duel's sample (with the
    enemy that gave it, renamed as the attacker in the hit: in the shared world that enemy is an actor in both games
    and the receiver refuses a template naming one); the receiver names the friend's real character
    (`partner_actor_key`) and uses its own sample when the attacker had none. Still no stagger when neither player
    was ever hit by an enemy in the session.
- **Townsfolk and shopkeepers gone for the joined player** while the host was in the Abyss, or 100-150 m away
  ("once host was within 50 meters npcs respawned en masse"): the joined player's own townsfolk (U-1,
  user2_stages=1) arm at >= 4 cells (64 m, Chebyshev) for 3 s and drain at <= 2 cells for 5 s, and only while both
  players are in the same field; with the fields different an armed U-1 drains at once (user2_stages.cpp control():
  "another field ... never wait"). So with the host in another field (the Abyss, if it is one) the joined player has
  neither its own stages nor the host's. In their earlier sessions U-1 armed and drained as designed (1125 host log
  13:48:07 ARMED at 4 cells, 13:48:44 DRAINED at 2; pendant-host 15:26). The 2-3 cell band is the host's stages only
  (they are made 1-3 cells from the host, mundo-escenarios-propios.md §3.2), so at 100-150 m the joined player sees
  part of them at best. To check in the zips: the fields at the Abyss, the [state] lines' distance at the time.
- **Why U-1 never armed while the host was on the Abyss island** (first session, 13:27:19-13:35, host 51 cells away,
  same field, arm check READY, "U off (would be OFF)" for 8 minutes): U-1's dwell counts only while user 2 is "in
  the world" (server_user2_in_world: its move phase STABLE), and the phase was MOVE_ASKED from 13:22:45 to 13:28:2x
  and again later: the joined player's checkpoint revive (the layer's U: ReviveLoadingStartReq 2972, ReviveLoadingReq
  2887, GameLoadingStartReq 2350 + ReviveReq, all sent by the layer) is answered by the server with a reload in the
  same field (LoadingFieldDataAck 2286 + 3066), and the client sends nothing more (its load machine stays 0x19 in the
  joiner's log; "trip R2 ends: reload, but user 2 sent no 2873"), so no LoadingCompleteReq ever ends the move. The
  world anchor, the clock push and the weather stream wait for the same phase. Our checklist revives the joined
  player with Q (in place, no load), never with U. Fix (native, scratchpad native_revive_phase_fix.py): a revive's
  reload in the same field is STABLE at its LoadingFieldDataAck (unit tests); a checklist step u2revive (U, then the
  host 430 m away: U-1 must arm).
- **The schedule stage's fields are read 8 bytes short since 1.0.0.3663** (user2_stages_logic.h kSt_*): the [close]
  lines read the stage's position as (0, z = the real x) ("stage at (0, -11413)" for the stage [create]d at (-11413,
  -6800)) and its owner as 0x22F78310 ("other"); every [state] line has the host's stages "162..162 cells from the
  host" (all at cell (0, 0); the same on 3663, cs27e: 141..141). So the position is at +0xB4 now (+8), and the owner
  (+0x50), data (+0x60) and NPC entries (+0x138 / +0x140) are likely +8 too (the key at +0x30 still reads right).
  What reads them: U-f2 (`stage_at`: no schedule stage was ever skipped as alive in the other manager, 0/0 in every
  log: the duplicate guard has been blind), U-e's `npc_exact_stage` (data + entries: no exact identity, so no write),
  the S33 node veto (owner: never user 2's), the [close] / [state] diagnostics. Duplicate townsfolk where both
  managers' windows meet, and user 2's own stages without their NPC control, fit "NPCs respawned en masse" and the
  T-posed "?" shopkeepers. To verify against the 3732 code (the stage ctor's 40-byte transform copy, the entry
  vector) before changing them.
- **Their fourth session's zips (a03a63b9 both; host 17:16-18:08):**
  - The Abyss: from ~17:2x the host stood at (-10675, 1795, -3690) and on (the island ~1180 m straight above the
    Hernand market, y ~612); its manager ran 0-2 schedule stages up there, and the joined player in the town below
    was 0-2 cells away (x / z only), so U-1 never armed: nobody made the town's people. When the host came down they
    "respawned en masse". Fix (native_u1_fixes.py): 300 m or more apart in height counts as far (16 cells).
  - U-1 armed and drained as designed three times (17:36:42-17:41:34, 17:42:19-17:43:25, 17:50:18-17:51:29, at 4
    cells / back at 2 or 0).
  - Duel: every "the other player hit us ... -N hp" without ", reaction" (g_react off), as read from the code.
  - The grindstone: the joined player's interaction 0x2BA1 on 0xB04022F6 (18:08:20) was accepted (preemption and do
    step SuccessAcks to user 2), and the next stage branch (sequencer 0xB82) went to the HOST's character: the
    interaction's little scene runs as the field owner's, the same class as the shops before ShopTwin. Not fixed.
  - Windows: no refused interaction of either player in the log; asked the testers what they mean.
- **T-posed "?" shopkeepers nobody can use**, the **joined player's grindstone** (prompt, hold, nothing), the
  **portal chest** left open after the host left (the joined player cannot trigger the portal), **windows neither
  player can open**: to read in the zips.
- **The second player's pickup of a ground item plays no animation** (the item is taken and the inventory is right,
  "each their own" loot): cosmetic.

## Smaller things seen

- The host's crash log has first-chance AVs at cdmp.asi+0x959DB / +0x959E3 at startup: the guarded reads (our pp60
  logs have the same at +0x95E9B / +0x95EA3). Benign.
- Borque's 24 Nov host coopplus.prev2.log (an older run, 12:49): "exception at 0x0 executing 0x0" in coopplus.asi
  with steamclient64 on the stack, 3 s after start, caught. Not seen since; noted.
