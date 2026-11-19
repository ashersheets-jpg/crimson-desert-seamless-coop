import portmap as p, struct
from extra import callers
def start(img, a):
    while img.rd(a - 2, 2) != b'\xcc\xcc': a -= 1
    return a
for site in (0x2331320, 0x2331990):
    s = start(p.OLD, site)
    cs = callers(p.OLD, s)
    print(hex(site), 'fn', hex(s), '+%X' % (site - s), 'callers', [hex(c) for c in cs])
    for c in cs:
        ns, hits, _ = p.code(c, 32)
        if ns is None: print('  caller unmapped'); continue
        b = p.NEW.rd(ns, 5); nt = ns + 5 + struct.unpack('<i', b[1:5])[0]
        ok = p.OLD.rd(site, 12) == p.NEW.rd(nt + site - s, 12)
        print('  caller', hex(c), '->', hex(ns), 'hits', hits, 'new fn', hex(nt), 'site', hex(nt + site - s), 'bytes same' if ok else 'bytes DIFFER')
