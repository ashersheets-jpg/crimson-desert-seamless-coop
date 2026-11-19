"""Whole-body checksums (guest_sched_load / guest_sched_bind `Body{rva, end, fnv}`): does the stated fnv hold on 3663
at the OLD range (from git, before the port) and what is the NEW range's fnv on 3732 (same length, same masked code)?"""
import re, subprocess
import portmap as p
import fmap

REPO = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/cdmp-native'
OLD_REV = '1b1cc67'   # the last commit before the 3732 port


def fnv(b):
    h = 0xCBF29CE484222325
    for x in b:
        h = ((h ^ x) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def masked(img, a, n):
    rx, _used = p.pattern(img, a, n)
    return rx


for f in ('src/mod/session2/guest_sched_load.cpp', 'src/mod/session2/guest_sched_bind.cpp'):
    old = subprocess.run(['git', '-C', REPO, 'show', OLD_REV + ':' + f], capture_output=True, text=True, encoding='utf-8').stdout
    new = open(REPO + '/' + f, encoding='utf-8').read()
    ro = re.findall(r'Body (k\w+)\{0x([0-9A-F]+), 0x([0-9A-F]+), 0x([0-9A-F]+)ull\}', old)
    rn = {m[0]: m for m in re.findall(r'Body (k\w+)\{0x([0-9A-F]+), 0x([0-9A-F]+), 0x([0-9A-F]+)ull\}', new)}
    for name, a, e, h in ro:
        a, e, h = int(a, 16), int(e, 16), int(h, 16)
        _n, na, ne, _h = rn[name]
        na, ne = int(na, 16), int(ne, 16)
        ok_old = fnv(p.OLD.rd(a, e - a)) == h
        same_len = (ne - na) == (e - a)
        # the same code but relative operands: the old body's masked pattern matches at the new start
        rx = masked(p.OLD, a, e - a)
        same_code = bool(rx and rx.match(p.NEW.d, p.NEW.off(na)))
        print('%s %-9s old 0x%X..0x%X fnv on 3663 %s | new 0x%X..0x%X len %s | masked code %s | new fnv 0x%016X' % (
            f.split('/')[-1], name, a, e, 'HOLDS' if ok_old else 'DIFFERS', na, ne, 'same' if same_len else 'DIFF',
            'SAME' if same_code else 'differs', fnv(p.NEW.rd(na, ne - na))))
