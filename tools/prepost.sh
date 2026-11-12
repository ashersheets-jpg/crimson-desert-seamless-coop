#!/bin/bash
# The release checklist's runner (docs/tests/RELEASE-CHECKLIST.md). One step per call, so no call runs longer than
# one game session and every step's screenshots can be looked at before the next one starts:
#
#   bash tools/prepost.sh TAG "<package folder>" STEP        STEP = static | real | fresh | xess | fsr | display |
#                                                                   lowsave | chest | revive | loot | crime | farfight | farfight2 | newsave | solostart | mounts | mountfight | nosave |
#                                                                   retry | stuck | mismatch | hostrevive |
#                                                                   summary
#
# TAG names the run (pp1 ...): the test names are TAG-STEP, the report is build/native/prepost-TAG.txt.
# A step appends its checks (PASS / FAIL / INFO lines) to the report. "summary" prints the report and the totals.
# The game steps use the test harness in build/native (two copies on this PC, sandbox saves, the player's install
# put back afterwards); they refuse to start while Crimson Desert runs. Nothing here posts or publishes anything.
ROOT=/c/Users/asher/CrimsonDesert-SeamlessCoop
N=$ROOT/build/native
TAG=${1:?tag}; PKG=${2:?package folder}; STEP=${3:?step}
case "$PKG" in /*|[A-Za-z]:*) ;; *) PKG="$ROOT/$PKG" ;; esac
MOD="$PKG/mod"; MODW=$(cygpath -w "$MOD")
R=$N/prepost-$TAG.txt
NAME=$TAG-$STEP
SCAN='C:\Users\asher\CrimsonDesert-SeamlessCoop\build\native\scan'

say()  { echo "$1" >> "$R"; }
ok()   { say "  PASS  $1"; }
bad()  { say "  FAIL  $1"; }
info() { say "  INFO  $1"; }
has()    { if grep -qE -- "$2" "$1" 2>/dev/null; then ok "$3"; else bad "$3   [no /$2/ in $(basename "$1")]"; fi; }
hasnot() { if grep -qE -- "$2" "$1" 2>/dev/null; then bad "$3   [$(grep -cE -- "$2" "$1") x /$2/ in $(basename "$1")]"; else ok "$3"; fi; }
# the number after WORD in the LAST line matching PAT
lastnum() { grep -E -- "$2" "$1" 2>/dev/null | tail -1 | sed -n "s/.*$3 \([0-9][0-9]*\).*/\1/p"; }
atleast() { local v; v=$(lastnum "$1" "$2" "$3"); if [ -n "$v" ] && [ "$v" -ge "$4" ]; then ok "$5 ($3 $v)"; else bad "$5 ($3 '${v:-none}', wanted >= $4)   [$(basename "$1")]"; fi; }

