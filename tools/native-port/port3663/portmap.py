"""Port helper 3549 -> 3576: map addresses from the old exe to the new one, with both exes on disk.

code(rva):  the instructions at rva in the OLD exe, relative operands (branch targets, rip-relative displacements)
            masked, searched in the NEW exe's code; the hit nearest the old address wins (hits and runner-up shown).
data(rva):  a global: up to 8 instructions in the old code that address it rip-relatively are mapped with code(),
            decoded in the new exe; the targets they agree on win.
"""
import re
import struct
import sys
from collections import Counter

import capstone
import numpy as np

ARCHIVE = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/build/exe-archive/'
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True


class Image:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        d = self.d
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        n = struct.unpack_from('<H', d, pe + 6)[0]
        opt = struct.unpack_from('<H', d, pe + 20)[0]
        sh = pe + 24 + opt
        self.secs = []
        for i in range(n):
            o = sh + i * 40
            name = d[o:o + 8].rstrip(b'\0').decode('latin1')
            vs, va, rs, ro = struct.unpack_from('<IIII', d, o + 8)
            ch = struct.unpack_from('<I', d, o + 36)[0]
            self.secs.append((name, va, vs, ro, rs, ch))
        self.code = [s for s in self.secs if s[5] & 0x20000000 and s[4] > 0x100000]  # executable, big

    def off(self, rva):
        for _n, va, vs, ro, rs, _c in self.secs:
            if va <= rva < va + min(vs, rs):
                return ro + rva - va
        return None

    def rd(self, rva, n):
        o = self.off(rva)
        return self.d[o:o + n] if o is not None else b''

    def insns(self, rva, n):
        return list(md.disasm(self.rd(rva, n + 16), rva))


OLD = Image(ARCHIVE + 'CrimsonDesert-1.0.0.3576.exe')
NEW = Image(ARCHIVE + 'CrimsonDesert-1.0.0.3663.exe')


def pattern(img, rva, length=40):
    """Regex for the code at rva: relative operands masked. Returns (regex, byte count) or (None, 0)."""
    out = b''
    used = 0
    for ins in img.insns(rva, length):
        b = bytes(ins.bytes)
        mask = set()
        if ins.disp_size and any(op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP
                                 for op in ins.operands):
            mask |= set(range(ins.disp_offset, ins.disp_offset + ins.disp_size))
        if ins.imm_size and ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL):
            if ins.imm_size:
                mask |= set(range(ins.imm_offset, ins.imm_offset + ins.imm_size))
        for i, c in enumerate(b):
            out += b'.' if i in mask else re.escape(bytes([c]))
        used += len(b)
        if used >= length:
            break
    return (re.compile(out, re.S), used) if used >= 8 else (None, 0)


def find(img, rx):
    hits = []
    for _n, va, vs, ro, rs, _c in img.code:
        for m in rx.finditer(img.d, ro, ro + min(vs, rs)):
            hits.append(va + m.start() - ro)
    return hits


def code(rva, length=40, quiet=False):
    """(new rva, number of hits, runner-up distance) for the code at old rva."""
    for ln in (length, 24, 16):
        rx, used = pattern(OLD, rva, ln)
        if rx is None:
            return None, 0, None
        hits = sorted(find(NEW, rx), key=lambda h: abs(h - rva))
        if hits:
            second = abs(hits[1] - rva) if len(hits) > 1 else None
            return hits[0], len(hits), second
    return None, 0, None


def target_of(img, at, want=None):
    """The rip-relative target of the instruction at `at` (first memory operand on RIP)."""
    ins = img.insns(at, 1)
    if not ins:
        return None
    ins = ins[0]
    for op in ins.operands:
        if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
            return ins.address + ins.size + op.mem.disp
    return None


def xrefs(img, g, limit=8):
    """Instruction starts in img's code that address g rip-relatively."""
    out = []
    for _n, va, vs, ro, rs, _c in img.code:
        n = min(vs, rs)
        for a in range(4):
            cnt = (n - a) // 4
            disp = np.frombuffer(img.d, dtype='<i4', count=cnt, offset=ro + a).astype(np.int64)
            pos = va + a + np.arange(cnt, dtype=np.int64) * 4  # rva of the disp
            delta = g - (pos + 4 + disp)                       # = bytes of the instruction after the disp
            for p in pos[(delta >= 0) & (delta <= 4)][:limit * 4]:
                p = int(p)
                for s in range(p - 7, p - 1):
                    if target_of(img, s) == g:
                        ins = img.insns(s, 1)[0]
                        if ins.address + ins.disp_offset == p:
                            out.append(s)
                            break
                if len(out) >= limit:
                    return out
    return out


def data(g, limit=8):
    """(new rva, votes, of) for the global at old rva g."""
    votes = Counter()
    sites = xrefs(OLD, g, limit)
    for s in sites:
        ns, hits, _ = code(s, 32)
        if ns is None:
            continue
        t = target_of(NEW, ns)
        if t is not None:
            votes[t] += 1
    if not votes:
        return None, 0, len(sites)
    t, v = votes.most_common(1)[0]
    return t, v, len(sites)


if __name__ == '__main__':
    kind, rva = sys.argv[1], int(sys.argv[2], 16)
    print({'code': code, 'data': data}[kind](rva))
