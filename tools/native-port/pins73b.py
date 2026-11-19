# The rest of pins73.py after its stop at the namespace line (offsets_3732.h writes `cdmp::game::build_3663`):
# everything before it was written, the git mv done. ONE SHOT.
import glob, re
ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/'
L = 'cd-coop-3446/src/mod/'


def sub(path, pairs, count=None):
    s = open(ROOT + path, encoding='utf-8', errors='surrogateescape', newline='').read()
    for old, new in pairs:
        n = s.count(old)
        assert n >= 1, (path, old)
        if count is not None:
            assert n == count, (path, old, n)
        s = s.replace(old, new)
    open(ROOT + path, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(s)
    print('  ', path)


sub(L + 'game/offsets_3732.h', [('namespace cdmp::game::build_3663', 'namespace cdmp::game::build_3732')], 2)
sub(L + 'game/offsets.h', [('#include "mod/game/offsets_3663.h"', '#include "mod/game/offsets_3732.h"')], 1)
n = 0
for path in glob.glob(ROOT + 'cd-coop-3446/**/*.*', recursive=True):
    path = path.replace('\\', '/')
    if '/build' in path[len(ROOT):] or not path.endswith(('.cpp', '.h')) or re.search(r'offsets_3\d{3}\.h$', path):
        continue
    s = open(path, encoding='utf-8', errors='surrogateescape', newline='').read()
    t = s.replace('build_3663', 'build_3732')
    if t != s:
        open(path, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(t)
        n += 1
print('   build_3663 -> build_3732 in', n, 'layer files')
sub(L + 'mod.cpp', [('Game reader enabled (build 1.0.0.3663 offsets)', 'Game reader enabled (build 1.0.0.3732 offsets)'),
                    ('this mod was made for game version 1.0.0.3663, not {}', 'this mod was made for game version 1.0.0.3732, not {}')], 1)
print('done')
