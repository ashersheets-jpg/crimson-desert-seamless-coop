# Crimson Desert Seamless Co-op+ (alpha): features of the shared world

Two-player co-op for Crimson Desert on Steam, game version 1.0.0.3732, in ONE shared world: the host's game runs the
world and the friend joins it with their own character. Unofficial fan mod, built on Crimson Desert Multiplayer by
ByJunter (its NativeServer shared world) and its port by nouzert1. The player's guide is the package's README.txt
(source: `kit/coopplus/README-shared-world.txt`); this page is the short list with what each status rests on.
Updated 2026-11-22 (build 677c87bb).

**How to read the Status column**
- **Works:** checked in the game by the release checklist (two copies of the game on one PC, Nvidia). A report from
  two real PCs still helps.
- **Needs testing:** built, but one PC can't show it (or no test save reaches it). Two real players confirm it.
- **Not yet:** not in this version.

## Joining and sessions

| Feature | Status |
|---|---|
| The Co-op menu stands in for the title screen: Start Solo / Invite Friends / Start Co-op, the party with a crown on the host | Works |
| Invite a Steam friend from the menu, or they use "Join Game" on you in Steam | Needs testing (Steam between two PCs) |
| Continue Our Co-op Save / Start From My Solo Progress / New Co-op Save (both begin the story; the host picks the difficulty) | Works |
| Both games restart into co-op; the friend follows the host and lands beside them | Works |
| A save per friend; your own solo saves are never written in co-op | Works |
| Different Co-op+ builds or game versions refused at the menu, with the reason | Works |
| Direct play by IP (no Steam invite) | Works on one PC (the checklist runs over it); over Radmin VPN / LAN between two PCs: needs testing |
| A second Start Co-op in the same launch; a friend that is not let in is told, the host starts again | Works |
| Join only from the title screen (not during play); two players | By design |

## One shared world

| Feature | Status |
|---|---|
| The same townsfolk and merchants for both, by the same keys (checked at Hernand and Fort Windridge) | Works |
| The friend talks to townsfolk and uses shops with their own money and stock, also far from the host | Works |
| Doors of a building the friend reached first open for both | Works (new in 677c87bb) |
| Shared enemies: both hit and kill the same ones; an enemy fights the player who fights it; ~1.5x enemy health with a friend | Works |
| Items in the world each player can take once (plants, ore, chests, bodies' loot), shares kept at the chest | Works |
| Each player's own quests and story in the shared world | Works |
| Shared quest credit (a step you are both on is done for both) | Option, new: shown working once (cs93), being confirmed |
| Story scenes together: a story scene waits for both, then each game plays it | Experimental option: the first scene of a step works; scenes that start from a talk do not wait yet |
| Time of day and weather follow the host | Works |
| Crimes and "wanted" are the player's own | Works |
| Menus (map, inventory, pause) do not pause the shared world | Works (cs104b: each game's three menus while the other was fighting) |
| No freeze of the host when the friend joins or travels | Works (fixed in the build after 7d0d63b2: the host had stood still 4-15 s) |

## Together

| Feature | Status |
|---|---|
| The friend's name over their head, their health and stamina on screen | Works (the stamina bar is back on 3732 from 3a83292f) |
| Hold Interact on your friend: challenges, giving and trading, Leave | Works (keyboard and pad) |
| Mounts: each sees the other ride (own, borrowed or wild horse, Blackstar); fights from the saddle | Works for the friend's mounted fights (every checklist); the host's mounted fights are its own game's combat (needs testing in play) |
| Downed: Q instant revive, U checkpoint revive, or the friend gets you up within 30 s; wait for the friend at a boss | Works |
| Death penalty (option, off by default): getting up by yourself costs a share of your own silver (10 / 25 / 50 / 100 %), the host's choice; the death screen says the cost | Works (cs97b / cs97c, shared world, 10 %) |
| Give an item, trade stacks, stake on a challenge | Works |
| Shared stash (the host's warehouse) | Needs testing (no test save has a warehouse yet) |
| Chat and quick messages with map pins | Works |
| Group photos (the game's photo mode shows the friend; Co-op+ labels hide) | Works |
| Partner sounds: swings and footsteps heard | Works (voices being checked) |

## Challenges

| Feature | Status |
|---|---|
| Duel on a marked ground (no deaths), rounds and the result the same on both games | Works (cs43b, shared world) |
| Rock-paper-scissors between the two players | Works (cs100, shared world: a 2 - 0 match, the same result and the Victory banner on both games) |
| Hide and seek between the two players | Works (cs102b, shared world: a three-round match, the same rounds and result on both games) |
| Race to the host's map waypoint | Works (cs101c, shared world: both games race to the same point and agree on the winner) |
| Hunting contest, fishing contest | Built and checked in the older two-worlds version; not yet re-checked in the shared world |
| Arm wrestling at an inn's table against the townsfolk, for each player (the fee and the prize their own) | Works (cs103, shared world: a loss, Play Again, a win, Back, then the host's match) |
| Arm / hand wrestling against each other; the game's other minigames against each other | Not yet |

## Not yet / known issues

Mining with the hand drill and fishing as the joined player; the host's armour dye on the friend's screen; a tree the
friend cuts can show twice for the host; taming a wild horse as the friend can leave them stuck; bench sitters may
T-pose on the friend's screen; boss fights with two players are untested; more than two players. The full list is in
README.txt (KNOWN ISSUES).
