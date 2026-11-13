# Port 1.0.0.3576 -> 1.0.0.3663 (2026-11-13) of BOTH trees: the native base (upstream/cdmp-native) and the Co-op+ layer
# (upstream/cd-coop-3446/src/mod). The 3576 pipeline (scan3576 / judge3576 / filt3576 / apply3576 / heads3576 /
# inline3576) as one file with the two builds and the tree as parameters, and the mapping run on several processes.
#   python p63.py scan native|layer     every address literal -> build/native-port/map_3663_<tree>.json
#   python p63.py judge native|layer    verdicts against the neighbours' shift (+ the base-value filter for native)
#   python p63.py apply native|layer [--dry]
#   python p63.py heads native|layer [--dry]     byte arrays compared against the game, re-read
#   python p63.py inline native|layer [--dry]    {0xADDR, {bytes}} initialisers
# Hand decisions go into OVERRIDES / SKIP below, with the evidence in a comment.
import bisect, glob, json, os, re, struct, subprocess, sys
from collections import Counter

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/'
sys.path.insert(0, ROOT + 'build/port3663')
IB = 0x140000000
CODE_HI = 0x5597000              # 3576: the code section ends here
DATA_HI = 0x7A44000              # 3576: the copy protection's section starts here
TREES = {
    'native': dict(repo=ROOT + 'upstream/cdmp-native', base='3cbb03c'),
    'layer': dict(repo=ROOT + 'upstream/cd-coop-3446', base=None),
}
OLD_TABLES = re.compile(r'offsets_(3424|3446|3460|3479|3527|3576)\.h$')

OVERRIDES = {'native': {}, 'layer': {}}
SKIP = {'native': {0x100000, 0x6C00000, 0x7400000}, 'layer': set()}
try:
    import p63_hand  # OVERRIDES / SKIP decided by hand, kept apart so that this file stays the method
    for t in TREES:
        OVERRIDES[t].update(p63_hand.OVERRIDES.get(t, {}))
        SKIP[t] |= p63_hand.SKIP.get(t, set())
except ImportError:
    pass


def out_path(tree, suffix=''):
    return ROOT + 'build/native-port/map_3663_%s%s.json' % (tree, suffix)


def code_part(s):
    i = s.find('//')
    return s if i < 0 else s[:i]


def cand_lines(tree):
    """(file relative to the repo, line number, text) of every line that may hold a 3576 address."""
    t = TREES[tree]
    out = []
    if t['base']:
        diff = subprocess.run(['git', '-C', t['repo'], 'diff', t['base'], 'HEAD', '-U0', '--', 'src'],
                              capture_output=True, text=True, encoding='utf-8', errors='replace').stdout
        f, ln = None, 0
        for line in diff.splitlines():
            if line.startswith('+++ '):
                f = line[6:] if line.startswith('+++ b/') else None
            elif line.startswith('@@'):
                m = re.search(r'\+(\d+)', line)
                ln = int(m.group(1)) if m else 0
            elif line.startswith('+') and f:
                out.append((f, ln, line[1:]))
                ln += 1
    else:
        for path in glob.glob(t['repo'] + '/src/mod/**/*.*', recursive=True):
            path = path.replace('\\', '/')
            if not path.endswith(('.cpp', '.h')) or OLD_TABLES.search(path):
                continue
            rel = path[len(t['repo']) + 1:]
            for i, s in enumerate(open(path, encoding='utf-8', errors='replace', newline='').read().split('\n')):
                out.append((rel, i + 1, s))
    return out


def base_lits(tree, f, cache={}):
    t = TREES[tree]
    if not t['base']:
        return set()
    if f not in cache:
        r = subprocess.run(['git', '-C', t['repo'], 'show', '%s:%s' % (t['base'], f)], capture_output=True, text=True,
                           encoding='utf-8', errors='replace')
        cache[f] = {int(x, 16) for x in re.findall(r'\b0x([0-9A-Fa-f]{5,8})\b', r.stdout)} if r.returncode == 0 else set()
    return cache[f]


