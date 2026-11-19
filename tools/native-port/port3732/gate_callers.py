"""The ready check (2026-11-14): the host client's callers of PostPacket for its own story volume entry (2808) and the
scene's branch (2644), from the stack scan of cs8d. For each return address: the call before it, the function start
(the CC padding before it) and the function's first instructions."""
import sys
import portmap as p

IMG = p.NEW
BASE = 0  # portmap reads RVAs


def dis(at, n):
    out = []
    for ins in p.md.disasm(IMG.rd(at, n * 15 + 16), at):
        out.append('   %X: %-8s %s' % (ins.address - BASE, ins.mnemonic, ins.op_str))
        if len(out) == n:
            break
    return '\n'.join(out)


def func_start(at):
    a = at
    for _ in range(0x6000):
        b = IMG.rd(a - 2, 2)
        if b == b'\xcc\xcc':
            return a
        a -= 1
    return None


def call_before(ret):
    # A call ending at ret: e8 rel32 (5), ff 15 / ff 90.. (6), ff d0 (2), ff 50 xx (3), ff 90 xxxxxxxx (6)
    for back in (5, 6, 2, 3, 7):
        a = ret - back
        for ins in p.md.disasm(IMG.rd(a, back + 8), a):
            if ins.address + ins.size == ret and ins.mnemonic == 'call':
                return '%X: call %s' % (ins.address - BASE, ins.op_str)
            break
    return '(no call found right before)'


for h in sys.argv[1:]:
    ret = BASE + int(h, 16)
    f = func_start(ret)
    print('== ret exe+0x%X | %s | function exe+0x%X' % (ret - BASE, call_before(ret), (f - BASE) if f else 0))
    print(dis(ret - 40, 16))
    if f:
        print('  function head:')
        print(dis(f, 8))