# Lines per minute of a log (first to last "[HH:MM:SS" stamp). More than $3 a minute is a FAIL: a player's log must
# not grow by a line or two every second for hours (2026-11-11: the mod's own once-a-second request did that).
growth() {  # log, who, limit per minute
  local f=$1 a b n
  [ -s "$f" ] || return
  a=$(grep -m1 -oE "^\[[0-9]{2}:[0-9]{2}:[0-9]{2}" "$f" | tr -d '['); b=$(grep -oE "^\[[0-9]{2}:[0-9]{2}:[0-9]{2}" "$f" | tail -1 | tr -d '[')
  n=$(grep -vc "Lootr probe" "$f")  # not the loot probe's own lines (a dev key the loot step switches on)
  local sa=$((10#${a:0:2}*3600 + 10#${a:3:2}*60 + 10#${a:6:2})) sb=$((10#${b:0:2}*3600 + 10#${b:3:2}*60 + 10#${b:6:2}))
  local secs=$((sb - sa)); [ $secs -lt 60 ] && secs=60
  local per=$((n * 60 / secs))
  # the single most repeated line (numbers and addresses blanked): more than 30 a minute = something logs every
  # second or two for the whole session
  local top cnt
  # (not the base's horse diagnostics: capped at 400 lines a session by its own counter, horse_diag.cpp kMaxLines;
  # not its stage-creation lines: capped at 300 a session since pp7, user2_stages.cpp kMaxCreateLines, checked below)
  top=$(grep -v "HorseDiag \|User2Stages \[create\]" "$f" | cut -c29- | sed -E 's/0x[0-9A-Fa-f]+/N/g; s/[0-9]+/N/g' | cut -c1-60 | sort | uniq -c | sort -rn | head -1)
  cnt=$(echo "$top" | awk '{print $1}')
  local tper=$(( ${cnt:-0} * 60 / secs ))
  [ $tper -le 30 ] && ok "$2: no line repeats more than 30 times a minute (the most repeated: $tper a minute)" || bad "$2: one line repeats $tper times a minute: $(echo "$top" | cut -c1-90)"
  local cr=$(grep -c "User2Stages \[create\]" "$f")
  [ $cr -le 301 ] && ok "$2: the stage-creation diagnostic is capped ($cr lines, at most 301)" || bad "$2: $cr stage-creation diagnostic lines (the cap is 300)"
  [ $per -le $3 ] && ok "$2: log growth $per lines a minute ($n lines in $((secs / 60)) min, limit $3)" || bad "$2: log growth $per lines a minute ($n lines in $((secs / 60)) min, limit $3)"
}
# Nothing may record packets to disk in a player's game: the base's log must say so, and no capture of this run may
# be larger than 64 KB.
no_recording() {  # native log, who
  hasnot "$1" "recording through a disk queue" "$2: packets are not recorded to disk"
}

head_step() { say ""; say "== $STEP ($NAME) $(date '+%Y-%m-%d %H:%M') : $1"; }

overlay_checks() {  # layer log, who
  has    "$1" "Overlay ready"            "$2: overlay ready"
  hasnot "$1" "Overlay disabled"         "$2: overlay never disabled"
  atleast "$1" "Overlay: installed true" "drawn" 100 "$2: overlay draws"
  hasnot "$1" "\] ERR "                  "$2: no error line in the layer log"
  # Warnings. "Crash log: exception ... at cdmp.asi+0x..." lines are first-chance exceptions; the base's own guarded
  # reads raise some on purpose and catch them (fast_read, guest_seq_route's rd). Each faulting address is named from
  # the build's .pdb (tools/symaddr.py): a known guarded read is INFO, anything else is a FAIL to look into.
  local addrs; addrs=$(grep "Crash log: exception" "$1" 2>/dev/null | sed -n 's/.*exception 0x[0-9A-F]* at \([A-Za-z0-9_.]*+0x[0-9A-F]*\).*/\1/p' | sort -u)
  local a known=0
  for a in $addrs; do
    local m=${a%%+*} o=${a##*+} img="" fn
    [ "$m" = "cdmp.asi" ] && img=$ROOT/upstream/cdmp-native/build/bin/RelWithDebInfo/cdmp.asi
    [ "$m" = "coopplus.asi" ] && img=$ROOT/upstream/cd-coop-3446/build-native-layer/bin/RelWithDebInfo/coopplus.asi
    fn=$([ -n "$img" ] && python $ROOT/tools/symaddr.py "$img" $o 2>/dev/null | cut -d' ' -f3- | tr -d '\r')
    case "$fn" in
      *fast_read*|*guest_seq_route.cpp*|*safe_memory*) known=$((known+1)) ;;
      *) bad "$2: first-chance exception at $a = ${fn:-unknown code} (not a known guarded read)" ;;
    esac
  done
  [ $known -gt 0 ] && info "$2: $known first-chance exception site(s), all the base's own guarded reads (caught by design)"
  local w; w=$(grep "\] WRN " "$1" 2>/dev/null | grep -vc "Crash log: exception")
  [ "${w:-0}" -gt 0 ] && { info "$2: $w other warning line(s) in the layer log:"; grep "\] WRN " "$1" | grep -v "Crash log: exception" | cut -d']' -f3- | cut -c1-150 | sort | uniq -c | sort -rn | head -6 | sed 's/^/          /' >> "$R"; }
}

# The native log's ERR lines outside the known set, one per line (nothing = clean). Known: the harness's own sandbox
# copy, three far-world hooks not ported to this game build, the guest's guarded reads, and the hook library's
# trapped thread. That last one is ONE event with three address forms: while a hook is written, the page of the
# patched bytes and the page of its trampoline are read/write for an instant; a thread running there gets an execute
# fault (kind 0x8) and is retried by the library. Forms: a system DLL's page (at 0x7FF...), the trampoline pages
# right below the exe (at 0x13FFE.... / 0x13FFF....), and the game's own page that is being patched (at exe+0x...;
# 13 logs since tcB69, always the UI time scale hook's page). The exe form is only known when one of the five lines
# before or after it (the fault is logged at once, the hook's own line when it is in) names a hook in the same
# 4 KB page: an execute fault in game code with no hook being written is real.
unknown_errs() {  # native log
  awk -v KNOWN='Session2 save redirect: (cannot copy|CDMP_SANDBOX_ADD)|User2Stages: [DSF], .* REFUSED, not hooked|GuestXVeto: unsupported game build|GuestS3 first-chance AV at cdmp.asi|GuestS3 first-chance AV at 0x(7FF|13FF[EF])[0-9A-F]+ kind 0x8' '
    { L[NR] = $0 }
    END {
      for (n = 1; n <= NR; n++) {
        line = L[n]
        if (line !~ /\] ERR /) continue
        known = (line ~ KNOWN)
        if (!known && match(line, /GuestS3 first-chance AV at exe\+0x([0-9A-F]+) kind 0x8/, m) && length(m[1]) > 3) {
          page = "exe\\+0x" substr(m[1], 1, length(m[1]) - 3) "[0-9A-F][0-9A-F][0-9A-F]([^0-9A-F]|$)"
          for (k = n - 5; k <= n + 5; k++)
            if (k >= 1 && k <= NR && k != n && L[k] !~ /\] ERR / && L[k] ~ /hook/ && L[k] ~ page) known = 1
        }
        if (!known) print line
      }
    }
  ' "$1" 2>/dev/null
}
# Warning kinds (tools/wrn-kinds.sh) that tools/known-warnings.txt does not list. A new warning in a run that passes
# is a finding: the overlay watcher's false trigger sat in a passing step's log for three hours (2026-11-12).
# $WRN_ALLOW: a pattern a step allows for itself (latehook: the watcher's own line). [dev] lines are test switches.
new_wrn() {  # who, logs...
  local who=$1; shift
  local n; n=$(bash "$N/../../tools/wrn-kinds.sh" "$@" | grep -v '\[dev\]' | grep -vE "${WRN_ALLOW:-^this never matches$}" | sort -u |
               grep -vxFf <(tr -d '\r' < "$N/../../tools/known-warnings.txt" | sed -n 's/ ## .*//p'))
  if [ -z "$n" ]; then ok "$who: no warning kind that has not been seen before"
  else bad "$who: warning kind(s) never seen before (look, then add to tools/known-warnings.txt with the reason):"; echo "$n" | head -6 | sed 's/^/          /' >> "$R"; fi
}
two_player_checks() {  # [round]: "2" = the harness's second round of restarts (-SecondRound)
  local O=$N/$NAME.out T=${1:-}
  has "$O" "^host in the shared world$T: True"   "host reached the shared world${T:+ (round $T)}"
  has "$O" "^guest in the shared world$T: True"  "guest reached the shared world${T:+ (round $T)}"
  has "$O" "^host alive=True guest alive=True" "both games still running at the end"
  has "$O" "^SAVES OK"                         "the player's saves untouched (SAVES OK)"
  has "$O" "user install restored: 5/5 OK"     "the player's install put back (5/5)"
  for r in host guest; do
    local L=$N/$NAME.coopplus.$r.log
    overlay_checks "$L" "$r"
    atleast "$L" "Net: connected" "remote states" 100 "$r: linked to the friend"
    hasnot "$L" "Prologue: we are in the prologue" "$r: no solo-prologue hold"
    hasnot "$L" "shared loot is ON but"            "$r: no misleading loot warning"
    # ("first-chance AV at 0x7FF... kind 0x8" = a thread ran into a system DLL's page in the instant a hook was being
    # written into it; the hook library traps such threads on purpose and lets them go on. Seen since tc51.
    # "at 0x13FFE.... / 0x13FFF.... kind 0x8" is the same event in the hook library's own trampoline pages right
    # below the exe: safetyhook's trap_threads makes the 'to' page read/write too while it writes a hook, a thread
    # running an earlier hook's trampoline there faults and is retried. 10 logs since tc29, the game went on in all.)
    # native log errors: the known ones (the harness's own sandbox copy, three far-world hooks not ported to this
    # game build, the guest's guarded reads) are counted; any other is a FAIL.
    no_recording $N/$NAME.$r.log "$r"
    growth $N/$NAME.$r.log "$r native" 420
    growth "$L" "$r layer" 240
    new_wrn "$r" "$L" $N/$NAME.coopplus.$r.before.log $N/$NAME.$r.log
    local e; e=$(unknown_errs $N/$NAME.$r.log | grep -c .)
    [ "${e:-0}" -eq 0 ] && ok "$r: native log: no error line outside the known set" || { bad "$r: native log: $e unknown error line(s):"; unknown_errs $N/$NAME.$r.log | cut -d']' -f3- | cut -c1-170 | sort | uniq -c | head -5 | sed 's/^/          /' >> "$R"; }
  done
  info "screenshots to LOOK AT: $(cd $N/shots && ls $NAME-*.png 2>/dev/null | tr '\n' ' ')"
}

