# Hand-test checklist (build of 2026-11-09 afternoon)

Two copies on this PC, joined from the title screen with "Start Co-op" (the release flow), the user's save copy.
Each line: what to do, what should happen. Note anything else odd (FPS too, for later).

## Merchants (B02b)
- [ ] Guest at Renee (Hernand butcher), hold R: "Buy / Sell / Leave"; buy something, sell something. Money changes on the guest only.
- [ ] Guest at Delkin (Hernand grocer): same (worked in tcB194).
- [ ] Guest at the smithy, the general store, the equipment shop, the inn: the shop opens, buy works.
- [ ] Host and guest at the same merchant at the same time: both can shop.

## Far from the host (B02)
- [ ] Host rides / fast-travels 400 m or more away (e.g. to Rodbel Fishing Dock). Guest stays in Hernand market.
- [ ] After ~10 s the guest's town has townsfolk walking around (before today it was empty).
- [ ] Do they behave (walk their routes, no flicker / vanish-and-reappear loop)? (Known: some may vanish and come back in the first seconds -- the NPC loop guard stops it after ~10 s; note how it looks.)
- [ ] Can the guest talk to them? (Shops far away work in my test with extra settings that are not on by default yet.)
- [ ] Host comes back: no duplicate NPCs, nothing pops.

## Quests (B10, option 3)
- [ ] Guest does something for one of ITS OWN quests (kill / reach a place / talk): its journal advances, the host's does not.
- [ ] Host does the same for its quest: only the host's advances.

## Shared map (B04)
- [ ] Guest on a save that has explored LESS than the host's: open the map (M) after joining: the host's explored area shows.

## Arm wrestling (B14)
- [ ] Hernand inn, the arm-wrestling table: is the mercenary at the table, and does "Try arm wrestling" work for host and guest?

## Player interaction menu (B16b, your request)
- [ ] Walk up to the other player (within 3 m, look at them): "R  Interact" with a ring appears; hold R: the ring fills and the menu opens (the other player's name, every challenge, Trade (coming soon), Leave).
- [ ] On a controller: the same prompt shows the co-op buttons (View / Touchpad + D-pad Up) next to the ring; hold them: the ring fills and the menu opens (a quick press must not open it at once).
- [ ] W / S move, Space picks a challenge (the other player gets the invite), Esc leaves. Does it look and feel like the shop menu?

## Duel (from the menu)
- [ ] Pick Duel; the other player accepts; hit each other: does health go down on BOTH sides, and does the round end at the floor (no real death)?

## Revive (B17)
- [ ] One player goes down; the other walks over and HOLDS E (or Shift+F7): "Reviving X... %" then they stand up where they fell. Q / U still work for the downed player.

## Anti-grief (B19)
- [ ] As the GUEST, attack a townsperson or a merchant (e.g. Renee): no damage, no crime / "Wanted" for anyone. Enemies and animals still take damage normally.

## For trading (B11) -- one action that teaches the mod the game's item-move message
- [ ] If your save has another playable party character: in the inventory, move ONE cheap item from Kliff to that character (and back). Nothing else needed: the mod's log captures it.

## Things the shared world should give for free (just look / listen)
- [ ] B20 partner audio: you hear the other player's footsteps, swings and hits, quieter with distance.
- [ ] B18 combat feedback: the other player's hits show their effects (stagger, knockback, status) on enemies you see.
- [ ] B06 mounted combat: the guest on horseback hits an enemy: it takes damage and reacts.
- [ ] B05 other mounts: if either save has another mount (bear, wolf, wagon...), the other player sees it and the rider on it.
- [ ] B09 cutscenes: when the host triggers a story cutscene near the guest, what does the guest see? (Note if its camera / controls get taken over.)
- [ ] B03 world events: join a world event or siege together: both can fight in it and get credit.

## FPS (for the pre-release pass)
- [ ] Note the FPS (top-left counter) in the town with both players, at your normal resolution. My two-copy runs today with the release settings read ~95-150 FPS in the world per copy (windowed, DLSS); if yours drops to 20-50 again, note where and what was happening.

## Already-known open items (no need to test)
- Shops when the host is far away (the shop scene goes away with the host).
- Shared quest credit (option 2) is built but off.
