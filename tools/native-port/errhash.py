import csv, sys

def rot(x, k):
    return ((x << k) | (x >> (32 - k))) & 0xFFFFFFFF

def lookup3(data, initval=0):
    length = len(data)
    a = b = c = (0xdeadbeef + length + initval) & 0xFFFFFFFF
    i = 0
    while length > 12:
        a = (a + int.from_bytes(data[i:i+4], 'little')) & 0xFFFFFFFF
        b = (b + int.from_bytes(data[i+4:i+8], 'little')) & 0xFFFFFFFF
        c = (c + int.from_bytes(data[i+8:i+12], 'little')) & 0xFFFFFFFF
        a = (a - c) & 0xFFFFFFFF; a ^= rot(c, 4); c = (c + b) & 0xFFFFFFFF
        b = (b - a) & 0xFFFFFFFF; b ^= rot(a, 6); a = (a + c) & 0xFFFFFFFF
        c = (c - b) & 0xFFFFFFFF; c ^= rot(b, 8); b = (b + a) & 0xFFFFFFFF
        a = (a - c) & 0xFFFFFFFF; a ^= rot(c, 16); c = (c + b) & 0xFFFFFFFF
        b = (b - a) & 0xFFFFFFFF; b ^= rot(a, 19); a = (a + c) & 0xFFFFFFFF
        c = (c - b) & 0xFFFFFFFF; c ^= rot(b, 4); b = (b + a) & 0xFFFFFFFF
        length -= 12; i += 12
    tail = data[i:] + b'\0' * 12
    if length == 0:
        return c
    a = (a + int.from_bytes(tail[0:4], 'little')) & 0xFFFFFFFF
    if length > 4: b = (b + int.from_bytes(tail[4:8], 'little')) & 0xFFFFFFFF
    if length > 8: c = (c + int.from_bytes(tail[8:12], 'little')) & 0xFFFFFFFF
    c ^= b; c = (c - rot(b, 14)) & 0xFFFFFFFF
    a ^= c; a = (a - rot(c, 11)) & 0xFFFFFFFF
    b ^= a; b = (b - rot(a, 25)) & 0xFFFFFFFF
    c ^= b; c = (c - rot(b, 16)) & 0xFFFFFFFF
    a ^= c; a = (a - rot(c, 4)) & 0xFFFFFFFF
    b ^= a; b = (b - rot(a, 14)) & 0xFFFFFFFF
    c ^= b; c = (c - rot(b, 24)) & 0xFFFFFFFF
    return c

names = set()
with open(r'C:\Users\asher\CrimsonDesert-SeamlessCoop\build\decomp\strings.tsv', encoding='utf-8', errors='replace') as f:
    for line in f:
        parts = line.rstrip('\n').split('\t')
        if len(parts) >= 3 and parts[2].startswith('eErr'):
            names.add(parts[2])
want = {int(x, 16) for x in sys.argv[1:]}
known = {0x6467532C: 'eErrNoInteractionOwnerConditionCheckFail', 0x349EFD61: 'eErrNoInvalidActionChart'}
for init in (0, 0xC5EDE):
    hits = {n: lookup3(n.encode(), init) for n in names}
    ok = all(hits.get(n) == v for v, n in known.items() if n in hits)
    print('initval', hex(init), 'calibrated' if ok else 'no', len(names), 'names')
    if ok:
        for n, v in hits.items():
            if v in want: print(hex(v), n)
