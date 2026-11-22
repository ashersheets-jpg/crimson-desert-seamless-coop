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
5. cs86b (on that fix, with guest_x_research=1 to log the twin's SetInteraction adds): the joined player talks to
   the organizer, Participates, plays and leaves the menu. Open questions it answers: does the server spawn user 2's
   opponent for the host's scene, take its fee and pay it; does the joined player's game play the intro, the countdown
   and the match from its twin.
