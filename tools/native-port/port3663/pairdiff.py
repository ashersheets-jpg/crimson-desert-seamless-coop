"""fdiff for an OLD address only: the function holding it and its paired new function, diffed (python pairdiff.py <old hex> [filter]).
   With a filter (a regex), only diff hunks whose changed lines match it are printed."""
import difflib, re, sys
import portmap as p
import fmap


def listing(img, b, e):
    out = []
    for ins in p.md.disasm(img.rd(b, e - b), b):
        s = '%-8s %s' % (ins.mnemonic, ins.op_str)
        if ins.mnemonic.startswith('j') or ins.mnemonic == 'call':
            s = re.sub(r'0x[0-9a-f]+', 'ADDR', s)
        s = re.sub(r'rip [+-] 0x[0-9a-f]+', 'rip + D', s)
        out.append(s)
    return out


old = int(sys.argv[1], 16)
flt = re.compile(sys.argv[2]) if len(sys.argv) > 2 else None
i = fmap.func(old)
if i is None or i not in fmap.PAIRS:
    sys.exit('0x%X: no paired function' % old)
ob, oe = fmap.A[i]
nb, ne = fmap.B[fmap.PAIRS[i]]
print('old 0x%X..0x%X -> new 0x%X..0x%X (%s)' % (ob, oe, nb, ne, fmap.HOW.get(i)))
a, b = listing(p.OLD, ob, oe), listing(p.NEW, nb, ne)
hunk = []


def flush():
    if hunk and (flt is None or any(flt.search(x) for x in hunk if x[:1] in '+-')):
        print('\n'.join(hunk))


for line in difflib.unified_diff(a, b, 'old', 'new', n=2, lineterm=''):
    if line.startswith('@@'):
        flush()
        hunk = [line]
    elif not line.startswith(('---', '+++')):
        hunk.append(line)
flush()