run2() {  # the two-copy harness with $1 = PSARGS   (JUDGE_ONLY=1: no game, the checks again on the files of the last run)
  [ -n "$JUDGE_ONLY" ] && { say "  (checks run again on the existing files, no game run)"; return; }
  tasklist | grep -qi CrimsonDesert.exe && { say "  FAIL  Crimson Desert is running: step not started"; echo "GAME RUNNING"; exit 1; }
  say "  args: $1"
  ( cd $N && PSARGS="$1" bash run_keep.sh $NAME > /dev/null 2>&1 )
}

FLOW_G="w:3000_shot:$NAME-g0_k:ESC:120_w:3000_shot:$NAME-g1_k:ESC:120_w:1500"
FLOW_H="w:500_shot:$NAME-h0"
BASE="-CoopPlus -Lobby -Fresh -PackageIni $MODW -HostVariant coop-test-bandits"

case $STEP in
static)
  : > "$R"; say "RELEASE CHECKLIST RUN $TAG   package: $(basename "$PKG")"
  head_step "the package itself (no game)"
  [ -f "$PKG.zip" ] && ok "zip exists ($(du -h "$PKG.zip" | cut -f1))" || bad "zip missing: $PKG.zip"
  a=$(sha256sum "$MOD/cdmp.asi" | cut -c1-64); b=$(sha256sum $ROOT/upstream/cdmp-native/build/bin/RelWithDebInfo/cdmp.asi | cut -c1-64)
  [ "$a" = "$b" ] && ok "package cdmp.asi = the native build the harness tests" || bad "package cdmp.asi differs from the native build output (the game steps would test another file)"
  a=$(sha256sum "$MOD/coopplus.asi" | cut -c1-64); b=$(sha256sum $ROOT/upstream/cd-coop-3446/build-native-layer/bin/RelWithDebInfo/coopplus.asi | cut -c1-64)
  [ "$a" = "$b" ] && ok "package coopplus.asi = the layer build the harness tests" || bad "package coopplus.asi differs from the layer build output"
  ini=$(iconv -f UTF-16LE -t UTF-8 "$MOD/coopplus.ini")
  echo "$ini" | grep -q "^\[dev\]" && bad "package coopplus.ini has a [dev] section" || ok "package coopplus.ini: no [dev] section"
  echo "$ini" | grep -qE "^(onboarded|experimental_asked)=" && bad "package coopplus.ini carries first-launch marks" || ok "package coopplus.ini: a first-time player's (no first-launch marks)"
  echo "$ini" | grep -q "^transport=steam" && ok "package coopplus.ini: transport=steam" || bad "package coopplus.ini: transport is not steam"
  t=$($ROOT/upstream/cd-coop-3446/build-native-layer/bin/RelWithDebInfo/cdmp_tests.exe 2>&1 | tail -1 | tr -d '\r')
  [ "$t" = "All tests passed" ] && ok "layer unit tests: $t" || bad "layer unit tests: $t"
  # native unit tests: 31 of them still carry fixtures of the older game version and fail (docs/tests/
  # native-unit-baseline.txt, the same set since 2026-11-09); only a test OUTSIDE that list failing is a finding.
  nt=0; nnew=""; for t in $ROOT/upstream/cdmp-native/build/bin/RelWithDebInfo/cdmp_tests*.exe; do
    b=$(basename "$t" .exe); nt=$((nt+1)); "$t" > /dev/null 2>&1 || { grep -qx "$b" $ROOT/docs/tests/native-unit-baseline.txt || nnew="$nnew $b"; }
  done 2>/dev/null
  [ -z "$nnew" ] && ok "native unit tests: $nt run, no failure outside the known old list ($(wc -l < $ROOT/docs/tests/native-unit-baseline.txt) old-fixture tests)" || bad "native unit tests: NEW failures:$nnew"
  o=$(cd $ROOT && powershell -NoProfile -ExecutionPolicy Bypass -File tools/test-install-fake.ps1 -Zip "$(cygpath -w "$PKG.zip")" 2>&1 | tr -d '\r')
  echo "$o" | grep -q "All steps passed" && ok "install / uninstall / vanilla on a fake game folder: All steps passed" || { bad "test-install-fake: $(echo "$o" | tail -2 | tr '\n' ' ')"; }
  ( cd $ROOT && powershell -NoProfile -ExecutionPolicy Bypass -File tools/test-diagnose-redact.ps1 > /dev/null 2>&1 ) && ok "diagnose report: names and paths are redacted (test-diagnose-redact)" || bad "test-diagnose-redact failed"
  ( cd $ROOT/upstream/cdmp-native && [ -z "$(git status --porcelain -- src)" ] ) && ok "native source: committed (HEAD $(cd $ROOT/upstream/cdmp-native && git log --format=%h -1))" || bad "native source has uncommitted changes"
  ( cd $ROOT && [ -z "$(git status --porcelain -- upstream/cd-coop-3446/src kit tools)" ] ) && ok "layer / kit / tools: committed (HEAD $(cd $ROOT && git log --format=%h -1))" || bad "layer / kit / tools have uncommitted changes"
  ;;
real)
  head_step "the package as a REAL install on this PC, started as Steam starts it, held at the title"
  ( cd $N && bash realtitle.sh $NAME "$MOD" 60 > /dev/null 2>&1 )
  O=$N/$NAME.out
  has "$O" "user install restored: 5/5 OK" "the player's install put back (5/5)"
  has "$O" "^REAL SAVES UNCHANGED"          "the real save folder unchanged"
  overlay_checks "$N/$NAME.real.coopplus.log" "real install"
  has "$N/$NAME.real.coopplus.log" "Room: hosting" "real install: the Steam room opened (title Co-op menu can invite)"
  no_recording "$N/$NAME.real.cdmp.log" "real install (solo mode at the title)"
  has "$N/$NAME.real.cdmp.log" "not recording packets to disk" "real install: the base says it does not record"
  growth "$N/$NAME.real.cdmp.log" "real install native" 420
  info "screenshots to LOOK AT: $(cd $N/shots && ls $NAME-*.png 2>/dev/null | tr '\n' ' ')"
  ;;
fresh)
  head_step "two first-time players, the package's own settings, title -> shared world"
  run2 "$BASE -Hold 15 -GuestKeys $FLOW_G -HostKeys $FLOW_H"; two_player_checks ;;
