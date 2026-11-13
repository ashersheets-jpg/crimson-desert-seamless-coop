import portmap as p, struct
import rtti_vt as r
B = 0x140000000
for ver, kbt in (('3479', 0x24BE2F0), ('3527', 0x24C1C60), ('3549', 0x24C1C60), ('3576', None)):
    img = p.Image(p.ARCHIVE + 'CrimsonDesert-1.0.0.%s.exe' % ver)
    r.IMG = img
    out = []
    for nm in ('.?AVAICondition_CheckAIBattleType@pa@@', '.?AVAICondition_IsKeepAggro@pa@@'):
        vts = r.vtables(nm)
        out.append((nm[13:-5], [hex(v) for v in vts], [hex(struct.unpack('<Q', img.rd(v + 8, 8))[0] - B) for v in vts]))
    print(ver, 'mod uses', hex(kbt) if kbt else '-', out, 'head', img.rd(kbt, 5).hex(' ') if kbt else '')
