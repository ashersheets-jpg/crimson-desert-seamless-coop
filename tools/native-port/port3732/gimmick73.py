"""docked_gimmicks / gimmick_event_dedup on 1.0.0.3732 (T23): where its 2976 addresses are now.

No 2976 exe exists here. Anchors: every 'Class::vfN' of ByJunter's 2976 name list whose class has an RTTI vtable in
3732 (slot N read there) gives a (2976, 3732) pair; functions keep their order between builds, so a 2976 target lies
between the 3732 images of its nearest anchors. Inside that window the target's recorded head bytes (the
byte arrays the module checks) are searched; each hit is listed with its distance from the linear guess and the
instructions that the module's own comments describe (the control check, the accept test, the docked-to read).
The controller write / block / field-owner read are at 3663's (or 3576's) values: checked in 3663, mapped by fmap.
   python gimmick73.py            -> build/native-port/gimmick73.txt
"""
import bisect, csv, re, struct, sys
import portmap as p
import fmap

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/'
B = 0x140000000
NEW = p.NEW
OUT = []


def say(*a):
    s = ' '.join(str(x) for x in a)
    OUT.append(s)
    print(s)


def h(b):
    return bytes(b)


HEADS = {
    'kClientRequest': (0x8E59D0, h([0x48, 0x89, 0x5C, 0x24, 0x10, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x55,
                                    0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57])),
    'kClientRequest2': (0x8E5F60, h([0x48, 0x89, 0x5C, 0x24, 0x10, 0x4C, 0x89, 0x4C, 0x24, 0x20,
                                     0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x4C, 0x24, 0x08])),
    'kDockEquip': (0x28AD170, h([0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10,
                                 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54])),
    'kDockOther': (0x28ADE40, h([0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10,
                                 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54])),
    'kServerExecute': (0x2AC1270, h([0x4C, 0x89, 0x4C, 0x24, 0x20, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x48,
                                     0x89, 0x4C, 0x24, 0x08, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54])),
    'kServerUpdate': (0x2AC0790, h([0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41,
                                    0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xA0])),
}
CTL = {  # 3663 / 3576 values in docked_gimmicks_logic.h
    'kControllerWrite': (0x2A31BBB, h([0x41, 0x89, 0x86, 0x90, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x06,
                                       0x4D, 0x8B, 0xCE, 0x4C, 0x8B, 0xC7, 0x48, 0x8D, 0x55, 0x60])),
    'kControllerBlock': (0x2A31B52, h([0x41, 0x8B, 0x86, 0x90, 0x00, 0x00, 0x00, 0x85,
                                       0xC0, 0x75, 0x5E, 0x48, 0x8B, 0x46, 0x40, 0x48])),
    'kFieldOwnerRead': (0x2A31BB1, h([0x8B, 0x87, 0x80, 0x01, 0x00, 0x00, 0x85, 0xC0, 0x74, 0x07])),
}


def code_range():
    return [(va, va + min(vs, rs)) for _n, va, vs, ro, rs, _c in NEW.code]


def in_code(img, rva):
    return any(va <= rva < va + min(vs, rs) for _n, va, vs, ro, rs, _c in img.code)


# ---------------------------------------------------------------- RTTI vtables in 3732
_TD = {}


def type_descs():
    """name -> type descriptor rva, for every '.?AV...@pa@@' string in the exe."""
    if _TD:
        return _TD
    d = NEW.d
    for m in re.finditer(rb'\.\?AV(\w+)@pa@@\0', d):
        o = m.start() - 0x10
        for _n, va, vs, ro, rs, _c in NEW.secs:
            if ro <= o < ro + rs:
                _TD[m.group(1).decode()] = va + o - ro
                break
    return _TD


_COLS = None


