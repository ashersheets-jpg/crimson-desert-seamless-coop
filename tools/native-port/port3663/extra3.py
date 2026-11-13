import struct
import portmap as p
B = 0x140000000
print('battle vt new', hex(96980128))
def ptrs(img, t):
    pat = struct.pack('<Q', B + t); out = []; i = img.d.find(pat)
    while i >= 0 and len(out) < 8:
        # file off -> rva
        for n, va, vs, ro, rs, c in img.secs:
            if ro <= i < ro + rs: out.append(va + i - ro)
        i = img.d.find(pat, i + 1)
    return out
for name, a in [('kActionLayersAdvance', 0x2331320), ('kPending', 0x2331990), ('kCheckAIBattleType', 0x24C1C60)]:
    print(name, [hex(x) for x in ptrs(p.OLD, a)])
nv = 96980128
print('new battle slot1', hex(struct.unpack('<Q', p.NEW.rd(nv + 8, 8))[0] - B), 'old head', p.OLD.rd(0x24C1C60, 8).hex(' '))
