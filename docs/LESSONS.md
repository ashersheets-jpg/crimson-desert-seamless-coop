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

## A mid hook in the game's protected code broke player damage silently; read a counter's halves (2026-11-23)
pp47 (0f519102): no player blow damaged an NPC in any step, and research runs on every build since 499b8923 showed the
same. The only game-side change from the posted 7d0d63b2 was a mid hook at exe+0x37E45B1 (an actor update's null
guard) that never fired: the PATCH itself is what broke it, most likely the game's protection checking that code and
reacting without a crash. Two lessons. (1) A guard for a rare crash should not patch code at all when it can be a
vectored handler that acts only on the faulting instruction (it costs nothing in normal play and changes no bytes);
and every new hook in game code needs one fight check (players hurting NPCs) before the checklist, not after.
(2) I first called it a false alarm because the leave step showed "guest>NPC 13": that was the half of the DamageRange
tally the JOINED player's game counts (its own swings), not the host's half (blows that landed: 0). Read which half
a number is before concluding, and grep the same counter across the last good pass (pp46-*) and this one first.