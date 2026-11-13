"""Hand look at addresses the map could not place.  python look.py stubs <tree>   |   python look.py at <old hex> [n]
stubs: every weak:gap-guess of the tree: the old and the new bytes; a 5-byte jmp at both places = a protection stub
       whose neighbours agree on the place (the jmp target changes with every build)."""
import json, sys
import portmap as p
import fmap

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/'


def dis(img, at, n):
    out = []
    for ins in p.md.disasm(img.rd(at, n * 15 + 16), at):
        out.append('   %X: %-8s %s' % (ins.address, ins.mnemonic, ins.op_str))
        if len(out) == n:
            break
    return '\n'.join(out)


def stubs(tree):
    m = json.load(open(ROOT + 'build/native-port/map_3663_%s.json' % tree))
    v = json.load(open(ROOT + 'build/native-port/map_3663_%s_verdict.json' % tree))
    base = set(json.load(open(ROOT + 'build/native-port/map_3663_%s_base.json' % tree)))
    for k, (st, nv) in v.items():
        if k in base or not st.startswith('weak:gap-guess'):
            continue
        old = int(k, 16)
        ob, nb = p.OLD.rd(old, 12), p.NEW.rd(nv, 12)
        jmp = ob[:1] == b'\xe9' and nb[:1] == b'\xe9'
        same_tail = ob[5:] == nb[5:]
        print('%s 0x%X -> 0x%X  old %s | new %s  %s  %s' % ('STUB' if jmp else '????', old, nv, ob.hex(' '), nb.hex(' '),
              'tail same' if same_tail else 'tail differs', m[k]['at'][0]))


def at(old, n=10):
    i = fmap.func(old)
    print('old 0x%X' % old, 'function', i is not None and ('0x%X..0x%X' % fmap.A[i]), 'paired' if i in fmap.PAIRS else 'NOT paired',
          fmap.HOW.get(i))
    back = old
    if i is not None:
        # start a few instructions earlier, on an instruction boundary
        starts = [ins.address for ins in p.md.disasm(p.OLD.rd(fmap.A[i][0], fmap.A[i][1] - fmap.A[i][0]), fmap.A[i][0])]
        before = [a for a in starts if a <= old]
        back = before[-4] if len(before) >= 4 else fmap.A[i][0]
        if old not in starts:
            print('   (not on an instruction boundary of a straight read)')
    print(dis(p.OLD, back, n + 4))
    if i is not None and i in fmap.PAIRS:
        nb, ne = fmap.B[fmap.PAIRS[i]]
        ob, oe = fmap.A[i]
        print('new function 0x%X..0x%X (size %d, old %d); same offset 0x%X' % (nb, ne, ne - nb, oe - ob, nb + old - ob))
        want = fmap.shape(p.OLD, old, 3)
        for a in fmap.sweep(nb, ne):
            if fmap.shape(p.NEW, a, 3) == want:
                print(' shape match (3 insns) at 0x%X (+0x%X; old +0x%X)' % (a, a - nb, old - ob))
                print(dis(p.NEW, a, 5))


if __name__ == '__main__':
    if sys.argv[1] == 'stubs':
        stubs(sys.argv[2])
    else:
        at(int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 10)
