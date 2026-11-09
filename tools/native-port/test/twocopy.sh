#!/bin/bash
# Two copies (host + guest) of the native base, then the usual cleanup: both copies closed (and only them), savecheck
# against the snapshot test_guard.sh made, the player's ini, the release asi back, the role inis removed.
source /c/Users/asher/CrimsonDesert-SeamlessCoop/build/port3479/test_guard.sh
N=/c/Users/asher/CrimsonDesert-SeamlessCoop/build/native; H=/c/Users/asher/CrimsonDesert-SeamlessCoop/build/port3479
BIN="/d/SteamLibrary/steamapps/common/Crimson Desert/bin64"
REL=/c/Users/asher/CrimsonDesert-SeamlessCoop/dist/CrimsonDesert-SeamlessCoop-0.1.0-alpha+862b4256/mod/cdmp.asi
NAME=${1:-tc1}
T0=$(powershell -NoProfile -Command "(Get-Date).ToString('o')" | tr -d '\r')
powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w $N/twocopy.ps1)" -Name $NAME ${PSARGS:-} 2>&1 < /dev/null
# tc34: a script bug left no pids file and both copies ran on: every CrimsonDesert started after T0 is ours.
[ -s $N/$NAME.pids ] || { echo "WARNING: no $NAME.pids; closing the copies started after $T0"; \
  powershell -NoProfile -Command "Get-Process CrimsonDesert -ErrorAction SilentlyContinue | Where-Object { \$_.StartTime -ge [datetime]'$T0' } | ForEach-Object { \$_.Id }" | tr -d '\r' > $N/$NAME.pids; }
for r in host guest; do cp "$BIN/cdmp.$r.log" $N/$NAME.$r.log 2>/dev/null; done
for P in $(cat $N/$NAME.pids 2>/dev/null | tr -d '\r'); do
  powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='CrimsonDesert.exe' AND ParentProcessId=$P\" | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" >/dev/null 2>&1
  taskkill //PID $P //F >/dev/null 2>&1
done
# B01 (-Lobby): the layer restarts the copies (new processes): every CrimsonDesert started after T0 is ours too.
powershell -NoProfile -Command "Get-Process CrimsonDesert -ErrorAction SilentlyContinue | Where-Object { \$_.StartTime -ge [datetime]'$T0' } | Stop-Process -Force" >/dev/null 2>&1
sleep 6
for r in host guest; do cp "$BIN/cdmp.$r.log" $N/$NAME.$r.log 2>/dev/null; done
cp $REL "$BIN/cdmp.asi"; rm -f "$BIN/cdmp.host.ini" "$BIN/cdmp.guest.ini"
# Co-op+ layer runs (-CoopPlus): its logs kept, then the DLL, its inis and the relay removed (the player's game
# must never load coopplus.asi).
for r in host guest; do cp "$BIN/coopplus.$r.log" $N/$NAME.coopplus.$r.log 2>/dev/null; done
rm -f "$BIN/coopplus.asi" "$BIN/coopplus.host.ini" "$BIN/coopplus.guest.ini" "$BIN/coopplus.host.log" "$BIN/coopplus.guest.log" \
      "$BIN/coopplus.host_scores.ini" "$BIN/coopplus.guest_scores.ini"
taskkill //IM cdmp_peer.exe //F >/dev/null 2>&1
[ -f "$BIN/coopplus.asi" ] && echo "WARNING: coopplus.asi still in the game folder"
tasklist | grep -qi CrimsonDesert.exe && echo "WARNING: a CrimsonDesert.exe is still running after the cleanup"
cmp -s $REL "$BIN/cdmp.asi" || echo "WARNING: the game folder's cdmp.asi is not the release one"
bash $H/savecheck.sh --restore-ini; bash $H/savecheck.sh --fix | tail -2
for r in host guest; do echo "== $r"; grep -aE " ERR | WRN |user 2|User 2|Session2 guest|tunnel|login|Login" $N/$NAME.$r.log | cut -c1-240 | head -40; done
