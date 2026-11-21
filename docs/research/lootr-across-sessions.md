# Items in the world across sessions (B19's last part): a design (2026-11-21)

Status (2026-11-21 09:25): a first version was written ([session2] lootr_persist, default 0; a text file, one share per
line, `cdmp_lootr_shares.txt`, instead of JSON) and an independent review found faults that could hand out items
twice across sessions: the file was read even when the save hook that rewrites it was not installed; it was written
up to 2 s after the save (shares made after the save could get in) and not retried after a failure; a race could
lose a "given" mark; and one file served every save slot, so loading an older save brought the items back while
their shares stayed. Being reworked (one file per save slot, written in the save itself, read at the host's load of
that slot, nothing restored or written without the save hook); not in a build yet. The live test still needs one
save into the test co-op world, the owner's question below.

v2 (10:00, scratchpad b19/b19_v2.patch: per-slot files written at the commit, a ledger of what the joined player got
and saved, armed only at the host's LoadSlot) was reviewed again: nothing that can crash, deadlock or stall the server
thread, nothing changes with the option off; still four ways to give twice, for v3: (1) ledger ids missed (the 1.5 s
rule for synchronous gives, no retry of a failed ledger write, a later disarm stops the tracking); (2) the stamp's
120 s slack trusts an older file after a failed write (set the old file aside at the first failed write; drop pending
snapshots older than ~60 s; compare against the save files' mtime recorded at the commit); (3) the 4000-mark cap drops
marks long before their shares expire (ledger only ids ever in a file, prune by age only); (4) a 15 s dedup of
LoadSlot can swallow a real reload (dedup on the user-actor / character pair instead).

v3 plan (2026-11-21 12:30, not written yet; v2's patch is the base):
1. Ledger, complete: every give is recorded ON THE SERVER THREAD once its add has run (persist_give and the chest
   shares add synchronously there; the in-session "guest took it too" path records at its server-thread add, not in
   lootr_tick). The joined player's save (SavePlayerToSlot, also the server thread) then marks every give recorded
   before it: no 1.5 s rule. Marks of a save whose ledger write failed stay queued with that save and are written
   again every 2 s until they are on the ledger. Recording and ledger writes need only "armed once in this process"
   (the folder known), never the current arming, so a later failed load does not drop them.
2. Slot file, never older than its save: at a failed write at the commit, the slot's old file is set aside at once
   (renamed .stale, deleted if that fails): that slot then restores nothing. The file's stamp is the newest mtime of
   the slot's own save files measured right after the commit; at load, newer save files than stamp + 2 s mean a
   later save missed its file: nothing restored. A snapshot whose commit comes more than 60 s after it is not written
   (the old file set aside instead).
3. Ledger size: only ids that were ever in a slot file (written or read in this process, or waiting in a pending
   snapshot) are marked; marks are pruned by age only (32 days); a 100 000-mark safety cap.
4. Arming: at the host's LoadSlot of a world slot for its CHARACTER (the 3093 character call, exe+0xE274364, actor
   not a user actor): one arming per world load; the UserLogin and lobby calls (user actors) do not arm. No time dedup.

## What happens today (lootr.h, native)

"Items in the world: each their own" works inside one session:
- the host takes an item first: its pickup runs as always (the save's cool time stays right); what the server tells
  the guest about that gimmick is held back, so the guest still sees it; when the guest takes it, the mod answers like
  the server and adds the same item to the guest's bag on the server thread;
- the guest takes first: what the host's client is told is blanked in place, so the host still sees it;
- chests: one open, each their own content; the other player's share goes into their bag when they come within 6 m
  of the chest (30 minutes);
- bodies: the other player within 30 m gets its own roll.

Across sessions it breaks: the world (the co-op save) records the item as taken whoever took it, so next session it is
gone for both, and the player who had not taken it yet lost their share. Chest and body shares also expire with the
session.

## Identity

A world item is a gimmick; its actor key is NOT stable between runs (actors are numbered in load order, LESSONS
2026-11-21). Its 16-byte SAVE KEY is (SyncFieldGimmickSavedByCoolTimeAck 2985 on 3732: u32 | u32 | 16-byte save key
| u32): the key the world save keeps the cool time under. Shares are keyed by it, plus the item key and count the
taker's bag gained, the gimmick's position, and who the share is for (the host = the co-op world's owner; the guest
= the friend of that co-op world: one co-op world per friend).

## Design: pending shares in a file next to the co-op save, written when the game saves

1. While playing: every share that would be lost at the end of the session (an item one player took and the other
   has not, a chest's other share, a body's) is a PENDING SHARE: {for whom, save key, item key, count, position,
   game time created}. That is today's in-memory state, made explicit.
2. When the host's game SAVES the co-op world (the native base's save redirect writes the co-op folder), the mod
   writes the pending shares as of that moment to `lootr-shares.json` in the same folder (write to a temporary
   name, then rename). Only at a save: a pickup that the save does not hold is undone by the game itself next session
   (the item is back for the taker too, and the taker's bag lost it), so a share written before the save could
   hand out a duplicate.
3. When a co-op session starts on that co-op world, the file is read: each share for a player who is in the world
   waits for them, as a chest share does today: within 6 m of its position, the item goes into their bag (the game's
   own "acquired" notice). No item is shown in the world (the server has none).
4. A share ends when given, or when the world's cool time for that save key ends (the item grew back: both can take
   it again), or after 30 days of play, whichever is first.

The guest's bag is the guest's own save: if the guest quits without saving after a share was given, the game
undoes it on the guest's side, and the share is already gone from the host's file. Acceptable (the same as any item
the guest picks up and does not save), but the line says so.

## Test plan, and the question

Two sessions in a row on the test co-op world: session 1, the host takes an item the guest does not, and the game
SAVES; session 2 (the harness's second round), the guest walks to the item's place and must get its share once.
Our tests never save today (a standing rule). The save would go only into the TEST co-op world (coopworld.sh backs
it up before and puts it back after; the "SAVES OK" check still compares the player's own save folder before and
after). Question for the owner: may a test save into the test co-op world on those terms?
