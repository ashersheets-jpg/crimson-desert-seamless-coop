"""Linear trace of the manager pointer (rcx at entry) through each scene-Req callee on 3732: register copies and stack
spills followed; the first `test r,r` / `cmp r,0` on it (SAFE before use) or the first memory operand based on it
(DEREF). Branches are not followed (the straight path from the entry); good enough to see which comes first."""
import re
import capstone
import portmap as p

CALLEES = {2654: 0x2B56AD0, 3141: 0x2B571B0, 2563: 0x2B5A150, 2313: 0x2B57E50, 2546: 0x2B57940}
SITES = {2644: 0x2D55B39, 2672: 0x2D5569A, 2678: 0x2D549DA, 2125: None}
R64 = {'rax', 'rbx', 'rcx', 'rdx', 'rsi', 'rdi', 'rbp', 'rsp', 'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15'}


def trace(start, reg='rcx', n=400):
    held = {reg}
    slots = set()
    for ins in p.NEW.insns(start, n * 8)[:n]:
        m, ops = ins.mnemonic, ins.op_str
        # a test of a held register
        if m == 'test':
            a = [x.strip() for x in ops.split(',')]
            if a[0] == a[1] and a[0] in held:
                return 'SAFE: %X %s %s' % (ins.address, m, ops)
        if m == 'cmp' and ops.split(',')[1].strip() == '0' and ops.split(',')[0].strip() in held:
            return 'SAFE: %X %s %s' % (ins.address, m, ops)
        # a memory operand based on a held register (a load / store through it)
        mem = re.findall(r'\[([a-z0-9]+)(?: [+-][^\]]*)?\]', ops)
        for base in mem:
            if base in held and m not in ('lea',):
                return 'DEREF: %X %s %s' % (ins.address, m, ops)
        parts = [x.strip() for x in ops.split(',', 1)]
        if m == 'mov' and len(parts) == 2:
            dst, src = parts
            if src in held and dst in R64:
                held.add(dst)
                continue
            if src in held and dst.startswith('qword ptr [rsp') or src in held and dst.startswith('qword ptr [rbp'):
                slots.add(dst)
                continue
            if src in slots and dst in R64:
                held.add(dst)
                continue
            if dst in held and src not in held:
                held.discard(dst)
        if m in ('call', 'ret', 'jmp'):
            # after a call, volatile registers are gone
            if m != 'call':
                return 'END at %X %s' % (ins.address, m)
            held -= {'rax', 'rcx', 'rdx', 'r8', 'r9', 'r10', 'r11'}
            if not held and not slots:
                return 'LOST at call %X (not kept)' % ins.address
    return 'no decision in %d instructions (held %s)' % (n, sorted(held))


for op, a in CALLEES.items():
    print(op, 'callee 0x%X:' % a, trace(a))
for op, a in SITES.items():
    if a:
        print(op, 'site 0x%X (rax):' % a, trace(a, 'rax'))
