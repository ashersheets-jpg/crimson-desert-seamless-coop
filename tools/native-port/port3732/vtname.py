import portmap as p, struct, sys
B = 0x140000000
def name(img, vt):
    col = struct.unpack('<Q', img.rd(vt - 8, 8))[0] - B
    td = struct.unpack('<I', img.rd(col + 0x0C, 4))[0]
    return img.rd(td + 0x10, 80).split(b'\0')[0].decode('latin1')
def slot(img, vt, i): return struct.unpack('<Q', img.rd(vt + 8*i, 8))[0] - B
print('old 0x5C7CAD0', name(p.OLD, 0x5C7CAD0), hex(slot(p.OLD, 0x5C7CAD0, 1)))
print('new 0x5C7CCA0', name(p.NEW, 0x5C7CCA0), hex(slot(p.NEW, 0x5C7CCA0, 1)))
