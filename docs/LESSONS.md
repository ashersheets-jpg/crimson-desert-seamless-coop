# Lessons (read before designing a test, calling something "works", or posting)

Short list of mistakes that cost time or went out to testers, and what to do instead. PROGRESS.md is the long log;
this file is the part to re-read. Add a line the moment a mistake costs a run. Newest lessons at the top of each
section. Research details (addresses, layouts) are in the local notes `build/decomp/notes_*.md`: read the note for a
topic BEFORE testing or guessing about it.

## Priorities (the owner, 2026-11-11)

- The big foundational goals first, and good: shared world, same NPCs, guest talking to NPCs, merchants, world
  events, shared map, mounts, mounted combat, bosses, cutscenes, quests. Polish on rows that are already high comes
  after.
- Work efficiently: one batch of changes, then ONE full release checklist pass. Do not rebuild in the middle of a
  pass (every pass on a build that then changes is thrown away: three partial passes were, on 2026-11-11).
- Keep the machine busy: start a game test, then read / write / research while it runs. Nothing can be built while a
  test runs, so queue source edits and build once.

## Saying "works"

- "Works" always comes with what it was tested under (one PC, two copies, which save, which settings) and what it
  was not. "Confirmed" in the test harness is not "confirmed for a player".
- A test must look like a player's PC or say that it does not: first-time settings (-Fresh), the package's own
  coopplus.ini (-PackageIni), a real install (realtitle.sh), frame generation, a new save. Three tester blockers on
  2026-11-11 hid behind a harness that had none of these.
- Look at the screenshots. Log lines said "overlay draws" while the title menu's footer was cut off in every build.
- Read what a session leaves behind, not only what it shows: log size, the most repeated log line, files written
  into the game folder. Solo play recorded every packet to disk (no limit) in every posted build, and my own
  once-a-second request wrote two log lines a second; neither was found by a test.

## Explaining a result

- Do not write an explanation as a fact until a test separates it from the alternatives. On 2026-11-11 the far-guest
  fight got three wrong explanations in a row ("enemies frozen", "nobody controls them", "not hostile to the guest")
  before the real one (the game sets damage to 0 more than 80 m from the world owner's character). Each wrong one was
  written into the notes and had to be corrected.
- A research agent's "80% guess" is a guess. Check it against data already in hand before building on it (the sector
  arithmetic and the host's own list of loaded NPCs already contradicted the "removed from the host's client" guess).
- Before writing "open question", read the local research note for that topic. The body-loot question of 2026-11-11
  was already answered in notes_lootr_bodies_chests.md.
- Put a time limit on an investigation: when two runs have not narrowed it, write down what is known and unknown,
  and move on or ask for research with a precise question.

## Test design

- Before automating a game screen, grep the mod for how that screen is already handled. Two runs (35 minutes) went
  into pressing Space on the game's agreement, which only accepts after its text is scrolled to the end; the mod's
  own guest flow (net/coop_worlds.cpp) has done exactly that since 2026-11-06.
- A run that replaces the test co-op world (-HostChoice 2 or 3) goes between `coopworld.sh backup` and
  `coopworld.sh restore NAME`. Decisions the owner already made are in NOTES/FLAGS.md: read the flag before
  questioning a behaviour (the agreement accepted for the guest is F25).

- Know what is at the test spot without the host. The "bandit camp" at the dock is the HOST's quest: with the host
  away there is nobody to fight. Five runs tested a fight that could not happen. Use the `npcs` dev step first.
- A scripted player who only clicks attack walks forward with every swing: 150 clicks = 280 m away. A fight test
  needs enemies that come to the player, or a check that the player is still where it should be.
- A test must not pass or fail by luck. The enemy-loot step needed an incidental hit to trigger a test cheat, then
  depended on a loot roll. Count the tries, make enough of them, and make the failure message say "not exercised".
- Know what the harness really does: its lobby flow always took "Continue Our Co-op Save" (the first card), its log
  line said otherwise for two days; -HostVariant has no effect on the host once the co-op world exists; the guest's
  usual test save happens to be saved on the host's spot, which hid that a guest starts at its own save's place.
- A check pattern can be wrong too: three "FAIL" lines on 2026-11-11 were my own patterns (too broad, counting a
  capped diagnostic, counting a probe's own lines). When a check fails, read the line it matched before the code.
- The tally of blows (AntiGrief line) is right for the guest's blows and for blows on players; the HOST's own blows
  never show under "host>NPC". Do not read the host's hits from it.

## Harness and tools

- The horse call for tests: a simulated pad (-HostDev pad_sim=1 / -GuestDev pad_sim=1) and the dev script steps
  `pad 0001 700`, `pad 0401 1300`, `pad 0400 400` (D-pad Up, then Up + right stick down, then the stick alone: the
  wheel picks when Up is released with the stick held). Pressing both together and releasing together does nothing.
  Then `tphorse`, `key 45 200` (E = Ride). The keyboard wheel (F1) of the old harness does not call the horse.

- Script edits with backslashes go through a script FILE written with the Write tool, never a bash heredoc: the
  heredoc turned `\1` into a control byte, `'\r'` into `''`, `\a` into a bell, a C++ `"\r\n"` into a line break (six
  times on 2026-11-11, first noted on 2026-11-06). After any scripted edit: syntax check, grep the changed line.
- Never start a test in the same command as an edit of a script the test uses.
- Every run keeps its exact switches in `build/native/NAME.args`; the checklist report has them too. Use them when
  noting or repeating a run.
- A game that restarts itself comes up seconds after its old process dies. The harness cleanup watches 20 s for
  late copies since nc1 (a test copy ran 17 minutes with the test files in the game folder; the install could not be
  put back while it ran). After any aborted run: check for a running game, "user install restored: 5/5", SAVES OK.
- A test that replaces the test co-op world (New Co-op Save) backs it up first and puts it back (checksums).
- Windows macros: `near`, `far` cannot be variable names in the native code.
- Names for crash-log addresses: `tools/symaddr.py`; a look at the game's code at an address: `tools/disasm_rva.py`.

## Releases

- No post before the whole checklist has passed on that exact package (docs/tests/RELEASE-CHECKLIST.md,
  tools/prepost.sh). One post per batch. After a tester report: reproduce it as a real player first.
- Posting needs the owner's OK, or a standing OK with an end time written in PROGRESS.md.
- The post says what was tested and what was not.
