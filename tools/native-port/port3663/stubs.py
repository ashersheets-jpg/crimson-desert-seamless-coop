"""Protected stubs (5-byte jmp) 3549 -> 3576: through the vtable slots that hold them (RTTI class name) or their
callers (call/jmp rel32 sites mapped by code())."""
import struct, sys, numpy as np
import portmap as p, rtti_vt
from extra import callers
IB = 0x140000000
def rtti(img, vt):
    try:
        col = struct.unpack('<Q', img.rd(vt - 8, 8))[0] - IB
        if struct.unpack('<I', img.rd(col, 4))[0] != 1: return None
        td = struct.unpack('<I', img.rd(col + 0x0C, 4))[0]
        return img.rd(td + 0x10, 200).split(b'\0')[0].decode('latin1')
    except Exception: return None
def ptr_refs(img, t, limit=12):
    pat = struct.pack('<Q', IB + t); out = []; i = img.d.find(pat)
    while i >= 0 and len(out) < limit:
        for n, va, vs, ro, rs, c in img.secs:
            if ro <= i < ro + rs: out.append(va + i - ro)
        i = img.d.find(pat, i + 1)
    return out
def vt_start(img, at):
    # walk back to the first slot (the qword before it is a COL pointer with signature 1)
    a = at
    for _ in range(400):
        if rtti(img, a): return a
        a -= 8
    return None
def resolve(s):
    out = []
    for r in ptr_refs(p.OLD, s):
        vt = vt_start(p.OLD, r)
        if vt is None: continue
        nm = rtti(p.OLD, vt); slot = (r - vt) // 8
        rtti_vt.IMG = p.NEW
        nvs = rtti_vt.vtables(nm)
        rtti_vt.IMG = p.OLD
        ovs = rtti_vt.vtables(nm)
        if vt in ovs and len(nvs) == len(ovs):
            nvt = nvs[ovs.index(vt)]
            out.append(('vt', nm, slot, hex(struct.unpack('<Q', p.NEW.rd(nvt + 8 * slot, 8))[0] - IB)))
    for c in callers(p.OLD, s, 4):
        ns, hits, _ = p.code(c, 32)
        if ns is None: continue
        b = p.NEW.rd(ns, 5)
        if b[0] in (0xE8, 0xE9):
            out.append(('call', hex(c), hits, hex(ns + 5 + struct.unpack('<i', b[1:5])[0])))
    return out
for a in sys.argv[1:]:
    s = int(a, 16)
    r = resolve(s)
    print(hex(s), p.OLD.rd(s, 5).hex(' '), r)