def vtables_of(td):
    """Vtables (rva) whose complete object locator names td."""
    global _COLS
    if _COLS is None:
        import numpy as np
        _COLS = {}
        d = NEW.d
        # every COL: u32 sig 1 | u32 off | u32 cd off | u32 td | u32 chd | u32 self
        for _n, va, vs, ro, rs, ch in NEW.secs:
            if ch & 0x20000000 or rs < 0x1000:
                continue
            arr = np.frombuffer(d, dtype='<u4', count=rs // 4, offset=ro)
            sig = np.nonzero(arr[:-6] == 1)[0]
            selfrva = va + sig.astype(np.int64) * 4
            ok = sig[arr[sig + 5].astype(np.int64) == selfrva]
            for k in ok:
                col = va + int(k) * 4
                _COLS.setdefault(int(arr[k + 3]), []).append((col, int(arr[k + 1])))
        # qword pointers to COLs, anywhere in data
        ptr = {}
        want = {B + col for v in _COLS.values() for col, _o in v}
        for _n, va, vs, ro, rs, ch in NEW.secs:
            if ch & 0x20000000 or rs < 0x1000:
                continue
            q = np.frombuffer(d, dtype='<u8', count=rs // 8, offset=ro)
            hits = np.nonzero(np.isin(q, np.array(sorted(want), dtype=np.uint64)))[0]
            for j in hits:
                ptr.setdefault(int(q[j]) - B, []).append(va + int(j) * 8 + 8)
        for tdk, v in _COLS.items():
            _COLS[tdk] = [(off, vt) for col, off in v for vt in ptr.get(col, [])]
    return sorted(_COLS.get(td, []))


def primary_vtable(cls):
    td = type_descs().get(cls)
    if td is None:
        return None
    vts = [vt for off, vt in vtables_of(td) if off == 0]
    return vts[0] if len(vts) == 1 else (sorted(vts)[0] if vts else None)


def slot(vt, n):
    q = struct.unpack('<Q', NEW.rd(vt + 8 * n, 8))[0]
    return q - B if q > B else None


# ---------------------------------------------------------------- anchors 2976 -> 3732
def anchors():
    rows = list(csv.reader(open(ROOT + 'upstream/cdmp-native/docs/research/ghidra/scripts/data/known_names_repo.csv',
                                encoding='utf-8')))
    pairs, miss = [], 0
    for r in rows[1:]:
        if len(r) < 3 or r[1] != 'func':
            continue
        m = re.match(r'^(\w+)::vf(\d+)$', r[2])
        if not m:
            continue
        old = int(r[0], 16)
        got = None
        for cls in (m.group(1), 'TrocTr' + m.group(1)):
            vt = primary_vtable(cls)
            if vt is None:
                continue
            t = slot(vt, int(m.group(2)))
            if t and in_code(NEW, t):
                got = (old, t, r[2])
                break
        if got:
            pairs.append(got)
        else:
            miss += 1
    pairs.sort()
    # keep the longest increasing run of new addresses (a wrong slot or a reordered class drops out)
    tails, prev, idx = [], [-1] * len(pairs), []
    for n, (_o, t, _nm) in enumerate(pairs):
        pos = bisect.bisect_left(tails, t)
        if pos == len(tails):
            tails.append(t); idx.append(n)
        else:
            tails[pos] = t; idx[pos] = n
        prev[n] = idx[pos - 1] if pos else -1
    lis, n = [], idx[-1] if idx else -1
    while n >= 0:
        lis.append(pairs[n]); n = prev[n]
    lis.reverse()
    say('anchors: %d vf names resolved in 3732, %d not, %d kept in order' % (len(pairs), miss, len(lis)))
    return lis


def window(an, old):
    olds = [a[0] for a in an]
    i = bisect.bisect_right(olds, old)
    lo = an[i - 1] if i > 0 else None
    hi = an[i] if i < len(an) else None
    return lo, hi


def disasm(rva, n=40):
    out = []
    for ins in p.md.disasm(NEW.rd(rva, n * 15), rva):
        out.append('    %X: %s %s' % (ins.address, ins.mnemonic, ins.op_str))
        if len(out) >= n:
            break
    return out


def find_all(blob_bytes, lo, hi):
    out = []
    for _n, va, vs, ro, rs, _c in NEW.code:
        a, b = max(lo, va), min(hi, va + min(vs, rs))
        if a >= b:
            continue
        o = ro + a - va
        seg = NEW.d[o:o + (b - a)]
        k = seg.find(blob_bytes)
        while k >= 0:
            out.append(a + k)
            k = seg.find(blob_bytes, k + 1)
    return out


def err_global():
    """The global the game stores eErrNoActorNotExist's hash 0xD65F8D70 in: mov dword [rip+X], 0xD65F8D70."""
    rx = re.compile(rb'\xC7\x05(....)\x70\x8D\x5F\xD6', re.S)
    out = []
    for _n, va, vs, ro, rs, _c in NEW.code:
        for m in rx.finditer(NEW.d, ro, ro + min(vs, rs)):
            at = va + m.start() - ro
            out.append((at, at + 10 + struct.unpack('<i', m.group(1))[0]))
    return out


def reads_global(fn_rva, g, size=0x600):
    hits = []
    for ins in p.md.disasm(NEW.rd(fn_rva, size), fn_rva):
        for op in ins.operands:
            if op.type == p.capstone.x86.X86_OP_MEM and op.mem.base == p.capstone.x86.X86_REG_RIP:
                if ins.address + ins.size + op.mem.disp == g:
                    hits.append(ins.address)
    return hits


def main():
    an = anchors()
    for name, (old, head) in HEADS.items():
        lo, hi = window(an, old)
        say('\n== %s (2976 0x%X), %d head bytes' % (name, old, len(head)))
        if not lo or not hi:
            say('  no anchors on both sides')
            continue
        guess = lo[1] + (old - lo[0]) * (hi[1] - lo[1]) // max(1, hi[0] - lo[0])
        say('  window: %s 0x%X->0x%X .. %s 0x%X->0x%X; linear guess 0x%X' %
            (lo[2], lo[0], lo[1], hi[2], hi[0], hi[1], guess))
        hits = find_all(head, lo[1], hi[1])
        say('  head hits in the window: %d' % len(hits))
        for a in sorted(hits, key=lambda a: abs(a - guess))[:6]:
            say('   0x%X (guess %+X)' % (a, a - guess))
    say('\n== the error global (2976 0x6CF6B68)')
    eg = err_global()
    for at, g in eg:
        say('  store at 0x%X -> global 0x%X' % (at, g))
    say('\n== pa::ClientGimmickActorComponent (2976 vtable 0x55B7898, request slot +0x740)')
    vt = primary_vtable('ClientGimmickActorComponent')
    if vt:
        say('  vtable 0x%X' % vt)
        for off in (0x730, 0x738, 0x740, 0x748, 0x750, 0x7D8, 0x7E0, 0x7E8):
            t = slot(vt, off // 8)
            head = NEW.rd(t, 21) if t else b''
            say('  +0x%X -> 0x%X  head %s%s' % (off, t or 0, head[:21].hex(),
                                                '  = kClientRequestHead' if head == HEADS['kClientRequest'][1] else ''))
        t = slot(vt, 0x740 // 8)
        if t:
            for at, g in eg:
                say('  +0x740 0x%X reads the error global 0x%X at %s' % (t, g, [hex(x) for x in reads_global(t, g)]))
            OUT.extend(disasm(t + 0x50, 24))
        t2 = slot(vt, 0x7E0 // 8)
        if t2:
            calls = [ins for ins in p.md.disasm(NEW.rd(t2, 0x400), t2)
                     if ins.mnemonic == 'call' and ins.op_str.startswith('0x')]
            for c in calls:
                tg = int(c.op_str, 16)
                hd = NEW.rd(tg, 20)
                if hd == HEADS['kClientRequest2'][1]:
                    say('  +0x7E0 0x%X calls 0x%X at 0x%X: head = kClientRequest2Head' % (t2, tg, c.address))
    else:
        say('  no vtable')
    say('\n== the controller write (3663 values?)')
    for name, (rva, b) in CTL.items():
        ok63 = p.OLD.rd(rva, len(b)) == b
        n, how = fmap.addr(rva)
        ok73 = n is not None and NEW.rd(n, len(b)) == b
        say('  %s 0x%X: 3663 bytes %s; 3732 0x%s (%s) bytes %s' % (name, rva, 'MATCH' if ok63 else 'differ',
                                                                 '%X' % n if n else '?', how, 'MATCH' if ok73 else 'differ'))
        if n and not ok73:
            OUT.extend(disasm(n - 8, 8))
    open(ROOT + 'build/native-port/gimmick73.txt', 'w').write('\n'.join(OUT))


if __name__ == '__main__':
    main()
