"""3549 -> 3576: the values map_all.py does not cover (template copies via their callers, vtable slots, heads)."""
import struct, numpy as np
import portmap as p

def callers(img, t, limit=6):
    out = []
    for _n, va, vs, ro, rs, _c in img.code:
        n = min(vs, rs)
        b = np.frombuffer(img.d, np.uint8, n, ro)
        for op in (0xE8, 0xE9):
            idx = np.nonzero(b[:-5] == op)[0]
            rel = np.frombuffer(img.d, '<i4', count=n // 4, offset=ro)  # unused
            for i in idx:
                r = struct.unpack_from('<i', img.d, ro + i + 1)[0]
                if va + i + 5 + r == t:
                    out.append(va + int(i))
                    if len(out) >= limit: return out
    return out

def via_callers(t):
    res = {}
    for s in callers(p.OLD, t):
        ns, hits, _ = p.code(s, 32)
        if ns is None: continue
        b = p.NEW.rd(ns, 5)
        if b[0] in (0xE8, 0xE9):
            nt = ns + 5 + struct.unpack('<i', b[1:5])[0]
            res[hex(s)] = (hex(ns), hits, hex(nt))
    return res

for name, a in [('kActionLayersAdvance', 0x2331320), ('kPending', 0x2331990)]:
    print(name, hex(a), via_callers(a))
for name, a in [('kLayerSample', 0x968F50), ('kCcSlot18', 0x943910), ('kCheckAIBattleType', 0x24C1C60)]:
    new, hits, second = p.code(a)
    print(name, hex(a), '->', hex(new) if new else None, hits, 'head same' if new and p.OLD.rd(a,16)==p.NEW.rd(new,16) else 'head CHANGED',
          p.NEW.rd(new, 8).hex(' ') if new else '')
# vtables: CharacterControl slots 16/18/24, NormalMovementMethod slot 2, CheckAIBattleType vtable 0x5C7CAD0 (slot 1 at +8)
B = 0x140000000
def slot(img, vt, i):
    return struct.unpack('<Q', img.rd(vt + 8 * i, 8))[0] - B
print('CC vt new slots 16/18/24', [hex(slot(p.NEW, 0x59459F0, i)) for i in (16, 18, 24)], 'old', [hex(slot(p.OLD, 0x59438F0, i)) for i in (16, 18, 24)])
print('NMM slot2 new', hex(slot(p.NEW, 0x595AD08, 2)), 'old', hex(slot(p.OLD, 0x5958C10, 2)))
print('battle vt old slot1', hex(slot(p.OLD, 0x5C7CAD0, 1)))
