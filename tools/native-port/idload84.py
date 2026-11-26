# Port 3732 -> 3840 (2026-11-26, found by pp66-fresh): identity_probe.cpp's shared LoadComponentsFromFile entry hook
# (identity_fix's C2 fix and guest_save_host's own save use it; on by default) kept 3732's prologue (a SHAPE head no
# hand table owned), so C2 refused user 2's logins on 3840. The 3840 function 0x2360510 keeps the five arguments:
# its only caller 0x1AB6981 sets rcx = actor (r14), rdx = &res, r8 = [rdi+0x50] store, r9 = name, [rsp+0x20] = slot
# exactly as 3732's 0x1A18AB1 did (build/port3840/hand/loadcff84.py / loadcff84.txt); the body takes them in r13 /
# rbx / r12 / r9 -> FindSlot. Only the 50 head bytes change (through `mov r13,rcx`). ONE SHOT.
import pathlib, sys
sys.path.insert(0, 'C:/Users/asher/CrimsonDesert-SeamlessCoop/build/port3840')
import portmap as p

head = p.NEW.rd(0x2360510, 50)
assert head[:5] == bytes.fromhex('48895c2408') and head[-3:] == bytes.fromhex('4c8be9'), head.hex()
rows, line = [], []
for i, b in enumerate(head):
    line.append('0x%02X' % b)
    if len(line) == 12:
        rows.append(', '.join(line))
        line = []
if line:
    rows.append(', '.join(line))
new = 'constexpr uint8_t kLoadHead[] = {' + (',\r\n' + ' ' * 33).join(rows) + '};'
P = pathlib.Path('C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/cdmp-native/src/mod/session2/identity_probe.cpp')
s = P.read_bytes().decode('utf-8')
nl = '\r\n' if '\r\n' in s else '\n'
new = new.replace('\r\n', nl)
old_start = s.index('constexpr uint8_t kLoadHead[] = {')
old_end = s.index('};', old_start) + 2
old = s[old_start:old_end]
assert '0x49, 0x8B, 0xD9' in old and s.count('constexpr uint8_t kLoadHead[] = {') == 1
s = s[:old_start] + new + s[old_end:]
a = 'constexpr uintptr_t kLoad = 0x2360510;  // LoadComponentsFromFile(actor, res*, store, name, u32 slot) -> res'
assert s.count(a) == 1
s = s.replace(a, a + nl + '// 1.0.0.3840: the same five arguments (the only caller 0x1AB6981 sets them as 3732\'s did); the prologue changed: mov [rsp+8],rbx ;'
              + nl + '// mov [rsp+0x18],r8 ; mov [rsp+0x10],rdx ; 7 pushes ; lea rbp,[rsp-0x220] ; sub rsp,0x320 ; mov r12,r8 ; mov rbx,rdx ;'
              + nl + '// mov r13,rcx (3732: mov [rsp+0x10],rdx ; mov [rsp+8],rcx ; 8 pushes ; ... ; mov r14,rcx).')
P.write_bytes(s.encode('utf-8'))
print('kLoadHead ->', head.hex())
