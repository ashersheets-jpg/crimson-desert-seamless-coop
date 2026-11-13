"""Every call rel32 (E8) to a target, in every executable section, 3549 and 3576 (Denuvo's code too)."""
import struct, sys, numpy as np
import portmap as p
def calls(img, t):
    out = []
    for n, va, vs, ro, rs, ch in img.secs:
        if not ch & 0x20000000: continue
        size = min(vs, rs)
        b = np.frombuffer(img.d, np.uint8, size, ro)
        idx = np.nonzero(b[:-5] == 0xE8)[0]
        rel = np.array([0], dtype=np.int64)
        if len(idx) == 0: continue
        r = np.frombuffer(img.d, '<i4', count=1, offset=ro)  # placeholder
        # vectorised rel32 read
        raw = np.frombuffer(img.d, np.uint8, size, ro)
        i = idx[idx + 5 <= size]
        rel = (raw[i + 1].astype(np.int64) | (raw[i + 2].astype(np.int64) << 8) | (raw[i + 3].astype(np.int64) << 16) | (raw[i + 4].astype(np.int64) << 24))
        rel = np.where(rel >= 1 << 31, rel - (1 << 32), rel)
        tgt = va + i + 5 + rel
        out += [(n, va + int(k)) for k in i[tgt == t]]
    return out
pairs = [(0x19B1610, 'LoadSlot'), (0x19B0310, 'Save'), (0x19B3C60, 'Flush')]
from heads_map import inv
for new, nm in pairs:
    old = inv[new]
    print(nm, 'old', hex(old), [(s, hex(a)) for s, a in calls(p.OLD, old)])
    print(nm, 'new', hex(new), [(s, hex(a)) for s, a in calls(p.NEW, new)])