# ---------------------------------------------------------------- scan (worker processes)
def rtti_name(img, vt):
    try:
        col = struct.unpack('<Q', img.rd(vt - 8, 8))[0] - IB
        if not (0 < col < 0x15000000) or struct.unpack('<I', img.rd(col, 4))[0] != 1:
            return None
        td = struct.unpack('<I', img.rd(col + 0x0C, 4))[0]
        nm = img.rd(td + 0x10, 200).split(b'\0')[0].decode('latin1')
        return nm if nm.startswith('.?A') else None
    except Exception:
        return None


def map_one(v):
    import portmap as p
    import rtti_vt
    e = {}
    if v < CODE_HI:
        new, hits, second = p.code(v)
        e.update(kind='code', new=new, hits=hits, second=second,
                 same16=bool(new) and p.OLD.rd(v, 16) == p.NEW.rd(new, 16))
    else:
        nm = rtti_name(p.OLD, v)
        if nm:
            rtti_vt.IMG = p.NEW
            vts = rtti_vt.vtables(nm)
            old_vts = None
            if len(vts) != 1:
                rtti_vt.IMG = p.OLD
                old_vts = rtti_vt.vtables(nm)
                rtti_vt.IMG = p.NEW
            # several vtables of one class (multiple inheritance): the same position in both builds
            new = vts[0] if len(vts) == 1 else (vts[old_vts.index(v)] if old_vts and len(old_vts) == len(vts) and v in old_vts else None)
            e.update(kind='vtable', name=nm, new=new, cands=vts)
        else:
            new, votes, sites = p.data(v)
            e.update(kind='data', new=new, votes=votes, sites=sites)
    return v, e


def scan(tree):
    from multiprocessing import Pool
    lits = {}
    for f, ln, s in cand_lines(tree):
        for m in re.finditer(r'\b0x([0-9A-Fa-f]{5,8})\b', code_part(s)):
            v = int(m.group(1), 16)
            if 0x1000 <= v < DATA_HI:
                lits.setdefault(v, []).append('%s:%d' % (f, ln))
    print(len(lits), 'candidate address literals', flush=True)
    res = {}
    with Pool(int(os.environ.get('P63_PROCS', '6'))) as pool:
        for i, (v, e) in enumerate(pool.imap_unordered(map_one, sorted(lits), chunksize=4)):
            e['at'] = lits[v][:6]
            res['0x%X' % v] = e
            if i % 100 == 99:
                print(' ', i + 1, flush=True)
    os.makedirs(os.path.dirname(out_path(tree)), exist_ok=True)
    json.dump(dict(sorted(res.items(), key=lambda t: int(t[0], 16))), open(out_path(tree), 'w'), indent=1)
    print(len(res), 'mapped ->', out_path(tree), Counter(e['kind'] for e in res.values()))


def map_two(item):
    """The function map's answer (build/port3663/fmap.py) for one literal already scanned by portmap."""
    v, kind = item
    import fmap
    if kind == 'code':
        n, how = fmap.addr(v)
        if n is None or how in ('guess', 'gap-guess', None):
            i = fmap.func(v)
            if i is not None and fmap.A[i][0] == v:
                t = fmap.target(v)
                if t is not None:
                    n, how = t, 'callee'
        return v, {'f_new': n, 'f_how': how}
    n, votes, sites = fmap.data(v)
    return v, {'f_new': n, 'f_votes': votes, 'f_sites': sites}


def scan2(tree):
    from multiprocessing import Pool
    m = json.load(open(out_path(tree)))
    todo = [(int(k, 16), e['kind']) for k, e in m.items() if e['kind'] in ('code', 'data')]
    with Pool(int(os.environ.get('P63_PROCS', '6'))) as pool:
        for i, (v, e) in enumerate(pool.imap_unordered(map_two, todo, chunksize=4)):
            m['0x%X' % v].update(e)
            if i % 200 == 199:
                print(' ', i + 1, flush=True)
    json.dump(m, open(out_path(tree), 'w'), indent=1)
    print(Counter(str(e.get('f_how')) for e in m.values() if e['kind'] == 'code'))


