# Port 3576 -> 3663 by hand: constants the native tests pin, and the field-owner read that lost its REX byte.
ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/cdmp-native/'


def rw(p, pairs):
    s = open(ROOT + p, encoding='utf-8', errors='surrogateescape', newline='').read()
    n = 0
    for old, new in pairs:
        if s.count(old) == 0 and s.count(new) == 1:
            continue
        assert s.count(old) == 1, (p, old, s.count(old))
        s = s.replace(old, new)
        n += 1
    open(ROOT + p, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(s)
    print('  %-48s %d' % (p, n))


rw('src/mod/session2/docked_gimmicks_logic.h', [
    ('inline constexpr uint8_t kFieldOwnerReadBytes[11] = {0x8B, 0x87, 0x80, 0x01, 0x00, 0x00, 0x85, 0xC0, 0x74, 0x07, 0x41};',
     'inline constexpr uint8_t kFieldOwnerReadBytes[10] = {0x8B, 0x87, 0x80, 0x01, 0x00, 0x00, 0x85, 0xC0, 0x74, 0x07};  /* 3663: mov eax,[rdi+0x180] (no REX: 10 bytes; 3576 mov eax,[r13+0x180], 11) */'),
    ('inline constexpr uintptr_t kControllerWrite = 0x2A2F43B;  // 3576 (2976 0x2792A7B): ICreateServerActorDesc vf3 0x29CBEE0, same bytes and registers',
     'inline constexpr uintptr_t kControllerWrite = 0x2A2F43B;  // 3663: ICreateServerActorDesc vf3 0x2A2E6C0; THE FIELD IS IN RDI (2976 / 3576: r13), mov r8,rdi | lea rdx,[rbp+0x60]'),
])
rw('tests/optd_docked_gimmicks_tests.cpp', [
    ('static_assert(dgl::kFieldOwnerReadBytes[3] == 0x80 && dgl::kFieldOwnerReadBytes[4] == 0x01);',
     'static_assert(dgl::kFieldOwnerReadBytes[2] == 0x80 && dgl::kFieldOwnerReadBytes[3] == 0x01);  // 3663: mov eax,[rdi+0x180], no REX'),
    ('static_assert(dgl::kFieldOwnerRead + 11 == dgl::kControllerWrite);', 'static_assert(dgl::kFieldOwnerRead + 10 == dgl::kControllerWrite);'),
])
rw('tests/optd_shop_c1_tests.cpp', [
    ('static_assert(isl::kCtxLookup + sizeof isl::kCtxLookupBytes - 5 == 0x22EE424);  // 3576 (2976 0x2126A6A)',
     'static_assert(isl::kCtxLookup + sizeof isl::kCtxLookupBytes - 5 == 0x235ECB4);  // 3663 (3576 0x22EE424; 2976 0x2126A6A)'),
    ('CHECK(call_at + 5 + rel == 0x21578E0);  // 3576 (2976 0x1F6C190)', 'CHECK(call_at + 5 + rel == 0x21C5750);  // 3663 (3576 0x21578E0; 2976 0x1F6C190)'),
])
