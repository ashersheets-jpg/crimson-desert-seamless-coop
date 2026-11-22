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

v3 written (2026-11-21 13:45, worktree upstream/cdmp-native-wip branch wip-b19-v3: 281e103) and reviewed again (14:05): the four v2 findings fixed, four new ways to give twice found and fixed in v3.1 (c22bc27): an in-session give the worker does not confirm in 60 s counts as given; a failed ledger write sets aside the slot files that list those ids; a commit without its snapshot sets the old file aside (and drops an older snapshot still waiting); no save files found = restore nothing, 60 s slack (the stamp is the commit hook's clock); host gives of this process never come back at a re-arm. Not built yet; the live test still needs the owner's answer.

v3.1 reviewed (2026-11-21 19:20, read only): B1 closed; no new way to give twice opened, nothing changes with the option off, no race; but the design's commit point is wrong: per the native base's save research (5-guardado-anfitrion.md §3) the game writes the slot inside SavePlayerToSlot, and FlushPendingSaveData names a slot (+0x5C) only for autosaves with options&4 (progreso-invitado.md §2.1): MANUAL saves (slots 100-108) never get a share file, so loading one restores nothing; an autosave without &4 gets no commit and leaves an older file in place. Also left: a failed ledger write sets aside only this process's known files (other slots of the earlier session still list the id); set_aside does not check the file is really gone; a snapshot taken before a give can be written after a failed ledger write; the host's phase-2b copy is not in g_given_host; a host reload loses shares given in this process; a failed ledger write drops whole files. v4 (parked until the owner's save-test answer): write the slot file when SavePlayerToSlot returns 0 (its I/O is done there), drop the flush hook; on a failed ledger write rewrite every slot*.txt without the due ids (verify, else set aside); filter due ids from every write; confirm a set-aside; the host's 2b copy recorded; g_given_host only for shares whose slot file was written after the give.

v4 + v4.1 (2026-11-21 late, worktree upstream/cdmp-native-wip, branch wip-b19-v3, uncommitted, NOT built; the live
test still needs the owner's save-test answer):
- the commit point: a slot's file is written when the game's SavePlayerToSlot of that slot returns 0 (the game writes
  the save's own files inside that call: S34 research section 3); FlushPendingSaveData no longer does anything here;
- the stamp (v4.1, review of v4 item 1) is the slot's newest save-file time in FILETIME ticks, measured right after
  the save; at a load the slot's files must show EXACTLY that (newer: a later save lost its file; older: files put back;
  none: nothing to compare): otherwise nothing is restored. A save that did not move the files keeps the old file; one
  whose files went back sets it aside;
- a failed ledger write rewrites EVERY slot*.txt in the folder without its marks (read back), or sets the file aside;
  marks still due are never filed again; the ledger is written durably (flush + write-through) with a second copy
  (given_guest.bak), and both are read and joined (v4.1, item 6);
- set-aside is confirmed: renamed, else deleted, else overwritten in place through a handle that shares everything,
  else said once for that slot (v4.1, item 4);
- the host's own gives are no longer subtracted at a load (the host's bag is in the slot it loads; v3.1 lost them
  after a reload; the v4 review checked this reasoning and agreed);
- a host load clears the old world's shares before anything can fail, and the waiting shares are given only while
  armed (v4.1, item 2); a share leaves the waiting list before its adds (a save inside an add cannot list it);
- arming only at the host character's own LoadSlot (its 3732 return exe+0xE274364; kRetCharacter3093 / kRetLobby3093
  were still 3576's, so every load logged "OTHER") (v4.1, item 5);
- every persistence entry holds one recursive lock before g_mu (v4.1, item 3: the review asked whether LoadSlot, the
  saves and Lootr's 2 s entry share a thread; cs83w2 / cs86f logs show them all on the "server realm" thread, the lock
  is insurance).
Left from the v4 review (losing, never giving twice): the host's phase-2b copy between its queued add and the
worker's run, and a partial give (marks per share, not per kind). kRetUserLogin is still 3576's (it changes
decide_load_on for an Unidentified actor: its own check). Next: build the worktree, its unit tests, a read-only review
of v4.1, then the live test when the owner answers.

v4.1 reviewed (2026-11-21 ~23:50, read only): compiles as read, no crash or stall, nothing changes with the option off;
items 2, 3, 5 correct, 4 and 6 correct with notes; three ways left to give twice, each needing a condition not ruled
out: (1) the narrowed arming had no disarm: a host character load from another caller kept the old world's shares
armed in the new one; (2) a save that returns 0 without rewriting the slot could stamp a snapshot with files it does
not belong to (no valid file, or an older stamp); (3) the two ledger copies were joined even when one could not be
read now and the other lagged (its write had failed, only logged). Edge cases: two Steam accounts hosting the same
friend share the co-op folder; FAT32 / exFAT times are too coarse. v4.2 (same worktree, uncommitted, NOT built):
- any host character load (any caller) disarms and drops the old world's shares BEFORE the game's call
  (lootr_host_loading); only the character's 3093 call restores and arms (item 1);
- the slot's save-file time is measured before AND after the game's SavePlayerToSlot: written only when the files
  moved; when they did not, the old file stays only if its stamp is exactly their time; anything else sets it aside
  (lpl::after_save, unit-tested; item 2);
- a ledger copy that cannot be read now means nothing is trusted (lpl::ledger_trusted, unit-tested); a .bak whose write
  failed is deleted or cleared, else the marks stay due (item 3);
- more than one account folder with files in the slot = no file is that save's; persistence stays off on a drive that
  is not NTFS / ReFS (item 4);
- the lock is timed: a game hook's entry waits at most 2 s, then takes its safe path (a missed joined-player save writes
  its marks at Lootr's next 2 s entry); Lootr's entry skips a round instead of waiting (the review's hang caveat);
- a chest share being given is not listed by a save inside its add; the durable ledger write flushes the renamed file
  (MOVEFILE_WRITE_THROUGH does nothing for a rename on one volume); marks written on a retry are counted; dead code gone;
  the stale and ARMED lines say what happened; the logic header's comments describe FILETIME stamps.
kRetUserLogin: the 3732 port audit (same night) found all seven caller returns of guest_save_logic.h still 2976's; the
fix (scratchpad port_gap_rets.py, kCallSites + 5 tied by static_asserts) goes to the main tree and this worktree alike.

v4.2 reviewed (2026-11-22 ~00:10, read only): the four findings closed, nothing changes with the option off, no new
crash or deadlock path; seven smaller findings. v4.3 (same worktree, not built):
- a joined player's save that missed the lock now also requests Lootr's 2 s entry (else its catch-up never ran when
  nothing else waited: a share could be given twice) (finding 1);
- a give to the joined player is recorded BEFORE its adds (persist_give and the chest shares), and what its bag did not
  take comes back under a new id (finding 2);
- a world generation counter: a loop with adds stops after them when a load inside an add cleared the lists
  (finding 3, theoretical: it would have written g_shares[-1]);
- an unidentified owner's character load disarms too; only the armed slot's saves write a shares file, another
  slot's file is set aside (finding 4);
- a snapshot still waiting for its slot is kept on Keep (finding 6); a durable write whose flush fails is a failure,
  and a failed ledger write is retried at 2, 4, 8, 16, 32, then 60 s (finding 7); a shadowed lock renamed; tests for
  the remaining after_save cases.
Accepted, with reasons: (5) Keep trusting the newest time only: the game rewrites slot<N> as a whole folder
(slot<N>_new renamed over it; save.save always new, lobby.save copied with its old time: pp40-newsave's redirect log),
so an unchanged newest time after a real save needs the new save.save's write time to equal the old one to 100 ns;
the game's own recovery from slot<N>_backup (a damaged save) would restore the newer save's shares into the older
world (rare; to watch in the live test's logs); a junction to another drive can name the wrong volume for the NTFS
check.
v4.3's deltas reviewed (2026-11-22 ~00:19): two of my changes were wrong, v4.4 (same worktree, not built):
- the "only the loaded slot's saves" rule is gone: the host's saves go to other slots as a matter of course (autosaves
  rotate 0-2, manual saves 100-108), so it set aside nearly every save's file;
- the joined player's leftover (its bag full) keeps its own id again: a new id escaped the old id's ledger mark in a
  slot file written meanwhile (given twice across sessions); when nothing at all was added, the record made before the
  adds is undone (only that record, and only if no save of the joined player's ran during the adds);
- the disarm is again for the host's own character loads only: in the guest save's probe / off modes an unidentified
  owner's load is usually the joined player's;
- a save that did not move the files keeps a snapshot still waiting for them (stamp equal).
Left open from finding 4: a world started without the host character's LoadSlot (no such path in the co-op flow:
every co-op world starts in a fresh game process through Start Co-op; to check in the live test's logs).
v4.4's deltas reviewed (~00:23): no new way to give twice, no crash, no compile issue (one stale comment fixed,
<iterator> made explicit). Next: build it with its unit tests (merged into the next batch, switched off, or in the
worktree), then the live test when the owner answers.

v3 plan (2026-11-21 12:30; v2's patch is the base):
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
