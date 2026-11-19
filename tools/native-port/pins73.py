# Port 3663 -> 3732 (2026-11-19), the values p73.py does not see. ONE SHOT (asserts stop a second run).
# - inside the copy protection (3663 .data1 0x7AC4000..0x14046000, 3732 .text1 0x7ACB000..0x137DB000):
#   call sites and the return after a call, by the k-th call of the protection to the mapped callee (build/port3732/
#   prot73.py: old and new count equal for each, bytes around each the same but the rel32); the three timer returns
#   by the 12/16 bytes before them (one hit each, the 16 bytes after identical but a call's rel32); the menu's two
#   protected functions by their masked pattern (one hit each, first 24 bytes identical).
# - the build stamps (PE 0x6AFDC0CF, SizeOfImage 0x13C7E000), the protection's range and section name, the layer's
#   table (offsets_3663.h -> offsets_3732.h, build_3663 -> build_3732), the version texts.
import glob, re, subprocess

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/'


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


N = 'cdmp-native/src/mod/'
sub(N + 'session2/anti_grief.cpp', [('0xEA2BAD2', '0xE2AEA62')], 2)  # the constant and its comment
sub(N + 'session2/guest_save_host.cpp', [('0xE9F5065', '0xE27435F'), ('0xBB0BEC8', '0xB6A4AA0'), ('0xE98E356', '0xE214B60'),
                                         ('0xED099C8', '0xE584AC8')], 1)
sub(N + 'session2/user2_funnels.cpp', [('0xECFEA3A', '0xE57536A'), ('0xED021AF', '0xE57791F'), ('0xED05E4F', '0xE5780FF')])
sub(N + 'session2/user2_shops_logic.h', [('kProtectedBegin = 0x7AC4000, kProtectedEnd = 0x14046000',
                                          'kProtectedBegin = 0x7ACB000, kProtectedEnd = 0x137DB000')], 1)
for f in ('game/offsets.h', 'session2/offsets_s2.h', 'session2/scene_level_share_logic.h'):
    sub(N + f, [('kPeTimestamp = 0x6AF5D198;  /* 1.0.0.3663 (3576: 0x6AED2E85) */',
                 'kPeTimestamp = 0x6AFDC0CF;  /* 1.0.0.3732 (3663: 0x6AF5D198) */')], 1)
sub(N + 'session2/guest_actor_diag_logic.h',
    [('inline bool is_denuvo_name(std::string_view n) { return n == ".data1"; }',
      '// 3732: ".text1".\ninline bool is_denuvo_name(std::string_view n) { return n == ".data1" || n == ".text1"; }')], 1)
for f in ('session2/server.cpp', 'session2/story_gate.cpp'):     # stack-chain labels: the image size
    sub(N + f, [('base + 0x144E9000', 'base + 0x13C7E000')], 1)

L = 'cd-coop-3446/src/mod/'
sub(L + 'game/main_menu.cpp', [('kListSelect = 0xA97F1D0;  /* 3663 (3576 0xA7866B0)', 'kListSelect = 0xA5562A0;  /* 3732 (3663 0xA97F1D0, 3576 0xA7866B0)'),
                               ('kReturnLevel = 0x9790FD0;  /* 3663 (3576 0x96525F0)', 'kReturnLevel = 0x94CF960;  /* 3732 (3663 0x9790FD0, 3576 0x96525F0)')], 1)
sub(L + 'game/offsets_3663.h', [('kPeTimestamp = 0x6AF5D198;  // 3576:0x6AED2E85', 'kPeTimestamp = 0x6AFDC0CF;  // 3663:0x6AF5D198'),
                                ('kImageSize = 0x144E9000;  // 3576:0x13CC4000', 'kImageSize = 0x13C7E000;  // 3663:0x144E9000')], 1)
r = subprocess.run(['git', '-C', ROOT + 'cd-coop-3446', 'mv', 'src/mod/game/offsets_3663.h', 'src/mod/game/offsets_3732.h'],
                   capture_output=True, text=True)
assert r.returncode == 0, r.stderr
sub(L + 'game/offsets_3732.h', [('namespace build_3663', 'namespace build_3732')])
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
