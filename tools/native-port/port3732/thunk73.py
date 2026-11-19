"""Old (3663) jump stubs / leaf functions: confirm the new address through their CALLERS. Every old call (E8) or jump
(E9) to the address is mapped with fmap.addr; the new instruction there must be a call/jmp too, and its target is a vote.
   python thunk73.py <old hex> ...    (mid-function addresses: the offset inside the stub is kept)"""
import struct, sys
from collections import Counter
import portmap as p
import fmap


def branch_target(img, a):
    op = img.rd(a, 1)
    if op in (b'\xE8', b'\xE9'):
        return a + 5 + struct.unpack('<i', img.rd(a + 1, 4))[0]
    return None


def refs_to(img, t, limit=40):
    """E8/E9 rel32 sites whose target is t, over every code section (numpy)."""
    import numpy as np
    out = []
    for _n, va, vs, ro, rs, _c in img.code:
        blob = np.frombuffer(img.d, dtype=np.uint8, count=min(vs, rs), offset=ro)
        idx = np.nonzero((blob[:-5] == 0xE8) | (blob[:-5] == 0xE9))[0]
        rel = (blob[idx + 1].astype(np.int64) | (blob[idx + 2].astype(np.int64) << 8) |
               (blob[idx + 3].astype(np.int64) << 16) | (blob[idx + 4].astype(np.int64) << 24))
        rel = np.where(rel >= 2 ** 31, rel - 2 ** 32, rel)
        out += [int(va + h) for h in idx[(va + idx + 5 + rel) == t][:limit]]
    return out


for s in sys.argv[1:]:
    a = int(s, 16)
    g, how = fmap.addr(a)
    callers = refs_to(p.OLD, a)
    votes = Counter()
    for c in callers:
        nc, h = fmap.addr(c)
        if nc is None:
            continue
        t = branch_target(p.NEW, nc)
        if t is not None:
            votes[t] += 1
    top = votes.most_common(3)
    ok = top and top[0][0] == g
    print('0x%X: guess %s | %d old refs | new targets %s | %s' % (
        a, ('0x%X' % g) if g else '-', len(callers), ', '.join('0x%X x%d' % t for t in top) or '-',
        'GUESS CONFIRMED' if ok else ('DIFFERENT' if top else 'no votes')))