xess)
  head_step "the same with Intel XeSS frame generation"
  run2 "$BASE -FrameGen -FrameGenMode Intel_XeSS_Frame_Generation -Upscale Intel_XeSS_3.0 -Hold 15 -GuestKeys $FLOW_G -HostKeys $FLOW_H"; two_player_checks
  has $N/$NAME.coopplus.host.log "frame generation \(XeSS-FG\): its swap chain class is hooked" "host: the XeSS-FG class is hooked" ;;
fsr)
  head_step "the same with AMD FSR frame generation (on this Nvidia card)"
  run2 "$BASE -FrameGen -FrameGenMode AMD_FSR_Frame_Generation -Hold 15 -GuestKeys $FLOW_G -HostKeys $FLOW_H"; two_player_checks
  has $N/$NAME.coopplus.host.log "frame generation \(FSR-FG\): its swap chain class is hooked" "host: the FSR-FG class is hooked" ;;
latehook)
  head_step "no frame through Streamline's class (as on a tester's AMD card): the watcher hooks the class a new swap chain gets, the menu draws"
  WRN_ALLOW='^Overlay: N batches of drawing commands in N s and no frame'
  run2 "$BASE -HostDev overlay_no_streamline=1 -GuestDev overlay_no_streamline=1 -WaitShots -Hold 15 -GuestKeys $FLOW_G -HostKeys $FLOW_H"; two_player_checks
  WRN_ALLOW=
  for r in host guest; do
    for f in $N/$NAME.coopplus.$r.before.log $N/$NAME.coopplus.$r.log; do
      has $f "overlay_no_streamline=1: Streamline's swap chain class is NOT hooked" "$r ($(basename $f | sed 's/.*coopplus.//')): Streamline's class was left unhooked (the test's premise)"
      has $f "batches of drawing commands in [0-9.]+ s and no frame through a hooked swap chain class" "$r: the watcher saw drawing without frames"
      has $f "that swap chain class is hooked: the overlay draws through it" "$r: the class a new swap chain gets was hooked"
      has $f "Overlay ready:" "$r: the overlay drew after that"
    done
  done
  info "seconds from the hooks to the late hook (host, co-op launch): $(grep -h 'Late init: game is up\|batches of drawing' $N/$NAME.coopplus.host.log | cut -c2-13 | tr '\n' ' ')"
  info "LOOK AT: $NAME-Wguest* (the waiting screen must be drawn on the guest), $NAME-L1-host-start-coop (the menu)" ;;
display)
  head_step "the player's own display mode and graphics, frame generation on, Steam lobby, title only"
  run2 "-CoopPlus -Lobby -Fresh -KeepDisplay -FrameGen -HostTransport steam -TitleHold 45"
  O=$N/$NAME.out
  has "$O" "^SAVES OK" "the player's saves untouched (SAVES OK)"; has "$O" "user install restored: 5/5 OK" "the player's install put back (5/5)"
  overlay_checks "$N/$NAME.coopplus.host.log" "host"
  info "screenshots to LOOK AT: $(cd $N/shots && ls $NAME-*.png 2>/dev/null | tr '\n' ' ')" ;;
lowsave)
  head_step "a guest on a new save (1 mission done, its save 1.3 km away) joins the host"
  # the guest also opens its map (M): the new save's map must show what the HOST has explored (fog share)
  run2 "-CoopPlus -Lobby -Fresh -PackageIni $MODW -HostVariant newgame -GuestVariant newgame -Hold 15 -GuestKeys ${FLOW_G}_w:1500_k:M:120_w:4000_shot:$NAME-g2-map_k:ESC:120_w:1500 -HostKeys $FLOW_H"; two_player_checks
  has "$N/$NAME.host.log" "FogShare: the guest's map chunk [0-9]+ sent with the host's explored cells added \\([1-9][0-9]* bytes changed" "the new guest's map got the host's explored cells (fog share)"
  # What this step really is (pp4): the host's co-op world persists, so only the GUEST is on the new save, and that
  # save's place is 1.3 km from the host. The guest must be brought to the host's side by itself.
  L=$N/$NAME.coopplus.guest.log
  has "$L" "Co-op: joined: our save's place was [0-9]+ m from the host: moved to its side" "guest: started far away and was moved to the host's side"
  near=$(grep "Net: connected" "$L" | tail -1 | sed -n 's/.*last remote (\([-0-9.]*\), [-0-9.]*, \([-0-9.]*\)).*/\1 \2/p')
  mine=$(grep "Local: pos" "$L" | tail -1 | sed -n 's/.*pos (\([-0-9.]*\), [-0-9.]*, \([-0-9.]*\)).*/\1 \2/p')
  d=$(echo "$near $mine" | awk 'NF==4{printf "%d", sqrt(($1-$3)^2 + ($2-$4)^2)}')
  [ -n "$d" ] && [ "$d" -le 30 ] && ok "guest: $d m from the host at the end" || bad "guest: ${d:-?} m from the host at the end (wanted within 30 m)"
  has "$N/$NAME.coopplus.guest.log" "Teleport: [0-9]+ m: the game's own travel" "guest: the join's move was the game's own travel (not hops)"
  hasnot "$N/$NAME.coopplus.guest.log" "far travel did not take us there|back where we started" "guest: the travel took at the first try" ;;
chest)
  head_step "chest: the host opens it, the guest is away, comes back and gets its own share"
  A="w:800_shot:$NAME-h0_kd:E_w:2800_ku:E_w:3600_shot:$NAME-h1_w:10500"
  run2 "$BASE -Hold 10 -HostScript $SCAN\\tp_chest4_host.txt -GuestScript $SCAN\\chest_far_guest.txt -HostWalkTo -10347.8_-3991.8_-10347.8_-3991.8 -AfterWalkKeys $A"; two_player_checks
  H=$N/$NAME.host.log
  has "$H" "Lootr: chest 0x[0-9A-F]+ at .* opened by 0xA0100001: [0-9]+ item kind" "chest opened by the host, a share kept for the guest"
  has "$H" "the share waits for 0xA0100066"                                        "the share waited while the guest was away"
  has "$H" "0xA0100066 is there and got its own share .* its bag gained [1-9]"     "the guest got its share at the chest (bag grew)"
  has "$N/$NAME.coopplus.guest.log" "Your friend opened a chest"                   "the guest was told (notice)" ;;
