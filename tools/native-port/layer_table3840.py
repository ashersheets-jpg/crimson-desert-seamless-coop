# Port 3732 -> 3840 (2026-11-26), the Co-op+ layer's table: game/offsets_3840.h as a copy of offsets_3732.h
# (namespace build_3840, the new PE stamp and image size; its addresses and message numbers are then ported by
# p84.py / op84.py like every other file), offsets.h pointed at it, and build_3732 -> build_3840 everywhere else.
# Run once (it refuses when offsets_3840.h exists).
import glob, pathlib, re, sys

L = pathlib.Path('C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/cd-coop-3446/src')
old, new = L / 'mod/game/offsets_3732.h', L / 'mod/game/offsets_3840.h'
if new.exists():
    sys.exit('offsets_3840.h exists already')


def rd(p):
    return open(p, 'rb').read().decode('utf-8')


def wr(p, s):
    open(p, 'wb').write(s.encode('utf-8'))


s = rd(old)
nl = '\r\n' if '\r\n' in s else '\n'
assert s.count('namespace cdmp::game::build_3732') == 2, s.count('namespace cdmp::game::build_3732')
s = s.replace('namespace cdmp::game::build_3732', 'namespace cdmp::game::build_3840')
a = 'inline constexpr uint32_t kPeTimestamp = 0x6AFDC0CF;  // 3663:0x6AF5D198'
assert s.count(a) == 1
s = s.replace(a, 'inline constexpr uint32_t kPeTimestamp = 0x6B07E5E9;  // 3732:0x6AFDC0CF, 3663:0x6AF5D198')
a = 'inline constexpr uintptr_t kImageSize = 0x13C7E000;  // 3663:0x144E9000'
assert s.count(a) == 1
s = s.replace(a, 'inline constexpr uintptr_t kImageSize = 0x13D05000;  // 3732:0x13C7E000, 3663:0x144E9000')
first = s.split(nl, 1)
s = ('// 2026-11-26: Steam updated the game from 1.0.0.3732 (PE 0x6AFDC0CF) to 1.0.0.3840 (PE 0x6B07E5E9, SizeOfImage' + nl +
     '// 0x13D05000; Steam calls it "Crimson Desert Enhanced"). This table is offsets_3732.h ported (docs/research/' + nl +
     '// port-3840.md); the notes below about older builds are kept as they were.' + nl + s)
wr(new, s)
print('offsets_3840.h written')

h = rd(L / 'mod/game/offsets.h')
assert h.count('#include "mod/game/offsets_3732.h"') == 1
h = h.replace('#include "mod/game/offsets_3732.h"', '#include "mod/game/offsets_3840.h"')
h = h.replace('`namespace off = build_3732;`', '`namespace off = build_3840;`')
wr(L / 'mod/game/offsets.h', h)
print('offsets.h -> offsets_3840.h')

n_files = n_hits = 0
for f in glob.glob(str(L / '**/*.*'), recursive=True):
    if not f.endswith(('.cpp', '.h', '.inc')) or re.search(r'offsets_\d{4}\.h$', f) or f.endswith('offsets.h'):
        continue
    t = rd(f)
    k = t.count('build_3732')
    if not k:
        continue
    t = t.replace('build_3732', 'build_3840')
    wr(f, t)
    n_files += 1
    n_hits += k
print('build_3732 -> build_3840:', n_hits, 'in', n_files, 'files')

m = rd(L / 'mod/mod.cpp')
for a, b in (('log::info("Game reader enabled (build 1.0.0.3732 offsets)");',
              'log::info("Game reader enabled (build 1.0.0.3840 offsets)");'),
             ('"Co-op+ is off: this mod was made for game version 1.0.0.3732, not {}. "',
              '"Co-op+ is off: this mod was made for game version 1.0.0.3840, not {}. "')):
    assert m.count(a) == 1, a
    m = m.replace(a, b)
wr(L / 'mod/mod.cpp', m)
print('mod.cpp: the version texts -> 1.0.0.3840')
