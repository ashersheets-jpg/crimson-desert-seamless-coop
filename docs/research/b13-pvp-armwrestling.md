# B13: player-vs-player arm wrestling in the shared world (2026-11-21 late, design note)

The owner's question (PROGRESS, waiting list, 19:30): who sits where. This note says what each option needs on top of
what exists, so the answer can turn into work at once.

## What exists
- The layer's versus (D35, two-worlds era, coop_activity.cpp `versus_tick`): while BOTH players are in a match (each at
  its own table, in its own world), the host owns one gauge, 50 + 2 x (host's E presses - friend's E presses), clamped
  0..100, and writes it into its game's gauge every tick ([[exe+0x724B3A8]+0x90]+0xD30 on 3732, not checked in a
  match yet: cs83v checks it); the friend writes 100 - that into its own. 100 / 0 end the match in each game. kVersus
  packets carry each side's presses and the host's gauge every 100 ms. "We won / they won" from the last value.
- The challenge plumbing (invite / accept / series score, the friend menu, wagers) for all challenges.
- In the shared world: the host's own match at a table works (cs83w3: talk, Participate = E held 2 s, mash E, win,
  prize). The joined player's own match does not push yet (B14, cs86c-f; cs87 / cs87b tonight).
- The friend menu offered "Arm wrestling" in the shared world although it cannot finish there; hidden for the next
  batch (scratchpad hide_wrestle_native.py) until B13.

## (1) Each at a table, two matches, one shared bar
Needs B14 (the joined player's own match) first; then the existing versus nearly as is: both in live matches, the
host's gauge written into both games. New: a "both in a match" handshake in the shared world (two tables, or one table
twice: what the server does with two matches on one scene id is B14's open (e)). Each screen shows its own NPC
opponent's body, not the friend's; the friend is somewhere else in the room. Least new code once B14 works; the look
is "two matches mirrored", not a face-off.

## (2) One match: the host wrestles the table's NPC, the friend stands by and mashes for the other side
No B14 needed. New:
- the friend's side of the versus without a match of its own: a "pusher" state (challenge on + the host in a live
  match nearby): its E presses counted and sent, a banner with the bar and score drawn by the layer (no game gauge on
  its screen);
- the host's side: the gauge from both press counts (the existing host branch), the NPC's own pushing overridden by
  the written gauge every tick (as the versus does today);
- either player can be the one at the table (whoever sits down first); the other pushes.
The look: one player at the table against the NPC's body, the other standing by mashing. Smallest work after the
probe (cs83v) confirms the gauge address and that writing it each tick holds.

## (3) The friend in the opponent's chair (the NPC hidden, the friend's character posed in its seat)
(2) plus the seat: the two-worlds versus had it (`seat_tick`, `puppet_set_seat`: the other player's COPY drawn at the
NPC's transform, the NPC hidden). In the shared world there is no copy: the friend's real character would have to be
placed in the seat and play the seat's animations (837487D4 / 35DD392F ...) on both screens, while the server's
opponent NPC is hidden on both. Needs: hiding a server NPC on both games for a while, moving the friend's character
into the seat and back, its animations in sync with the match's phases. The best look, clearly the most work and the
most ways to break (the friend's character left in the chair, the NPC staying hidden).

## Recommendation (unchanged from the question)
(2) now: it needs no B14 and reuses the versus; (3) after the release. (1) only if B14 lands anyway and the owner
prefers each at a table.
