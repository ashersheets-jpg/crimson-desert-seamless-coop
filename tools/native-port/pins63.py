# Port 3576 -> 3663, the values p63.py does not see (2026-11-13). ONE SHOT (asserts stop a second run).
# - call sites INSIDE the copy protection (above 0x7A44000 on 3576): by the function they call, the k-th such call of
#   the protection section in both builds (build/port3663: 1/1, 2/2 callers each), and the three timer returns by
#   the 24 bytes before them (one hit each; the middle one by 8 bytes between its two neighbours, its next
#   instruction reads [rax+0x178], which was [rax+0x170]: the scheduler moved by 8 in the stage manager).
# - the build stamps, the protection's range and name, the layer's table switch.
import glob, re

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/'


def sub(path, pairs, count=None):
    s = open(ROOT + path, encoding='utf-8', errors='surrogateescape', newline='').read()
    for old, new in pairs:
        n = s.count(old)
        assert n >= 1, (path, old)
        if count is not None:
            assert n == count, (path, old, n)
        s = s.replace(old, new)
    for attempt in range(5):   # 2026-11-13: one write failed with "Invalid argument" (a scanner held the file); the
        try:                   # file was untouched, the second try worked
            open(ROOT + path, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(s)
            break
        except OSError:
            import time
            time.sleep(1)
    else:
        raise SystemExit('could not write ' + path)
    print('  ', path)


N = 'cdmp-native/src/mod/'
if '0xE3C55B2' in open(ROOT + N + 'session2/anti_grief.cpp', encoding='utf-8', errors='replace').read():
    sub(N + 'session2/anti_grief.cpp', [('0xE3C55B2', '0xEA2BAD2')])
sub(N + 'session2/guest_save_host.cpp', [('0xE3927BF', '0xE9F5065'), ('0xB8A786C', '0xBB0BEC8'), ('0xE35801A', '0xE98E356'),
                                         ('0xE69FA88', '0xED099C8')], 1)
sub(N + 'session2/user2_funnels.cpp', [('0xE68CDEA', '0xECFEA3A'), ('0xE6911DF', '0xED021AF'), ('0xE6946CF', '0xED05E4F')])
sub(N + 'session2/user2_shops_logic.h', [('kProtectedBegin = 0x7A44000, kProtectedEnd = 0x13826000',
                                          'kProtectedBegin = 0x7AC4000, kProtectedEnd = 0x14046000')], 1)
for f in ('game/offsets.h', 'session2/offsets_s2.h', 'session2/scene_level_share_logic.h'):
    sub(N + f, [('kPeTimestamp = 0x6AED2E85;', 'kPeTimestamp = 0x6AF5D198;  /* 1.0.0.3663 (3576: 0x6AED2E85) */')], 1)
sub(N + 'session2/guest_actor_diag_logic.h',
    [('inline bool is_denuvo_name(std::string_view n) { return n == ".xtls" || n == ".xpdata" || n == ".tls$"; }',
      '// 3663: the protection is ".data1" (and ".tls$" is now a small data section, so it is no longer in the list).\n'
      'inline bool is_denuvo_name(std::string_view n) { return n == ".data1"; }')], 1)

L = 'cd-coop-3446/src/mod/'
sub(L + 'game/offsets_3663.h', [('kImageSize = 0x13CC4000;  // 3549:0x13CFF000', 'kImageSize = 0x144E9000;  // 3576:0x13CC4000')], 1)
sub(L + 'game/offsets.h', [('#include "mod/game/offsets_3576.h"', '#include "mod/game/offsets_3663.h"'),
                           ('Consumers write `namespace off = build_3576;`', 'Consumers write `namespace off = build_3663;`')], 1)
n = 0
for path in glob.glob(ROOT + L + '**/*.*', recursive=True):
    path = path.replace('\\', '/')
    if not path.endswith(('.cpp', '.h')) or re.search(r'offsets_3\d{3}\.h$', path):
        continue
    s = open(path, encoding='utf-8', errors='surrogateescape', newline='').read()
    t = s.replace('build_3576', 'build_3663')
    if t != s:
        open(path, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(t)
        n += 1
print('   build_3576 -> build_3663 in', n, 'layer files')
sub(L + 'mod.cpp', [('Game reader enabled (build 1.0.0.3576 offsets)', 'Game reader enabled (build 1.0.0.3663 offsets)'),
                    ('this mod was made for game version 1.0.0.3576, not {}', 'this mod was made for game version 1.0.0.3663, not {}')], 1)
print('done')
