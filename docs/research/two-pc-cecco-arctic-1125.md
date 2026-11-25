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
  (SceneLevelShare for "owner native" levels), or the story scene together (B09) moving both.

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

## Smaller things seen

- The host's crash log has first-chance AVs at cdmp.asi+0x959DB / +0x959E3 at startup: the guarded reads (our pp60
  logs have the same at +0x95E9B / +0x95EA3). Benign.
- Borque's 24 Nov host coopplus.prev2.log (an older run, 12:49): "exception at 0x0 executing 0x0" in coopplus.asi
  with steamclient64 on the stack, 3 s after start, caught. Not seen since; noted.
