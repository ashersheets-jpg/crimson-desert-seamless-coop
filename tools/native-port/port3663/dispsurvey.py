"""Struct field shifts 3576 -> 3663, surveyed over every function our code names (the port maps) and, with --callees,
the functions they call. For each pair of functions the two listings are diffed; in equal-length replaced runs an
instruction pair that differs ONLY in one displacement of a memory operand not based on rsp / rbp / rip counts as
"old disp -> new disp". Output: the shifts by number of functions that show them, with two example sites each.
   python dispsurvey.py [--callees] [--min N]        -> build/native-port/dispsurvey.txt
A shift here is not yet a fact about one struct: read the examples (pairdiff.py <old>) before touching code."""
import difflib, json, re, sys
import portmap as p
import fmap

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/'
REG = re.compile(r'\b(r[a-ds][xip]|r[sb]p|e[a-ds][xi]|[a-ds][xi]|[a-d]l|[sd]il|r(?:8|9|1[0-5])[dwb]?|e[sb]p|[sb]pl?)\b')
HEX = re.compile(r'0x[0-9a-f]+')
MEM = re.compile(r'\[([^\]]*)\]')


def ins_list(img, b, e):
    out = []
    for ins in p.md.disasm(img.rd(b, e - b), b):
        s = '%s %s' % (ins.mnemonic, ins.op_str)
        if ins.mnemonic.startswith('j') or ins.mnemonic == 'call':
            s = HEX.sub('ADDR', s)
        s = re.sub(r'rip [+-] 0x[0-9a-f]+', 'rip + D', s)
        out.append((ins.address, s))
    return out


def skel(s):
    # stack-based operands keep their base so they can be told apart; other registers become R
    t = re.sub(r'\b(rsp|rbp)\b', lambda m: m.group(1).upper(), s)
    t = REG.sub('R', t)
    return t


def shift(a, b):
    """(old, new) when the two instruction texts differ only in one memory displacement (not stack / rip)."""
    sa, sb = skel(a), skel(b)
    if sa == sb:
        return None
    if HEX.sub('#', sa) != HEX.sub('#', sb):
        return None
    na, nb = HEX.findall(sa), HEX.findall(sb)
    diff = [(x, y) for x, y in zip(na, nb) if x != y]
    if len(diff) != 1:
        return None
    x, y = diff[0]
    for m in MEM.finditer(sa):
        if x in m.group(1) and 'RSP' not in m.group(1) and 'RBP' not in m.group(1) and 'rip' not in m.group(1):
            return int(x, 16), int(y, 16)
    return None


def funcs_named():
    seen = set()
    for tree in ('native', 'layer'):
        try:
            m = json.load(open(ROOT + 'build/native-port/map_3663_%s.json' % tree))
        except FileNotFoundError:
            continue
        for k in m:
            i = fmap.func(int(k, 16))
            if i is not None and i in fmap.PAIRS:
                seen.add(i)
    return seen


def callees(i):
    out = set()
    b, e = fmap.A[i]
    for ins in p.md.disasm(p.OLD.rd(b, e - b), b):
        if ins.mnemonic == 'call' and ins.op_str.startswith('0x'):
            j = fmap.func(int(ins.op_str, 16))
            if j is not None and j in fmap.PAIRS:
                out.add(j)
    return out


def main():
    todo = funcs_named()
    if '--callees' in sys.argv:
        more = set()
        for i in list(todo):
            more |= callees(i)
        todo |= more
    lo = int(sys.argv[sys.argv.index('--min') + 1]) if '--min' in sys.argv else 1
    table = {}
    for n, i in enumerate(sorted(todo)):
        ob, oe = fmap.A[i]
        nb, ne = fmap.B[fmap.PAIRS[i]]
        if oe - ob > 60000:
            continue
        a, b = ins_list(p.OLD, ob, oe), ins_list(p.NEW, nb, ne)
        sm = difflib.SequenceMatcher(None, [skel(x[1]) for x in a], [skel(x[1]) for x in b], autojunk=False)
        got = {}
        for tag, a0, a1, b0, b1 in sm.get_opcodes():
            if tag != 'replace' or a1 - a0 != b1 - b0:
                continue
            for k in range(a1 - a0):
                sh = shift(a[a0 + k][1], b[b0 + k][1])
                if sh:
                    got.setdefault(sh, (a[a0 + k][0], b[b0 + k][0], a[a0 + k][1], b[b0 + k][1]))
        for sh, ex in got.items():
            table.setdefault(sh, []).append(ex)
    rows = sorted(table.items(), key=lambda kv: (-len(kv[1]), kv[0]))
    out = ['%d functions surveyed; %d distinct shifts' % (len(todo), len(rows))]
    for (x, y), ex in rows:
        if len(ex) < lo:
            continue
        out.append('0x%X -> 0x%X (%+d)  in %d function(s)' % (x, y, y - x, len(ex)))
        for oa, na, so, sn in ex[:2]:
            out.append('      old %X: %-46s new %X: %s' % (oa, so, na, sn))
    open(ROOT + 'build/native-port/dispsurvey.txt', 'w').write('\n'.join(out))
    print('\n'.join(out[:1]))


if __name__ == '__main__':
    main()
