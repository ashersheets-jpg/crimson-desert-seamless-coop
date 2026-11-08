#!/bin/bash
# Lag measurement on one PC (2026-11-06): test save <VARIANT>, the relay peer echoing our own moves (PEERX: extra peer
# args, default --delay 0), [dev] lag_probe=1. We walk forward / back / left / right with held keys; the probe writes
# where we and the copy are drawn every 4 ms (bin64/cdmp_lag.csv) and tools/lag/lag.py finds the delay between the two.
# Never saves. Cleanup as always.
source /c/Users/asher/CrimsonDesert-SeamlessCoop/build/port3479/test_guard.sh
P=/c/Users/asher/CrimsonDesert-SeamlessCoop/build/port3460; PW='C:\Users\asher\CrimsonDesert-SeamlessCoop\build\port3460'
H=/c/Users/asher/CrimsonDesert-SeamlessCoop/build/port3479; BIN="/d/SteamLibrary/steamapps/common/Crimson Desert/bin64"
REL=/c/Users/asher/CrimsonDesert-SeamlessCoop/dist/CrimsonDesert-SeamlessCoop-0.1.0-alpha+862b4256/mod/cdmp.asi
PEER='C:\Users\asher\CrimsonDesert-SeamlessCoop\upstream\cd-coop-3446\build\bin\RelWithDebInfo\cdmp_peer.exe'
SCRIPT=noop.txt; NAME=${1:-lag1}; VARIANT=${2:-coop-test-bandits}; PEERX=${PEERX:-",'--delay','0'"}
peer_down() { powershell -NoProfile -Command "Get-Process | Where-Object { \$_.ProcessName -match 'cdmp' } | Stop-Process -Force -ErrorAction SilentlyContinue" >/dev/null 2>&1; }
cd $H
taskkill //IM CrimsonDesert.exe //F >/dev/null 2>&1; peer_down; sleep 8; bash savecheck.sh --fix | tail -1
powershell -NoProfile -ExecutionPolicy Bypass -File $P/testsave.ps1 -Mode Out >/dev/null 2>&1
rm -f "$BIN"/cdmp_packets_*
# Saves made during the test (the autosave on a finished quest step) go to a scratch co-op world, never to the
# player's slots: Steam Cloud uploads the slots the moment the game exits, before any restore could run.
# The world is made here: copies of the player's slots (times kept) plus the test save as slot103 (newest, so Play
# loads it). The player's save folder is only read.
W="$LOCALAPPDATA/CrimsonDesert-CoopPlus/Worlds/devtest"; A=1160582305
rm -rf "$W" "$W.making"; mkdir -p "$W/$A"
[ "${WITH_PLAYER_SLOTS:-0}" = 1 ] && for s in "$LOCALAPPDATA/Pearl Abyss/CD/save/$A"/slot*; do case "$s" in *_new|*_backup|*_delete) continue;; esac; cp -rp "$s" "$W/$A/"; done
rm -rf "$W/$A/slot103"; cp -r /c/Users/asher/CrimsonDesert-SeamlessCoop/build/test-saves/slot103-$VARIANT "$W/$A/slot103"; echo "test world made ($VARIANT)"
cp scripts/$SCRIPT stage.txt
powershell -NoProfile -Command "Start-Process -WindowStyle Hidden -FilePath '$PEER' -ArgumentList @('--mode','relay','--quiet'$PEERX) -RedirectStandardOutput '$(cygpath -w $H/peer_$NAME.txt)'" >/dev/null 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$PW\run_echo.ps1' -Name $NAME -LoadWait 60 -Extra @('general.onboarded=1','sniffer.record=1','general.experimental_asked=1','dev.coop_world=devtest','dev.script=$(cygpath -w $H/stage.txt)','dev.lag_probe=1'${XEXTRA:-})" 2>&1 < /dev/null | grep -a "started="
GPID=$(powershell -NoProfile -Command "(Get-Process CrimsonDesert -ErrorAction SilentlyContinue | Select-Object -First 1).Id" | tr -d '\r')  # only this game is closed at the end
sleep 20
rm -f "$BIN/cdmp_lag.csv.keep"
k() { powershell -NoProfile -ExecutionPolicy Bypass -File $P/keys.ps1 -Keys "$1" -HoldMs $2 >/dev/null; }
date +%T; for r in 1 2 3 4; do k W 1500; k S 1500; k A 1000; k D 1000; k W 600; k S 600; done; date +%T
sleep 3
powershell -NoProfile -ExecutionPolicy Bypass -File $P/shot.ps1 -Out $P/shots/$NAME.png >/dev/null
cp "$BIN/cdmp.log" $H/$NAME.log; cp "$BIN/cdmp_lag.csv" $H/$NAME.lag.csv 2>/dev/null
[ -n "$GPID" ] && taskkill //PID $GPID //F >/dev/null 2>&1; peer_down; sleep 6
bash /c/Users/asher/CrimsonDesert-SeamlessCoop/build/port3479/savecheck.sh --restore-ini; bash savecheck.sh --fix | tail -1
powershell -NoProfile -ExecutionPolicy Bypass -File $P/testsave.ps1 -Mode Out | tail -1
cp $REL "$BIN/cdmp.asi"
grep -aE "Lag probe|Co-op: enabled|clock" $H/$NAME.log | cut -c1-200 | head -8
python /c/Users/asher/CrimsonDesert-SeamlessCoop/tools/lag/lag.py $H/$NAME.lag.csv
echo "-- warnings/errors:"; grep -a -E " (WRN|ERR) " $H/$NAME.log | cut -c15-200 | sed -E "s/\[T[0-9]+\] //" | sort | uniq -c | sort -rn | head -10; echo "-- peer:"; tail -3 $H/peer_$NAME.txt
