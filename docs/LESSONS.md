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

- One passing geometry is not a test of a rule about distance. The far-fight fix read two registers of the wrong
  actor and still passed at 108 m, because the host stood 36 m away along one axis; it was posted to the changelog
  as working and reached two release candidates before a run at 484 m (in another direction) showed it counted
  nothing. For anything about positions: test two placements that differ along BOTH map axes, and read the
  numbers the fix itself logs (it said "another player is 37 m from the victim" for a guest 2 m from its victim:
  that line was wrong on its face and I did not question it).
- When a hook reads registers, write down which instruction loads each one, per actor, before using them.

- "Works" always comes with what it was tested under (one PC, two copies, which save, which settings) and what it
  was not. "Confirmed" in the test harness is not "confirmed for a player".
- A test must look like a player's PC or say that it does not: first-time settings (-Fresh), the package's own
  coopplus.ini (-PackageIni), a real install (realtitle.sh), frame generation, a new save. Three tester blockers on
  2026-11-11 hid behind a harness that had none of these.
- Look at the screenshots. Log lines said "overlay draws" while the title menu's footer was cut off in every build.
- Read what a session leaves behind, not only what it shows: log size, the most repeated log line, files written
  into the game folder. Solo play recorded every packet to disk (no limit) in every posted build, and my own
  once-a-second request wrote two log lines a second; neither was found by a test.

- A feature a document claims must be checked against what the release ini writes. ROADMAP said "time of day and
  weather follow the host"; the base's clock push was off ([session2] user2_clock_push=0) in every posted build and
  its game addresses were still 1.0.0.2976's after two ports (only its opcodes had moved). The friend's sky was 37
  game minutes off (found by reading the HUD clock in two screenshots, 2026-11-15). When porting, port every
  module's addresses or mark the module as unported in its header; when calling a row "works", grep the release
  native ini text (native_mode_text.h) for its key.

- A port is not done until every vtable the code checks has been re-read on the new build. The base's PlayerReader
  checked the physics object against 2976's vtable through the 3576 and 3663 ports, so it failed every read
  ("Local: unavailable (physics object missing)" every 5 s in EVERY log for days, never questioned) and every
  module waiting for it never started. A repeated "unavailable" line is a finding: read it. A vtable can be read
  live (log [object] once on a mismatch) or found by its RTTI name (scratchpad vt3663.py).

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

- A scripted step that goes "to our horse" must check the horse came. mc1 (2026-11-11): the host's script went to a
  horse 430 m away in a town and the whole run tested something else. `tphorse` now refuses a horse over 60 m away.
  After every scripted move, the first screenshot is there to confirm WHERE the player is and WHAT it is doing
  (mounted or not) before anything else is read from the run.
- An accident is still data: read what the run did show (mc1: a guest alone 490 m from the host).

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

- While a test runs, wait with Read / Grep on its .out and the browser's clock, never with a "placeholder" Bash call
  (2026-11-25: three no-op `echo` calls slipped through during cs127a-c by reflex; the rule is no Bash at all).
- The horse call for tests: a simulated pad (-HostDev pad_sim=1 / -GuestDev pad_sim=1) and the dev script steps
  `pad 0001 700`, `pad 0401 1300`, `pad 0400 400` (D-pad Up, then Up + right stick down, then the stick alone: the
  wheel picks when Up is released with the stick held). Pressing both together and releasing together does nothing.
  Then `tphorse`, `key 45 200` (E = Ride). The keyboard wheel (F1) of the old harness does not call the horse.

- Script edits with backslashes go through a script FILE written with the Write tool, never a bash heredoc: the
  heredoc turned `\1` into a control byte, `'\r'` into `''`, `\a` into a bell, a C++ `"\r\n"` into a line break (six
  times on 2026-11-11, first noted on 2026-11-06). After any scripted edit: syntax check, grep the changed line.
- A research run with [dev] native_record=1 writes its packet logs into the GAME folder and nothing moves them out:
  832 of them (14 GB) had piled up in the owner's bin64 by 2026-11-15. A runner that records packets moves them to
  build/ at its end; check bin64 for cdmp_packets.*.log after such runs.
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
- How the checklist is run (the owner, 2026-11-14 23:00; eight passes in three days cost ~20 h):
  1. A failing step does not stop the pass: note it and run every remaining step on the same package, so one pass
     finds every problem. Fix them all, package once, then one final full pass.
  2. A fix that only touches test-kit code (code that runs only under [dev] switches or in tools/, never for a
     player) does not restart the pass: re-run the affected steps plus a smoke set (static, fresh, solostart) on the
     new package, and say so in PROGRESS and the post's test line.
  3. Post less often: bundle more fixes and features per post (about one full pass per post, not per fix).
  The full pass on the final package before any post stays (rule 2 is the only exception, and only for test-kit-only
  changes).
- Posting needs the owner's OK, or a standing OK with an end time written in PROGRESS.md.
- The post says what was tested and what was not.
- 2026-11-12: read an exploration run with the checklist's generic checks too (log line repeat, log growth,
  exceptions), not only for the question it was run for. The far-fight exploration runs already had 250-330
  `User2Stages [create]` lines; the checklist found it two builds later and cost a whole pass.
- 2026-11-12: a diagnostic line that scales with what the player does (one per stage, per NPC, per hit) needs a
  per-session cap from the day it is written.

- 2026-11-12: A "known harmless" log pattern in the checklist must describe the EVENT, not the one address form it
  was first seen at. The hook library's trapped-thread fault was known as "AV at 0x7FF... kind 0x8" (system DLL
  pages); the same fault in the library's trampoline pages (0x13FFE.... below the exe) had been in 9 older logs and
  failed a checklist step the first time it fell into one. Before adding a pattern, grep all old logs for the
  neighbours of it (same kind, other addresses) and read the library's code to know the whole set.
- 2026-11-12: Commit everything under tools/ BEFORE starting a checklist pass (the static step fails on any
  uncommitted file there, also one that is not in the package), and never re-run `static` under the same pass name:
  it empties the result file. A re-run of single steps goes under a second name (pp8b).
- 2026-11-12: Look at the WORDS in every screenshot, not only whether something is drawn: the guest's waiting line
  said "Joining the start of the story..." on a Continue start through three checklist passes before it was read.

- 2026-11-12: Reading a tester's report: FIRST the settings block (`[native_resume] plan=` says which start the host
  picked, `mode=`, `role=`), THEN the base log's packet story (did `out LoginCharacterToFieldServerReq 2733` go out?
  did "the host says: active" and the packet burst follow?), THEN the layer log. The first reading of JackyChan004's
  report stopped at the overlay line and left "why did the game stay at the title" open for two hours; plan=1 (New
  Co-op Save, a bug already fixed here) was in the report all along.
- 2026-11-12: One failed session usually has more than one cause. Explain EVERY symptom the tester names from the
  log before calling a report understood; a symptom that the fix in hand does not explain is a second bug.
