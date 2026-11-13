"""Every mid-function address our code uses (hook sites, patch sites, return addresses), 3576 against 3663: the
instructions around it in both builds. A site is CLEAN when the 6 instructions before and the 8 after are the same
text once rip-relative targets and branch / call targets are masked (same registers, same stack slots, same
fields); otherwise both windows are printed for a hand look.
  python midsites.py native|layer [file filter]  -> prints; the summary line counts clean / to look at
"""
import json, re, sys

import capstone
import portmap as p
import fmap

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/'
X = capstone.x86


def norm(ins):
    """Instruction text with rip-relative displacements and branch / call targets masked."""
    t = ins.op_str
    if ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL):
        if ins.operands and ins.operands[0].type == X.X86_OP_IMM:
            t = '<target>'
    t = re.sub(r'\[rip [+-] 0x[0-9a-f]+\]', '[rip+<d>]', t)
    return ins.mnemonic + ' ' + t


def window(img, fb, fe, at, before, after):
    ins = list(p.md.disasm(img.rd(fb, fe - fb), fb))
    idx = next((k for k, i in enumerate(ins) if i.address == at), None)
    if idx is None:
        return None
    w = ins[max(0, idx - before):idx + after]
    return [(i.address, norm(i), i.address == at) for i in w]


def main(tree, filt):
    m = json.load(open(ROOT + 'build/native-port/map_3663_%s.json' % tree))
    v = json.load(open(ROOT + 'build/native-port/map_3663_%s_verdict.json' % tree))
    base = set(json.load(open(ROOT + 'build/native-port/map_3663_%s_base.json' % tree)))
    sys.path.insert(0, ROOT + 'tools/native-port')
    import p63
    mp, _ = p63.mapping(tree)
    clean = look = skipped = 0
    for k, e in sorted(m.items(), key=lambda t: t[1]['at'][0]):
        old = int(k, 16)
        if k in base or e['kind'] != 'code' or old not in mp:
            continue
        if filt and not any(filt in a for a in e['at']):
            continue
        i = fmap.func(old)
        if i is None or i not in fmap.PAIRS or fmap.A[i][0] == old:
            continue                       # a function start, or outside the unwind table
        ob, oe = fmap.A[i]
        nb, ne = fmap.B[fmap.PAIRS[i]]
        if oe - ob > 80000:
            skipped += 1
            continue
        wo = window(p.OLD, ob, oe, old, 6, 8)
        wn = window(p.NEW, nb, ne, mp[old], 6, 8)
        if wo is None or wn is None:
            look += 1
            print('?? 0x%X -> 0x%X %s: not on an instruction boundary of a straight read' % (old, mp[old], e['at'][0]))
            continue
        if [t for _a, t, _h in wo] == [t for _a, t, _h in wn]:
            clean += 1
            continue
        look += 1
        print('== 0x%X -> 0x%X  %s  (%s)' % (old, mp[old], ', '.join(a.replace('src/mod/', '') for a in e['at'][:3]), e.get('f_how')))
        for r in range(max(len(wo), len(wn))):
            a = wo[r] if r < len(wo) else (0, '', False)
            b = wn[r] if r < len(wn) else (0, '', False)
            mark = '  ' if a[1] == b[1] else '!!'
            print('  %s %s%-52s | %s%s' % (mark, '>' if a[2] else ' ', a[1][:52], '>' if b[2] else ' ', b[1][:60]))
    print('SUMMARY %s: clean %d, to look at %d, skipped (huge function) %d' % (tree, clean, look, skipped))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else '')
