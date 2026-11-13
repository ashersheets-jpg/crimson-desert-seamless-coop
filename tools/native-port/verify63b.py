# Port check 2 (2026-11-13): byte arrays whose address constant has ANOTHER name (kBytesR / kPointR, want_a /
# kAddCall ...). An array "holds" when its bytes stand in the exe exactly AT one of the address literals of its own
# file. Compared: the source before the port against the 3576 exe, the source now against the 3663 exe. Printed: every
# array that held before and does not hold now, with the address it held at (old), where that address went, and what
# the 3663 exe has there.
#   python verify63b.py native|layer
import glob, json, os, re, sys
sys.path.insert(0, 'C:/Users/asher/CrimsonDesert-SeamlessCoop/build/port3663')
sys.path.insert(0, 'C:/Users/asher/CrimsonDesert-SeamlessCoop/tools/native-port')
import portmap as p
import p63

SP = 'C:/Users/asher/AppData/Local/Temp/claude/b8f94c77a294ec45/3d56af72-2691-4d25-8c6b-5d6e25b2ed6a/scratchpad/pre63/'
ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/'
NOW = {'native': ROOT + 'cdmp-native/src/', 'layer': ROOT + 'cd-coop-3446/src/mod/'}
BEFORE = {'native': SP + 'native/src/', 'layer': SP + 'layer/upstream/cd-coop-3446/src/mod/'}
ARRS = re.compile(r'(?:(\w+)\[\w*\]\s*=\s*|std::array<\s*(?:std::)?uint8_t\s*,\s*[\w:]+\s*>\s+(\w+)\s*=?\s*)\{([^{}]*)\}', re.S)
LIT = re.compile(r'\b0x([0-9A-Fa-f]{5,8})\b')
OLD_TABLES = re.compile(r'offsets_(3424|3446|3460|3479|3527|3549|3576)\.h$')


def arrays(path):
    try:
        s = open(path, encoding='utf-8', errors='replace').read()
    except FileNotFoundError:
        return {}, set()
    code = re.sub(r'//[^\n]*', '', s)
    out = {}
    for m in ARRS.finditer(code):
        body = m.group(3)
        if re.search(r'0x[0-9A-Fa-f]{3,}', body):
            continue
        vals = re.findall(r'0x([0-9A-Fa-f]{2})\b', body)
        if len(vals) >= 5:
            out.setdefault(m.group(1) or m.group(2), bytes(int(x, 16) for x in vals))
    return out, {int(x, 16) for x in LIT.findall(code)}


def main(tree):
    mp, _ = p63.mapping(tree)
    n_ok = n_bad = 0
    for path in sorted(glob.glob(NOW[tree] + '**/*.*', recursive=True)):
        path = path.replace('\\', '/')
        if not path.endswith(('.cpp', '.h')) or OLD_TABLES.search(path):
            continue
        rel = path[len(NOW[tree]):]
        a_now, lits_now = arrays(path)
        a_old, lits_old = arrays(BEFORE[tree] + rel)
        for name, b_old in a_old.items():
            held = [a for a in lits_old if p.OLD.rd(a, len(b_old)) == b_old]
            if not held:
                continue
            b_now = a_now.get(name)
            if b_now is None:
                continue
            if any(p.NEW.rd(a, len(b_now)) == b_now for a in lits_now):
                n_ok += 1
                continue
            n_bad += 1
            for a in held[:2]:
                na = mp.get(a)
                print('%s %s (%d bytes): held at 0x%X on 3576; that address is now %s; the 3663 exe has %s there; the source has %s' % (
                    rel, name, len(b_old), a, na and '0x%X' % na, na and p.NEW.rd(na, len(b_old))[:14].hex(' '), b_now[:14].hex(' ')))
    print('%s: arrays that held on 3576 and hold on 3663: %d; that do NOT: %d' % (tree, n_ok, n_bad))


if __name__ == '__main__':
    main(sys.argv[1])
