# Port check (2026-11-13): every byte guard of the source against the exe it is meant for.
# A guard = an address constant `kName` / `kNameRva` (constexpr uintptr_t / uint32_t = 0x...) and a byte array named
# after it (`kNameBytes`, `kNameHead`, `kNameCode`; `uint8_t x[] = {..}`, `x[N] = {..}` or std::array<uint8_t, N>).
# Prints the pairs whose bytes are NOT at that address in the exe (and where, within 0x40, they are, if anywhere).
#   python verify63.py native|layer [3663|3576] [file filter]
import glob, re, sys
sys.path.insert(0, 'C:/Users/asher/CrimsonDesert-SeamlessCoop/build/port3663')
import portmap as p

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/'
TREE = {'native': ROOT + 'cdmp-native/src/', 'layer': ROOT + 'cd-coop-3446/src/mod/'}
ADDR = re.compile(r'\b(k\w+?)(?:Rva)?\s*=\s*(0x[0-9A-Fa-f]{5,8})\b')
ARRS = re.compile(r'(?:(\w+)\[\w*\]\s*=\s*|std::array<\s*(?:std::)?uint8_t\s*,\s*[\w:]+\s*>\s+(\w+)\s*=?\s*)\{([^{}]*)\}', re.S)
OLD_TABLES = re.compile(r'offsets_(3424|3446|3460|3479|3527|3549|3576)\.h$')


def main(tree, build, filt):
    img = p.NEW if build == '3663' else p.OLD
    code_end = img.code[0][1] + img.code[0][2]
    ok = bad = 0
    for path in sorted(glob.glob(TREE[tree] + '**/*.*', recursive=True)):
        path = path.replace('\\', '/')
        if not path.endswith(('.cpp', '.h')) or OLD_TABLES.search(path) or (filt and filt not in path):
            continue
        s = open(path, encoding='utf-8', errors='replace').read()
        code = re.sub(r'//[^\n]*', '', s)
        addrs = {}
        for m in ADDR.finditer(code):
            addrs.setdefault(m.group(1), int(m.group(2), 16))
        for m in ARRS.finditer(code):
            name = m.group(1) or m.group(2)
            body = m.group(3)
            if re.search(r'0x[0-9A-Fa-f]{3,}', body):
                continue
            vals = re.findall(r'0x([0-9A-Fa-f]{2})\b', body)
            if len(vals) < 3:
                continue
            b = bytes(int(x, 16) for x in vals)
            base = re.sub(r'(Bytes|Head|Code|Body|Sig)$', '', name)
            a = addrs.get(base) or addrs.get(base + 'Site') or addrs.get(base + 'Fn')
            if a is None or not (0x1000 <= a < code_end):
                continue
            got = img.rd(a, len(b))
            if got == b:
                ok += 1
                continue
            bad += 1
            near = img.rd(a - 0x40, 0x80 + len(b)).find(b)
            line = s.count('\n', 0, s.find(name)) + 1
            print('%s:%d %s @0x%X (%d bytes): exe has %s%s' % (path[len(TREE[tree]):], line, name, a, len(b), got[:12].hex(' '),
                  ' | the bytes are at %+d' % (near - 0x40) if near >= 0 else ''))
    print('guards that match: %d, that do not: %d (%s, %s exe)' % (ok, bad, tree, build))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else '3663', sys.argv[3] if len(sys.argv) > 3 else '')