- 2026-11-12: Widening a known-error pattern: classify EVERY line of that kind in ALL kept logs first
  (`grep -h "kind 0x8" build/native/*.log | <normalise the address> | sort | uniq -c`), then write the rule for the
  whole set. The trapped-thread fault cost two failed steps in one pass because the pattern was widened for the one
  new form that had just shown up (trampoline page) while a third form (the game's own patched page) sat in 13 logs.
- 2026-11-12: A check's NAME and its PATTERN must say the same thing. "no guest HIT ... booked as a crime" matched
  every crime type and failed on the game's own Vandalism rule. When a check fails, read what the matched line IS
  (type, path, victim) before calling it the build's fault or the test's.
- 2026-11-12: A check on the outcome of a live fight varies with the scene (who is still alive, where a swing
  carries the character). Such a check needs a second number that tells "nothing happened because nobody was
  there" from "it did not work" (here: NPC>guest blows and the guest's path), and the note says which it was.
- 2026-11-12: Edit scripts with backslashes: ALSO not through `cat > file <<'EOF'` in this shell (the quoted
  heredoc still lost them). Write tool, and Python raw strings (r'''...''') so the text lands as written.
- 2026-11-12: Every lobby test started from a FRESH game process at the title. Three faults only show when the
  process has a history: a world was loaded before (the run-generation mark), the guest's old process is already
  in guest mode (the tunnel's reconnection rule), the guest sits at the title when the host leaves (the goodbye's
  channel). A flow that players can reach "again" needs a test that reaches it again in the SAME processes
  (twocopy.ps1 -SecondRound; [dev] lobby_recover / lobby_retry / lobby_skip_play).
- 2026-11-12: A phase that is entered from more than one place must set its own marks where it is entered
  (g_gen_mark in host_restart_into), not rely on the caller having been somewhere that set them.
- 2026-11-12: Read the WRN lines of PASSING runs too. The overlay watcher's false trigger was in pp8-loot's guest
  log (a passing checklist step of the posted package) and was only noticed three hours later in another test's
  log. A new WRN in a run that passed is a finding. To do: the checklist lists every WRN kind it has not seen.
- 2026-11-12 (third time today): no Python through a heredoc. Edit tool for a string, Write tool for a script.
- 2026-11-12: A check that compares two numbers is only as good as the number. The "same build" handshake compared a
  commit id frozen at CMake's last configure; it was the same in every package for two days and the check passed
  for two different builds. For anything "both sides must match": derive it from what is INSTALLED (a hash of the
  files), and have a test where the two sides differ ([dev] build_id).
- 2026-11-12: A redaction rule needs the real shapes of the secret in the test data, not only the plain one. The
  Steam id was tested as a bare number and leaked inside "with_<id>". For every secret, list where the program
  itself writes it (paths, folder names, URLs) and put each form in tools/test-diagnose-redact.ps1.
- 2026-11-12: When my own test script meets something odd (sk1a: a scripted move that did not arrive), a player
  will meet it too. I wrote "a player never moves like that" and fixed the script; two hours later a tester
  described the same thing in "Go to my friend". An oddity in a test tool that uses the mod's own code is a finding
  about the mod until shown otherwise.
- 2026-11-12: "It took 0.6 s" was MY log line's measure (the position jumped), not the player's (a loading screen of
  about ten seconds). I put that number into the changelog and a post draft. Before a number goes into words for
  players: what did the PLAYER wait for, measured where the wait ends (here the server's "load complete" and the
  screenshot ten seconds later that still showed the warp)?
- 2026-11-12: A new mechanism replaces an old one only where it is better. The game's travel beats hops over
  kilometres and loses to them at 100 m. Decide the threshold from both costs before wiring the new path in
  everywhere (the first threshold was a guess, and a checklist step paid for it).
- 2026-11-12: A check that failed once "on the scene's luck" and passed on the re-run (pp8 far fight) was left as
  it was, and it failed again three passes later and cost a re-run of two steps. When a step fails for a reason
  that is not the build, fix the STEP the same day: find what varied (here: the guest's swings walk it out of the
  camp) and take the variation out of the scenario. "Passed on re-run" is not a result.
- 2026-11-12: A rate limit (lines a minute) over a 7-minute test counts per-session-capped output as if it went on
  all evening. Measure a rate only over what is unbounded; check the capped things against their caps.

## 2026-11-13: a block is tested with every kind of input it claims to block

- The title's input hold was written for "keyboard, mouse and every pad" and tested with five keyboard keys. A pad
  went straight through for a week of playtests. A claim about pads needs a pad in the test (the simulated one is
  enough to see the title react).
- To see what a covered screen does, draw the cover see-through for the test ([dev] lobby_see_through). The shot
  showed in one look that the title has button prompts, not a menu: no reasoning about "which item is selected".
- When testing whether something hidden reacts, press the one harmless button (Y = Load opens a list); never the one
  that loads a save.
- A simulated press has to be timed against what is on screen, not against when the code path first runs (the first
  batch pressed during the intro logos and proved nothing).

## 2026-11-13: porting to a new game build (1.0.0.3663)

- Keep both exes. Pair the FUNCTIONS of the two builds first (exception tables: order and size), then place each
  address inside its pair; "the same bytes nearest to the old address" fails when shifts vary from +0x3650 to
  +0x7C000 and whole families of functions were rewritten.
- After addresses: message numbers (by name), byte arrays (once!), build stamps, then the things no tool sees:
  fields that moved inside structures, enum values that shifted by one, registers and stack slots at mid-function
  hooks, calling conventions (the getters took a pointer instead of a value), patch lengths, whole-body checksums.
  For each there is now a check in tools/native-port; run them before the first live test, not after.
- Named constants are not all: grep the raw numbers too (`mgr + 0x259`). One missed WRITE kept the guest in a dark
  scene for three runs.
- A tool that rewrites source is run ONCE; make it refuse a second run or make it idempotent. Two of mine were
  neither and damaged files; a commit right before each rewriting step made that a one-minute repair.
- Tests are not exempt from care: "every number in a test that is not time is an opcode" rewrote 3000 ms loops.
- The first live run costs 8 minutes and names the refusals that an hour of reading would not: start it as soon as
  the build links, and read the code while it runs.
- Compare a new run with the SAME scenario on the old build (logdiff.py), numbers masked; a different scenario
  drowns the real differences.
- (2026-11-22, after 3732) The address tools only rewrite what they recognise: values with no recorded bytes and a
  consumer that byte-checks or compares them stay at an older build's and fail SILENTLY (a hook "REFUSED", a caller
  that always reads "OTHER", a self-test "NOT PROVEN"). guest_save_logic.h's seven caller returns had been 2976's
  since the 3549 port, two months of logs naming every caller "OTHER". After each port, before calling it done:
  list every install / self-check line of the first checklist's logs that refuses or does not prove something, tie
  derived constants to their source (the returns are kCallSites + 5: static_asserts now), and run a read-only audit
  of the remaining literals (docs/research/port-gaps-3732.md) with the pins checked first.

## A shared byte pattern for two functions breaks on the next game build (2026-11-13)
- Give compared two handlers against ONE 16-byte head. The update recompiled one of them with another prologue; a tool that refreshes patterns from the exe then made the pattern match only that one. Keep one pattern per function, and after any pattern refresh check every USE of the pattern, not only its definition.
- To see whether a function really changed or only its registers did: `python build/port3663/fdiff.py <old> <new>`.

## After a game update, survey field shifts in every function we name, not only the ones that crash (2026-11-13)
- The 3663 port fixed the structs whose shift made a run fail. The inventory manager (-0x28, its holder -0x20) made nothing fail: Lootr's chest share, Give and the stash just stopped working quietly, and only the checklist's chest step showed it.
- Now part of a port: run dispsurvey.py and dispcand.py (build/port3663) right after the address map, and read every candidate line. A read through a moved offset usually does not crash; it returns a plausible wrong number.
- A feature that reads game data should log once what it read at start (the bag index, the table size), so a wrong offset shows in the first log and not as a silent "0 shares".

## A struct field survey only covers the files that name the function (2026-11-13)
- dispcand.py checks a source file against the shifts seen in the game functions THAT FILE names. stage_lookup_logic.h mirrors the game's lookup 0x21578E0 but never names it (a test pins it), so its table offsets (+8 on 3663) were not flagged, and every "is this scene in this manager" read was blind on 3663: no crash, a silent fallback to the host's manager. Found only when the shared cutscenes runs needed a lookup to succeed.
- For a port: every `*_logic.h` that MIRRORS a game function (comments "mirrors 0x...", "as 0x... does") must be diffed against that function's new body (fdiff.py), whether or not the code calls it.
- A lookup that can only say "not found" needs a positive test in a run: count the hits, and treat 0 hits over a whole session as a failure.

