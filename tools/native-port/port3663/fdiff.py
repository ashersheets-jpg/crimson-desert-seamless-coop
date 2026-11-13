"""Two functions side by side, old (3576) against new (3663), with code addresses blanked so only real changes show.
   python fdiff.py <old hex> <new hex> [max insns]      (a unified diff of the two listings)"""
import difflib, re, sys
import portmap as p
import fmap


def listing(img, table, a, cap):
    i = None
    for k, (b, e) in enumerate(table):
        if b == a:
            i = k
            break
    end = table[i][1] if i is not None else a + cap * 6
    out = []
    for ins in p.md.disasm(img.rd(a, end - a), a):
        s = '%-8s %s' % (ins.mnemonic, ins.op_str)
        if ins.mnemonic.startswith('j') or ins.mnemonic == 'call':
            s = re.sub(r'0x[0-9a-f]+', 'ADDR', s)
        s = re.sub(r'rip [+-] 0x[0-9a-f]+', 'rip + D', s)
        out.append(s)
        if len(out) >= cap:
            break
    return out


old, new = int(sys.argv[1], 16), int(sys.argv[2], 16)
cap = int(sys.argv[3]) if len(sys.argv) > 3 else 400
a, b = listing(p.OLD, fmap.A, old, cap), listing(p.NEW, fmap.B, new, cap)
print('old %d insns, new %d insns' % (len(a), len(b)))
for line in difflib.unified_diff(a, b, 'old 0x%X' % old, 'new 0x%X' % new, n=2, lineterm=''):
    print(line)
