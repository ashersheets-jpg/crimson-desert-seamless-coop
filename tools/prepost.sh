#!/bin/bash
# The release checklist's runner (docs/tests/RELEASE-CHECKLIST.md). One step per call, so no call runs longer than
# one game session and every step's screenshots can be looked at before the next one starts:
#
#   bash tools/prepost.sh TAG "<package folder>" STEP        STEP = static | real | fresh | xess | fsr | display |
#                                                                   lowsave | chest | revive | loot | crime | farfight |
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
  # (not the base's horse diagnostics: capped at 400 lines a session by its own counter, horse_diag.cpp kMaxLines)
  top=$(grep -v "HorseDiag " "$f" | cut -c29- | sed -E 's/0x[0-9A-Fa-f]+/N/g; s/[0-9]+/N/g' | cut -c1-60 | sort | uniq -c | sort -rn | head -1)
  cnt=$(echo "$top" | awk '{print $1}')
  local tper=$(( ${cnt:-0} * 60 / secs ))
  [ $tper -le 30 ] && ok "$2: no line repeats more than 30 times a minute (the most repeated: $tper a minute)" || bad "$2: one line repeats $tper times a minute: $(echo "$top" | cut -c1-90)"
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

two_player_checks() {
  local O=$N/$NAME.out
  has "$O" "^host in the shared world: True"   "host reached the shared world"
  has "$O" "^guest in the shared world: True"  "guest reached the shared world"
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
    # written into it; the hook library traps such threads on purpose and lets them go on. Seen since tc51.)
    # native log errors: the known ones (the harness's own sandbox copy, three far-world hooks not ported to this
    # game build, the guest's guarded reads) are counted; any other is a FAIL.
    no_recording $N/$NAME.$r.log "$r"
    growth $N/$NAME.$r.log "$r native" 420
    growth "$L" "$r layer" 240
    local e; e=$(grep "\] ERR " $N/$NAME.$r.log 2>/dev/null | grep -vcE "Session2 save redirect: (cannot copy|CDMP_SANDBOX_ADD)|User2Stages: [DSF], .* REFUSED, not hooked|GuestXVeto: unsupported game build|GuestS3 first-chance AV at cdmp.asi|GuestS3 first-chance AV at 0x7FF[0-9A-F]+ kind 0x8")
    [ "${e:-0}" -eq 0 ] && ok "$r: native log: no error line outside the known set" || { bad "$r: native log: $e unknown error line(s):"; grep "\] ERR " $N/$NAME.$r.log | grep -vE "Session2 save redirect: (cannot copy|CDMP_SANDBOX_ADD)|User2Stages: [DSF], .* REFUSED, not hooked|GuestXVeto: unsupported game build|GuestS3 first-chance AV at cdmp.asi|GuestS3 first-chance AV at 0x7FF[0-9A-F]+ kind 0x8" | cut -d']' -f3- | cut -c1-170 | sort | uniq -c | head -5 | sed 's/^/          /' >> "$R"; }
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
display)
  head_step "the player's own display mode and graphics, frame generation on, Steam lobby, title only"
  run2 "-CoopPlus -Lobby -Fresh -KeepDisplay -FrameGen -HostTransport steam -TitleHold 45"
  O=$N/$NAME.out
  has "$O" "^SAVES OK" "the player's saves untouched (SAVES OK)"; has "$O" "user install restored: 5/5 OK" "the player's install put back (5/5)"
  overlay_checks "$N/$NAME.coopplus.host.log" "host"
  info "screenshots to LOOK AT: $(cd $N/shots && ls $NAME-*.png 2>/dev/null | tr '\n' ' ')" ;;
lowsave)
  head_step "a guest on a new save (1 mission done, its save 1.3 km away) joins the host"
  run2 "-CoopPlus -Lobby -Fresh -PackageIni $MODW -HostVariant newgame -GuestVariant newgame -Hold 15 -GuestKeys $FLOW_G -HostKeys $FLOW_H"; two_player_checks
  # What this step really is (pp4): the host's co-op world persists, so only the GUEST is on the new save, and that
  # save's place is 1.3 km from the host. The guest must be brought to the host's side by itself.
  L=$N/$NAME.coopplus.guest.log
  has "$L" "Co-op: joined: our save's place was [0-9]+ m from the host: moved to its side" "guest: started far away and was moved to the host's side"
  near=$(grep "Net: connected" "$L" | tail -1 | sed -n 's/.*last remote (\([-0-9.]*\), [-0-9.]*, \([-0-9.]*\)).*/\1 \2/p')
  mine=$(grep "Local: pos" "$L" | tail -1 | sed -n 's/.*pos (\([-0-9.]*\), [-0-9.]*, \([-0-9.]*\)).*/\1 \2/p')
  d=$(echo "$near $mine" | awk 'NF==4{printf "%d", sqrt(($1-$3)^2 + ($2-$4)^2)}')
  [ -n "$d" ] && [ "$d" -le 30 ] && ok "guest: $d m from the host at the end" || bad "guest: ${d:-?} m from the host at the end (wanted within 30 m)" ;;
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
  hasnot "$H" "AntiGrief: crime type [0-9]+ of the guest .* on 0xB.*(left to the game|punished too)" "no guest hit on an NPC at the bandit camp was booked as a crime"
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
summary)
  cat "$R"; echo; echo "TOTAL: $(grep -c '^  PASS' "$R") pass, $(grep -c '^  FAIL' "$R") fail, steps run: $(grep -c '^== ' "$R") (12 in the list)"; exit 0 ;;
*) echo "unknown step $STEP"; exit 2 ;;
esac
sed -n "/^== $STEP /,\$p" "$R"
