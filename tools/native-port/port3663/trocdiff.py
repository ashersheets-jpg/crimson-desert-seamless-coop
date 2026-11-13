"""Which MESSAGES changed between 3576 and 3663 beyond their number: for every message (by name) the functions in its
Troc vtable (the first 8 slots: construction, serialise / deserialise, the handler ...) old against new. A slot is
"same" when both functions read as the same instructions with the same numbers once rip-relative displacements and
branch / call targets are masked; "numbers" when only numbers differ (a field offset, a size); "shape" otherwise.
A slot whose function is a 5-byte jmp stub into the copy protection cannot be compared ("stub").
  python trocdiff.py            -> trocdiff.json + a summary;   python trocdiff.py Name1 Name2 ...  -> details
"""
import json, re, struct, sys
from collections import Counter

import capstone
import portmap as p
import fmap

IB = 0x140000000
X = capstone.x86
_num = re.compile(r'0x[0-9a-f]+|\b\d+\b')


def vtables(img, begin, end):
    def rtti(vt):
        try:
            col = struct.unpack('<Q', img.rd(vt - 8, 8))[0] - IB
            td = struct.unpack('<I', img.rd(col + 12, 4))[0]
            raw = img.rd(td + 0x10, 200).split(b'\0')[0].decode('ascii', 'replace')
        except Exception:
            return None
        m = re.match(r'\.\?AVTrocTr(\w+?)@pa@@', raw)
        return m.group(1) if m else None
    out = {}
    pat = re.compile(rb'\x48\x8D([\x05\x0D\x15\x1D\x35\x3D])(....)\x48\x89(\1)(....)', re.S)
    for _n, va, vs, ro, rs, _c in img.code:
        blob = img.d[ro:ro + min(vs, rs)]
        for m in pat.finditer(blob):
            a = va + m.start()
            vt = a + 7 + struct.unpack('<i', m.group(2))[0]
            st_at = va + m.start(3) - 2
            inst = st_at + 7 + struct.unpack('<i', m.group(4))[0]
            if not (begin <= inst < end):
                continue
            name = rtti(vt)
            if name:
                out.setdefault(name, vt)
    return out


def text(img, fn):
    """(kind, [masked instruction text]) of the function at fn (by the unwind table; a stub when it is a bare jmp)."""
    head = img.rd(fn, 5)
    if head[:1] == b'\xe9':
        return 'stub', []
    tab, begins = (fmap.A, fmap.A_BEGIN) if img is p.OLD else (fmap.B, None)
    import bisect
    if img is p.OLD:
        i = bisect.bisect_right(fmap.A_BEGIN, fn) - 1
        rng = fmap.A[i] if i >= 0 and fmap.A[i][0] == fn else None
    else:
        global _BB
        try:
            _BB
        except NameError:
            _BB = [b for b, _ in fmap.B]
        i = bisect.bisect_right(_BB, fn) - 1
        rng = fmap.B[i] if i >= 0 and fmap.B[i][0] == fn else None
    size = (rng[1] - rng[0]) if rng else 64
    out = []
    for ins in p.md.disasm(img.rd(fn, size), fn):
        t = ins.op_str
        if (ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL)) and ins.operands and ins.operands[0].type == X.X86_OP_IMM:
            t = '<t>'
        t = re.sub(r'\[rip [+-] 0x[0-9a-f]+\]', '[rip]', t)
        out.append(ins.mnemonic + ' ' + t)
        if not rng and ins.mnemonic in ('ret', 'jmp'):
            break
    return ('fn' if rng else 'leaf'), out


def main(names):
    ov = vtables(p.OLD, 0x6E8E000, 0x6EC8000)
    nv = vtables(p.NEW, 0x6F10000, 0x6F60000)
    res = {}
    for name in sorted(ov):
        if name not in nv:
            continue
        slots = []
        for s in range(8):
            fo = struct.unpack('<Q', p.OLD.rd(ov[name] + 8 * s, 8))[0] - IB
            fn = struct.unpack('<Q', p.NEW.rd(nv[name] + 8 * s, 8))[0] - IB
            if not (0x1000 <= fo < 0x14000000 and 0x1000 <= fn < 0x15000000):
                slots.append('none')
                continue
            ko, to = text(p.OLD, fo)
            kn, tn = text(p.NEW, fn)
            if ko == 'stub' or kn == 'stub':
                slots.append('stub')
            elif to == tn:
                slots.append('same')
            elif [_num.sub('#', x) for x in to] == [_num.sub('#', x) for x in tn]:
                slots.append('numbers')
            else:
                slots.append('shape')
            if name in names and slots[-1] in ('numbers', 'shape'):
                print('-- %s slot %d: 0x%X -> 0x%X (%s)' % (name, s, fo, fn, slots[-1]))
                import difflib
                for tag, a0, a1, b0, b1 in difflib.SequenceMatcher(None, to, tn, autojunk=False).get_opcodes():
                    if tag != 'equal':
                        print('     old: %s' % ' ; '.join(to[a0:a1][:6]))
                        print('     new: %s' % ' ; '.join(tn[b0:b1][:6]))
        res[name] = slots
    json.dump(res, open('trocdiff.json', 'w'), indent=0)
    c = Counter()
    for name, slots in res.items():
        c['/'.join(sorted(set(s for s in slots if s not in ('same', 'none'))) or ['all same'])] += 1
    print(len(res), 'messages:', dict(c))


if __name__ == '__main__':
    main(set(sys.argv[1:]))
