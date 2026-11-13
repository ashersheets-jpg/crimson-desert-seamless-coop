"""Which fields moved inside the game's structures between 3576 and 3663, as far as OUR code can see it.

For every source file: the game functions its addresses lie in (old build), each paired with its new function; the two
are read side by side (instructions aligned by their shape, numbers masked) and every memory operand whose
displacement differs (not rip-relative, not on rsp / rbp: those are stack slots) is counted as "old -> new".
Then: the lines of that file that hold one of the OLD displacements as a constant. Those are the constants to check.
  python structdiff.py native|layer [file filter]
"""
import difflib, json, re, sys
from collections import Counter, defaultdict

import capstone
import portmap as p
import fmap

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/'
REPO = {'native': ROOT + 'upstream/cdmp-native/', 'layer': ROOT + 'upstream/cd-coop-3446/'}
X = capstone.x86
STACK = (X.X86_REG_RSP, X.X86_REG_RBP, X.X86_REG_RIP)
_num = re.compile(r'0x[0-9a-f]+|\b\d+\b')


def insns(img, begin, end):
    out = []
    for ins in p.md.disasm(img.rd(begin, end - begin), begin):
        mem = [(op.mem.base, op.mem.disp) for op in ins.operands if op.type == X.X86_OP_MEM]
        out.append(((ins.mnemonic, _num.sub('#', ins.op_str)), mem))
    return out


def func_diff(i):
    """Counter of (old disp, new disp) over the aligned instructions of old function i and its pair."""
    c = Counter()
    if i not in fmap.PAIRS:
        return c
    ob, oe = fmap.A[i]
    nb, ne = fmap.B[fmap.PAIRS[i]]
    if oe - ob > 60000:
        return c
    a, b = insns(p.OLD, ob, oe), insns(p.NEW, nb, ne)
    sm = difflib.SequenceMatcher(None, [x[0] for x in a], [x[0] for x in b], autojunk=False)
    for tag, a0, a1, b0, b1 in sm.get_opcodes():
        if tag != 'equal':
            continue
        for k in range(a1 - a0):
            for (r1, d1), (r2, d2) in zip(a[a0 + k][1], b[b0 + k][1]):
                if r1 in STACK or r2 in STACK or r1 == 0:
                    continue
                if d1 != d2 and 0 < d1 < 0x4000 and 0 < d2 < 0x4000:
                    c[(d1, d2)] += 1
    return c


def main(tree, filt):
    m = json.load(open(ROOT + 'build/native-port/map_3663_%s.json' % tree))
    base = set(json.load(open(ROOT + 'build/native-port/map_3663_%s_base.json' % tree)))
    by_file = defaultdict(set)
    for k, e in m.items():
        if k in base or e['kind'] != 'code':
            continue
        i = fmap.func(int(k, 16))
        if i is None:
            continue
        for at in e['at']:
            by_file[at.rsplit(':', 1)[0]].add(i)
    cache = {}
    total = Counter()
    for f in sorted(by_file):
        if filt and filt not in f:
            continue
        c = Counter()
        for i in by_file[f]:
            if i not in cache:
                cache[i] = func_diff(i)
            c += cache[i]
        if not c:
            continue
        total += c
        olds = defaultdict(list)
        for (d1, d2), n in c.items():
            olds[d1].append((d2, n))
        try:
            lines = open(REPO[tree] + f, encoding='utf-8', errors='replace').read().split('\n')
        except FileNotFoundError:
            continue
        hits = []
        for ln, s in enumerate(lines, 1):
            code = s.split('//')[0]
            for mm in re.finditer(r'\b0x([0-9A-Fa-f]{2,4})\b', code):
                v = int(mm.group(1), 16)
                if v in olds and v >= 0x20:
                    hits.append('      L%d %s   <- 0x%X now %s' % (ln, s.strip()[:150], v, ', '.join('0x%X (x%d)' % t for t in sorted(olds[v], key=lambda t: -t[1])[:3])))
        print('== %s: %d functions; moved: %s' % (f.replace('src/mod/', ''), len(by_file[f]),
              ' '.join('0x%X->0x%X(x%d)' % (a, b, n) for (a, b), n in c.most_common(14))))
        for h in hits[:40]:
            print(h)
    print('ALL: ' + ' '.join('0x%X->0x%X(x%d)' % (a, b, n) for (a, b), n in total.most_common(60)))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else '')
