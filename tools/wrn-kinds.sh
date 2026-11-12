#!/bin/bash
# The kinds of WRN lines in a log, one per line: the text after the thread tag with numbers, hex values and quoted
# names taken out, cut to 60 characters. Used by tools/prepost.sh (a warning kind that tools/known-warnings.txt does
# not list fails the step: a new warning in a run that passed is a finding) and to build that list:
#   bash tools/wrn-kinds.sh build/native/pp8-*.log | sort | uniq -c
grep -h " WRN \[" "$@" 2>/dev/null |
  sed -E 's/^\[[0-9:.]+\] WRN \[T[0-9]+\] //' |
  sed -E "s/(^| )'[^']*'/\1Q/g; s/0x[0-9A-Fa-f]+/X/g; s/[0-9]+(\.[0-9]+)?/N/g" |
  cut -c1-60 | sed -E 's/ +$//'