revive)
  head_step "going down: death screen, input held, Q revive (the guest)"
  GA=""; for i in $(seq 1 60); do GA="${GA}click:L:90_w:900_"; done
  G="w:9000_shot:$NAME-g0_${GA}shot:$NAME-g1_kd:W_w:2500_ku:W_shot:$NAME-g2_k:Q:300_w:8000_shot:$NAME-g3_kd:W_w:1500_ku:W_w:500_shot:$NAME-g4"
  run2 "$BASE -Hold 10 -HostScript $SCAN\\host_kills_guest.txt -GuestScript $SCAN\\tp_dock_guest.txt -GuestKeys $G"; two_player_checks
  L=$N/$NAME.coopplus.guest.log
  has "$L" "Input: the game's own input held \(we are down\)" "guest: input held while down"
  has "$L" "our death notice goes to the client"              "guest: the death reaches the client (the character lies down)"
  has "$L" "game-over state is skipped"                       "guest: the game's own game-over state is skipped (HUD kept)"
  has "$L" "Revive: instant revive asked"                     "guest: Q asked for the instant revive"
  has "$L" "Revive: we are alive again"                       "guest: alive again"
  has "$L" "input back \(up again\)"                          "guest: input back" ;;
loot)
  head_step "enemy loot: the host kills ten bandits and loots the bodies, the guest nearby gets its own share"
  # 2026-11-11: reworked twice. (1) The old step needed an incidental hit to trigger a test cheat (failed twice, pp4).
  # Now the host waits at its start until the guest stands at the dock, goes there, and its script kills the nearest
  # bandit (KillCheatReq) and asks for that body's loot (the game's own request). (2) What a body gives is a ROLL:
  # most requests take nothing ("pool 4 -> 4" in the probe), about one in six takes an item, and the friend's share
  # exists only when the looter's bag gained something (t5: 0 of 2, t6: 0 of 6, t7: 1 of 6, lt1: 1 of 2). So: ten
  # bodies, each asked twice. If still no body gave anything, the FAIL line says so: run the step once more.
  { printf 'wait 10000\nwait 10000\nwait 5000\ntp -10239 546 -3957\nwait 3000\n'
    for i in 1 2 3 4 5 6 7 8 9 10; do printf 'killnear 15\nwait 3000\nlootlast\nwait 1500\nlootlast\nwait 1500\n'; done; } > $N/scan/loot_host.txt
  run2 "$BASE -Hold 10 -HostNativeExtra lootr_probe=1 -HostScript $SCAN\\loot_host.txt -GuestScript $SCAN\\tp_dock_guest.txt -GuestKeys w:2000_shot:$NAME-g0 -HostKeys w:40000_shot:$NAME-h0"; two_player_checks
  H=$N/$NAME.host.log
  has "$N/$NAME.coopplus.host.log" "killnear: KillCheatReq on 0x" "the host's script killed a bandit"
  has "$N/$NAME.coopplus.host.log" "lootlast: the body 0x[0-9A-F]+ looted" "the host's script asked for that body's loot"
  has "$H" "Lootr probe: body loot by 0xA0100001 of body 0x[0-9A-F]+ .* err 0" "the server took the host's loot requests (err 0)"
  took=$(grep -c "Lootr: body 0x[0-9A-F]* at .* looted by 0xA0100001" "$H" 2>/dev/null)
  asked=$(grep -c "Lootr probe: body loot by 0xA0100001" "$H" 2>/dev/null)
  if [ "${took:-0}" -gt 0 ]; then
    ok "$took of $asked loot requests gave the host an item, a share kept each time"
    has "$H" "0xA0100066 is there and got its own share .* its bag gained [1-9]" "the guest got its own share (bag grew)"
  else
    bad "none of $asked loot requests gave the host an item (a roll, about one in six): the share was not exercised; run this step again"
  fi ;;
crime)
  head_step "fight at the bandit camp: guest hits count and are no crime, host fights too"
  GA=""; for i in $(seq 1 12); do GA="${GA}click:L:90_w:350_kd:E_w:1200_ku:E_w:500_"; done
  HA=""; for i in $(seq 1 6); do HA="${HA}kd:E_w:2200_ku:E_w:1200_click:L:90_w:300_"; done
  run2 "$BASE -Hold 10 -HostScript $SCAN\\tp_dock_host.txt -GuestScript $SCAN\\tp_dock_guest.txt -GuestKeys w:2000_shot:$NAME-g0_${GA}shot:$NAME-g1 -HostKeys w:1000_shot:$NAME-h0_${HA}shot:$NAME-h1 -GuestAfterWalkKeys w:1000_shot:$NAME-g2_${GA}shot:$NAME-g3"; two_player_checks
  H=$N/$NAME.host.log
  has "$H" "AntiGrief: crime type [0-9]+ of the guest .* skipped" "a guest's hit on a bandit was not booked as a crime"
  # Only the hit and kill paths: the same function also gets the game's property crimes (type 1 Vandalism: a swing
  # broke something that belongs to an NPC; the "victim" is the owner). Those are the game's own rule for any player
  # (the host gets them too, tcB384) and are left alone on purpose; they are listed, not failed.
  hasnot "$H" "AntiGrief: crime type [0-9]+ of the guest .* on 0xB[0-9A-F]+ from exe\\+0x[0-9A-F]+ \\(a (hit|kill)\\).*(left to the game|punished too)" "no guest hit on an NPC at the bandit camp was booked as a crime"
  info "property crimes of the guest with an owner (the game's own rule, not a hit): $(grep -cE 'AntiGrief: crime type [0-9]+ of the guest .* on 0xB[0-9A-F]+ from exe\+0x[0-9A-F]+ \(another path\)' "$H")"
  info "AntiGrief last summary: $(grep -h 'AntiGrief' "$H" | grep -v 'crime type' | tail -1 | cut -d']' -f3- | cut -c1-260)" ;;
farfight)
  head_step "a guest fights 108 m from the host: the host draws its bandits to the dock and leaves, the guest stays and swings"
  GA=""; for i in $(seq 1 50); do GA="${GA}click:L:90_w:800_"; done
  run2 "$BASE -Hold 10 -HostScript $SCAN\\ff_host_lure.txt -GuestScript $SCAN\\ff_guest_watch40.txt -GuestKeys w:9000_shot:$NAME-g0_${GA}shot:$NAME-g1_w:20000_shot:$NAME-g2 -HostKeys w:500_shot:$NAME-h0"; two_player_checks
  H=$N/$NAME.host.log
  has "$H" "DamageRange ON" "host: the damage range hook is in"
  has "$H" "DamageRange: on .* counted because another player was in range [1-9]" "hits far from the host counted because the guest was in range"
  has "$H" "kills host [0-9]+ guest [1-9]" "the guest killed at least one enemy far from the host"
  hurt=$(grep "Local:.*hp" $N/$NAME.coopplus.guest.log 2>/dev/null | grep -vc "hp 300.0/300.0")
  [ "${hurt:-0}" -gt 0 ] && ok "the guest lost health far from the host ($hurt samples below full)" || bad "the guest never lost health far from the host"
  ;;
