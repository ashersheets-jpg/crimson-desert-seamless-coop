import portmap as p, struct
def call_t(img, at):
    b = img.rd(at, 5)
    return at + 5 + struct.unpack('<i', b[1:5])[0] if b and b[0] == 0xE8 else None
rx, used = p.pattern(p.OLD, 0x23312D0, 0x1A0)
print('pattern bytes', used)
o = sorted(p.find(p.OLD, rx)); n = sorted(p.find(p.NEW, rx))
print('old copies', len(o), 'new copies', len(n))
for name, f in (('LayersAdvance', 0x23312D0), ('Pending', 0x2331940)):
    i = o.index(f)
    print(name, 'index', i, 'old', hex(f), '-> new by index', hex(n[i]) if len(o) == len(n) else '?',
          'neighbors old', [hex(x) for x in o[max(0,i-2):i+3]])
    print('   new near', [hex(x) for x in n[max(0,i-2):i+3]])
    print('   +F1 old', hex(call_t(p.OLD, f + 0xF1)), 'new', hex(call_t(p.NEW, n[i] + 0xF1)), '+3C new', hex(call_t(p.NEW, n[i] + 0x3C)))
