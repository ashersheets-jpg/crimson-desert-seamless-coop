# Port 3732 -> 3840 (2026-11-26), the native values p84.py does not see. ONE SHOT (asserts stop a second run).
# - the build stamps (PE 0x6B07E5E9, SizeOfImage 0x13D05000) in the three places that check them;
# - the protection's range (3732 .text1 0x7ACB000..0x137DB000 -> 3840 .reloc 0x7C78000..0x1385C000, the next
#   section's start as before) and its section name for the diagnostic that names it;
# - call sites / returns INSIDE the protection by the k-th call to the mapped callee (build/port3840/prot73.py, old
#   and new call counts equal: the save function 0x1A175A0 -> 0x1AB5580 2 / 2, 0x1A188B0 -> 0x1AB6780 1 / 1,
#   0x1A1AF40 -> 0x1AB8C40 1 / 1, the crime function 0x2C38B60 -> 0x2D709B0 2 / 2).
ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/cdmp-native/src/mod/'


def sub(path, pairs, count=1):
    s = open(ROOT + path, encoding='utf-8', errors='surrogateescape', newline='').read()
    for old, new in pairs:
        n = s.count(old)
        assert n == count, (path, old, n)
        s = s.replace(old, new)
    open(ROOT + path, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(s)
    print('  ', path)


for f in ('game/offsets.h', 'session2/offsets_s2.h', 'session2/scene_level_share_logic.h'):
    sub(f, [('kPeTimestamp = 0x6AFDC0CF;  /* 1.0.0.3732 (3663: 0x6AF5D198) */',
             'kPeTimestamp = 0x6B07E5E9;  /* 1.0.0.3840 (3732: 0x6AFDC0CF, 3663: 0x6AF5D198) */')])
sub('session2/user2_shops_logic.h', [('kProtectedBegin = 0x7ACB000, kProtectedEnd = 0x137DB000;',
                                      'kProtectedBegin = 0x7C78000, kProtectedEnd = 0x1385C000;  // 3840 .reloc (3732 .text1 0x7ACB000..0x137DB000)')])
for f in ('session2/server.cpp', 'session2/story_gate.cpp'):   # stack-chain labels: the image size
    sub(f, [('base + 0x13C7E000', 'base + 0x13D05000')])
sub('session2/guest_actor_diag_logic.h',
    [('inline bool is_denuvo_name(std::string_view n) { return n == ".data1" || n == ".text1"; }',
      '// 3840: ".reloc".\ninline bool is_denuvo_name(std::string_view n) { return n == ".data1" || n == ".text1" || n == ".reloc"; }')])
# the save paths' call sites in the protection (guest_save_host) and their returns (guest_save_logic)
sub('session2/guest_save_host.cpp', [('{0xE27435F, kLoadSlot}', '{0xE2874B0, kLoadSlot}'),
                                     ('{0xB6A4AA0, kSave}', '{0xB7D9D93, kSave}'),
                                     ('{0xE214B60, kSave}', '{0xE23E2F0, kSave}'),
                                     ('{0xE584AC8, kFlush}', '{0xE5A2578, kFlush}')])
sub('session2/guest_save_logic.h', [('kRetCharacter3093 = 0xE274364;', 'kRetCharacter3093 = 0xE2874B5;'),
                                    ('kRetAutosave = 0xB6A4AA5;', 'kRetAutosave = 0xB7D9D98;'),
                                    ('kRetManual2910 = 0xE214B65;', 'kRetManual2910 = 0xE23E2F5;'),
                                    ('kRetFlushTimer = 0xE584ACD;', 'kRetFlushTimer = 0xE5A257D;')])
sub('session2/anti_grief.cpp', [('kCrimeRetHit = 0xE2AEA62', 'kCrimeRetHit = 0xE2C96CD')])
print('pins84 done')
