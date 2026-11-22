#!/bin/bash
# Any twocopy test while the player has the PLAYTEST install in the game folder: kept aside and put back afterwards.
cd /c/Users/asher/CrimsonDesert-SeamlessCoop/build/native
BIN="/d/SteamLibrary/steamapps/common/Crimson Desert/bin64"
KEEP=/c/Users/asher/CrimsonDesert-SeamlessCoop/build/native/user-install-keep
tasklist | grep -qi CrimsonDesert.exe && { echo "CRIMSON DESERT IS RUNNING: not started"; exit 1; }
mkdir -p $KEEP
for f in cdmp.asi coopplus.asi cdmp.ini coopplus.ini winmm.dll; do cp -p "$BIN/$f" $KEEP/$f || { echo "no $f"; exit 1; }; done
( cd $KEEP && sha256sum cdmp.asi coopplus.asi cdmp.ini coopplus.ini winmm.dll > sums.txt )
: "${PSARGS:?set PSARGS}"; export PSARGS
echo "$PSARGS" > ${1:-tcB260}.args   # 2026-11-11: every run keeps its exact switches (notes, re-runs)
bash twocopy.sh ${1:-tcB260} > ${1:-tcB260}.out 2>&1
for f in cdmp.asi coopplus.asi cdmp.ini coopplus.ini winmm.dll; do cp -p $KEEP/$f "$BIN/$f"; done
( cd "$BIN" && sha256sum -c $KEEP/sums.txt ) >> ${1:-tcB260}.out 2>&1
echo "user install restored: $(cd "$BIN" && sha256sum -c $KEEP/sums.txt 2>&1 | grep -c OK)/5 OK" >> ${1:-tcB260}.out