## Finding the game code behind a Req (2026-11-14, the ready check)
- A raw stack scan at a hook is mostly stale slots (destructors, `__chkstk` returns); even "a call right before it" does not make a word a live frame. Unwind properly instead: at a mid hook on a function's first instruction, build a CONTEXT with Rip = the function, Rsp = the hook's rsp and the nonvolatile registers from safetyhook's context, and walk with RtlLookupFunctionEntry / RtlVirtualUnwind. Protected (obfuscated) functions still unwind; a 15-frame chain came out clean on the first run.
- The client may act before the server: holding a Req at the network level does not stop what the client already does locally (the host's own story scene started 0.5 s after its held Req). Look for the client-side call that both sends and acts, and hold that call (skip it at a mid hook on the call instruction, replay it later on the same thread with the same arguments, keeping a reference on ref-counted arguments).
- A per-frame point on the game thread for replays: a mid hook right after a frame-level call on the unwound chain (here after the frame's event flush).
- Python edit scripts: write them with the Write tool (a bash heredoc broke on a quote); files edited by several tools can mix CRLF and LF, so an anchor may need the other line ending.

- An input hold that stops the player's keys reaching the game also stops the keys the mod sends itself, unless it is
  opened for them on purpose. A flag meant for that (LobbyView::driving) was filled only on a path the native flow never
  takes, and the auto-Play "worked" by luck for weeks (8 to 41 presses). When an automated press needs many tries,
  ask what eats the first one.

- A research log line that fires "when a value changed" must compare against the last LOGGED value with a real step,
  not the last read value with 1e-6: a smooth ramp (cloudiness 0.5005 -> 0.5006 ...) then logs every tick, and it
  turned a feature switched on by default into log spam that failed the checklist (pp19, 2026-11-15). Before a
  research feature goes on by default, read its log in one default run and count lines a minute.
- A combat check ("the guest killed / hit an enemy") can fail on the fight's outcome (the camp downed the guest
  first). Read the shot before calling it a fault, and compare with the same counter in the other steps of the pass.
- Actor keys that look fixed in the test world can vary in real worlds: the guest's character is 0xA0100066 only in
  our test co-op world (0xA0100116 / 11E / 12B ... in New Co-op Saves). Before hard-coding a key, grep it across the
  logs of several worlds (the newsave steps' logs have them). Find it at run time instead.
- A new setting needs its kTags entry (section + help): the title's Co-op page lists settings by section, and an
  untagged one was simply never shown (three settings, one of them announced in a post). Check a new setting on the
  page itself (a shot), not only through the ini.
- One PC cannot show the friend MOVING on the other player's screen: a game takes no pad or keys while its window is
  not in front, and the harness drives one window at a time (cs22a: the "running" guest stood still in every host
  shot). For anything that only shows while the other player moves (name tag, interpolation, animation), log the
  number on the watching side while the moving side has the focus, and read the log, not the shots.
- A fix for a rare fault needs its hypothesis tested before it ships as "the fix": the 10 s Steam wait (pp20) rested
  on "always at +2 s", and the next run faulted at +12 s. Check what the failing call has in common across ALL the
  logs (here: always the first call on the first interface pointer) before picking the cause.

- Two fails in a row on a new build, after a long run of passes, are not proof against it: the crime step failed
  twice on 0245f27d (12 passes before), passed on the old layer (cs23), then passed twice more on 0245f27d. Its
  check needed a bandit not yet fighting beside the guest, which depended on how long the host stood there first.
  When the change cannot plausibly reach what failed, take more samples of BOTH builds before blaming one, and fix
  the step's dependence on chance (here: the host now arrives 40 s later).
- The two-copy harness runs the DLLs of the BUILD folders (cdmp.asi from upstream/cdmp-native/build, the layer from
  build-native-layer), not the package named by -PackageIni (only its coopplus.ini). A "baseline on the posted
  package" needs that package's DLLs put in the build folders first, and any compile in between changes what the
  next test runs. Say in PROGRESS which DLLs a run had (cs28a ran a fix that was meant to be absent).
  Again 2026-11-21: the "research packages" gx1-gx7 (make_research_pkg.sh) changed nothing a run used, and cs82a,
  called a run "on the release build 908c60c5", ran the tree's gx7 research native with the evaluator change in it,
  which spoiled its near-shop half. The package id in a script's arguments says nothing about the binaries; the run's
  kept sums.txt (run_keep.sh) does. Before a run meant to test a package, build that package's source into the tree.
- Name a HUD element only after zooming in on it. cs27c called the line "Balthazar, the Wyvernflame" over a green
  bar a "boss bar"; it was the region panel's fort holder (with "100.00% / Fort Windridge" under it). That label cost
  three test runs and a module (trigger_share, shelved) aimed at the wrong thing. Crop both players' shots at the
  same spot side by side and read every line before deciding what differs.
- Packet records go to the game's bin64 folder. A research runner must move its own records out after the run, .bin
  AND .log (the 11-15 move took only the .log files and left 29 GB behind).
- Simulated pad input (the dev script's "pad", [dev] pad_sim) acts only in the game window that is in FRONT. The
  host's Blackstar call worked once in four runs: the one where a screenshot watcher kept bringing the host's window
  forward during the press. Before a pad step, focus that copy (an actpid shot does it) and keep it in front.
- A check that reads a periodic summary line (once a minute) misses whatever happens after the last one: the run
  can end first. Count the per-event lines too, or log a summary at the end (2026-11-16, pp25 mountfight: 17 real
  hits, the check saw none).
- Two game windows cannot both take keys at once (keys need the window in front), but the window that is NOT in front
  can still be captured with PrintWindow(hwnd, dc, PW_RENDERFULLCONTENT=2): real frames, no focus change (scratchpad
  pwshot.ps1, cs37c). Pick the window by "not the foreground one": the game restarts into co-op with new pids, so
  the harness's .pids file is stale by then.
- The pause menu's tabs move with the pad's RB / LB (pad_sim + a GuestScript, e.g. scan\pad.txt), not with E / Q.
- A probe that drives the game into a state must PROVE the state in a screenshot before its data is read. cs39c /
  cs39d / cs41a sampled "photo mode" that never opened (a shortened menu path) and two of them produced convincing
  false candidates (2026-11-16).
- Harness `pad:HEX:ms` steps only work when the loaded dev script IS scan\pad.txt: the step rewrites pad.txt and
  presses the dev-script hotkey (Ctrl+Shift+F6), which re-runs whatever script is loaded. A test that needs its own
  dev script must drive menus with keys and the mouse (`at:x:y`, `click:L`), not the pad (2026-11-16, cs39 / cs41).
- A new detector is proven only when it stays QUIET in runs where the thing does not happen, not just when it fires
  where it does. The photo-mode id fired in every pp26 step (loading, after cutscenes, the friend's arrival,
  knock-downs) and cs38i's own guest log already showed one; I read only the photo window. After adding a state
  detector, grep every log of the next checklist for its line and add a prepost check that it is silent outside
  the step that triggers it (2026-11-16, pp26).
- Stopping a checklist inside newsave or solostart leaves the step's NEW co-op world in coop_worlds (the test world
  is set aside as with_127_0_0_1_old_*): the step's own restore never runs. After such a stop, run
  `bash coopworld.sh restore <name>` in build/native before anything else; run_pp.sh now refuses to start a step
  while an _old_ world exists. A resumed pass gets a new pass name (pp27b), so the report file holds each step once
  (2026-11-17: newsave refused to back up, and mounts began in the new story world).
- Discord in the app's browser pane during a two-copy test: a `computer` click in the pane moves the OS focus to the
  Claude app (fg went CrimsonDesert -> claude at 03:08), and the harness fails a step when the game is not in front
  at a shot. Reading (get_page_text, javascript) and navigate keep the game in front. Post with javascript only:
  focus the [role="textbox"], dispatch a ClipboardEvent('paste') with the text, then a KeyboardEvent('keydown',
  Enter). Raw <@userid> mentions resolve; user ids come from the avatar URLs (avatars/<id>/) (2026-11-17).
- A fix that makes the game more correct can break a test's SCENE, not a check: after the combat stream (B18) put
  enemies where they really are, the farfight guest, made to stand 26 s among five bandits, was down before its
  first swing (12 blows instead of 5). When a step fails right after a gameplay fix, look at its first shot before
  suspecting the build or the check, and re-time the scene to the new behaviour (2026-11-17, pp28).
- "Not heard" from loudness needs an A/B inside one run: switching the sound off in alternate windows and
  comparing those windows. Comparing phases (stand vs walk) or runs mixes in wind and birds that change more than a
  footstep adds: cs42a called the friend's steps silent; cs48c showed every step played and heard (2026-11-17).
- Check that a research switch really switched before reading its result: the run's own log line ("FogShare off")
  is the proof, not the harness argument. -HostNativeExtra could add keys but never override a release default
  (the native's ini takes the first copy; the extras were written last) until 2026-11-17 (cs50c).
- To port a data global, start from the code that CONSUMES it in the new exe (the call that uses the value, then the
  getter it calls), not from its static initializer. The mission table (2026-11-17/18): following 3576's initializer
  gave two 3663 candidates that were zero at run time (cs44f); the index argument of the 3663 advance call led to
  the getter exe+0x4FFA10 in two minutes: same global, new layout (+0x28 count, +0x20 entries of 0x18). When a
  port "finds nothing", dump what the candidate holds at run time before searching further.
- Discord in the browser pane during a game test is safe only as reads. A page reload plus a paste-and-send at the
  moment of a lobby screenshot took the foreground from the game: cs44h stopped at "game lost focus" two minutes in
  (2026-11-18). Post to Discord between runs, not during one.

## Before calling a checklist failure a regression, re-run it on the old package -- and know what that changes (2026-11-18)
The crime step failed twice on 62f49d19 and passed on "9e85b891" -- but the harness copies the BUILD OUTPUTS
(cdmp.asi, coopplus.asi), and -PackageIni only takes that package's ini. So the "old package" run was the same
binaries with an ini that differed in the version line: the pass proved test variance, not a fix. Look at the
first screenshot of the failing runs before bisecting: here the guest's held E (for the assassinate) mounted a horse
standing at its teleport spot. A key meant for an NPC can take the nearest prompt (a horse, a door, a seat).

## Search PROGRESS.md for the test before writing it; never reuse a scratch test's name (2026-11-18)
I wrote "cs60" for the vendors and "cs61" for the running freeze without searching PROGRESS: cs60 already existed
(17 Nov: the FPS-cap runs 40/40, 40/20, 20/40 of the running freeze) and I overwrote its script, then nearly ran the
same FPS comparison again. Before a new test: `grep -n "<topic>" PROGRESS.md | tail`, and `ls scratchpad/csNN*` before
taking a number. The 17 Nov conclusion was the useful part: position samples show no freeze; the next probe is the
copy's animation.
- A step that fails twice "on the same binaries" and then passes is not proven variance: compare a count that says
  whether the test DID the thing (pp31-pp32 crime: guest hits seen 2-6 failing vs 37-55 passing). If the test only
  works when the game happens to line up, make it line up (a script step: tpnear, tpactor) instead of re-running.

## Run nothing that can open a window while a checklist step drives a game (2026-11-18)

The harness sends keys only to the game in front and throws "game lost focus" the moment another window is in front
(pp34 farfight and farfight2 both lost their run that way; the second matched a python edit script I started). During
a running step, use only the Read / Grep / Write tools; save edits, builds and shell commands for between steps.
- Read-only does not mean light: a Grep over build/decomp (gigabytes, a multi-threaded ripgrep on every core) during a
  step competes with both games for CPU and disk. pp43-crime had one 58 ms network tick a minute after such searches
  (its own cause was a burst of NPC hand-overs; the searches may have stretched it). Search the decomp between steps,
  or narrow it to one file.
- Waiting on a pass: a step takes 8-10 minutes and its result line appears only at its end. My sense of elapsed time
  runs far ahead of the clock while I read and write (2026-11-21: "23:40" guessed at 23:25); the clock is the newest
  [hh:mm:ss] line of bin64\cdmp.host.log or the running step's .out. Poll the pass file every few minutes, not after
  every small task.
- An Edit of an .html file also opens it in the app's browser pane (a hook: "now visible in the Browser pane"; pp43
  lowsave, 2026-11-21 22:52, a new tab with the upload staging page). Edit .html files between steps only.
- That includes `date`, `tail`, `ls` and `cat` (2026-11-21 late: I ran them during pp43's xess step out of habit;
  nothing broke, but each is a process launch). The time for a PROGRESS entry during a step comes from the running
  step's own output (build/native/<pass>-<step>.out has timestamped lines), read with the Read tool.

## Stopping a checklist pass between steps (2026-11-18)

run_pp.sh stops before its next step when a coop_worlds\with_127_0_0_1_old_* folder exists. A dummy folder of that
name (with a README saying so) stops it cleanly after the current step, which still restores the install and the
saves. Delete the folder right after. Never kill a step in the middle.

## A game update can land on any restart: read the first failing step's log before the whole pass (2026-11-19)

Steam updated the game during the user's restart; pp35 then ran 10 hours with Co-op+ off in every step (290 fails).
The first failing step's coopplus log said so in its 7th line ("Co-op+ is off: this mod was made for game version").
After a restart: check the game exe's date/version BEFORE a long pass (`ls -la .../bin64/CrimsonDesert.exe`), and
read the first failing step before letting the rest run.

## An address port rewrites game DATA that looks like an address (2026-11-19, port 3732)

p73.py's scan takes every 5+ digit hex literal below the protection as a candidate address, and a value that lies
inside a paired function gets "mapped": interaction keys (0xF42BE), stat ids (the HP stat 0x000F4240 in 7 files), the
Steam app-id mask 0xFFFFFF, our own DLL's size, a bit mask (I mapped that one by hand myself). Nothing failed to
build; enemy scaling, catching, photo mode and the friend-join check would have broken quietly. Run
build/port3732/review73.py on the apply commit before the first build and read every line it prints; mapcheck's
"OUTSIDE the paired function" lines are usually such numbers. The port's tools that import the previous port's
module (midsites.py imported p63) give "clean 0" until pointed at the new one.

## Two-copy tests: keys and screenshots of the other copy take the focus (2026-11-20)
A dev script's `key` presses go to whichever window has the focus. -GuestKeys run first, then -HostKeys, and every
actpid step (a `shot:` too) brings its own copy's window to the front. cs69c: the guest's script pressed E beside a
partridge five times while the host's shots ran every 2.2 s; not one catch reached the host (cs69a, with the host's
shots after the catches, caught). Put a script's key presses before the other copy's actpid steps start (the guest's
keys begin ~21 s after its script loads, the host's ~2 s after the guest's end), or press keys with actpid itself.
Also: the guest's `npcs` list never shows horses (type 5 on the host's server; the guest reads 3 / 6), and wild horses
at the bandits start do not spawn every run; their keys move between runs. Take a horse's key from the host log's
"observer load defer: NPC 0x... (type 5" line of the same run, not from an earlier one.


