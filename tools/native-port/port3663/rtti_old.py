"""vtable(s) of an RTTI class name in the NEW (3576) exe: name -> type descriptor -> complete object locators -> vtables."""
import struct, sys
import numpy as np
import portmap as p
IMG = p.OLD
B = 0x140000000
def file_to_rva(o):
    for n, va, vs, ro, rs, ch in IMG.secs:
        if ro <= o < ro + rs: return va + o - ro
def vtables(name):
    d = IMG.d
    i = d.find(name.encode())
    out = []
    while i >= 0:
        td = file_to_rva(i - 0x10)
        # COLs: signature 1, ..., pTypeDescriptor (rva) at +0x0C, pSelf at +0x14
        for n, va, vs, ro, rs, ch in IMG.secs:
            if ch & 0x20000000 or rs < 0x1000: continue
            arr = np.frombuffer(d, dtype='<u4', count=rs // 4, offset=ro)
            for k in np.nonzero(arr == td)[0]:
                col = va + int(k) * 4 - 0x0C
                if struct.unpack('<I', IMG.rd(col, 4))[0] != 1: continue
                # vtable: a qword == B + col, vtable starts right after it
                q = np.frombuffer(d, dtype='<u8', count=rs // 8, offset=ro)
                for j in np.nonzero(q == B + col)[0]:
                    out.append(va + int(j) * 8 + 8)
        i = d.find(name.encode(), i + 1)
    return sorted(set(out))
if __name__ == '__main__':
    for nm in sys.argv[1:]:
        vts = vtables(nm)
        print(nm, [hex(v) for v in vts])
        for v in vts[:2]:
            print('  slots', [hex(struct.unpack('<Q', IMG.rd(v + 8 * s, 8))[0] - B) for s in range(8)])
