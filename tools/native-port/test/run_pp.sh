#!/bin/bash
# Run checklist steps of one pass on one package, one after another. Not a harness script.
#   bash run_pp.sh <pass> <package id> <step> [step ...]
cd /c/Users/asher/CrimsonDesert-SeamlessCoop || exit 1
PASS=$1; ID=$2; shift 2
PK="dist/CrimsonDesert-SeamlessCoop-0.1.0-alpha+$ID-shared-world"
N=build/native
one() {  # pass step
  bash tools/prepost.sh $1 "$PK" $2 > "$N/prepost-$1-$2.console.txt" 2>&1 < /dev/null
  local R=$N/prepost-$1.txt
  local sec; sec=$(awk -v s="== $2 " 'index($0, s) == 1 {on = 1} on && /^== / && index($0, s) != 1 {on = 0} on' "$R")
  echo "== $1 $2 done $(date +%H:%M): $(echo "$sec" | grep -c '^  PASS') pass, $(echo "$sec" | grep -c '^  FAIL') fail"
  echo "$sec" | grep '^  FAIL' | cut -c1-400
  for o in $N/$1-$2*.out; do [ -f "$o" ] && echo "   $(basename $o): $(grep -c 'SAVES OK' $o) saves-ok | $(grep -h 'user install restored' $o | tail -1)"; done
}
# 2026-11-17: a stop during newsave / solostart left the new world in place and the next steps ran in it.
W="$LOCALAPPDATA/Pearl Abyss/CD/coop_worlds"
for s in "$@"; do
  ls -d "$W"/with_127_0_0_1_old_* > /dev/null 2>&1 && { echo "STOP before $s: a test co-op world is set aside (with_127_0_0_1_old_*): run coopworld.sh restore first"; break; }
  one $PASS $s
done
echo "$PASS TOTAL: $(grep -c '^  PASS' $N/prepost-$PASS.txt) pass, $(grep -c '^  FAIL' $N/prepost-$PASS.txt) fail, steps $(grep -c '^== ' $N/prepost-$PASS.txt)"
tasklist | grep -i crimson
exit 0
