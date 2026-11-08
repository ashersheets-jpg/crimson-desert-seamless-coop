#!/bin/bash
# Two copies of the released mod (old base) on this PC, host + guest (twocopy_cp.ps1), then the usual cleanup: both
# copies closed (only them), the role inis and scratch worlds removed, the release asi back, savecheck against the
# snapshot test_guard.sh made, the player's ini. Lag: the host's own path (cdmp_lag.host.csv) against its copy in the
# guest's game (cdmp_lag.guest.csv), same PC clock (tools/lag/lag.py with two files).
#   PSARGS="-HostSeq 'k:W:1500 k:S:1500'" bash twocopy_cp.sh cp1
source /c/Users/asher/CrimsonDesert-SeamlessCoop/build/port3479/test_guard.sh
D=/c/Users/asher/CrimsonDesert-SeamlessCoop/build/port3527; H=/c/Users/asher/CrimsonDesert-SeamlessCoop/build/port3479
BIN="/d/SteamLibrary/steamapps/common/Crimson Desert/bin64"
REL=/c/Users/asher/CrimsonDesert-SeamlessCoop/dist/CrimsonDesert-SeamlessCoop-0.1.0-alpha+862b4256/mod/cdmp.asi
W="$LOCALAPPDATA/CrimsonDesert-CoopPlus/Worlds"
NAME=${1:-cp1}
T0=$(powershell -NoProfile -Command "(Get-Date).ToString('o')" | tr -d '\r')
eval powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -m $D/twocopy_cp.ps1)" -Name $NAME ${PSARGS:-} 2>&1 < /dev/null
[ -s $D/$NAME.pids ] || { echo "WARNING: no $NAME.pids; closing the copies started after $T0"; \
  powershell -NoProfile -Command "Get-Process CrimsonDesert -ErrorAction SilentlyContinue | Where-Object { \$_.StartTime -ge [datetime]'$T0' } | ForEach-Object { \$_.Id }" | tr -d '\r' > $D/$NAME.pids; }
for P in $(cat $D/$NAME.pids 2>/dev/null | tr -d '\r'); do
  powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='CrimsonDesert.exe' AND ParentProcessId=$P\" | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" >/dev/null 2>&1
  taskkill //PID $P //F >/dev/null 2>&1
done
sleep 6
for r in host guest; do cp "$BIN/cdmp.$r.log" $H/$NAME.$r.log 2>/dev/null; cp "$BIN/cdmp_lag.$r.csv" $H/$NAME.$r.lag.csv 2>/dev/null; done
cp $REL "$BIN/cdmp.asi"; rm -f "$BIN/cdmp.host.ini" "$BIN/cdmp.guest.ini" "$BIN"/cdmp.host*.log "$BIN"/cdmp.guest*.log "$BIN"/cdmp_lag.*.csv \
  "$BIN/cdmp.host_scores.ini" "$BIN/cdmp.guest_scores.ini"
for r in host guest; do [ -f "$W/twocopy_$r/.coopplus-test" ] && rm -rf "$W/twocopy_$r"; done
rm -rf "$W"/with-guestpc* "$W"/with-hostpc*  # the -Worlds test co-op saves (test-only names)
tasklist | grep -qi CrimsonDesert.exe && echo "WARNING: a CrimsonDesert.exe is still running after the cleanup"
cmp -s $REL "$BIN/cdmp.asi" || echo "WARNING: the game folder's cdmp.asi is not the release one"
bash $H/savecheck.sh --restore-ini; bash $H/savecheck.sh --fix | tail -2
for r in host guest; do echo "== $r"; grep -aE " ERR | WRN |Two copies|Co-op: |Fast lane|SaveIO: own|pose stream" $H/$NAME.$r.log | cut -c15-220 | head -14; done
python /c/Users/asher/CrimsonDesert-SeamlessCoop/tools/lag/lag.py $H/$NAME.host.lag.csv $H/$NAME.guest.lag.csv
