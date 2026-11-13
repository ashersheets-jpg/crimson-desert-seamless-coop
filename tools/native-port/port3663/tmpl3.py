import portmap as p, struct
def call_t(img, at):
    b = img.rd(at, 5)
    return at + 5 + struct.unpack('<i', b[1:5])[0] if b[0] == 0xE8 else None
for site in (0x2331320, 0x2331990):
    c_at = site - 0x14
    old_callee = call_t(p.OLD, c_at)
    new_callee, hits, _ = p.code(old_callee)
    print(hex(site), 'callee', hex(old_callee), '->', hex(new_callee) if new_callee else None, 'hits', hits)
    rx, _ = p.pattern(p.OLD, c_at, 40)
    cands = [h + 0x14 for h in p.find(p.NEW, rx) if call_t(p.NEW, h) == new_callee]
    print('   candidates', [hex(c) for c in cands])