farfight2)
  head_step "a guest fights 484 m from the host, in another direction: the host draws its bandits, then goes to Hernand"
  # 2026-11-12 (of1-of3): the first far-fight fix read two registers of the wrong actor and still passed the 108 m
  # step, because the host stood 36 m away along one map axis. This placement differs along both axes.
  printf 'tp -10239 546 -3957\nwait 9000\nnpcs 40\nwait 9000\nnpcs 40\nwait 8000\nnpcs 40\ntp -10665 634.5 -3701\nwait 5000\nnpcs 30\n' > $N/scan/ff_host_lure430.txt
  GA=""; for i in $(seq 1 50); do GA="${GA}click:L:90_w:800_"; done
  run2 "$BASE -Hold 10 -HostScript $SCAN\\ff_host_lure430.txt -GuestScript $SCAN\\ff_guest_watch40.txt -GuestKeys w:9000_shot:$NAME-g0_${GA}shot:$NAME-g1_w:20000_shot:$NAME-g2 -HostKeys w:500_shot:$NAME-h0"; two_player_checks
  H=$N/$NAME.host.log
  has "$H" "DamageRange: on .* counted because another player was in range [1-9]" "hits 484 m from the host counted because the guest was in range"
  has "$H" "DamageRange: a hit [34][0-9][0-9] m from the field owner's character counts: another player is [0-9] m from the victim" "the hook's own numbers are right: the hit is 300-499 m from the host and under 10 m from a player"
  hurt=$(grep "Local:.*hp" $N/$NAME.coopplus.guest.log 2>/dev/null | grep -vc "hp 300.0/300.0")
  [ "${hurt:-0}" -gt 0 ] && ok "the guest lost health 484 m from the host ($hurt samples below full)" || bad "the guest never lost health 484 m from the host"
  ;;
newsave)
  head_step "New Co-op Save: the host starts the story in a brand-new co-op world, the guest joins it"
  # 2026-11-11 (nc5-nc7): never run on the shared world before today, and broken: the guest hung on "Waiting for Host"
  # because its login came before the new world had a save of the host's. The test co-op world is backed up and put
  # back around the run (build/native/coopworld.sh); in the TEST copy the harness answers the game's own agreement
  # and difficulty for the host.
  bk=$(cd $N && bash coopworld.sh backup 2>&1 | tail -1); say "  test co-op world: $bk"
  case "$bk" in *"backed up"*|*"backup is current"*) ;; *) bad "the test co-op world could not be backed up: step not run"; sed -n "/^== $STEP /,\$p" "$R"; exit 1 ;; esac
  run2 "-CoopPlus -Lobby -Fresh -PackageIni $MODW -HostChoice 3 -Hold 30 -GuestKeys w:3000_shot:$NAME-g0_w:15000_shot:$NAME-g1 -HostKeys w:500_shot:$NAME-h0"
  # 2026-11-12 (the owner's rule, fs1 / fs2): a New Co-op Save gives the friend a new save too, a copy of the host's
  # first REAL save (not the 5 KB stub written at world entry), and Continue of that world brings that character
  # back, whatever save the friend has of its own. So a second run continues the world this one made, the guest with
  # ANOTHER save of its own; only then the test world is put back.
  FIRST=$NAME
  NAME=${FIRST}c
  run2 "-CoopPlus -Lobby -Fresh -PackageIni $MODW -HostVariant keep -KeepSlots -GuestVariant coop-test-bandits -Hold 30 -GuestKeys w:3000_shot:$NAME-g0_w:15000_shot:$NAME-g1 -HostKeys w:500_shot:$NAME-h0"
  NAME=$FIRST
  if [ -z "$JUDGE_ONLY" ]; then
    rs=$(cd $N && bash coopworld.sh restore $NAME 2>&1 | tail -1)
    case "$rs" in *"identical to the backup"*|*"is the backup's"*) ok "the test co-op world is back ($rs)" ;; *) bad "the test co-op world: $rs" ;; esac
  fi
  two_player_checks
  H=$N/$NAME.host.log
  has "$H" "user 2's LoginUserToFieldServerReq waits" "the guest's login waited for the host's new game"
  has "$H" "this co-op world has a save of the host's now" "the wait ended when the host's first save was there"
  has "$H" "new co-op save: marker .* written \\(user2_character=pending" "new world: marked as one whose friend gets a new character"
  has "$H" "is checked and kept aside, NOT staged" "the friend's own save was checked and NOT used"
  has "$H" "only the new game's first stub" "the host's 5 KB first save was not taken"
  has "$H" "the friend gets a new character, the host's save: staged a copy of the host's own slot" "the friend's character is a copy of the host's first real save"
  hasnot "$H" "co-op world\\): FAILED" "no fall-back to the friend's own save"
  has "$H" "the staged guest slot moved in" "the friend's new save moved into the new world"
  hasnot "$H" "C2 identity: refused user 2" "the guest's character login was not refused"
  info "the seeded save: $(grep -h 'the friend gets a new character' "$H" | sed -n 's/.*\(save [0-9]* B\).*/\1/p' | head -1)"
  say "  -- Continue of that world (${NAME}c), the guest with another save of its own:"
  NAME=${FIRST}c; two_player_checks; NAME=$FIRST
  C=$N/${NAME}c.host.log
  has "$C" "marker .*user2_character=world" "Continue: the world knows its friend's character"
  has "$C" "is checked and kept aside, NOT staged" "Continue: the friend's own save was NOT used"
  has "$C" "marker world .*: (staged the newest generation of that line|no mailbox generation of that line.*: staged last session's guest slot)" "Continue: the friend came back as this world's character"
  hasnot "$C" "LOST and made anew|co-op world\\): FAILED" "Continue: the character was neither lost nor replaced"
  info "LOOK: $NAME-g1 and ${NAME}c-g1: the guest in the new game's clothes at the host's side, with the story's first objective on ITS screen" ;;
solostart)
  head_step "Start From My Solo Progress: a new co-op world is made from the host's own save, the guest joins it"
  # 2026-11-11: the second way a session begins; the harness had not taken it since 2026-11-09 (it always pressed
  # the first card, Continue Our Co-op Save). Replaces the test co-op world: backed up and put back.
  bk=$(cd $N && bash coopworld.sh backup 2>&1 | tail -1); say "  test co-op world: $bk"
  case "$bk" in *"backed up"*|*"backup is current"*) ;; *) bad "the test co-op world could not be backed up: step not run"; sed -n "/^== $STEP /,\$p" "$R"; exit 1 ;; esac
  run2 "-CoopPlus -Lobby -Fresh -PackageIni $MODW -HostVariant coop-test-bandits -HostChoice 2 -Hold 20 -GuestKeys $FLOW_G -HostKeys $FLOW_H"
  if [ -z "$JUDGE_ONLY" ]; then
    rs=$(cd $N && bash coopworld.sh restore $NAME 2>&1 | tail -1)
    case "$rs" in *"identical to the backup"*|*"is the backup's"*) ok "the test co-op world is back ($rs)" ;; *) bad "the test co-op world: $rs" ;; esac
  fi
  two_player_checks
  has "$N/$NAME.coopplus.host.before.log" "Co-op lobby: Start From My Solo Progress" "the host chose Start From My Solo Progress"
  has "$N/$NAME.host.log" "Co-op save: folder .* \\(seed copy\\)" "the co-op world was made as a copy of the host's own save"
  has "$N/$NAME.host.log" "the earlier co-op save .* set aside as" "the earlier co-op world was set aside, not deleted"
  has "$N/$NAME.host.log" "LoadSlot .* = user 2 \\| slot [0-9]+ -> REMAP 204" "the guest's lobby was loaded from its own slot"
  hasnot "$N/$NAME.host.log" "C2 identity: refused user 2" "the guest's character login was not refused" ;;