## A comparison "near the player" needs the player to be where the script put it (2026-11-21)
cs71a compared the NPC keys each game listed within 60 m of its own character at Fort Windridge and found 20-56
"missing" soldiers per side. The guest had DIED there 20 s in (the fort is the Balthazar boss arena; the host log's
"the load began with ReviveLoadingReq 2390") and was revived 99 m away, so its later lists were of another place. Before
reading a per-player comparison: check each list's "from (x, y, z)" against the teleport target and the other
player's, and grep both logs for a revive (2390) or a load (2437). A fight zone is no place for a sync check.
Also: the two sides' lists are ~15 s apart (each game runs its own script from its own load); compare what one side saw
near a FIXED point against everything the other side listed at any distance, not set against set.

## A two-PC bug that one PC never shows: add the network back with TEST sim_lag (2026-11-21)
The running freeze was reported by every two-PC tester for days; four one-PC runs (cs49, cs60) found nothing, because
one PC has no network between the games. Native 9b9bad7's TEST sim_lag ([session2] sim_lag_delay_ms / _jitter_ms /
_spike_ms / _spike_per_mille, each end holds what its tunnel receives, in order; -HostNativeExtra k=v,k=v and
-GuestDev native_extra=k=v+k=v) reproduced it in the first run (cs74b: held 8.4% vs 2.2%, Borque's 3-8%). For any
report that only two PCs show (lag, desync, order of events), run it with sim_lag first. And measure the cause, not the
symptom: the sample GAPS looked fine; the samples' LATENESS at arrival (100-300 ms against the game's 200 ms window)
was the cause.

