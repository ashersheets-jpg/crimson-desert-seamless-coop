"""Name gimmick event hashes: every printable string (4..80 chars) of the 3732 exe hashed with the game's hashlittle
(Bob Jenkins' lookup3, seed 0xC5EDE: kErrNotController = hashlittle("eErrNoActorNotExist", 0xC5EDE)); the wanted
hashes are matched.  python evnames.py <hex hash> ...   (first checks the seed on eErrNoActorNotExist)"""
import re, sys

M = 0xFFFFFFFF


def rot(x, k):
    return ((x << k) | (x >> (32 - k))) & M


def hashlittle(data, initval):
    length = len(data)
    a = b = c = (0xDEADBEEF + length + initval) & M
    i = 0
    while length > 12:
        a = (a + (data[i] | data[i + 1] << 8 | data[i + 2] << 16 | data[i + 3] << 24)) & M
        b = (b + (data[i + 4] | data[i + 5] << 8 | data[i + 6] << 16 | data[i + 7] << 24)) & M
        c = (c + (data[i + 8] | data[i + 9] << 8 | data[i + 10] << 16 | data[i + 11] << 24)) & M
        a = (a - c) & M; a ^= rot(c, 4); c = (c + b) & M
        b = (b - a) & M; b ^= rot(a, 6); a = (a + c) & M
        c = (c - b) & M; c ^= rot(b, 8); b = (b + a) & M
        a = (a - c) & M; a ^= rot(c, 16); c = (c + b) & M
        b = (b - a) & M; b ^= rot(a, 19); a = (a + c) & M
        c = (c - b) & M; c ^= rot(b, 4); b = (b + a) & M
        length -= 12
        i += 12
    tail = data[i:] + b'\0' * 12
    if length == 0:
        return c
    a = (a + (tail[0] | tail[1] << 8 | tail[2] << 16 | tail[3] << 24)) & M
    if length > 4:
        b = (b + (tail[4] | tail[5] << 8 | tail[6] << 16 | tail[7] << 24)) & M
    if length > 8:
        c = (c + (tail[8] | tail[9] << 8 | tail[10] << 16 | tail[11] << 24)) & M
    # lookup3 masks the tail bytes beyond length (the zero padding above does that)
    c ^= b; c = (c - rot(b, 14)) & M
    a ^= c; a = (a - rot(c, 11)) & M
    b ^= a; b = (b - rot(a, 25)) & M
    c ^= b; c = (c - rot(b, 16)) & M
    a ^= c; a = (a - rot(c, 4)) & M
    b ^= a; b = (b - rot(a, 14)) & M
    c ^= b; c = (c - rot(b, 24)) & M
    return c


SEED = 0xC5EDE
want = {int(x, 16) for x in sys.argv[1:]}
print('seed check eErrNoActorNotExist: %08X (want D65F8D70)' % hashlittle(b'eErrNoActorNotExist', SEED))
d = open('C:/Users/asher/CrimsonDesert-SeamlessCoop/build/exe-archive/CrimsonDesert-1.0.0.3732.exe', 'rb').read()
seen = set()
found = {}
for m in re.finditer(rb'[\x20-\x7e]{4,80}', d):
    s = m.group(0)
    for t in {s, s.strip()}:
        if t in seen:
            continue
        seen.add(t)
        h = hashlittle(t, SEED)
        if h in want:
            found.setdefault(h, []).append(t.decode('latin1'))
print('strings hashed', len(seen))
for h in sorted(want):
    print('%08X -> %s' % (h, found.get(h, 'not found')))
