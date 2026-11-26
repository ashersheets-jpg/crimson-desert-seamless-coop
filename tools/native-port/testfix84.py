# Port 3732 -> 3840 (2026-11-26): the native unit tests' checks that pin a 3732 value which is not an address literal
# tests84.py maps (field offsets, bytes, a mid-function sum). Run AFTER tests84.py. ONE SHOT (asserts).
import pathlib

T = pathlib.Path('C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/cdmp-native/tests')


def sub(name, pairs):
    p = T / name
    s = p.read_bytes().decode('utf-8')
    for a, b in pairs:
        assert s.count(a) == 1, (name, a, s.count(a))
        s = s.replace(a, b)
    p.write_bytes(s.encode('utf-8'))
    print('  ', name)


sub('optd_docked_gimmicks_tests.cpp', [
    ('static_assert(dgl::kFieldOwnerReadBytes[2] == 0x80 && dgl::kFieldOwnerReadBytes[3] == 0x01);  // 3663: mov eax,[rdi+0x180], no REX',
     'static_assert(dgl::kFieldOwnerReadBytes[2] == 0x58 && dgl::kFieldOwnerReadBytes[3] == 0x01);  // 3840: mov eax,[rdi+0x158] (3663..3732 +0x180), no REX'),
    ('static_assert(dgl::kClientGimmickRequestSlot == dgl::kClientGimmickVtbl + 0x748);  // 3732 (2976 +0x740)',
     'static_assert(dgl::kClientGimmickRequestSlot == dgl::kClientGimmickVtbl + 0x758);  // 3840 (3732 +0x748, 2976 +0x740)'),
])
sub('optd_mount_sync_tests.cpp', [
    ('static_assert(kMcEmptyFlag == 0x430);', 'static_assert(kMcEmptyFlag == 0x480);  // 3840 (3732 0x430)'),
])
sub('optd_shop_c1_tests.cpp', [
    ('static_assert(isl::kCtxLookup + sizeof isl::kCtxLookupBytes - 5 == 0x2360924);  // 3732 (3663',
     'static_assert(isl::kCtxLookup + sizeof isl::kCtxLookupBytes - 5 == 0x2433C44);  // 3840 (3732 0x2360924; 3663'),
])
print('test checks: done')
