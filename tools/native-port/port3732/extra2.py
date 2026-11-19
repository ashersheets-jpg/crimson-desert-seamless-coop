import struct
import portmap as p
B = 0x140000000
for name, a in [('kActionLayersAdvance', 0x2331320), ('kPending', 0x2331990), ('battle vt', 0x5C7CAD0)]:
    sites = p.xrefs(p.OLD, a)
    print(name, hex(a), 'old refs', [hex(s) for s in sites], 'mapped', p.data(a))
