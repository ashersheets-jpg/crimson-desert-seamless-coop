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
- [ ] Do they behave (walk their routes, no flicker / vanish-and-reappear loop)?
- [ ] Can the guest talk to them? (Shops far away are NOT expected to work yet.)
- [ ] Host comes back: no duplicate NPCs, nothing pops.

## Quests (B10, option 3)
- [ ] Guest does something for one of ITS OWN quests (kill / reach a place / talk): its journal advances, the host's does not.
- [ ] Host does the same for its quest: only the host's advances.

## Shared map (B04)
- [ ] Guest on a save that has explored LESS than the host's: open the map (M) after joining: the host's explored area shows.

## Arm wrestling (B14)
- [ ] Hernand inn, the arm-wrestling table: is the mercenary at the table, and does "Try arm wrestling" work for host and guest?

## Already-known open items (no need to test)
- Shops when the host is far away (the shop scene goes away with the host).
- Shared quest credit (option 2) is built but off.
