"""Candidates for missed struct offsets: for every source file, the field shifts seen (dispsurvey's method) in the
game functions THAT FILE names, kept only when the file also holds the old displacement as a literal.
   python dispcand.py            -> build/native-port/dispcand.txt  (file | old -> new | example site | the file's lines)
Each line is a question to answer by reading, not a fix."""
import difflib, json, re, sys
import portmap as p
import fmap
import dispsurvey as d

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/'
SRC = {'native': ROOT + 'upstream/cdmp-native/', 'layer': ROOT + 'upstream/cd-coop-3446/'}

by_file = {}   # (tree, file) -> set of function index
for tree in ('native', 'layer'):
    try:
        m = json.load(open(ROOT + 'build/native-port/map_3663_%s.json' % tree))
    except FileNotFoundError:
        continue
    for k, v in m.items():
        i = fmap.func(int(k, 16))
        if i is None or i not in fmap.PAIRS:
            continue
        for at in v.get('at', []):
            f = at.split(':')[0] if isinstance(at, str) else at[0]
            by_file.setdefault((tree, f), set()).add(i)

cache = {}


def shifts(i):
    if i in cache:
        return cache[i]
    ob, oe = fmap.A[i]
    nb, ne = fmap.B[fmap.PAIRS[i]]
    got = {}
    if oe - ob <= 60000:
        a, b = d.ins_list(p.OLD, ob, oe), d.ins_list(p.NEW, nb, ne)
        sm = difflib.SequenceMatcher(None, [d.skel(x[1]) for x in a], [d.skel(x[1]) for x in b], autojunk=False)
        for tag, a0, a1, b0, b1 in sm.get_opcodes():
            if tag != 'replace' or a1 - a0 != b1 - b0:
                continue
            for k in range(a1 - a0):
                sh = d.shift(a[a0 + k][1], b[b0 + k][1])
                if sh:
                    got.setdefault(sh, (a[a0 + k][0], a[a0 + k][1], b[b0 + k][1]))
    cache[i] = got
    return got


out = []
for (tree, f), fs in sorted(by_file.items()):
    path = SRC[tree] + f
    try:
        lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
    except OSError:
        try:
            path = SRC[tree] + 'src/' + f
            lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
        except OSError:
            out.append('?? cannot open %s %s' % (tree, f))
            continue
    seen = {}
    for i in fs:
        for sh, ex in shifts(i).items():
            seen.setdefault(sh, ex)
    for (x, y), (oa, so, sn) in sorted(seen.items()):
        if x < 0x10:
            continue
        pat = re.compile(r'0x0*%X\b' % x, re.I)
        hits = [(n + 1, l.strip()) for n, l in enumerate(lines)
                if pat.search(l.split('//')[0]) and not re.search(r'0x0*%X\b' % y, l.split('//')[0], re.I)]
        if not hits:
            continue
        out.append('%s %s | 0x%X -> 0x%X | old %X: %s  =>  %s' % (tree, f, x, y, oa, so, sn))
        for n, l in hits[:6]:
            out.append('        %d: %s' % (n, l[:170]))
open(ROOT + 'build/native-port/dispcand.txt', 'w', encoding='utf-8').write('\n'.join(out))
print(len([o for o in out if not o.startswith(' ')]), 'candidate (file, shift) pairs')
