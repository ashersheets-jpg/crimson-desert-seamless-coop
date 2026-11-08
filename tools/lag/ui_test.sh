#!/bin/bash
# Co-op page (render/coop_page.h) on one PC, scratch world, echo peer: the game's menu (Esc) with our Co-op tab, F8
# opens the page, a section and a setting changed, Tab to the next section, Esc closes; a shot after each step
# (port3460/shots/<name>-<n>.png). Never saves. Cleanup as always.
source /c/Users/asher/CrimsonDesert-SeamlessCoop/build/port3479/test_guard.sh
P=/c/Users/asher/CrimsonDesert-SeamlessCoop/build/port3460; PW='C:\Users\asher\CrimsonDesert-SeamlessCoop\build\port3460'
H=/c/Users/asher/CrimsonDesert-SeamlessCoop/build/port3479; BIN="/d/SteamLibrary/steamapps/common/Crimson Desert/bin64"
REL=/c/Users/asher/CrimsonDesert-SeamlessCoop/dist/CrimsonDesert-SeamlessCoop-0.1.0-alpha+862b4256/mod/cdmp.asi
PEER='C:\Users\asher\CrimsonDesert-SeamlessCoop\upstream\cd-coop-3446\build\bin\RelWithDebInfo\cdmp_peer.exe'
NAME=${1:-ui1}; VARIANT=${2:-coop-test-bandits}; SEQ=${SEQ:-default}
peer_down() { powershell -NoProfile -Command "Get-Process | Where-Object { \$_.ProcessName -match 'cdmp' } | Stop-Process -Force -ErrorAction SilentlyContinue" >/dev/null 2>&1; }
cd $H
taskkill //IM CrimsonDesert.exe //F >/dev/null 2>&1; peer_down; sleep 8; bash savecheck.sh --fix | tail -1
powershell -NoProfile -ExecutionPolicy Bypass -File $P/testsave.ps1 -Mode Out >/dev/null 2>&1
W="$LOCALAPPDATA/CrimsonDesert-CoopPlus/Worlds/devtest"; A=1160582305
rm -rf "$W" "$W.making"; mkdir -p "$W/$A"
cp -r /c/Users/asher/CrimsonDesert-SeamlessCoop/build/test-saves/slot103-$VARIANT "$W/$A/slot103"; echo "test world made ($VARIANT)"
powershell -NoProfile -Command "Start-Process -WindowStyle Hidden -FilePath '$PEER' -ArgumentList @('--mode','relay','--quiet')" >/dev/null 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -Command "& '$PW\run_echo.ps1' -Name $NAME -LoadWait 60 -Extra @('general.onboarded=1','general.experimental_asked=1','dev.coop_world=devtest')" 2>&1 < /dev/null | grep -a "started="
GPID=$(powershell -NoProfile -Command "(Get-Process CrimsonDesert -ErrorAction SilentlyContinue | Select-Object -First 1).Id" | tr -d '\r')
sleep 20
k() { powershell -NoProfile -ExecutionPolicy Bypass -File $P/keys.ps1 -Keys "$1" -HoldMs ${2:-80} >/dev/null; sleep 1.2; }
s() { powershell -NoProfile -ExecutionPolicy Bypass -File $P/shot.ps1 -Out $P/shots/$NAME-$1.png >/dev/null; }
k ESC; sleep 1.5; s 1-menu
k F8; s 2-page
k RIGHT; k DOWN; s 3-row
k TAB; k TAB; s 4-section
k RIGHT; k DOWN; k DOWN; k RIGHT; s 5-changed
k LEFT; k ESC; s 6-closed
k ESC; s 7-game
cp "$BIN/cdmp.log" $H/$NAME.log
[ -n "$GPID" ] && taskkill //PID $GPID //F >/dev/null 2>&1; peer_down; sleep 6
bash /c/Users/asher/CrimsonDesert-SeamlessCoop/build/port3479/savecheck.sh --restore-ini; bash savecheck.sh --fix | tail -1
powershell -NoProfile -ExecutionPolicy Bypass -File $P/testsave.ps1 -Mode Out | tail -1
cp $REL "$BIN/cdmp.asi"
grep -aE "Co-op page|Settings:" $H/$NAME.log | cut -c15-200 | tail -10
echo "-- warnings/errors:"; grep -a -E " (WRN|ERR) " $H/$NAME.log | cut -c15-200 | sed -E "s/\[T[0-9]+\] //" | sort | uniq -c | sort -rn | head -8
