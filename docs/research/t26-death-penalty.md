# T26: death penalty options in the shared world (design, 2026-11-22)

The roadmap's T26 asks for a death penalty option (none, partial loss, full loss) applied the same way on both sides.
The respawn half is done (respawn_at=checkpoint|partner). The penalty half was blocked on the two-worlds design
(BLOCKED.md T26): each player's world had its own server, and no request that REMOVES silver or an item was found that
the server accepts (AddMinusItemReq got no answer; the cheat item requests have unknown layouts).

## Why the shared world changes it
In the shared world there is one server, in the host's game, and the joined player's character lives in it. The
mod already runs code on that server thread (trade_probe's worker entries: 0xC0DE0005 adds an item to a player's bag
through the game's own add, exe+0x2E449C0 on 3732, for Lootr; 0xC0DE0004 reads the host's warehouse). A removal can
take the same road. And the game itself removes items there: at the Hernand jail wake-up the host's server sends
RemoveItemToInventoryAck (2976 2481) twice, the fine and the confiscated mask (the native base's research,
s25/s45-datos/carcel.md, the 23:50:22 row).

## Two routes, cheapest first
1. The dying player's own game asks, as its Discard does: RemoveItemToInventoryReq (2976 2630; fields [u8, u16, u8]
   by its handler, build/port3479/reqsizes.py) is a per-user request on ServerInventoryActorComponent
   (p8-sistemas-juego.md). The layer already injects requests on its own game's connection (net::sniffer_inject_c2s).
   To find: which inventory / slot / count the fields are (a Discard captured with the packet sniffer gives them),
   and whether the server takes it for silver (a currency may not be discardable). No native code if it works.
2. The host's server removes it directly: the function behind the jail's fine (the caller of the 2481 Ack's writer),
   called on the server thread through a new trade_probe entry (0xC0DE0008: player key, item key, count), checked
   like 0xC0DE0005 (the request must name that player's own character; the count capped).

## The option
[coop] death_penalty = none | silver_10 | silver_all (names to settle): on a player's death in co-op, that player
loses the share of its silver the option names, applied by that player's own game (route 1) or by the host's server
for that player (route 2), never for the other player. Shown on the death screen ("-123 silver") so it is never a
surprise. The game's own death rules stay as they are under none (today's behaviour).

## Safety
- Only the dying player's own silver, only once per death (the death's id: the revive request that follows it).
- Never during a boss's "wait for my friend" (no death yet).
- The count read from the bag right before, capped at what the bag holds; nothing below zero.
- Off by default; the host's setting decides (as the other co-op rules).

## Tests (two copies)
1. A captured Discard of a cheap item by the joined player: the request's fields.
2. Route 1 on silver: the joined player's game sends the request for 1 silver; its bag and the server's Ack.
3. The option on: each player dies once (the checklist's revive step kills the joined player); the right amount goes,
   from the right player, once.
Not started: the release items come first.

## 3732 addresses for route 2 (2026-11-22, static: build/port3732 fmap from the 3576 notes, capstone on the 3732 exe)
The discard handler's plain delete (3576 0x2E036A2..0x2E037B6, build/decomp/notes_give_paths.md 3.2) is in the 3732
handler at 0x2E6F367..0x2E6F47B, the same steps one for one:
- list grow exe+0x505A90 (3576 0x4DB910); entry 0x28 bytes as in the notes (+0x10 u16 inventory index, +0x12 slot,
  +0x18 i64 count, +0x20 u32 0x001B0000, +0x00 / +0x24 u16 0xFFFF, +0x08 i64 -1)
- scope exe+0x2EC97A0(*(tls0 + 0x250), scope) (3576 0x2E5D3B0); scope+8 = refcount (decremented after), +0x10 = tx
- remove by reason exe+0x2A54010(tx, &err, *(holder + 8), &list, time, u8 0, u16 0x2F, u8 1) (3576 0x29EE820)
- time = _time64(0) + (*(u8*)(tls0 + 0x1EB) ? [exe+0x71661A8] : [exe+0x724B5F0]) (the handler, 0x2E6F039..0x2E6F06C)
- the list's free after: exe+0x15972C0, then 0x4B8306C or 0x4B82F6C by *(u8*)(tls0 + 0x1FE)
Silver is item key 1 in the Money inventory (index at the inventory manager +0x78, trade_probe's stash_dump order);
its slot and count come from trade_probe's stash_slot(). The map's checks: the same chain turns the 3576 give and
add into 0x2E5C470 and 0x2E449C0, the 3732 addresses trade_probe already uses. Worker entry: 0xC0DE0009 (0008 is
the AI hand-over's).
