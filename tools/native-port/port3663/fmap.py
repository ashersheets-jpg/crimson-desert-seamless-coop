"""Function-level map 3576 -> 3663 from the two exception tables (.pdata).

The 3663 update changed far more code than 3576 did (shifts from +0x3650 to +0x7C000, many functions edited), so
"the same masked bytes, nearest to the old address" (portmap.code) finds nothing or the wrong copy too often.
Functions keep their ORDER between builds. Here: the two lists of (begin, end) are aligned on runs of equal sizes
(4 consecutive sizes that occur once in both builds, kept in increasing order), the gaps between anchors are
aligned by size again (difflib), and what is left in a gap with the same number of functions on both sides is
paired by position.
  pair(old_rva) -> (new function begin, old function begin, how) or None
  addr(old_rva) -> (new rva, how)   how: 'same' (the instruction pattern matches at the same offset),
                                         'moved' (found once elsewhere in the paired function), 'gap' (between
                                         functions, placed by the neighbours), 'guess' (same offset, pattern differs),
                                         None
"""
import bisect, difflib, pickle, os, struct, sys
from collections import Counter

import portmap as p

CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fmap.pickle')


def pdata(img):
    d = img.d
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    opt = pe + 24
    magic = struct.unpack_from('<H', d, opt)[0]
    dd = opt + (112 if magic == 0x20b else 96)
    rva, size = struct.unpack_from('<II', d, dd + 3 * 8)
    o = img.off(rva)
    code_end = img.code[0][1] + img.code[0][2]
    out = []
    for i in range(size // 12):
        b, e, _u = struct.unpack_from('<III', d, o + i * 12)
        if 0x1000 <= b < e <= code_end:
            out.append((b, e))
    out.sort()
    return out


def build():
    A, B = pdata(p.OLD), pdata(p.NEW)
    sa = [e - b for b, e in A]
    sb = [e - b for b, e in B]
    K = 4
    def grams(s):
        c = {}
        for i in range(len(s) - K + 1):
            k = tuple(s[i:i + K])
            c[k] = None if k in c else i
        return {k: i for k, i in c.items() if i is not None}
    ga, gb = grams(sa), grams(sb)
    cand = sorted((i, gb[k]) for k, i in ga.items() if k in gb)
    # longest increasing run of new indices
    tails, prev, idx = [], [-1] * len(cand), []
    for n, (_i, j) in enumerate(cand):
        pos = bisect.bisect_left(tails, j)
        if pos == len(tails):
            tails.append(j); idx.append(n)
        else:
            tails[pos] = j; idx[pos] = n
        prev[n] = idx[pos - 1] if pos else -1
    lis, n = [], idx[-1] if idx else -1
    while n >= 0:
        lis.append(cand[n]); n = prev[n]
    lis.reverse()
    pairs = {}
    for i, j in lis:
        for t in range(K):
            pairs[i + t] = j + t
    # gaps between consecutive anchored functions
    keys = sorted(pairs)
    how = {i: 'anchor' for i in keys}
    bounds = [(-1, -1)] + [(i, pairs[i]) for i in keys] + [(len(A), len(B))]
    for (i0, j0), (i1, j1) in zip(bounds, bounds[1:]):
        na, nb = i1 - i0 - 1, j1 - j0 - 1
        if na <= 0 or nb <= 0:
            continue
        if na * nb > 4_000_000:
            continue
        sm = difflib.SequenceMatcher(None, sa[i0 + 1:i1], sb[j0 + 1:j1], autojunk=False)
        for tag, a0, a1, b0, b1 in sm.get_opcodes():
            if tag == 'equal':
                for t in range(a1 - a0):
                    pairs[i0 + 1 + a0 + t] = j0 + 1 + b0 + t
                    how[i0 + 1 + a0 + t] = 'size'
            elif tag == 'replace' and a1 - a0 == b1 - b0:
                for t in range(a1 - a0):
                    pairs[i0 + 1 + a0 + t] = j0 + 1 + b0 + t
                    how[i0 + 1 + a0 + t] = 'position'
    return A, B, pairs, how


def load():
    if os.path.exists(CACHE):
        return pickle.load(open(CACHE, 'rb'))
    r = build()
    pickle.dump(r, open(CACHE, 'wb'))
    return r


A, B, PAIRS, HOW = load()
A_BEGIN = [b for b, _ in A]


def func(old):
    i = bisect.bisect_right(A_BEGIN, old) - 1
    if i >= 0 and A[i][0] <= old < A[i][1]:
        return i
    return None


def pair(old):
    i = func(old)
    if i is None or i not in PAIRS:
        return None
    return B[PAIRS[i]][0], A[i][0], HOW[i]


def _match_at(old, new, n=24):
    rx, used = p.pattern(p.OLD, old, n)
    if rx is None:
        return False
    blob = p.NEW.rd(new, used + 16)
    return rx.match(blob) is not None


GOOD = ('same', 'moved', 'gap', 'gap-moved', 'begin', 'same~', 'moved~', 'call')
_DATA = {}


def data(g, limit=12):
    """(new rva, votes, sites) of the global at old rva g: through the code that addresses it, each site placed with
    addr() (only sites placed for certain count)."""
    if g in _DATA:
        return _DATA[g]
    votes = Counter()
    sites = p.xrefs(p.OLD, g, limit)
    for s in sites:
        i = func(s)
        if i is not None and i not in PAIRS:
            continue                      # a site in a function that is not paired yet: no recursion
        n, how = addr(s)
        if n is None or how not in GOOD:
            continue
        t = p.target_of(p.NEW, n)
        if t is not None:
            votes[t] += 1
    r = (None, 0, len(sites)) if not votes else (votes.most_common(1)[0][0], votes.most_common(1)[0][1], len(sites))
    _DATA[g] = r
    return r


def refs(img, begin, size):
    """[(offset, 'c' | 'd', target)] of the rel32 calls / jumps and rip-relative operands in a function."""
    out = []
    for ins in p.md.disasm(img.rd(begin, size), begin):
        if ins.group(p.capstone.CS_GRP_CALL) or ins.group(p.capstone.CS_GRP_JUMP):
            if ins.imm_size == 4 and ins.operands and ins.operands[0].type == p.capstone.x86.X86_OP_IMM:
                t = ins.operands[0].imm
                if not (begin <= t < begin + size):
                    out.append((ins.address - begin, 'c', t))
            continue
        for op in ins.operands:
            if op.type == p.capstone.x86.X86_OP_MEM and op.mem.base == p.capstone.x86.X86_REG_RIP:
                out.append((ins.address - begin, 'd', ins.address + ins.size + op.mem.disp))
    return out


def pair_by_refs(i):
    """An old function the sizes could not place (a run of identical template copies): the new function of the same
    size between the same neighbours whose calls and globals are the mapped ones of the old."""
    lo = next((t for t in range(i - 1, max(-1, i - 3000), -1) if t in PAIRS and HOW[t] != 'refs'), None)
    hi = next((t for t in range(i + 1, min(len(A), i + 3000)) if t in PAIRS and HOW[t] != 'refs'), None)
    if lo is None or hi is None:
        return None
    ob, oe = A[i]
    taken = set(PAIRS.values())
    cands = [j for j in range(PAIRS[lo] + 1, PAIRS[hi]) if j not in taken and B[j][1] - B[j][0] == oe - ob]
    if not cands:
        return None
    ro = refs(p.OLD, ob, oe - ob)
    want = []
    for off, kind, t in ro:
        if kind == 'c':
            k = func(t)
            if k is not None and k in PAIRS and A[k][0] == t:
                want.append((off, B[PAIRS[k]][0]))
        else:
            n, v, sites = data(t)
            if n is not None and v >= 2:
                want.append((off, n))
    if not want:
        return None
    best = []
    for j in cands:
        rn = {off: t for off, _k, t in refs(p.NEW, B[j][0], oe - ob)}
        score = sum(1 for off, t in want if rn.get(off) == t)
        best.append((score, j))
    best.sort(reverse=True)
    top = best[0][0]
    if top == len(want) and (len(best) == 1 or best[1][0] < top):
        PAIRS[i] = best[0][1]
        HOW[i] = 'refs'
        return best[0][1]
    return None


_NUM = __import__('re').compile(r'0x[0-9a-f]+|\b\d+\b')


def shape(img, at, n):
    """The next n instructions as (mnemonic, operands with every number masked); None when they cannot be read."""
    out = []
    for ins in p.md.disasm(img.rd(at, n * 15 + 16), at):
        out.append((ins.mnemonic, _NUM.sub('#', ins.op_str)))
        if len(out) == n:
            return tuple(out)
    return None


def sweep(begin, end):
    """Instruction starts of the new function, read in a line from its beginning."""
    out = []
    for ins in p.md.disasm(p.NEW.rd(begin, end - begin), begin):
        out.append(ins.address)
    return out


def changed_numbers(old, new, n=6):
    """What differs between the old and the new instructions at a 'same~' / 'moved~' site: [(old text, new text)]."""
    a = list(p.md.disasm(p.OLD.rd(old, n * 15 + 16), old))[:n]
    b = list(p.md.disasm(p.NEW.rd(new, n * 15 + 16), new))[:n]
    out = []
    for x, y in zip(a, b):
        if x.group(p.capstone.CS_GRP_CALL) or x.group(p.capstone.CS_GRP_JUMP) or 'rip' in x.op_str:
            continue
        if x.op_str != y.op_str:
            out.append(('%s %s' % (x.mnemonic, x.op_str), '%s %s' % (y.mnemonic, y.op_str)))
    return out


def callers(img, target, limit=60):
    """Addresses of `call rel32` instructions in img's code that call target."""
    import numpy as np
    out = []
    for _n, va, vs, ro, rs, _c in img.code:
        blob = np.frombuffer(img.d, dtype=np.uint8, count=min(vs, rs), offset=ro)
        idx = np.nonzero(blob[:-5] == 0xE8)[0]
        rel = (blob[idx + 1].astype(np.int64) | (blob[idx + 2].astype(np.int64) << 8) |
               (blob[idx + 3].astype(np.int64) << 16) | (blob[idx + 4].astype(np.int64) << 24))
        rel = np.where(rel >= 2 ** 31, rel - 2 ** 32, rel)
        hit = idx[(va + idx + 5 + rel) == target]
        out += [int(va + h) for h in hit[:limit]]
    return out


def callee(old_fn, limit=60):
    """(new rva, votes, callers placed) of a function the sizes could not pair (it was rewritten or it moved): what
    the new build calls at the places where the old build called it."""
    votes = Counter()
    placed = 0
    for c in callers(p.OLD, old_fn, limit):
        i = func(c)
        if i is None or i not in PAIRS:
            continue
        n, how = addr(c)
        if n is None or how not in ('same', 'moved', 'same~', 'moved~'):
            continue
        ins = p.NEW.insns(n, 1)
        if not ins or ins[0].mnemonic != 'call' or not ins[0].operands or ins[0].operands[0].type != p.capstone.x86.X86_OP_IMM:
            continue
        placed += 1
        votes[ins[0].operands[0].imm] += 1
    if not votes:
        return None, 0, placed
    t, v = votes.most_common(1)[0]
    return t, v, placed


_TARGET = {}


def target(old_fn):
    """New address of a function called by rel32: its pair, or what its callers call now (callee)."""
    if old_fn in _TARGET:
        return _TARGET[old_fn]
    r = None
    i = func(old_fn)
    if i is not None and A[i][0] == old_fn and i in PAIRS and HOW[i] in ('anchor', 'size', 'refs'):
        r = B[PAIRS[i]][0]
    else:
        _TARGET[old_fn] = None            # no recursion through addr() -> _by_call() -> target()
        n, v, placed = callee(old_fn, 24)
        if n is not None and v >= 2 and v * 2 > placed:
            r = n
        elif i is not None and A[i][0] == old_fn and i in PAIRS:
            r = B[PAIRS[i]][0]
        else:
            g, how = addr(old_fn)
            r = g if how in ('gap', 'gap-moved') else None
    _TARGET[old_fn] = r
    return r


def _calls(img, begin, end):
    out = []
    for ins in p.md.disasm(img.rd(begin, end - begin), begin):
        if ins.mnemonic == 'call' and ins.operands and ins.operands[0].type == p.capstone.x86.X86_OP_IMM:
            out.append((ins.address, ins.size, ins.operands[0].imm))
    return out


def _by_call(old, ob, oe, nb, ne):
    oc = _calls(p.OLD, ob, oe)
    here = [(k, c) for k, c in enumerate(oc) if c[0] == old or c[0] + c[1] == old]
    if not here:
        return None
    k, (ca, cs, ct) = here[0]
    after = ca + cs == old
    nt = target(ct)
    if nt is None:
        return None
    same_old = [c for c in oc if c[2] == ct]
    same_new = [c for c in _calls(p.NEW, nb, ne) if c[2] == nt]
    if not same_new or len(same_old) != len(same_new):
        return None
    c = same_new[same_old.index((ca, cs, ct))]
    return c[0] + c[1] if after else c[0]


def addr(old):
    i = func(old)
    if i is not None:
        if i not in PAIRS and pair_by_refs(i) is None:
            return None, None
        j = PAIRS[i]
        ob, oe = A[i]
        nb, ne = B[j]
        if old == ob:
            return nb, 'begin'
        guess = nb + (old - ob)
        if guess < ne and _match_at(old, guess):
            return guess, 'same'
        rx, used = p.pattern(p.OLD, old, 24)
        if rx is not None:
            body = p.NEW.rd(nb, ne - nb)
            hits = [nb + m.start() for m in rx.finditer(body)]
            if len(hits) == 1:
                return hits[0], 'moved'
            if len(hits) > 1:
                near = min(hits, key=lambda h: abs(h - guess))
                if oe - ob == ne - nb and near == guess:
                    return guess, 'same'
                return near, 'moved?%d' % len(hits)
        # the instructions are the same but a number in them changed (a field that moved inside a structure, a stack
        # offset): the same mnemonics and operand shapes
        want = shape(p.OLD, old, 6)
        if want:
            if guess < ne and shape(p.NEW, guess, 6) == want:
                return guess, 'same~'
            starts = [a for a in sweep(nb, ne) if a != guess]
            hits = [a for a in starts if shape(p.NEW, a, 6) == want]
            if len(hits) == 1:
                return hits[0], 'moved~'
        # a call, or the address a call returns to: the same call (the k-th to that function) in the new function
        r = _by_call(old, ob, oe, nb, ne)
        if r:
            return r, 'call'
        return (guess if oe - ob == ne - nb else None), 'guess'
    # between functions (leaf functions and stubs have no unwind entry): by the nearest paired neighbours
    k = bisect.bisect_right(A_BEGIN, old) - 1
    lo = next((t for t in range(k, max(-1, k - 50), -1) if t in PAIRS), None)
    hi = next((t for t in range(k + 1, min(len(A), k + 51)) if t in PAIRS), None)
    c = []
    if lo is not None:
        c.append(B[PAIRS[lo]][1] + (old - A[lo][1]))
    if hi is not None:
        c.append(B[PAIRS[hi]][0] - (A[hi][0] - old))
    for g in c:
        if _match_at(old, g, 16):
            return g, 'gap'
    # the same bytes somewhere between the two neighbours
    if lo is not None and hi is not None:
        rx, used = p.pattern(p.OLD, old, 24)
        if rx is not None:
            a0, a1 = B[PAIRS[lo]][1], B[PAIRS[hi]][0]
            if 0 < a1 - a0 < 0x200000:
                hits = [a0 + m.start() for m in rx.finditer(p.NEW.rd(a0, a1 - a0))]
                if len(hits) == 1:
                    return hits[0], 'gap-moved'
    return (c[0] if len(c) == 2 and c[0] == c[1] else None), 'gap-guess'


if __name__ == '__main__':
    print('functions old', len(A), 'new', len(B), 'paired', len(PAIRS), Counter(HOW.values()))
    for a in sys.argv[1:]:
        v = int(a, 16)
        n, h = addr(v)
        print('%X -> %s %s' % (v, '%X (%+X)' % (n, n - v) if n else None, h), pair(v) and ['%X' % x if isinstance(x, int) else x for x in pair(v)])


def shape_len(img, at, length):
    """The instructions that lie wholly inside [at, at + length), numbers masked."""
    out = []
    for ins in p.md.disasm(img.rd(at, length + 16), at):
        if ins.address + ins.size > at + length:
            break
        out.append((ins.mnemonic, _NUM.sub('#', ins.op_str)))
    return tuple(out)


def text_len(img, at, length):
    out = []
    for ins in p.md.disasm(img.rd(at, length + 16), at):
        if ins.address + ins.size > at + length:
            break
        out.append('%s %s' % (ins.mnemonic, ins.op_str))
    return '; '.join(out)