mounts)
  head_step "horses: each player calls its horse, mounts and rides; each sees the other mounted"
  # 2026-11-11 (mr1-mr4). The horse is called with a pad: D-pad Up held, right stick down, Up released (a simulated
  # pad, dev key pad_sim; its two spare bits are the sticks). The script then goes to the horse (tphorse), presses E
  # (Ride) and rides forward. Screenshots g0-g2 are the guest's view (both mounted), h0 the host's.
  printf 'pad 0001 700\npad 0401 1300\npad 0400 400\nwait 5000\ntphorse\nwait 2500\nkey 45 200\nwait 5000\nkey 57 3000\nwait 2000\n' > $N/scan/mount_ride.txt
  run2 "$BASE -Hold 15 -HostDev pad_sim=1 -GuestDev pad_sim=1 -HostScript $SCAN\\mount_ride.txt -GuestScript $SCAN\\mount_ride.txt -GuestKeys w:14000_shot:$NAME-g0_w:3000_shot:$NAME-g1_w:4000_shot:$NAME-g2 -HostKeys w:500_shot:$NAME-h0"; two_player_checks
  for r in host guest; do
    has "$N/$NAME.coopplus.$r.log" "Dev script: teleport beside actor 0xB[0-9A-F]+: \\(" "$r: its called horse was there (the script went to it)"
    has "$N/$NAME.$r.log" "MountSync summary .* published Begin [1-9][0-9]* Commit [1-9]" "$r: mounted (its mount was published to the other player)"
  done ;;
mountfight)
  head_step "a mounted guest fights: it mounts at the dock while the bandits sleep, the host arrives and wakes them"
  # 2026-11-12 (mc1-mc4). The test's teleport does not move a mounted rider, so the guest mounts where the enemies
  # are before the host (whose quest they belong to) comes. Screenshots g1 / g2: the guest in the saddle in the fight.
  { printf 'tp -10243 546 -3955\nwait 2500\npad 0001 700\npad 0401 1300\npad 0400 400\nwait 5000\ntphorse\nwait 2500\nkey 45 200\nwait 5000\nnpcs 30\n'; for i in 1 2 3 4 5 6; do printf 'wait 9000\nnpcs 30\n'; done; } > $N/scan/mc4_guest.txt
  { printf 'wait 10000\nwait 10000\nwait 10000\nwait 10000\nwait 5000\ntp -10239 546 -3957\nwait 3000\nnpcs 30\n'; for i in 1 2 3 4 5 6; do printf 'wait 9000\nnpcs 30\n'; done; } > $N/scan/mc4_host.txt
  GA=""; for i in $(seq 1 40); do GA="${GA}click:L:90_w:800_"; done
  run2 "$BASE -Hold 10 -GuestDev pad_sim=1 -HostScript $SCAN\\mc4_host.txt -GuestScript $SCAN\\mc4_guest.txt -GuestKeys w:19000_shot:$NAME-g0_w:9000_shot:$NAME-g1_${GA}shot:$NAME-g2_w:2000_shot:$NAME-g3 -HostKeys shot:$NAME-h0"; two_player_checks
  H=$N/$NAME.host.log
  has "$N/$NAME.coopplus.guest.log" "Dev script: teleport beside actor 0xB[0-9A-F]+: \\(" "guest: its called horse was there"
  has "$N/$NAME.guest.log" "MountSync summary .* published Begin [1-9][0-9]* Commit [1-9]" "guest: mounted (its mount was published to the host)"
  has "$H" "blows from the guest's client: guest>NPC [1-9]" "the mounted guest's blows on enemies were seen"
  hurt=$(grep "Local:.*hp" $N/$NAME.coopplus.guest.log 2>/dev/null | grep -vc "hp 300.0/300.0")
  [ "${hurt:-0}" -gt 0 ] && ok "the mounted guest lost health ($hurt samples below full)" || bad "the mounted guest never lost health (not exercised: were the enemies there?)"
  info "LOOK: $NAME-g1 / -g2 must show the guest IN THE SADDLE (prompt Dismount) with enemies around; the logs cannot tell mounted from on foot"
  ;;
nosave)
  head_step "a guest who has never played (no save at all) tries to join"
  # 2026-11-11/12 (gn1-gn5): such a guest waited on 'Waiting for Host' for ever. Now its game goes back to solo by
  # itself with the instruction on the title menu, and the host is told. (The harness stops the run there.)
  run2 "$BASE -GuestVariant none -ExpectNoSave"
  O=$N/$NAME.out
  has "$O" "^SAVES OK"                         "the player's saves untouched (SAVES OK)"
  has "$O" "user install restored: 5/5 OK"     "the player's install put back (5/5)"
  has "$O" "^lobby flow done \\(the guest has no save\\)" "the harness saw the guest's game say it has no save"
  has "$N/$NAME.coopplus.guest.before.log" "this player has no save of their own: co-op cannot start for them yet" "guest: its game found it has no save and stopped the join"
  has "$N/$NAME.coopplus.guest.log" "back in solo after a join without a save of our own: the menu says what to do" "guest: back at the solo title with the instruction"
  has "$N/$NAME.coopplus.host.log" "has no save of their own: co-op cannot start with them yet" "host: was told why its friend left"
  hasnot "$N/$NAME.coopplus.host.log" "in the shared world" "host: did not load into its world alone"
  info "LOOK: $NAME-nosave-guest (the title with 'You have no character yet...') and $NAME-nosave-host (the reason on the host's menu)"
  ;;
