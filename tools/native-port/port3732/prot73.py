"""Call sites / returns INSIDE the copy protection, 3663 -> 3732 (as pins63.py did for 3576 -> 3663).
3663: protection .data1 0x7AC4000..0x14046000; 3732: .text1 0x7ACB000..0x137DB000.
For each old address: is it a call (E8) or the return after one? The callee is mapped with fmap; the new site is the
k-th call to that callee in the new protection section, k = its rank among the old section's calls to the old callee.
"""
import struct, sys
import portmap as p
import fmap

OLD_LO, OLD_HI = 0x7AC4000, 0x14046000
NEW_LO, NEW_HI = 0x7ACB000, 0x137DB000


def calls_to(img, lo, hi, target):
    """Every E8 rel32 in [lo, hi) whose target is `target` (rva of the call instruction)."""
    out = []
    o = img.off(lo)
    blob = img.d[o:o + (hi - lo)]
    i = blob.find(b'\xE8')
    while i >= 0:
        if i + 5 <= len(blob):
            rel = struct.unpack_from('<i', blob, i + 1)[0]
            if lo + i + 5 + rel == target:
                out.append(lo + i)
        i = blob.find(b'\xE8', i + 1)
    return out


def callee_at(img, a):
    if img.rd(a, 1) == b'\xE8':
        return a + 5 + struct.unpack('<i', img.rd(a + 1, 4))[0]
    return None


for s in sys.argv[1:]:
    a = int(s, 16)
    kind, site = None, None
    if callee_at(p.OLD, a) is not None:
        kind, site = 'call', a
    elif callee_at(p.OLD, a - 5) is not None:
        kind, site = 'return', a - 5
    if not site:
        print('0x%X: neither a call nor after one: %s' % (a, p.OLD.rd(a - 8, 16).hex(' ')))
        continue
    t = callee_at(p.OLD, site)
    if OLD_LO <= t < OLD_HI:
        print('0x%X: %s to 0x%X, a callee inside the protection: by hand' % (a, kind, t))
        continue
    nt, how = fmap.addr(t)
    olds = calls_to(p.OLD, OLD_LO, OLD_HI, t)
    k = olds.index(site) if site in olds else None
    news = calls_to(p.NEW, NEW_LO, NEW_HI, nt) if nt else []
    pick = news[k] if k is not None and len(news) == len(olds) else None
    res = (pick + (5 if kind == 'return' else 0)) if pick is not None else None
    print('0x%X: %s to 0x%X -> 0x%s (%s) | calls in protection old %d new %d | k=%s -> %s' % (
        a, kind, t, ('%X' % nt) if nt else '?', how, len(olds), len(news), k, ('0x%X' % res) if res else 'NONE'))