# ---------------------------------------------------------------- judge
def judge(tree):
    import portmap as p
    m = json.load(open(out_path(tree)))
    # anchors from BOTH trees when both are scanned: more neighbours
    allm = dict(m)
    for other in TREES:
        if other != tree and os.path.exists(out_path(other)):
            for k, e in json.load(open(out_path(other))).items():
                allm.setdefault(k, e)
    anch = sorted((int(k, 16), e['new'] - int(k, 16)) for k, e in allm.items()
                  if e['kind'] == 'code' and e['hits'] == 1 and e['new'] and e.get('same16'))
    xs = [a for a, _ in anch]

    def near_shift(v):
        i = bisect.bisect_left(xs, v)
        c = [anch[j] for j in (i - 1, i) if 0 <= j < len(anch) and anch[j][0] != v]
        if not c:
            return 0, 1 << 30
        a, s = min(c, key=lambda t: abs(t[0] - v))
        return s, abs(a - v)
    GOOD = ('same', 'moved', 'gap', 'gap-moved', 'begin', 'same~', 'moved~', 'call', 'callee')
    verdict = {}
    for k, e in m.items():
        v = int(k, 16)
        if e['kind'] == 'code':
            pm_ok = e['hits'] == 1 and e['new'] and e.get('same16')
            if e.get('f_how') in GOOD:
                # the function map placed it; a different unique whole-image match is worth a look
                verdict[k] = ('conflict' if pm_ok and e['new'] != e['f_new'] else 'ok', e['f_new'])
            elif pm_ok:
                s_, d = near_shift(v)
                verdict[k] = ('ok' if abs(e['new'] - v - s_) <= 0x2000 or d > 0x40000 else 'odd1', e['new'])
            elif e.get('f_new'):
                verdict[k] = ('weak:' + str(e.get('f_how')), e['f_new'])
            else:
                verdict[k] = ('nomap', None)
        elif e['kind'] == 'vtable':
            verdict[k] = ('ok' if e['new'] else 'vt?', e['new'])
        else:
            fv, fn = e.get('f_votes', 0), e.get('f_new')
            if fn and fv >= 2:
                verdict[k] = ('ok', fn)
            elif fn and fv == 1 and e['new'] == fn:
                verdict[k] = ('ok', fn)
            elif e['new'] and e['votes'] >= max(2, (e['sites'] + 1) // 2) and not fn:
                verdict[k] = ('ok', e['new'])
            else:
                verdict[k] = ('data?', fn or e['new'])
    keep, drop = {}, []
    for k, e in m.items():
        val = int(k, 16)
        files = {a.rsplit(':', 1)[0] for a in e['at']}
        if TREES[tree]['base'] and all(val in base_lits(tree, f) for f in files):
            drop.append(k)
        else:
            keep[k] = verdict[k]
    print('kept', len(keep), Counter(x[0] for x in keep.values()))
    print('dropped (values of the base author, never ported)', len(drop))
    json.dump(verdict, open(out_path(tree, '_verdict'), 'w'), indent=0)
    json.dump(sorted(drop), open(out_path(tree, '_base'), 'w'))
    hand = OVERRIDES[tree]
    for k, (st, nv) in sorted(keep.items(), key=lambda t: int(t[0], 16)):
        v = int(k, 16)
        if st != 'ok' and v not in hand and v not in SKIP[tree]:
            e = m[k]
            print(st, k, nv if not isinstance(nv, int) else '0x%X' % nv, e['at'][0], {x: (('0x%X' % e[x]) if x in ('new', 'f_new') and isinstance(e[x], int) else e[x]) for x in ('new', 'hits', 'same16', 'f_new', 'f_how', 'votes', 'sites', 'f_votes', 'f_sites', 'name', 'cands') if x in e},
                  'near shift %+X' % near_shift(v)[0] if e['kind'] == 'code' else '')


def mapping(tree):
    m = json.load(open(out_path(tree)))
    verdict = json.load(open(out_path(tree, '_verdict')))
    base = set(json.load(open(out_path(tree, '_base'))))
    out = {}
    for k in m:
        v = int(k, 16)
        if k in base or v in SKIP[tree]:
            continue
        st, nv = verdict[k]
        if v in OVERRIDES[tree]:
            out[v] = OVERRIDES[tree][v]
        elif st == 'ok':
            out[v] = nv
    return out, {int(k, 16) for k in m if k not in base and int(k, 16) not in SKIP[tree]}


# ---------------------------------------------------------------- apply
def apply(tree, dry):
    new_of, known = mapping(tree)
    repo = TREES[tree]['repo']
    want = {}
    for f, ln, _s in cand_lines(tree):
        want.setdefault(f, set()).add(ln)
    missing, changed = {}, {}
    for f, lines in want.items():
        path = repo + '/' + f
        try:
            raw = open(path, encoding='utf-8', newline='').read()
        except FileNotFoundError:
            continue
        rows = raw.split('\n')
        n = 0
        for i in sorted(lines):
            if i - 1 >= len(rows):
                continue
            row = rows[i - 1]
            cut = row.find('//')
            code, rest = (row, '') if cut < 0 else (row[:cut], row[cut:])

            def sub(mm):
                nonlocal n
                v = int(mm.group(1), 16)
                if v not in known:
                    return mm.group(0)
                if v not in new_of:
                    missing.setdefault(v, '%s:%d' % (f, i))
                    return mm.group(0)
                if new_of[v] == v:
                    return mm.group(0)
                n += 1
                return '0x%0*X' % (len(mm.group(1)), new_of[v])
            rows[i - 1] = re.sub(r'\b0x([0-9A-Fa-f]{5,8})\b', sub, code) + rest
        if n:
            changed[f] = n
            if not dry:
                open(path, 'w', encoding='utf-8', newline='').write('\n'.join(rows))
    for f, n in sorted(changed.items()):
        print('%4d  %s' % (n, f))
    print(sum(changed.values()), 'literals in', len(changed), 'files', '(dry run)' if dry else '')
    if missing:
        print('LEFT AS 3576 (no mapping):')
        for v in sorted(missing):
            print('   0x%X  %s' % (v, missing[v]))


# ---------------------------------------------------------------- heads / inline byte lists
ARR = re.compile(r'(?:(\w+)\[\w*\]\s*=\s*|std::array<\s*(?:std::)?uint8_t\s*,\s*[\w:]+\s*>\s+(\w+)\s*=?\s*)\{([^{}]*)\}', re.S)


def heads(tree, dry):
    import portmap as p
    mp, _ = mapping(tree)
    repo = TREES[tree]['repo']
    by_file = {}
    for k, e in json.load(open(out_path(tree))).items():
        v = int(k, 16)
        if v in mp:
            for a in e['at']:
                by_file.setdefault(a.rsplit(':', 1)[0], set()).add(v)
    touched = {}
    for f, ln, _s in cand_lines(tree):
        touched.setdefault(f, set()).add(ln)
    changed, unfound, same = [], [], 0
    for f, lines in touched.items():
        path = repo + '/' + f
        try:
            text = open(path, encoding='utf-8', newline='').read()
        except FileNotFoundError:
            continue
        addrs = sorted(by_file.get(f, ()))
        out = []
        for mm in ARR.finditer(text):
            line = text.count('\n', 0, mm.start()) + 1
            body = mm.group(3)
            vals = re.findall(r'0x([0-9A-Fa-f]{2})\b', body)
            span = set(range(line, text.count('\n', 0, mm.end()) + 2))
            if len(vals) < 4 or re.search(r'0x[0-9A-Fa-f]{3,}', body):
                continue
            b = bytes(int(x, 16) for x in vals)
            hit = None
            for a in addrs:
                if p.OLD.rd(a, len(b)) == b:
                    hit = (a, 0)
                    break
            if hit is None:
                for a in addrs:
                    win = p.OLD.rd(a - 0x400, 0x800 + len(b))
                    i = win.find(b)
                    if i >= 0:
                        hit = (a, i - 0x400)
                        break
            if hit is None:
                unfound.append('%s:%d %s (%d bytes)' % (f, line, mm.group(1) or mm.group(2), len(b)))
                continue
            a, off = hit
            nb = p.NEW.rd(mp[a] + off, len(b))
            if nb == b:
                same += 1
                continue
            rows = body.split('\n')
            counts = [len(re.findall(r'0x[0-9A-Fa-f]{2}\b', r)) for r in rows]
            it = iter(nb)
            new_rows = []
            for r, c in zip(rows, counts):
                if c == 0:
                    new_rows.append(r)
                    continue
                lead = re.match(r'\s*', r).group(0)
                tail = ',' if r.rstrip().endswith(',') else ''
                new_rows.append(lead + ', '.join('0x%02X' % next(it) for _ in range(c)) + tail)
            out.append((mm.start(3), mm.end(3), '\n'.join(new_rows)))   # group 3 = the bytes (2 is a std::array's name)
            import fmap
            so, sn = fmap.shape_len(p.OLD, a + off, len(b)), fmap.shape_len(p.NEW, mp[a] + off, len(b))
            tag = 'numbers' if so == sn else 'SHAPE'
            changed.append('%-7s %s:%d %s @0x%X%+d' % (tag, f, line, mm.group(1) or mm.group(2), mp[a], off) +
                           ('' if so == sn else ' || old: %s || new: %s' % (
                               fmap.text_len(p.OLD, a + off, len(b)), fmap.text_len(p.NEW, mp[a] + off, len(b)))))
        if out and not dry:
            for s, e, t in reversed(out):
                text = text[:s] + t + text[e:]
            open(path, 'w', encoding='utf-8', newline='').write(text)
    print('unchanged heads:', same)
    print('re-read (%d):' % len(changed))
    for c in changed:
        print('  ', c)
    print('NOT FOUND near a mapped address (%d):' % len(unfound))
    for u in unfound:
        print('  ', u)


PAT = re.compile(r'\{\s*0x([0-9A-Fa-f]{5,8})\s*,\s*\{((?:\s*0x[0-9A-Fa-f]{2}\s*,?)+)\}')


def inline(tree, dry):
    import portmap as p
    mp, _ = mapping(tree)
    inv = {n: o for o, n in mp.items()}
    repo = TREES[tree]['repo']
    for path in glob.glob(repo + '/src/**/*.*', recursive=True):
        path = path.replace('\\', '/')
        if not path.endswith(('.cpp', '.h')) or OLD_TABLES.search(path):
            continue
        raw = open(path, encoding='utf-8', newline='').read()
        out = []
        for m in PAT.finditer(raw):
            new = int(m.group(1), 16)
            if new not in inv:
                continue
            vals = re.findall(r'0x([0-9A-Fa-f]{2})', m.group(2))
            b = bytes(int(x, 16) for x in vals)
            nb = p.NEW.rd(new, len(b))
            if nb == b:
                continue
            ob = p.OLD.rd(inv[new], len(b))
            if ob != b:
                print('?? %s 0x%X: listed bytes are not the 3576 bytes either' % (path[len(repo) + 1:], new))
                continue
            out.append((m.start(2), m.end(2), ', '.join('0x%02X' % x for x in nb)))
            print('%s 0x%X: %s -> %s' % (path[len(repo) + 1:], new, b.hex(' '), nb.hex(' ')))
        if out and not dry:
            for s, e, t in reversed(out):
                raw = raw[:s] + t + raw[e:]
            open(path, 'w', encoding='utf-8', newline='').write(raw)


if __name__ == '__main__':
    cmd, tree = sys.argv[1], sys.argv[2]
    dry = '--dry' in sys.argv
    {'scan': lambda: scan(tree), 'scan2': lambda: scan2(tree), 'judge': lambda: judge(tree), 'apply': lambda: apply(tree, dry),
     'heads': lambda: heads(tree, dry), 'inline': lambda: inline(tree, dry)}[cmd]()