retry)
  head_step "a second Start Co-op in the same launch: both games restart again (the guest is already a guest), both reach the world"
  # rt1a / rt1b (2026-11-12): the host's tunnel took the guest's old process for a conversation and was dead for the
  # whole launch. [dev] lobby_retry=1: the host's first automatic start is a second Start Co-op instead.
  run2 "$BASE -HostDev lobby_retry=1 -WaitShots -Hold 10 -GuestKeys $FLOW_G -HostKeys $FLOW_H"; two_player_checks
  has "$N/$NAME.coopplus.host.before.log" "lobby_retry: instead of starting, a second Start Co-op" "host: the second Start Co-op ran (the test's premise)"
  has "$N/$NAME.coopplus.guest.before.log" "the host is back in co-op: this game restarts as its guest" "guest: restarted again although it already was a guest"
  has "$N/$NAME.host.log" "the guest's connection was replaced by a new one before anything was said on it" "host: the guest's old connection was replaced, the tunnel goes on"
  hasnot "$N/$NAME.host.log" "the Troc tunnel stops" "host: the tunnel was never stopped"
  ;;
stuck)
  head_step "a guest that is not let in: both are told; the host leaves the session and starts again; both reach the world"
  # rt2a / rt3a / rt3b (2026-11-12). [dev] lobby_skip_play=1: the guest does not press Play (as when the host's game
  # never answers). [dev] lobby_recover=1: the host answers its card with Leave the session and a new Start Co-op.
  run2 "$BASE -GuestDev lobby_skip_play=1 -HostDev lobby_recover=1 -SecondRound -WaitShots -Hold 10 -GuestKeys $FLOW_G -HostKeys $FLOW_H"; two_player_checks 2
  HB=$N/$NAME.coopplus.host.before.log; GB=$N/$NAME.coopplus.guest.before.log
  has "$N/$NAME.out" "^guest in the shared world: False" "round 1: the guest did not get in (the test's premise)"
  has "$GB" "Play was pressed [0-9]+ s ago and the host's world has not come: the host is told" "guest: said it is stuck"
  has "$HB" "pressed Play [0-9]+ s ago and is still not in our world" "host: was told"
  has "$HB" "Leave: the friend never got into our world: goodbye sent, back to the title now" "host: Leave went back to the title at once"
  has "$GB" "the host ended the session while we were at the title: back to the menu, still in the party" "guest: stayed in the party at its menu"
  has "$HB" "Native mode: restarting the game \\(the host starts co-op\\)" "host: the second Start Co-op restarted the game (it had been in a world)"
  has "$GB" "the host is back in co-op: this game restarts as its guest" "guest: restarted for the second start"
  hasnot "$N/$NAME.host.log" "the Troc tunnel stops" "host: the tunnel was never stopped"
  info "LOOK: $NAME-g* / $NAME-Wguest4* (the guest's 'has not let you in yet' screen), $NAME-R0-guest-after-leave and $NAME-R0-host-after-leave (both menus after the Leave, the notes readable), $NAME-L3-*2* / L5 (both in the world)"
  ;;
mismatch)
  head_step "two different builds: both title menus say so and nothing starts"
  # mm1a (2026-11-12). Two testers played a whole session with one build each and were told nothing: the handshake
  # compared a number frozen at CMake's last configure. Now a hash of the installed files; [dev] build_id gives the
  # guest another one.
  run2 "$BASE -GuestDev build_id=11112222 -ExpectMismatch"
  O=$N/$NAME.out
  has "$O" "^SAVES OK"                         "the player's saves untouched (SAVES OK)"
  has "$O" "user install restored: 5/5 OK"     "the player's install put back (5/5)"
  has "$O" "^lobby flow done \\(two builds" "the harness ended at the title (no start)"
  for r in host guest; do
    has "$N/$NAME.coopplus.$r.log" "Build for the handshake: [0-9A-F]{8}" "$r: its build is a hash of its installed files"
    has "$N/$NAME.coopplus.$r.log" "Co-op lobby: Can't play together: .* has another Co-op\\+ build" "$r: its title menu's line says the friend has another build"
    hasnot "$N/$NAME.coopplus.$r.log" "in the shared world" "$r: nothing started"
  done
  info "LOOK: $NAME-mismatch-host and $NAME-mismatch-guest: the bottom line of both menus, Start Co-op greyed on the host" ;;
hostrevive)
  head_step "the host goes down beside the guest and revives at the checkpoint (its world reloads with the guest in it)"
  # tg2a (2026-11-12). A tester's host crashed here: a schedule timer ran while the reloading owner had no manager.
  # The guard puts such a timer off and runs it when the owner is back. The race itself is rare (313 reloads before it
  # hit); this step proves the reload with a guest attached and that the timers go on afterwards.
  printf 'wait 3000\ntp -10239 546 -3957\nwait 2000\n' > $N/scan/tg_host_dock.txt
  run2 "$BASE -Hold 20 -HostScript $SCAN\\tg_host_dock.txt -GuestScript $SCAN\\tp_dock_guest.txt -HostKeys w:100000_shot:$NAME-h0_k:U:300_w:12000_shot:$NAME-h1_w:25000_shot:$NAME-h2 -GuestKeys w:3000_shot:$NAME-g0_w:120000_shot:$NAME-g1"; two_player_checks
  L=$N/$NAME.coopplus.host.log; H=$N/$NAME.host.log
  has "$L" "our death notice goes to the client" "host: went down"
  has "$L" "Revive: checkpoint revive, step 3" "host: asked for the checkpoint revive"
  has "$L" "Revive: at the checkpoint" "host: stands at the checkpoint again"
  has "$L" "Teleport: [0-9]+ m: the game's own travel" "host: moved by the game's own travel"
  has "$H" "reason 19" "host: its stages closed 'at the owner's load' (the reload happened with the guest in the world)"
  hasnot "$H" "the Troc tunnel stops" "host: the tunnel was never stopped"
  asked=$(grep -h 'timer guard: schedule timers' "$H" | tail -1 | sed -n 's/.*asked \([0-9]*\).*/\1/p')
  [ "${asked:-0}" -ge 500 ] && ok "host: schedule timers went on to the end (asked $asked)" || bad "host: schedule timers asked ${asked:-none} (wanted >= 500: about 220 a minute)"
  hasnot "$H" "timer guard: schedule timer .* given up" "host: no timer was given up"
  info "timer guard: $(grep -h 'timer guard: schedule timers' "$H" | tail -1 | sed 's/.*timer guard: //' | cut -c1-200)"
  info "LOOK: $NAME-h0 (the death screen with both choices), $NAME-h1 (the game's own warp), $NAME-h2 (at the checkpoint)" ;;
summary)
  cat "$R"; echo; echo "TOTAL: $(grep -c '^  PASS' "$R") pass, $(grep -c '^  FAIL' "$R") fail, steps run: $(grep -c '^== ' "$R") (23 in the list)"; exit 0 ;;
*) echo "unknown step $STEP"; exit 2 ;;
esac
sed -n "/^== $STEP /,\$p" "$R"
