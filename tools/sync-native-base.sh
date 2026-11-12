#!/bin/bash
# Publishes the native base's local commits: merges the local repository upstream/cdmp-native (branch coopplus-3549,
# ByJunter's Crimson Desert Multiplayer native server plus our changes, GPL-3.0) into this repository's native-base/
# folder, history and all. The owner asked for it to be public here on 2026-11-12. Run after committing in
# upstream/cdmp-native; then push as usual. Needs a clean index (commit or stash other staged work first).
#
#   bash tools/sync-native-base.sh
set -e
cd "$(dirname "$0")/.."
git diff --cached --quiet || { echo "the index has staged changes: commit them first"; exit 1; }
[ -z "$(git -C upstream/cdmp-native status --porcelain -- src tests CMakeLists.txt)" ] ||
  echo "note: upstream/cdmp-native has uncommitted changes; only its commits are published"
git fetch -q ./upstream/cdmp-native coopplus-3549
NEW=$(git rev-parse --short FETCH_HEAD)
if git merge-base --is-ancestor FETCH_HEAD HEAD; then echo "native-base is already at $NEW"; exit 0; fi
git merge -q -X subtree=native-base/ --no-edit -m "native-base: the native base at $NEW ($(git log -1 --format=%s FETCH_HEAD | cut -c1-80))" FETCH_HEAD
echo "native-base is now at $NEW; push to publish"
