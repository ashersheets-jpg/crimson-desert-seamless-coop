# The joined player's shops after a far teleport: twins made again (2026-11-21, game 1.0.0.3732)

## What happens

ShopTwin (native `session2/shop_twin.*`, guest) gives the joined player its own private copy ("twin") of each of the
host's shop scenes: when the host's PrepareSequencerAck of a shop scene reaches the joined player's game, a rewritten
one follows it (the same scene for the joined player's character, id = host id + 0x10000000), then the scene's initial
StageBranchStateAck. The twin plays in the joined player's OWN stage manager M; its StageChart SetInteraction adds the
shopkeeper's interaction entries for the joined player, and those are the entries its prompt check accepts ("R Shop").
The host's own entries for that shopkeeper (bound to the host's scene, which lives in the host character's manager X
on the joined player's game) are refused there by the game, on purpose: they belong to the host.

A far teleport of the joined player (cs75p5, cs75w2: the test's 330 m teleport; in play: fast travel) makes its game
end every scene of M: M's live scene count went 8 -> 0 and all 24 twin entries of the shopkeepers were removed in the
same half second. The host's scenes in X live on. Nothing prepared the twins again: the host's ObserverSeqCopy does not
count a same-field teleport as a trip, so it does not send the host's scenes again either. Result: in a town whose
shop scenes the host already runs, a joined player who arrives by fast travel finds every shop closed until the host
leaves and comes back (its scenes are then prepared anew, and so are the twins).

When the joined player walks in, or arrives before the host's shop scenes are prepared, nothing is lost: cs82c (the
shop alone with the host 440 m away, then with the host beside the shopkeeper) and cs75w1 opened every time.

## The fix ([session2] shop_twin_again, default 0 until tested)

Round 1 (c7fdb0d, off): a twin not live in M two checks in a row (every 2 s, 8 s after its last prepare) got its stored
packets again. It never fired in cs75w2 (most likely the ended twins stay in M's table flagged dead, which it skipped),
and a review found three faults: twins re-made forever after their host scene ended out of sight (its delete is not
forwarded after a trip), one id prepared twice when the host's copy comes again after a trip, and a twin re-made right
after its own delete (fixed in c7fdb0d: the check runs after the packet's own handling, never for a scene being
deleted).

Round 2 (worktree branch wip-twin-again, a45f580 + ab63cc4, not built yet):
- every re-make takes a FRESH id, `stl::twin_id_gen(host, gen)` = host id + 0x10000000 + gen << 20 (gen 1..255; unit
  tested: distinct, twin ids, never past 0x1FFFFFFF): the game never gets one id twice, and a dead entry in M's table
  no longer matters;
- a twin is made again only while its host scene is a live stage of a known X here; if the host scene is gone from
  every X for three checks and the twin is not live either, the twin is forgotten (no orphans);
- when the host's PrepareSequencerAck comes again for a scene whose twin is LIVE, the twin is kept (its stored packet
  refreshed); if the twin was lost, it is made again under a fresh id with its stored initial state;
- a status line every 30 s while a twin is not live: "ShopTwin [check] twins | live | ended (flagged dead) |
  unreadable | waiting for a readable X | made again | forgotten | own manager | X known".

Test: cs75w3 (the host arrives first; the joined player waits 30 s, so the twins exist, then teleports 330 m to the
butcher; with shop_twin_again=1 its R must reach the host's server).

Round 2 review (14:05, read only): six more faults (the host's delete names the generation-0 id, so a re-made twin is orphaned when the host's scene ends; a forgotten twin loses its generation, so the same id can be prepared twice later; the known X are not validated and live twins are never compared with X; the host's 2380 again can make a second twin while a re-make is queued; every refused try uses a generation, so after ~2 h the shop is lost; the initial 2920 names the generation-0 id). Not fixed in round 2: the better route is the host's own. Its ObserverSeqCopy already sends its live scenes again after the joined player's field move (a "trip", user 2's MoveFieldAck); a same-field far teleport is not one, and no tester log has a fast travel of the joined player (GuestFastTravel calls 0 everywhere), so whether a real fast travel loses the twins is not known. Round 3: first a real fast travel of the joined player (hands-on or a scripted world map), then, if it loses them, make the host send its live shop scenes again after it (host-confirmed liveness) and make ShopTwin take a repeated host 2380 safely (a fresh id only for a dead twin, kept when live or just queued).

Update 14:15: the native base's fast-travel research (native-base/docs/research/pendientes/p8-impl-viaje-rapido.md §0: after user 2's 2283 the server's 2437 takes the same-field path, SendMoveFieldAck) says a real fast travel ends with a MoveFieldAck to the joined player: the host's ObserverSeqCopy counts that as a trip and sends its live scenes again, so ShopTwin makes the twins again through its normal path. The loss seen in cs75p5 / cs75w2 is then the test's cheat teleport (opcode 3018, no MoveFieldAck) only. Unverified in a game (the research says so too): one hands-on fast travel decides whether round 3 is needed at all.
