# T23 doors in the shared world (2026-11-21, game 1.0.0.3732)

What the native base found on 2976 (ByJunter's S42 / S43, upstream/cdmp-native/docs/research/s25/, Spanish), and what
we run on 3732.

## The mechanism
- A door of a building is not a toggled object: near it, a client asks the server to spawn the door's scene on demand
  (2976 Req 2928, 3732 SceneSpawnReq 2772; the server answers Ack 2145 or Nak 2953 on 2976). The client asks by
  itself while its player is near (S42's capture: the joined player sent 144 requests, all refused; the host 1,
  accepted). So "the door does not open" for a player = its requests are refused.
- The server answers from its registry of SceneLevels, which is per FIELD ([ServerField+0x80]); each entry has a
  "ready" byte (+0x40). Only one function sets it ready, and it has an OWNER GATE: only the field owner's load (the
  host's) passes (2976 0x283044D, `cmp [r8+0x180],eax`).
- The ready setter runs only right after a level's FIRST registration (its SceneLevelLoadCompleteSyncSceneObjectListReq,
  2976 2598 / 3732 2720, or the Resync 2104 / 2623). If the JOINED player's game reports the level first, the entry is
  registered NOT ready, and the host's later report is dropped as "already registered": every door request of that
  level, from either player, gets "resource not ready" (0x6CF701C) until the entry goes (a fast travel unloads it).
- The Hernand tavern door (SceneLevel 'sector_-42_-15', hash 0xDE7B52AA, its scene UUID 4fb9eeab..., at
  (-10668.8, 617.1, -3838.3), ~9 m from the inn cellar's arm-wrestling table) was S42's case; ByJunter's tester
  confirmed "if the guest loads the zone, the doors do not open until a fast travel".
- Second fault behind the first: the delete of a SceneLevel works by name with no reference count; any client's
  SceneLevelUnloadReq (2976 2834 / 3732 2948), the owner's own load (DL != 0) and the field loader delete a level the
  other player still has.

## The fix: [session2] scene_level_share = 0 | probe | 1 (host only, default 0)
- A: the owner gate also passes for user 2's load (a mid hook on the gate's cmp): "ready for user 2".
- B: a SceneLevelUnloadReq's delete is skipped while the OTHER player still has the level loaded (per-user loaded sets
  from both clients' load / unload / resync packets; flushed at a reload, revive, game loading or logout). The owner's
  own load and the loader's deletes are only observed (S43's known gap: 173 such deletes in its run).
- S43 live (2976): with it on, "[ready for user 2] sector_-42_-15", the door opened for the joined player at once.
- Ported to 3732 (review_native.txt lines 58-65: every RVA mapped 3663 -> 3732; signatures checked at install), OFF in
  our builds, never run on 3732. door_trace's native-result hook is 2976-only (refused on 3732); its packet trace works.

## Tests (chain_h)
- cs88a: scene_level_share=probe: the joined player goes to the door first (the host 300 m east), then the host comes;
  door_trace on both. Shows the order ("[would ready] ... 0xDE7B52AA ... actor 0xA0100066") and each player's
  Req / Ack / Nak counts.
- cs88b: the same with scene_level_share=1.
If cs88a refuses the door for both and cs88b opens it: scene_level_share=1 on the host by default (scratchpad
sls_default.py), the unit test refreshed for 3732 (tests/optd_scene_level_share_tests.cpp), and a checklist step.
