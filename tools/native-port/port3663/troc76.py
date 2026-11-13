"""Troc opcode table (name -> u16) of 3549 vs 3576, read statically like tools/native-port/troc49.py."""
import csv, re, struct
import portmap as p
IB = 0x140000000
def table(img, begin, end):
    def rtti(vt):
        try:
            col = struct.unpack('<Q', img.rd(vt - 8, 8))[0] - IB
            td = struct.unpack('<I', img.rd(col + 12, 4))[0]
            raw = img.rd(td + 0x10, 200).split(b'\0')[0].decode('ascii', 'replace')
        except Exception:
            return None
        m = re.match(r'\.\?AVTrocTr(\w+?)@pa@@', raw)
        return m.group(1) if m else None
    out = {}
    pat = re.compile(rb'\x48\x8D([\x05\x0D\x15\x1D\x35\x3D])(....)\x48\x89(\1)(....)', re.S)
    for _n, va, vs, ro, rs, _c in img.code:
        blob = img.d[ro:ro + min(vs, rs)]
        for m in pat.finditer(blob):
            a = va + m.start()
            vt = a + 7 + struct.unpack('<i', m.group(2))[0]
            st_at = va + m.start(3) - 2
            inst = st_at + 7 + struct.unpack('<i', m.group(4))[0]
            if not (begin <= inst < end): continue
            name = rtti(vt)
            if not name: continue
            op = struct.unpack('<H', img.rd(inst + 0x0C, 2))[0]
            out.setdefault(name, op)
    return out
old = table(p.OLD, 0x6E8ED10, 0x6EC2378)
new = table(p.NEW, 0x6E90D10, 0x6EC4378)
both = [n for n in old if n in new]
same = [n for n in both if old[n] == new[n]]
print('3549', len(old), '3576', len(new), 'both', len(both), 'same id', len(same), 'new-only', sorted(set(new) - set(old))[:20], 'gone', sorted(set(old) - set(new))[:20])
with open('troc_3549_3576.csv', 'w', newline='') as f:
    w = csv.writer(f); w.writerow(['name', 'id_3549', 'id_3576'])
    for n in sorted(set(old) | set(new)): w.writerow([n, old.get(n, ''), new.get(n, '')])
for n in ('SetFieldTimeReq','SetFieldTimeAck','ActorLoadDataAck','RemoveActorAck','MoveActorReq','MoveActorAck','StartActionReq','StartActionAck','RotateActorReq','RotateActorAck','UpdateWallUpDataAck','UnequipItemAck','OtherEquipItemAck','OtherUnequipItemAck','CreateOrChangePinMarkerAck','RemovePinMarkerAck','ChangeDockingSocketByFrameEventAck','RemoteCatchResultAck','UpdateOnDemandGimmickAck'):
    print('%-36s %5s -> %5s' % (n, old.get(n), new.get(n)))
