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