## A dev script's "wait" is at most 10 s (2026-11-21)
dev_script.cpp caps every `wait N` at 10000 ms. cs73a's "wait 30000" and "wait 724000" ran as 10 s each: the host's
13-minute horse watch lasted 4 minutes, and the guest teleported 12 minutes early. Write a long wait as several
`wait 10000` (cs73b.sh's `w N` helper), and check a script's timeline in the log ("Dev script:" lines) before
reading its results.

## NPC keys are not stable between runs; find a person by place (2026-11-21)
cs75b / cs76b teleported to NPCs by the keys an earlier run of the SAME build and save had logged: one key named
another NPC 130 m away, the three others were "NOT loaded". The server numbers actors in load order, which shifts
with streaming and timing. In a script: `tp` to the spot, then `tpnear [radius] [n]` (the n-th nearest living person;
it logs the key it found), and aim from there (`py:faceto:R-1,0`: tpnear/tpactor put us 1 m east, +x, of the NPC).
Also: a talk / shop / table interaction starts with R while the camera faces the person; E is objects (cs75a's
stall prompt "Meat: E Buy" won while the shopkeeper stood beside the guest).
Two more (cs75c / cs76c): after a FAR `tp` (a load) the people there are not loaded for ~20 s, so a tpnear 7 s later
finds nobody ("NOT loaded"); and tpnear's spot, 1 m east of the person, can be inside a table or a wall, and the
game then puts the player wherever it fits: cs76c's guest landed on the floor ABOVE the inn's cellar (y 620.7, the
patrons at 617.1) and faced a wall for every try. Seats and stalls do not move between runs even when keys do: `tp`
to a spot an earlier run showed lands right (the y in its "npcs ... from (x, y, z)" line), wait 25 s, and aim by
world position (`py:faceto:X,Z`, the person's x, z from that run's npcs list). An exact aim is still not enough
(cs75d: the bowl on the butcher's table took the focus, "Wide Bowl: E Buy", and R did nothing; cs76d: no prompt at
three patrons): turn the camera in steps with R held at each, as the runs that worked did (tcB242, aw_route.sh), and
stop on the server's own word: `py:facesweep:X,Z,FROM,TO,STEP,82,1500,u2|host` waits for "<who> asked: requester" in
the host's log.

## "The other player is in the world" is also false while it loads (2026-11-21)
server_user2_in_world() (server.h) is true only while user 2's move phase is Stable and no goodbye was asked: a fast
travel, a respawn or any field load turns it false as surely as a disconnect. Every host rule that runs "while user 2
is in the world" (the AI hand-over checks, the clock push) stops during the friend's loads too, and anything the
friend's game held when the load began (an enemy it fought, a mount, a talk) is left with a controller whose game no
longer has it. Design such rules for both cases at once (the departure sweep, native next batch: any NPC whose
controller is not the host goes back to the host when user 2 leaves the world, for whatever reason).

## An unbuilt test edit: read the old binary's failures and the helpers first (2026-11-21)
The unit-test fixture fixes written while pp41 held the build had two faults a build would have shown at once: one
passed a std::string to the test's has(..., const char* needle) (no conversion: a compile error), and one checked the
"GuestLoad [in <kOpMilestone>]" label while the logic prints an old build's hard-coded number there (2384). Before
editing a failing test blind, run its existing binary (build/bin/RelWithDebInfo/cdmp_tests_*.exe, a few ms, no
game) for the exact failing lines, read the helper signatures, and match strings the logic really prints (better:
the message's name, not its number).

## No variable may be called `far` or `near` in code that includes <windows.h> (2026-11-21)
minwindef.h defines `far` and `near` as EMPTY macros (16-bit pointer leftovers), so `int far = 0;` compiles as
`int = 0;` and `int& far` as a nameless parameter: a confusing error far from the cause. Written into the departure
sweep while the build was locked and caught by reading the diff (renamed n_far). Grep a diff for `-w far` / `-w near`
before a native build.

## Player-facing text goes stale when the mode changes: survey it against the code (2026-11-21)
The shared-world zip shipped for two weeks with the two-worlds README (townsfolk "not shared", "no trading", the F8
panel and a dozen hotkeys the shared world never binds), the installer's last line named F8 / Ctrl+F1, and the
native layer still printed "Ctrl+F2" / "Shift+F1" / "(Ctrl+F8 too)" hints; a download post sent testers to a
"Combat & Loot" section their Co-op tab hid. Every checklist step passed: none reads text. When a build changes how
players do things, have the player-facing text (README, installer, on-screen hints, settings labels, the posts)
checked against the code that draws the menus (an agent's survey of the menu code found all of these in one pass).

## An independent review before the first build of a server-thread change (2026-11-21)
The departure sweep (ai_handoff.cpp) got three passes of my own reading and still held eight logic faults that an
agent's read-only review found in one pass (a "not in the world" signal that is also true for non-load phases, a
flag left set when the friend came back, the other rules running on stale data during a load, a radius beyond what
the host's client has, a flag that could latch into endless injections, ...). For code that runs on the host's
SERVER thread or changes who controls actors (a crash there goes to Pearl Abyss, a wrong controller freezes NPCs for
a player), ask for a review with the diff and the threading facts, not my conclusions, before building it.

## When the joined player cannot do something by script, run the host doing exactly the same (2026-11-21)
Five talk / shop / table runs (cs75c-e, cs76c-e) failed for the joined player and were first blamed on the test
method (the teleport, the aim, the bowl on the counter), each fix costing a run. One control run with the HOST at the
same spot, the same aim and the same R (cs75q1) opened the butcher's shop at the first try: the joined player's
misses were the bug the testers had reported ("can't interact with NPCs"), and its screen lacked the bottom-right
"Renee" panel the host's had. Make the host's control the first retake, not the last.

## Editing a Discord post: delete before you paste (2026-11-21)
A synthetic paste into the edit box of a message (ArrowUp in the empty composer opens the last own message) does NOT
replace a Ctrl+A selection: the new text lands after the old (3692 chars instead of 1897). Escape cancels the edit
without saving. Ctrl+A then the Delete key empties the box (innerText ~2); check that, paste, check the length and a
sentence, then Enter; afterwards the message shows "(edited)" and keeps its attachments.

## Never touch the native tree while a checklist runs; antivirus can eat a test exe (2026-11-21)
pp42's static step failed twice for reasons outside the package: an in-progress edit applied to the native tree
while the step ran ("native source has uncommitted changes"), and Windows Defender quarantining a freshly built unit
test exe at the moment the step ran it (cdmp_tests_optd_host_save, "Program:Win32/Wacapew.A!ml", a heuristic). Keep
work in progress in a separate worktree (upstream/cdmp-native-wip) or a patch while a checklist runs; when a test exe
vanishes, read Get-MpThreatDetection before calling it a failure, and never change a security setting for it.

## One Esc closes a game menu; a second opens the game's own menu and eats every key after it (2026-11-21)
cs33b (16 Nov) and cs82a / cs82b (21 Nov) each lost half a run to it: "Esc, Esc" after a shop menu or a talk closed it
and then opened the inventory, which stayed up through the next sweep, so every R after it did nothing. Close with one
Esc, shoot the screen after it, and keep double Escs for the very end of a script.

## A square key icon can still be a hold: try a held key before calling an input dead (2026-11-21)
The arm-wrestling organizer's "E Participate" ignored a 200 ms E and 20 s of E taps (cs83a, cs83w2), so the match
looked unreachable by script. Held for 2 s, E started the match at once (cs83w3). When a menu ignores a tap, try a
2 s hold, then other confirm keys (Space, left click, F), with a shot after each, in one run.

## A client-side copy of a server scene must not assume the server's message order (2026-11-21)
ShopTwin made a twin's start state only from a StageBranchStateAck that came AFTER the scene's PrepareSequencerAck,
as the host's own client gets them. On the joined player's game the order depends on the host's observer copy, which
holds a prepare until the scene's actors are announced to user 2 but passes states at once: in cs86a 21 of 31 scenes
came state-first, so every shop twin and the arm-wrestling twin was prepared but never started. The shop step passed
only because its friend is in town before the host's scenes exist. When a mod rebuilds a scene from another client's
messages, keep what arrives early and apply it in the order the game needs, and test the arrival order both ways
(the friend arriving first AND the friend teleporting into scenes that already live).

## Packet captures in two-copy runs: the file names and the joined player's switch (2026-11-21)
The sniffer's capture files are bin64\cdmp_packets.host_<stamp>.log/.bin (a dot and the role, not "cdmp_packets_"):
cs83w3's move-out glob missed them and they stayed in the game folder until moved by hand. For the HOST, twocopy's
-HostNativeExtra takes "k=v,k=v" (sniffer_record=1 among them); for the JOINED player, -GuestDev splits on commas
into separate [dev] lines, so its capture is the layer's own [dev] key native_record=1, next to native_extra=k=v.

## A log line that marks a step's request is not the step done: look at the shot (2026-11-21)
cs83a was written up as "the host's arm wrestling STARTED" from the layer's "we started a minigame" line. That line is
the client's ActivateMiniGameReq, sent when the organizer's "Participate" menu OPENS; the shots of cs83w2 show the
menu still up 20 s later and no match. Before calling a game action done, find what the player would see (a shot, a
state the game reports) and quote it, not the request on its way.

## Some game actions save the host's world by themselves; keep the test world as it was (2026-11-21)
The arm-wrestling menu's opening made the game save the host (SavePlayerToSlot slot 0 and 206) into the TEST co-op
world, creating a slot0 the backup did not have, so the next runs would have loaded the host where that save left
it. After a run that opens a minigame, a shop's purchase or anything else that may save, compare the test world with
its backup (bash coopworld.sh backup says "backup is current" when nothing changed) and move a new save out of it.

## Before building a test for a timing hypothesis, read the capture you already have (2026-11-21)
B14's hypothesis (a) said the joined player's stuck match came from the opponent being handed to its game too late
(0.6 s after the spawn), and a build to hand it over at once was the next step. cs86e's own packet capture already
answered it: the hand-over (ChangeAiControlAck at 339.756) came BEFORE the client joined the opponent (339.891), the
same order as a host match. Ten minutes of reading saved a build and a run. Before writing a fix for "X happens too
late / too early", line up X and the events around it in an existing capture of the failing run and of a working one.

## A script step "beside the nearest person" must start from a fixed place each time (2026-11-21)
The crime step's scripts repeated `tpnear 30 0` (the nearest living person within 30 m of where the player STANDS)
every 6 s. As bandits died, each hop landed beside someone farther out, and in pp43 the joined player walked from the
bandit camp to Serkis Estate (122 m) and fought townsfolk: a bounty, a downed player, 40 NPCs handed to its game at
once and two checklist FAILs that had nothing to do with the build. A repeated "nearest X" step needs the anchor
again before each search (`tp <dock>` then `tpnear`), so the radius is measured from the scene, not from the last hop.

## A fix for a review finding needs its own review: check it against the design's facts (2026-11-22)
B19 v4.3 answered two findings with rules that broke the feature: "only the loaded slot's saves write a shares file"
(the host's saves go to OTHER slots as a matter of course: autosaves rotate 0-2, manual saves 100-108, so nearly
every file would have been set aside) and "a new id for the joined player's leftover" (it escaped the old id's ledger
mark: a new way to give twice). A four-minute delta review caught both. Before writing a guard, list the facts it must
not contradict (which slots saves go to, which ids the ledger can cancel) and send every fix round, however small,
back to a reviewer with the deltas named.

## The time during a step: ask the browser, not a guess (2026-11-22)
During pp43's farfight2 I believed it was 00:17 and that both games had stopped logging at 00:07; the browser tab's
clock (javascript_tool: `new Date().toString()`) said 00:08: the logs were current and nothing was stuck. Elapsed time
inside a long turn feels much longer than it is. Before concluding a step is hung, read the clock that way (no
process is started, nothing takes the focus) and compare it with the newest line of the live logs (bin64
cdmp.host_<start>.log / cdmp.guest_<start>.log: the files named after the step's start; bin64\cdmp.log can be an old
file from another day). To wait during a step with nothing left to do, the same tab can wait too (`await new
Promise(r => setTimeout(r, 40000))`; the tool stops a call at 45 s): no process, no focus change, few tokens.

## Check the logs for the step a cause needs before writing the cause (2026-11-22, B09)
cs79b's doubled Marius / Ross turned out to be the friend's own story copies, and the roadmap note first said the
host's talk "took the friend's copy (refused)". The host's log had no talk request (2671) at all: nothing was
refused, the prompt most likely never came. A cause that names a step (a request sent, a reply refused) gets one
grep for that step first; what is not seen is written as a guess.

## A build after a candidate's pass changes what its re-runs test (2026-11-22)
The harness installs the build tree's cdmp.asi / coopplus.asi, not the package's (-PackageIni gives only the ini). A
research build made while a candidate (677c87bb) still had steps left would have sent those steps the research DLLs;
only the static step's hash check would have said so, and only if static ran first. Before a candidate's runs:
`bash build/native/dllswap.sh show` (which package the build tree is) and `put <candidate>`; after them, `put` the
research package back. A research run's own log says which build it tested only through that same hash.
The .pdb must travel with the DLL: the checklist names each first-chance exception site from the .pdb beside the
build tree's DLL, and another build's .pdb makes dbghelp fall back to export names (pp44 lagrun: fast_read showed as
"cdmp_chest_events+0x825D6", two false FAILs). dllswap put now unzips the package's symbols; a name with no
"[src...:line]" is a symbol mismatch, not a finding.

## An invisible notification window can hold the front (2026-11-22, pp44b)
A Windows Update toast came up during fresh; after it went, its "New notification" window (ShellExperienceHost) kept
the foreground, even minimised, and no SetForegroundWindow, Alt tap or AttachThreadInput could take it back: three
steps stopped at their first key with "game window is not in front". The screen looked clean. Only a real click
moves the front from it: the harness now clicks the game window's own title bar as a last try. When a step says
"not in front", read the front window's process and title, not just the screenshot.
The same toast came back an hour later (it repeats while a restart is pending) and the fallback did NOT fire: both
game copies are maximised at the same place, so the point on "our" title bar belonged to the OTHER copy, whose window
was on top. A last-resort fix has to be checked in the situation that calls it: two stacked windows, not one. The
fallback now lifts its window first (SetWindowPos topmost then not topmost, allowed without activation) and logs who
owned the point when it still cannot click; it was tried on two stacked test windows with the toast holding the front.
A chain of runs should also stop after its first "not in front" instead of spending an hour on runs that cannot start.

## A local answer that matches the server's can still skip what the server DOES (2026-11-22, B14)
The arm-wrestling twin answered every chart event with exactly the 2105 the host's server sends, and its match still
never started: on the server, the table's state-7 chart events also run the grip (CatchActionResultAck 2 ms later),
which no Ack carries. Found only by putting the host's own capture next to the joined player's (mgdiff.py: message
types after the same anchor, both sides). When a copy answers requests locally, list what the server sends AFTER each
request on the host's side, not only the direct Ack: anything there that names the player is a side effect the copy
never produces.

## A research switch made a default: its warnings and its test go into the checklist first (2026-11-22)
scene_level_share fixed the doors in cs88b and went on by default for the host. On 3732 its B part logs one WRN in
every host log ("B: el marco del 2834 ... B solo observa"), and the checklist fails every step on a warning kind
tools/known-warnings.txt does not list. Before the pass of a build that turns a switch on: grep the research runs'
logs for the switch's WRN / ERR lines (the source's log::warn list says which can appear), add the expected ones to
known-warnings with the reason, and give the fix its own checklist step (here `door`), or the pass only proves that
nothing else broke. Test-only probes the step needs (door_trace's 2976-only hook ERR) are allowed in that step only
(ERR_ALLOW / WRN_ALLOW), never in the known lists.

## Check an audit's "stale address" list against the port tools and the logs first (2026-11-21)
A read-only audit listed ten groups of addresses "left at 2976 / 3576 values". Two were already 3732's (pins73.py had
pinned them; only the code comments still named the old build) and one only fed diagnostics. Before fixing an item:
grep tools/native-port (pins*, p73_hand, fix73) for the old value, read the feature's counters in a recent log (a
route that counts 28/38/38 is working), and check what the hook does when it fires. When a port pins a value, update
the comment that names the build too, or the next audit reads it as stale.

## A message field is not an item key until one more message agrees (2026-11-22)
The death penalty's notes said "silver is item key 1 in the Money inventory". Key 1 was the Money INVENTORY's key, and
the arm-wrestling notes had read the prize's 0x4413 as an item, when it is the reason code at +5. cs97 then listed a
pouch with no coins in it. Before a key goes into a setting: read the field at the same offset the code already parses
(coop_fishing.cpp reads the item at +20 of AddItemToInventoryAck's body), and check it against a second message or a
listing that names it. Where an item lives is a separate question: search every inventory of the character rather than
trust one index.

## A cheat request the server takes for one target may do nothing for another (2026-11-22)
KillCheatReq kills NPCs and players on the host's server in every test, so cs99 used it on the joined player's horse:
nothing happened, and the run's "11064 m away" (the horse's unreadable position while ridden) looked like a result.
After any scripted cheat, check the target's state on both games (MountSync's parent key, the npcs hp line) before
reading anything into what follows.

