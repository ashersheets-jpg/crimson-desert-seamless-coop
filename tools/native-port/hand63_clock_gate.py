# Port 3576 -> 3663 by hand: session2/clock_gate.cpp (2026-11-13). ONE SHOT.
# The inlined SyncFieldTimeOnMove in 0x2E62600 -> 0x2ECA480: the same instructions, but the destination field is in
# RSI now (was RDI):
#   0x2ECAABD  cmp [rsi+0x180], eax ; jne 0x2ECABA4        (3576 0x2E62C28: cmp [rdi+0x180], eax)
#   0x2ECABA4  mov rbx,[r15] ; lea rdx,[rsp+0x70] ; mov rcx,rsi ; call getter 0x231A180 ; mov rdx,rax ; mov rcx,rbx ;
#              call SetFieldTime 0x23E17C0                  (3576 0x2E62D11, mov rcx,rdi)
# The bytes are read from the 3663 exe here, not typed.
import sys
sys.path.insert(0, 'C:/Users/asher/CrimsonDesert-SeamlessCoop/build/port3663')
import portmap as p

P = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/cdmp-native/src/mod/session2/clock_gate.cpp'
s = open(P, encoding='utf-8', errors='surrogateescape', newline='').read()
nl = '\r\n' if s.count('\r\n') * 2 > s.count('\n') else '\n'


def arr(b, per=9, indent=46):
    rows = [', '.join('0x%02X' % x for x in b[i:i + per]) for i in range(0, len(b), per)]
    return (',' + nl + ' ' * indent).join(rows)


cmp_at, call_at = 0x2ECAABD, 0x2ECABA4
cmp_b, call_b = p.NEW.rd(cmp_at, 12), p.NEW.rd(call_at, 27)
assert cmp_b[:6] == bytes.fromhex('398680010000') and cmp_b[6:8] == b'\x0f\x85', cmp_b.hex()
assert call_b[:11] == bytes.fromhex('498b1f488d542470488bce') and call_b[11] == 0xE8 and call_b[22] == 0xE8, call_b.hex()
a = s.index('constexpr uintptr_t kTravelOwnerCmp = ')
b = s.index('constexpr uintptr_t kTravelResume = ')
new = ('constexpr uintptr_t kTravelOwnerCmp = 0x%X;  // 3663: cmp [rsi+0x180], eax ; jne 0x2ECABA4 (3576 0x2E62C28, the field in rdi)' % cmp_at + nl +
       'constexpr uint8_t kTravelOwnerCmpBytes[] = {' + ', '.join('0x%02X' % x for x in cmp_b) + '};' + nl +
       'constexpr uintptr_t kTravelGetterCall = 0x%X;  // mov rbx,[r15]; getter(rsi); SetFieldTime(rbx, ft) (3576 0x2E62D11)' % call_at + nl +
       'constexpr uint8_t kTravelGetterCallBytes[] = {' + arr(call_b) + '};' + nl)
s = s[:a] + new + s[b:]
old = '        const uintptr_t field = ctx.rdi;'
assert s.count(old) == 1
s = s.replace(old, '        const uintptr_t field = ctx.rsi;  // 3663: the destination field is in rsi (3576 and before: rdi)')
old = '// 0x2E61990 here. FM = r15, the destination field = rdi, TS = [FM].'
assert s.count(old) == 1
s = s.replace(old, '// 0x2E61990 here. FM = r15, the destination field = rdi (1.0.0.3663: RSI), TS = [FM].')
open(P, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(s)
print('clock_gate.cpp: owner cmp', cmp_b.hex(' '), '| getter call', call_b.hex(' '))
# the other guards of this file against the new exe
import re
for name, addr in re.findall(r'constexpr uintptr_t (k\w+) = (0x[0-9A-Fa-f]+);', s):
    m = re.search(r'constexpr uint8_t %s(?:Head|Bytes)\[\] = \{([^}]*)\}' % re.escape(name), s)
    if not m:
        continue
    want = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', m.group(1)))
    got = p.NEW.rd(int(addr, 16), len(want))
    print('  %-20s %s  %s' % (name, addr, 'matches the 3663 exe' if got == want else 'DIFFERS: exe has ' + got.hex(' ')))
