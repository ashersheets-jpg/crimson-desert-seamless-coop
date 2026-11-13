"""How often do the game functions a source file names read given displacements, on the old and on the new build?
   python dispcount.py <file substring> <hex disp> [<hex disp> ...]
A displacement our code uses that the old functions read N times and the new ones 0 times has moved."""
import json, re, sys
import portmap as p
import fmap

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/'
want = sys.argv[1]
disps = ['0x%x' % int(x, 16) for x in sys.argv[2:]]
fs = set()
for tree in ('native', 'layer'):
    m = json.load(open(ROOT + 'build/native-port/map_3663_%s.json' % tree))
    for k, v in m.items():
        if any(want in a for a in v.get('at', [])):
            i = fmap.func(int(k, 16))
            if i is not None and i in fmap.PAIRS:
                fs.add(i)
print(len(fs), 'paired functions named by', want)
tot = {}
for i in sorted(fs):
    ob, oe = fmap.A[i]
    nb, ne = fmap.B[fmap.PAIRS[i]]
    row = {}
    for name, img, b, e in (('old', p.OLD, ob, oe), ('new', p.NEW, nb, ne)):
        for ins in p.md.disasm(img.rd(b, e - b), b):
            for m in re.finditer(r'[+] (0x[0-9a-f]+)\]', ins.op_str):
                if m.group(1) in disps and 'rsp' not in ins.op_str and 'rbp' not in ins.op_str:
                    row[(name, m.group(1))] = row.get((name, m.group(1)), 0) + 1
    if row:
        print('  old %X new %X: %s' % (ob, nb, '  '.join('%s %d/%d' % (d, row.get(('old', d), 0), row.get(('new', d), 0)) for d in disps
                                                         if row.get(('old', d)) or row.get(('new', d)))))
        for k, v in row.items():
            tot[k] = tot.get(k, 0) + v
print('TOTAL old/new: ' + '  '.join('%s %d/%d' % (d, tot.get(('old', d), 0), tot.get(('new', d), 0)) for d in disps))