## SW_RESTORE un-maximises; another app's overlay can hold a game's input (2026-11-22)
cs98b failed in the lobby: the Discord desktop app's overlay ("Use Discord from the overlay while playing", its
"Discord Overlay Input Trap" window) came up over the host's game; actpid's way of taking the front back called
ShowWindow(h, SW_RESTORE), which also takes a MAXIMISED window out of maximised, so the lobby's cursor points and every
shot after it missed the game (the host never left its title; the join timed out after 7 minutes and the run's later
steps clicked at a title screen). actpid now restores only a minimised window (IsIconic) and otherwise just shows it.
A chain also stops at the first failed join now ("guest REMOTE: False") instead of spending the rest of its runs.
Other people's apps on the test PC (Discord, Steam, Windows toasts) can start popups at any game launch: check the
first lobby shots (L1 / L2) of a stuck run before anything else.

## An NPC handed to the other game right after it spawns can crash the host (2026-11-22)
The AI hand-over's minigame rule moved the joined player's arm-wrestling opponent "at the next server check": 744 and
634 ms after the spawn in the research runs (no crash), 89 ms in the checklist's first armwrest step, and the HOST's
game crashed 0.5 s later in the movement update's current-action read (exe+0x8257C6, the site the guest's GuestS3
guard has covered since 3549): the NPC had no action layer yet. A rule that moves an actor between the two games
must not act on a freshly spawned one (the minigame rule now waits 1.5 s), and a guard that keeps the GUEST alive
in a controller change belongs on the host too (HostMoveGuard). Timing luck hides such races: one passing research
run proves little; a checklist step that repeats the path is what found it.

## In the shared world, know which packets reach the layer before porting a feature (2026-11-23)
The race to the waypoint (cs101) never started in the shared world although the host's map destination was set. The
native base's sniffer forwards EVERY packet to the layer (coopplus_observe in local_capture.cpp), but its
client-to-server branch feeds only activity / duel / weather, while server-to-client packets get the whole
capture_observe_s2c (so the kill feed and the hunt's kill notices work). The race's own waypoint is a client-to-server
2285, so it never arrived; the fix (the native base keeps our last 2285, cdmp_own_waypoint, the layer polls it) works
(cs101c: both games raced to the same point and agreed on the winner). Before porting a two-worlds feature: grep
coopplus_observe for its observer. And look at the shot of the action before judging a fix: cs101b "failed" only
because its scripted right-click never placed a destination (no red X on the map), which I first read as "the
export sees no 2285" and nearly reverted a working fix. The dev script's tpfriend had a gap of the same kind (the
joined player never learns the other player's key from a load): fixed with the host's fixed key.

## A VirtualQuery in a game-thread hook can freeze the host for seconds (2026-11-23)
The host froze ~4-15 s (three ~5 s main-loop gaps) whenever the joined player loaded in, in most checklist logs, and
nobody looked closer for days (cs74h parked it as "the join"). FreezeWatch ([session2] freeze_watchdog=1) named it in
one run: merchant_unhide's per-tick hook called game::safe_read, whose VirtualQuery waits on the process's
address-space lock while the loader maps memory. safe_memory.h already said safe_read is for the mod's threads and
fast_read for hooks on game threads; one hook broke the contract. When a freeze repeats, turn FreezeWatch on before
theorizing (my first theory, the window in the background, cost a test run), and keep every game-thread hook on
fast_read.

## "No player blow lands" was the test harness's cursor, not the build: A/B the posted build first (2026-11-23)
pp47 (0f519102): no player blow damaged an NPC in crime, farfight or leave, and research runs since ~05:00 showed the
same. I blamed, in turn, the newest game-code hook (a mid hook that never fired) and rebuilt it as a vectored handler;
pp48a failed the same way, and only then the decisive A/B ran: the POSTED 7d0d63b2, which passed this step at 01:40,
failed it at 11:16. The cause was the harness: its focus fallback for the Windows Update toast clicks the game's
title bar and left the mouse cursor there, so every later click:L (an attack) went to the title bar. Lessons: (1) when
a known-good step starts failing, run the last posted build on it FIRST: it splits "our code" from "the test PC" in
one run, before any theory or rebuild; (2) a harness change that moves the cursor (or focus) must put it back;
(3) read which half of a tally a number is ("blows from the guest's client" counts its own swings; "from the host's
client" counts blows that landed) before concluding anything from it.

## A step whose join failed must stop, not play its keys at the title menus (2026-11-23)
pp48-crime's join failed (Discord's overlay over an un-maximised host window at Start Co-op). The harness still ran
the step's 15 minutes of keys and clicks on both title menus; one of them started a New Co-op Save, which set the
test co-op world aside, and the checklist runner then stopped the whole pass before its next step. A test step's
input belongs only where its precondition holds: twocopy.ps1 now ends the lobby flow at "guest REMOTE: False". When a
pass stops early, look at what the failed step did AFTER its failure point, not only at the failure itself.

## A log-only probe of a tester's bug class can find it in our own runs (2026-11-23)
Brycecco's invisible boss could not be reproduced (no boss save, his zips not yet in). The cheap move was a log-only
watch on the mechanism we suspected (merchant_unhide's hide bit 9, for every scene instead of the shop list), in the
build the checklist runs anyway. Its first checklist step showed the bug class at our own start spot: three critters
of a host scene, visible on the host within 4 s, hidden on the joined player's game for good. That gave a fix (the
host tells the guest which of them it shows) and a test for it in one evening. Lessons: (1) when a report cannot be
reproduced directly, log the suspected mechanism wherever it runs and let the existing tests look for it; (2) such a
probe must compare BOTH games (here: hidden on the guest while visible on the host), or it cannot tell a bug from a
scene that hides people on purpose; (3) an intermittent repro (the scene bound in about half the runs) needs the
fix's own diagnostics (the host's first look) before concluding from a run where nothing happened.

## Porting from a build whose exe is gone: find each point through its caller (2026-11-24, T23)
docked_gimmicks still had 2976 addresses, and no 2976 exe exists here. Its recorded heads are generic prologues (8
matches in the right region for the docking spawns, none at all for two functions whose prologue changed). What
found every point for certain: the CALLER, read from names that survive builds. A troc message's handler is slot 2
of the RTTI vtable `.?AVTrocTr<Message>@pa@@` (UpdateGimmickReq's handler calls the server update, and so on). A
component's request is a slot of its class's vtable, and that slot can move: here +0x740 became +0x748, because a
virtual was added before it. The refusal's global confirmed the client and server pair (both store 0x71D5CC4).
2976 anchors from ByJunter's `Class::vfN` name list (176 resolved in 3732, 172 in order) narrowed each search to a
window. Also: constants whose comment names an older build may already be the new build's (the controller write
matched 3732, not 3663): check the bytes in each exe before "porting" them again. The tools are
tools/native-port/port3732/gimmick73.py / gimmick73b.py.

## A test save's items reach the joined player only in a Continue (2026-11-24, cs112a / cs113a)
cs112a gave the joined player the fishing3 test save (a rod) with -GuestVariant, but in a New Co-op Save (-Fresh)
the joined player gets a new character: no rod, and the scripted "take out the rod and cast" did nothing, which
looked like a fishing bug. Only a Continue uploads the joined player's own slot103 into the host's world (cs18a's
note). Before reading a joined player's failure as a game or mod fault, check that its character IS the save the
test meant: its name, equipment or level in a shot or log line. A second trap in the same runs: the quick-slot
key from an old session's notes ("2") brought up another item on that save; input notes go stale with saves and
patches, so a shot after each input step is what tells.

## A placement change: look at the first shot where it acts, not only its log line (2026-11-24, pp52-lowsave)
The T19 landing logged "lands on their own spot (0.0 m)" and the step passed, but the host's shot showed the two
characters inside each other. The unit tests checked the arithmetic; only the shot showed what that arithmetic
looks like in the game. The fallback went back to the old +1 m beside the friend. For anything that places a
character, read one shot from each game of the first run that exercises it.

