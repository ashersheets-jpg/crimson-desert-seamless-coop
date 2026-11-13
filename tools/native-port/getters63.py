# Port 3576 -> 3663: the info-table getters (2026-11-13). ONE SHOT.
# 3576: row = fn(ignored, u32 index in edx); the table = { +0x08 count, +0x58 -> record pointers [index] }.
# 3663: row = fn(const u16* index in rcx), a default record (never null) when the index is out of range; the table =
#       { +0x28 count, +0x20 -> entries of 24 bytes, the record pointer at entry + 8 (null until loaded: the getter
#       loads it through its slow path) }. Found through the getters' callers (build/port3663/fmap.py callee()).
# Every call `reinterpret_cast<uintptr_t (__fastcall*)(X, uint32_t)>(base + 0xADDR)(0, index)` of the four row getters
# becomes `game::info_row(base + 0xADDR, index)`.
import re

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/'
HELPER = '''#pragma once
// 1.0.0.3663: the game's info-table getters (item, field, ... : 127 copies of one template) take a POINTER to the u16
// row index in rcx and answer a default record, never null, when the index is out of range. Up to 1.0.0.3576 they
// took the index itself in edx. One place for the call, so that the next change of this kind is one edit.
#include <cstdint>

namespace cdmp::game {

inline uintptr_t info_row(uintptr_t getter, uint32_t index) noexcept {
    const uint16_t i = static_cast<uint16_t>(index);
    return reinterpret_cast<uintptr_t (__fastcall*)(const uint16_t*)>(getter)(&i);
}

}  // namespace cdmp::game
'''
CALL = re.compile(r'reinterpret_cast<uintptr_t \(__fastcall\*\)\((?:uint64_t|uintptr_t), uint32_t\)>\(base \+ (0x(?:3DAF10|3DAC70|57B290|57B410))\)\(0, ([^;]+?)\);')


def port(repo, files):
    open(ROOT + repo + '/src/mod/game/info_row.h', 'w', encoding='utf-8', newline='\n').write(HELPER)
    for f, want in files:
        p = ROOT + repo + '/src/mod/' + f
        s = open(p, encoding='utf-8', errors='surrogateescape', newline='').read()
        nl = '\r\n' if s.count('\r\n') * 2 > s.count('\n') else '\n'
        t, n = CALL.subn(lambda m: 'game::info_row(base + %s, %s);' % (m.group(1), m.group(2)), s)
        assert n == want, (f, n, want)
        # the include, after the file's last "mod/..." include
        incs = list(re.finditer(r'#include "mod/[^"]+"\r?\n', t))
        assert incs, f
        at = incs[-1].end()
        t = t[:at] + '#include "mod/game/info_row.h"' + nl + t[at:]
        open(p, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(t)
        print('  ', repo, f, n)


port('cdmp-native', [('session2/lootr.cpp', 5), ('session2/trade_probe.cpp', 4)])
port('cd-coop-3446', [('net/coop_give.cpp', 1), ('net/coop_stash.cpp', 2)])

# far_travel.cpp: its own typed call
p = ROOT + 'cdmp-native/src/mod/session2/far_travel.cpp'
s = open(p, encoding='utf-8', newline='').read()
old = '''        const auto fn = reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t, uint16_t)>(base + kFieldInfoGetter);
        const uintptr_t rec = fn(ts, index);
        if (!rec) return 0;'''
new = '''        // 3663: the getter takes a pointer to the index and never answers null (game/info_row.h)
        (void)ts;
        const uintptr_t rec = game::info_row(base + kFieldInfoGetter, index);
        if (!rec) return 0;'''
nl = '\r\n' if s.count('\r\n') * 2 > s.count('\n') else '\n'
assert s.count(old.replace('\n', nl)) == 1
s = s.replace(old.replace('\n', nl), new.replace('\n', nl))
a = '#include "mod/game/safe_memory.h"' + nl
assert s.count(a) == 1
s = s.replace(a, '#include "mod/game/info_row.h"' + nl + a)
s = s.replace('constexpr uintptr_t kFieldInfoGetter = 0x57B290;  // (TS, u16 index) -> record; FieldInfoKey = u32 [record+8]',
              'constexpr uintptr_t kFieldInfoGetter = 0x57B290;  // 3663: (const u16* index) -> record; FieldInfoKey = u32 [record+8]')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('   far_travel.cpp')

# the layer's direct read of the item table (the equipment watcher)
p = ROOT + 'cd-coop-3446/src/mod/game/offsets_3663.h'
s = open(p, encoding='utf-8', newline='').read()
for u in ('using build_3549::kItemInfoTable_Count;\n', 'using build_3549::kItemInfoTable_Records;\n'):
    assert s.count(u) == 1
    s = s.replace(u, '')
a = 'inline constexpr uintptr_t kItemInfoTableSlot = '
assert s.count(a) == 1
s = s.replace(a, '''// 3663: the item table is { +0x28 count, +0x20 -> entries of 24 bytes, the record pointer at entry + 8 }
// (3576: { +0x08 count, +0x58 -> record pointers }); the getter's load is `mov rcx,[rip+d]` (was `mov rbx`).
inline constexpr uint32_t kItemInfoTable_Count = 0x28;
inline constexpr uint32_t kItemInfoTable_Records = 0x20;
inline constexpr uint32_t kItemInfoTable_Stride = 24, kItemInfoTable_RecordAt = 8;
inline constexpr uint8_t kItemInfoTableLoadReg = 0x0D;  // the ModRM byte of the load (rcx; rbx was 0x1D)
''' + a)
open(p, 'w', encoding='utf-8', newline='').write(s)
p = ROOT + 'cd-coop-3446/src/mod/net/local_capture.cpp'
s = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if s.count('\r\n') * 2 > s.count('\n') else '\n'
for old, new in (('        auto record = game::read_ptr(*records + index * 8ull);',
                  '        auto record = game::read_ptr(*records + index * static_cast<uint64_t>(off::kItemInfoTable_Stride) +\n'
                  '                                     off::kItemInfoTable_RecordAt);'),
                 ('        if (load[0] != 0x48 || load[1] != 0x8B || load[2] != 0x1D ||',
                  '        if (load[0] != 0x48 || load[1] != 0x8B || load[2] != off::kItemInfoTableLoadReg ||')):
    assert s.count(old) == 1, old
    s = s.replace(old, new.replace('\n', nl))
open(p, 'w', encoding='utf-8', newline='').write(s)
print('   layer item table')
