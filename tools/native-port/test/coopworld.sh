#!/bin/bash
# The TEST co-op world (coop_worlds/with_127_0_0_1: the host at Nas River, the guest's slot) kept safe around a run
# that replaces it (-HostChoice 3 = New Co-op Save).   bash coopworld.sh backup | restore NAME
#   backup : copies with_127_0_0_1 and with_127_0_0_1_guest_prev to keep-coop-world/ with checksums (only when the
#            world there is the known one, i.e. no backup exists yet or it is identical).
#   restore: moves the world a test made to NAME-new-world/ (kept, for looking at), renames the mod's own
#            with_127_0_0_1_old_<date> back, and checks it against the backup's checksums.
cd /c/Users/asher/CrimsonDesert-SeamlessCoop/build/native
W="$LOCALAPPDATA/Pearl Abyss/CD/coop_worlds"
tasklist | grep -qi CrimsonDesert.exe && { echo "CRIMSON DESERT IS RUNNING: nothing done"; exit 1; }
sums() { ( cd "$W" && find with_127_0_0_1 with_127_0_0_1_guest_prev -type f -exec sha256sum {} + | sed 's# \*# *./#' | sort -k2 | awk '{print $1, $2}' | grep -vE 'user_engine_option_save.xml|/slot204/' ); }  # the settings file is not part of the world (a frame-generation step leaves another one)
case "$1" in
backup)
  if [ -f keep-coop-world.sums ] && diff -q <(awk '{print $1, $2}' keep-coop-world.sums | grep -vE 'user_engine_option_save.xml|/slot204/') <(sums) > /dev/null; then echo "backup is current"; exit 0; fi
  [ -d keep-coop-world ] && { echo "a different backup exists: not overwritten (look at keep-coop-world first)"; exit 1; }
  mkdir -p keep-coop-world && cp -rp "$W/with_127_0_0_1" "$W/with_127_0_0_1_guest_prev" keep-coop-world/ && sums > keep-coop-world.sums && echo "backed up ($(wc -l < keep-coop-world.sums) files)" ;;
restore)
  # 2026-11-12: a world set aside takes its _guest_prev with it (with_127_0_0_1_old_<stamp>_guest_prev: the friend's
  # character of the old world must never be picked for the new one). Both come back here.
  old=$(ls -d "$W"/with_127_0_0_1_old_* 2>/dev/null | grep -v '_guest_prev$' | tail -1)
  [ -z "$old" ] && { diff -q <(awk '{print $1, $2}' keep-coop-world.sums | grep -vE 'user_engine_option_save.xml|/slot204/') <(sums) > /dev/null && echo "TEST CO-OP WORLD is the backup's (nothing to restore)" || echo "NO _old_ folder and the world differs from the backup: look by hand"; exit 0; }
  rm -rf "${2:-last}-new-world" "${2:-last}-new-world_guest_prev"; mv "$W/with_127_0_0_1" "${2:-last}-new-world" && mv "$old" "$W/with_127_0_0_1"
  if [ -d "${old}_guest_prev" ]; then
    [ -d "$W/with_127_0_0_1_guest_prev" ] && mv "$W/with_127_0_0_1_guest_prev" "${2:-last}-new-world_guest_prev"
    mv "${old}_guest_prev" "$W/with_127_0_0_1_guest_prev"
  fi
  diff <(awk '{print $1, $2}' keep-coop-world.sums | grep -vE 'user_engine_option_save.xml|/slot204/') <(sums) > /dev/null && echo "TEST CO-OP WORLD BACK, identical to the backup" || echo "RESTORED WORLD DIFFERS FROM THE BACKUP: look by hand" ;;
*) echo "backup | restore NAME" ;;
esac