## A function that disassembles to junk after a jmp is a protection stub: read its target (2026-11-24, the taming)
The server's branch 0x2B56870 (3732) disassembled as one `jmp 0xE169180` and then nonsense. The body is whole at the
jmp's target, a relocated copy far out in the exe: readable code, its error codes loaded from globals the game fills
at start from hashed names (`mov eax, [rip+X]`, X = 0 in the file). Follow the jmp, then the call that writes the
result; to name an error code, find the global its name's hash goes into, then who loads that global.

## A Discord message holds 2000 characters: count the post before staging it (2026-11-24, 527e283a)
The first draft of the 527e283a post was 2362 characters; Discord would have refused it or cut it after the files
were attached. The staging script prints the length (json.dump of the text): keep the post under ~1900 (mentions grow
when Discord renders them). Long explanations go to the feedback reply, which has its own 2000.

## Guide key names are not this build's: look at the first shot before scripting a whole sequence (2026-11-24, cs116a)
The web guides said "hold F2, the mouse wheel to the rod". cs116a scripted the whole fishing sequence on that and ran
it for minutes; the first shot already showed no wheel. For an unknown key, run the key alone with a shot right after
it (cs116b's sweep), then build the sequence on what the shot shows.

## Git Bash rewrites Windows "/X" switches into paths (2026-11-24, cs120a)
`tasklist /FI "IMAGENAME eq ..." /FO CSV` from a bash script got "Invalid argument - 'C:/Program Files/Git/FI'": MSYS
turns an argument that looks like a POSIX path into a Windows one. Prefix such commands with MSYS_NO_PATHCONV=1 (or
write //FI). Run a sampler line once by hand before a 40-minute run depends on it: cs120a lost its memory data.

## A long hold lets the game autosave the test world (2026-11-24, cs120a)
In 40 minutes the host's game wrote slot0 AND slot1 into the test co-op world; the scripts moved only slot0 out, so
the backup check refused ("a different backup exists"). Long runs move every new slotN out (cs120.sh: slot0-2).

## A unit-test exe that crashes hides the new test's result (2026-11-24, seq_copy)
cdmp_tests_optd_seq_copy crashes partway (a baseline failure) and its stdout was buffered into a file, so the
crash lost every line, the new test's too: "old failure" said nothing about the new code. Now the suite sets
stdout unbuffered and runs the new test first. For a suite on the baseline list, run the exe by hand after adding a
test and read its own lines before calling the build fine.
## Search the base's research before reversing a structure (2026-11-24, the taming refusal)
Hours went into reading 0x25C82A0 / 0x21CABF0 to learn what a manager's +0x90 / +0xA0 are; ByJunter's S46 notes
(native-base/docs/research/s25/s46-datos/nativo.md) already said it: the manager's FOCUS stage, with its setter, its
undo and its clear. Before reading a function for its meaning, grep native-base/docs/research for the offsets and
the function's old (2976) neighbours; the base's notes name 2976 addresses, which the port tools and the function
order (lookup 0x1F6C190 -> 0x21C7370, etc.) map to 3732.

## Use the module's own offset names before naming a field in a probe (2026-11-24, BF probe)
The first branch-refusal probe logged [[scene+0x28]+0x60] as "the owner actor"; +0x28 is the scene's MANAGER (named
kStage_Mgr in four other modules). The probe's line would have pointed the analysis at an actor that was never
there. grep the tree for the offset (`kStage_`, `kMgr_`, `kProc_`) before writing a new name for it.

## A new overload can silently capture existing calls (2026-11-24, shop_probe name_of)
A new name_of(const uint64_t*, int) next to the module's name_of(const uint64_t*, size_t) would have taken every
existing call with a literal count (name_of(v + 6, 5)) and changed its meaning without a compile error. Give a new
helper its own name, or grep the callers of the name first.

## One state per manager shared by two players is a bug class (2026-11-24, the focus slot)
The joined player's own scenes live in the host's stage manager, so anything the game keeps once per manager (the
focus slot +0x90 / +0x98 / +0xA0) is shared by both players: one player's step is refused or checked against the
other's state. When a joined-player failure is a server refusal with no obvious cause, ask what per-manager or
per-owner state the check reads, and whose it is in the shared world.

## Messages from the user's phone can arrive late and together (2026-11-24)
Four messages written hours apart reached the session at once, after a later one ("those sent late"). When the user
says messages are missing, list what arrived, in order, and ask for the rest again; act on the newest intent.

## An opcode from a note or a log line is 2976's number: compare against the shp::kOp* constants (2026-11-25)
The research notes and the probes' log text name the requests by their 2976 opcodes (2880 StageBranchStateReq, 2591
the chart event Req), while the code's constants are 3732's (shp::kOpBranchReq = 2644, kOpChartEvent = 2200). The
focus slot's request filter was about to be written with 2880 / 2591 from the note, which on 3732 matches none of
them (the window would silently never open). In code, always use the named constant; when a note gives a number,
find the constant it names before using it.

## The checklist plays a minigame on the keyboard only: a pad report needs a pad test (2026-11-25, Edward)
The armwrest step mashes E inside a co-op session; a tester's solo match on a controller ("the character starts
jumping and it never ends") is a path no step drives. When a report names an input device or a mode the checklist
does not use, the repro starts with that device and mode (pad_sim, -NoGuest), not with the step that passes.

## A kit file edited during a checklist run fails its static step (2026-11-25, pp59)
The static step checks that layer / kit / tools have no uncommitted changes. Editing kit/coopplus/README-shared-world.txt
for the NEXT package while pp59 ran made static FAIL ("uncommitted changes") on a package built from a clean commit,
and the README change then needed a new package and a re-run of static + real + a smoke step before the post. During a
run, draft kit text in the scratchpad (or docs/drafts/) and copy it into kit/ after the run, then package once.

## A wait timed in seconds for a game state breaks when that state is late: wait for the state (2026-11-25, cs127c)
The join's trip to the host waited "12 s after both are in the world" for the friend's own move into the host's field
to end (nb5 / nb6, 2026-11-12). With the host's server stalled 25 s that move ended 35 s later, the trip went first,
the server ran its reload to the end, and the friend's game kept it where it was. The host's native base already
tracked the move from the tunnel's packets (MovePhaseTracker: Stable at the LoadingCompleteAck); the trip now waits
for that word. When a fix "waits N s for X", look for something that already knows X, and test the case where X is
late (here: the forced stall).

## A failure only a tester's PC shows: force it on one PC with a TEST-ONLY switch (2026-11-25, Borque / cs127)
Borque's host's server stopped taking input for 30 s, 26 s and 153 s right after his friend joined; no one-PC run in
weeks had a stall over 1 s, so neither the fix (the far travel waits) nor the new stall diagnostics could be seen
working. A switch that makes the failure happen on purpose ([session2] dev_server_stall_ms: ServerRunThread sleeps once
at user 2's 2nd MoveFieldCompleteReq 2723, the moment his host stalled) turns it into a scripted step (cs127, the
checklist's "stall"). cs127a first used "2471", the number the milestone lines print, and the switch never fired: the
opcode lesson below, again; a test switch must log when it FIRES, and its step must check that line first.
Such a switch: 0 by default, named TEST ONLY in its log line, set only through a harness argument, and the step allows
its own warnings while every other step still treats them as findings. And when a tester's log cannot say where a
thread is stuck, the next build must be able to (name the item it entered, sample its stack): guessing the cause from
the outside (his other mods, a slower PC) stays a guess.

## A hook "around a request" must be proven to wrap the handler's own run (2026-11-25, cs125d)
The per-player focus slot wrapped ServerRunThread's call of OnRecvPacket and treated the thread that built user 2's
session as "the server's". Both were the NETWORK side: the handlers and the focus writes run later on the LOGIC
thread, so the fix never acted in two posted builds, and 28 green checklist steps could not show it (no step had the
case it was for; "swaps 0" read as "nothing to do", not "never could"). A one-line probe would have shown it the first
day: from inside the handler (its own hook), log whether the window's thread-local is set and both thread ids. Do
that for every "around the request" design before trusting it, and treat a counter of refused calls ("other threads")
in the hundreds as a red flag, not noise.

## Before chasing a new failure, search PROGRESS for it as a known limitation (2026-11-25, cs123-cs124)
Five runs (cs123a-c, cs124a-b, ~1.5 h) chased "the friend cannot open the butcher's shop" (its game never looked for
anyone to talk to) through the host's talk, the table's twin and the distance, before PROGRESS 2026-11-21 13:05 turned
up: "after a far teleport INTO a town whose host scenes already run, it does not [work] (known, said in the post)":
the friend's game refuses a twin's prepare while the shopkeeper is not loaded there, and shop_twin_again (the re-make)
stays off. Every one of those runs teleported the friend into the market after the host. When a scripted setup
fails in a new way, grep PROGRESS / docs/research for the symptom and for the setup's own moves (a teleport, a load,
an order of arrival) first; and a test's setup must not be the known-broken case unless that case is the subject.

## A teleport's height must be below any floor above the target (2026-11-25, cs122b / cs122c)
"tp -10661.5 619 -3830.0" (the Hernand inn's arm-wrestling table) landed on the table's floor (617.09) in cs122a and in
every armwrest step, but on the gallery above (620.76) in cs122b and cs122c: 619 is inside the gallery's floor, and
the game sometimes pushes the character up instead of letting it fall. The screenshots showed an upstairs corridor and
two runs were lost before the logs' positions showed why. Teleport a little above the floor you want (617.5 here),
and when a run "cannot find" an NPC it reached before, compare the character's height in the logs first.

## A step that starts the report must not wipe the steps run before it (2026-11-25, pp60)
The checklist's static step began with ": > $R" (start the report), written when static always ran first. pp60 ran
the four newest steps first (armwrest shop talkwhile chest) to fail fast; static then wiped their sections, and the
pass's TOTAL said 1142 checks over 26 steps although all 30 had passed (their per-step lines and console files kept
the counts). Static now starts the report only when it holds no step. When a pass's total disagrees with its step
lines, count the steps before believing either, and keep each step's own output next to the shared report.

