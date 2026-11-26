# Port 3732 -> 3840 (2026-11-26): the game's TLS block 0 (gs:[0x58] -> [0]) grew by 0x10 before the fields we read.
# Evidence (build/port3840, mov r32,imm32 in all code): +0x1EB (the server-realm byte) 4598 uses in 3732 / 25 in 3840,
# +0x1FB 60 / 4633; +0x1FE (the allocator's heap flag) 63575 / 27, +0x20E 22 / 65273; +0x250 (the send context)
# 1173 / 326, +0x260 283 / 1148; the schedule loader's own read mov r14d,0x1eb -> 0x1fb (midsites). ONE SHOT.
ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/cdmp-native/src/mod/'


def sub(path, pairs):
    s = open(ROOT + path, encoding='utf-8', errors='surrogateescape', newline='').read()
    for old, new in pairs:
        n = s.count(old)
        assert n == 1, (path, old, n)
        s = s.replace(old, new)
    open(ROOT + path, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(s)
    print('  ', path)


sub('session2/offsets_s2.h', [('kTls_UseGlobalHeap = 0x1FE;', 'kTls_UseGlobalHeap = 0x20E;  /* 3840 (3732: 0x1FE) */')])
sub('session2/child_hold.cpp', [('constexpr uint32_t kTlsServerRealm = 0x1EB;', 'constexpr uint32_t kTlsServerRealm = 0x1FB;  /* 3840 (3732: 0x1EB) */')])
sub('session2/guest_sched_bind.cpp', [('constexpr uint32_t kTlsRealm = 0x1EB;', 'constexpr uint32_t kTlsRealm = 0x1FB;  /* 3840 (3732: 0x1EB) */')])
sub('session2/guest_sched_load.cpp', [('constexpr uint32_t kTlsRealm = 0x1EB;', 'constexpr uint32_t kTlsRealm = 0x1FB;  /* 3840 (3732: 0x1EB) */')])
sub('session2/observer_milestone.cpp', [('constexpr uint32_t kTls_Realm = 0x1EB, kTls_SendCtx = 0x250;',
                                         'constexpr uint32_t kTls_Realm = 0x1FB, kTls_SendCtx = 0x260;  /* 3840 (3732: 0x1EB, 0x250) */')])
sub('session2/observer_scene_copy.cpp', [('constexpr uint32_t kTls_Realm = 0x1EC, kTls_SendCtx = 0x250;',
                                          'constexpr uint32_t kTls_Realm = 0x1FB, kTls_SendCtx = 0x260;  /* 3840 (2976: 0x1EC, 3732: 0x250) */')])
sub('session2/user2_stages.cpp', [('rd(block + 0x1EB, flag)', 'rd(block + 0x1FB, flag)  /* 3840 (3732: 0x1EB) */')])
sub('session2/guest_save_host.cpp', [('game::safe_read(block + 0x1EB, &flag, sizeof flag)', 'game::safe_read(block + 0x1FB, &flag, sizeof flag)  /* 3840 (3732: 0x1EB) */')])
sub('session2/identity_fix.cpp', [('game::safe_read(block + 0x1EB, &flag, sizeof flag)', 'game::safe_read(block + 0x1FB, &flag, sizeof flag)  /* 3840 (3732: 0x1EB) */')])
sub('session2/knowledge_grant.cpp', [('*reinterpret_cast<const uint8_t*>(tls[0] + 0x1EB) != 0', '*reinterpret_cast<const uint8_t*>(tls[0] + 0x1FB) != 0  /* 3840 (3732: 0x1EB) */')])
sub('net/horse_diag.cpp', [('*reinterpret_cast<const uint8_t*>(tls[0] + 0x1EB) != 0', '*reinterpret_cast<const uint8_t*>(tls[0] + 0x1FB) != 0  /* 3840 (3732: 0x1EB) */')])
sub('session2/trade_probe.cpp', [('*reinterpret_cast<uintptr_t*>(tls0 + 0x250), scope);', '*reinterpret_cast<uintptr_t*>(tls0 + 0x260), scope);  /* 3840 (3732: 0x250) */'),
                                 ('(*reinterpret_cast<uint8_t*>(tls0 + 0x1EB)', '(*reinterpret_cast<uint8_t*>(tls0 + 0x1FB)  /* 3840 (3732: 0x1EB) */')])
print('tls84 done')
