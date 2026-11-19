"""Old (3663) code addresses the scan found nowhere (the exact masked pattern has 0 hits, so some non-relative byte
changed: a field offset, an immediate): a LOOSE pattern (every displacement and immediate masked, opcodes and
registers kept) searched in the new code at decreasing lengths; the hits within 0x40000 of the function map's guess
are listed, with the changed numbers. One hit near the guess = the answer.
   python near73.py <old hex> ...    """
import re, sys
import capstone
import portmap as p
import fmap


def loose(img, rva, length):
    out, used, insns = b'', 0, []
    for ins in img.insns(rva, length + 16):
        b = bytes(ins.bytes)
        mask = set()
        if ins.disp_size:
            mask |= set(range(ins.disp_offset, ins.disp_offset + ins.disp_size))
        if ins.imm_size:
            mask |= set(range(ins.imm_offset, ins.imm_offset + ins.imm_size))
        for i, c in enumerate(b):
            out += b'.' if i in mask else re.escape(bytes([c]))
        used += len(b)
        insns.append(ins)
        if used >= length or ins.mnemonic in ('ret', 'int3'):
            break
    return re.compile(out, re.S), used, insns


def text(img, a, n):
    return ' ; '.join('%s %s' % (i.mnemonic, i.op_str) for i in list(img.insns(a, n * 8))[:n])


for s in sys.argv[1:]:
    a = int(s, 16)
    g, how = fmap.addr(a)
    res, pick = [], None
    for L in (128, 96, 64, 48, 32, 24, 16):
        rx, used, _ = loose(p.OLD, a, L)
        hits = p.find(p.NEW, rx)
        near = [h for h in hits if g is None or abs(h - g) < 0x40000]
        res.append('%d:%d/%d' % (used, len(near), len(hits)))
        if len(near) == 1:
            pick = near[0]
            break
        if len(near) > 1 and L == 16:
            res.append('near: ' + ' '.join('0x%X' % h for h in sorted(near, key=lambda h: abs(h - g))[:4]))
    print('0x%X (guess %s %s): %s -> %s' % (a, ('0x%X' % g) if g else '-', how, ' '.join(res), ('0x%X' % pick) if pick else 'NONE'))
    if pick:
        print('     old: ' + text(p.OLD, a, 6))
        print('     new: ' + text(p.NEW, pick, 6))