## A pad test starts from the game's own prompt: which button, and tap or hold (2026-11-25, cs129a / cs129b)
Edward's report said "mash X", so cs129a held X at the arm-wrestling organizer; the dialog's own Xbox glyphs said
"(A) Participate", and nothing started. cs129b tapped A for 300 ms: still nothing, because Participate is a HOLD (the
keyboard step had held E for 2 s all along). Two runs of ten minutes each went to things one screenshot of the prompt
and one look at the keyboard step's key list would have told. Before scripting a pad test: take one shot at the
prompt, read the glyphs (which button, a ring = a hold), and copy the hold times from the keyboard step that works.
A tester's words for a button are a lead, not the binding.

## A diagnostic that prints an impossible value is a failed check (2026-11-25, the schedule stage offsets)
Since 1.0.0.3663 every U-1 [state] line said all of the host's schedule stages were "162..162 cells from the host"
(one cell, (0, 0), for every stage) and every [close] line printed "stage at (0, <the real x>)": the stage's fields
had moved 8 bytes and the module read them short. Nobody looked for two weeks, while the same reads fed the duplicate
guard (U-f2 skipped nothing, 0/0 in every log) and the joined player's per-NPC control. A value no real game can
give (all stages in one cell, a position with x = 0, an owner key that is no actor) is a broken read, and a counter
that stays 0 for a whole session is a feature that never ran (the 11-13 lesson, again). After a port, scan the first
checklist logs for such values and make the worst of them checks (the host's stages within 6 cells of the host).

## A switch set only on one path of the init is off on the other (2026-11-25, the duel's stagger)
The duel's hit reactions ([coop] player_hit_react, on by default) were switched on in mod.cpp's late init only on the
old base's path; the native layer returns from its own branch before it, so no duel hit ever staggered in the shared
world, and no step checked a reaction. When a feature moves to a new base, list the setters its old path calls and
check each against the new path; and a feature with an on/off switch should log its state once, so "never set" shows
in the first log ("our player reacts" was missing from every native log).

## The tests revived the joined player one way; testers used the other (2026-11-25, U vs Q)
The revive step presses Q (in place, no load); the checkpoint revive (U) sends a load the client never completes,
which left the native base's move phase at MOVE_ASKED until the next travel and kept the joined player's own
townsfolk, the world anchor, the clock and the weather off for minutes after each such revive. When a feature has two
ways (two keys, two menus, two revive kinds), the checklist drives both; and a tracker with a "waiting" state needs a
check that it leaves it (here: the phase back to STABLE after each revive).

## -HostDev / -GuestDev: [dev] keys go before any other section (2026-11-25, cs131b)
twocopy.ps1 turns "-HostDev a=1,[keys],K=V,[dev],b=1" into "[dev] a=1 / [keys] K=V / [dev] b=1" (and appends its own
fps_cap=40 at the end). Windows' INI reader takes the FIRST [dev] section only, so b=1 and the fps cap were never
read: cs131b's duel_test=1 did nothing (a swing at 134 degrees "missed"). Write every [dev] key first and the other
sections after them ("duel_test=1,[keys],Challenge=Shift+F5,[dev]"). No checklist step passes [keys]; cs100-cs131
did. And look for the switch's own log line before trusting a test that depends on it.

## A port updates one copy of a packet layout and not the other (2026-11-25, the duel's restriction)
guest_hits.cpp learnt on 3576 that ApplyActionRestrictionAck is 69 bytes ("with the old size no stagger was ever learnt
on 3576"); player_hits.cpp, which reads the same packet for the duel, kept 2976's 77 bytes and offsets for weeks.
When a port finds a packet's new size or layout, grep every module for that opcode and its old size, not only the
one that showed the bug.

## A packet right after an event is not that event's until its id says so (2026-11-25, the grindstone)
I told the testers the grindstone's "little scene went to the host" because a stage branch to the host's character
came 0.3 s after the joined player's grindstone request. That sequencer (0xB82) had been prepared for the host 18
minutes earlier: an unrelated long-lived scene. Before naming a cause to testers, trace the id in the evidence back to
its creation (its prepare line) and check it belongs to the event; a correction costs another message and some trust.

## During a test, a wanted shell command goes on a list, never into a neutered Bash call (2026-11-25, cs130a / cs129a)
The rule "no Bash while a test runs" was broken three more times in one afternoon, always the same way: a Bash call
was begun for something useful (a grep of our logs, a look at a file's tail, a timestamp), the rule came to mind
mid-call, and the call was turned into "echo skip" / "echo no" / "echo x" instead of being dropped. Each was harmless,
each was still a Bash call during a test. What works instead: Read / Grep / Glob for anything that only reads (they
cover the logs, the live game-folder logs and the screenshots), the browser's `new Date()` for the time, and for
anything that really needs a shell (curl, python, git, a build) a line in a scratchpad file, run after the test. If
the next tool call would be Bash and a test is running, the call is not made at all.

## A new checklist check rests on "the game sends X here": find X in that step's own log first (2026-11-26, pp65)
I put the level-state copy check into hostrevive because "a reload re-sends the host its level states" (the camp
scene's in-place reload did in cs135). The checkpoint revive's reload sends none, so the check failed and the step
needs a re-run. One run of that step on the new build (or a capture of it) before the pass would have shown it:
before adding a check that waits for a message, see the message in that step, and put the check where it already
appears (here u2revive, the host's long teleport: 3 lists copied).

## The button can change between a minigame's menu and its match: look at each phase's prompt (2026-11-26, cs129c-g)
Edward's arm wrestling on a pad: the organizer's menu shows "(A) Participate", so my tests mashed A through the match
and lost every time (the gauge only fell). Shots every 0.4 s through the match showed "(X) Push": the match takes X.
Mashing X won at once. The lesson "a pad test starts from the game's own prompt" applies to EVERY phase: take a shot
in each phase (the menu, the countdown, the match) before scripting the next phase's presses.

## Name the object a tester used from the place, not from the order of their notes (2026-11-26, the "grindstone")
Their list named a grindstone (prompt, hold, nothing) and church windows. In the log I found one interaction of the
joined player that was accepted and then nothing, and called it the grindstone; I even wrote a test for "the
grindstone" there. A shot at that exact spot (cs133a) showed the game's place name, "Church of Hernand", and the
joined player had climbed the wall to reach the objects (two of them, the same interaction): the windows. Before
telling testers what an interaction was, find where the player stood (its position lines), what moved it there
(climb, walk) and how many objects took the same interaction; a quick shot at the spot names the place.

## After asking testers a question, read the channel again before drafting the next reply (2026-11-26)
On 25 Nov I asked Brycecco two questions (the windows, the portal chest) at 21:52; he answered both at 21:54. My next
reply draft asked them again two hours later, because I had read the channel only before posting. Testers often answer
within minutes. Before writing a follow-up that asks anything: read the channel since my last post, and answer what
they already said (here the answer also closed a question by itself: the journal line it led to showed the quest had
moved, so the "portal chest" was the known host-only-place gap).

## A Discord check includes the account's unread DMs, not only the server's channels (2026-11-26)
Borque wrote the account two DMs at 12:30 UTC (a question for the owner); my checks read #play-testing-feedback,
#general and #bug-reports only, and saw them 2.5 hours later because the tab's count went up by one. Testers write
DMs too (Brycecco's video link and boss-fight note came that way). Each check also reads the guild bar's first items
(the unread DMs sit right after "Direct Messages", with "N unread messages" in their label) and opens the testers'
ones; a DM from someone who is not a tester stays unopened and goes on the owner's list.

## A game update's automatic map is a first pass: non-addresses, byte-alikes and moved calling conventions (2026-11-26, 3840)
What happened: the 3732 -> 3840 auto map rewrote numbers that are not addresses (guest_s3's mod-DLL span 0x2065780,
the layer's photo-mode float 0xCB0A77, a [dev] code-range bound 0x4A00000), placed one mid-function window in an
unrelated function whose bytes looked alike (user2_funnels kApplyCallCtx -> 0x08B22F5; the right site 0x3019762) and a
protected-section site on a byte-alike inside .reloc (guest_x_interaction 0xCA2BDC0; the 3732 site was plain code),
and "mapped" error globals to their neighbours (three of trade_probe's, user2_stages' eErrNoEmptyStage). Worse, the
info getters changed their CALLING CONVENTION (`row*(const u16*)` -> `row*(table*, u32)`, NULL for no row) and the
records lost their name pointer: every address can be right and the call still reads garbage.
Rule: after the auto map, (1) error globals are placed BY NAME (the registrars pass the name with the global);
(2) every literal the map changed that is used as a SIZE, MASK, KEY, FLOAT or RANGE goes on the SKIP list and back to
its value; (3) a raw CALL to a game function compares the callers' argument set-up in both builds, not only the
callee's address; (4) a mid-function window is accepted only inside the paired function (fmap pair or the callers'
votes), never by bytes alone.

## One owner per file when several helpers apply a port (2026-11-26, 3840)
What happened: four helpers each found the same structure moves (ServerField's owner +0x180 -> +0x158, the
TransformSync field +0x518 -> +0x528) in files outside their own lists, and two placed the same 13 points of one
shared header (stage_lookup_logic.h).
Rule: before anyone edits, give every file one owner (hand/APPLY.md), give the files nobody owns to the helper whose
facts they need, and have the others report what they found there instead of editing.
