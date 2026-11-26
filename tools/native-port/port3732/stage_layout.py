"""The schedule stage's layout in 3576 against 3732 (2026-11-25: U-1's reads of ServerNPCScheduleStage, user2_stages_logic.h
kSt_*, are 3576's; the 3732 logs read the position as (0, <x>) and the owner as no actor).

For each exe: the vtables of CommonNPCScheduleStage / ServerNPCScheduleStage (RTTI), the functions that store one of them
(the constructors), and every memory operand on a non-stack register with a displacement in 0x28..0x180 in those
constructors and in vf13 (the close U-1 hooks): the fields' offsets side by side.
    python stage_layout.py [> build/native-port/stage_layout.txt]
"""
import struct
import sys
from collections import defaultdict

import capstone
import numpy as np

ARCHIVE = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/build/exe-archive/'
B = 0x140000000
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
X = capstone.x86
STACK = {X.X86_REG_RSP, X.X86_REG_RBP, X.X86_REG_RIP}


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
        # .pdata: function begin / end RVAs
        dd = pe + 24 + 112  # data directories (PE32+)
        exc_rva, exc_size = struct.unpack_from('<II', d, dd + 3 * 8)
        o = self.off(exc_rva)
        arr = np.frombuffer(d, dtype='<u4', count=exc_size // 4, offset=o).reshape(-1, 3)
        self.fb, self.fe = arr[:, 0].astype(np.int64), arr[:, 1].astype(np.int64)

    def off(self, rva):
        for _n, va, vs, ro, rs, _c in self.secs:
            if va <= rva < va + min(vs, rs):
                return ro + rva - va
        return None

    def rd(self, rva, n):
        o = self.off(rva)
        return self.d[o:o + n] if o is not None else b''

    def func_of(self, rva):
        i = int(np.searchsorted(self.fb, rva, side='right')) - 1
        if i >= 0 and self.fb[i] <= rva < self.fe[i]:
            return int(self.fb[i]), int(self.fe[i])
        return None


def file_to_rva(img, o):
    for _n, va, vs, ro, rs, ch in img.secs:
        if ro <= o < ro + rs:
            return va + o - ro


def vtables(img, name):
    d = img.d
    out = []
    i = d.find(name.encode())
    while i >= 0:
        td = file_to_rva(img, i - 0x10)
        for _n, va, vs, ro, rs, ch in img.secs:
            if ch & 0x20000000 or rs < 0x1000:
                continue
            arr = np.frombuffer(d, dtype='<u4', count=rs // 4, offset=ro)
            for k in np.nonzero(arr == td)[0]:
                col = va + int(k) * 4 - 0x0C
                if struct.unpack('<I', img.rd(col, 4))[0] != 1:
                    continue
                q = np.frombuffer(d, dtype='<u8', count=rs // 8, offset=ro)
                for j in np.nonzero(q == B + col)[0]:
                    out.append(va + int(j) * 8 + 8)
        i = d.find(name.encode(), i + 1)
    return sorted(set(out))


def code_secs(img):
    return [s for s in img.secs if s[5] & 0x20000000 and s[4] > 0x100000]


_LEA = {}


def lea_sites(img, target):
    """Code addresses of `lea r64, [rip + disp]` whose target is `target` (48/4C 8D modrm(rip) disp32), numpy."""
    if id(img) not in _LEA:
        sites, tgts = [], []
        for _n, va, vs, ro, rs, _c in code_secs(img):
            b = np.frombuffer(img.d, dtype=np.uint8, count=rs, offset=ro)
            c = np.nonzero(((b[:-7] == 0x48) | (b[:-7] == 0x4C)) & (b[1:-6] == 0x8D) & ((b[2:-5] & 0xC7) == 0x05))[0]
            disp = (b[c + 3].astype(np.int64) | (b[c + 4].astype(np.int64) << 8) | (b[c + 5].astype(np.int64) << 16) |
                    (b[c + 6].astype(np.int64) << 24))
            disp = np.where(disp >= 2 ** 31, disp - 2 ** 32, disp)
            sites.append(va + c)
            tgts.append(va + c + 7 + disp)
        _LEA[id(img)] = (np.concatenate(sites), np.concatenate(tgts))
    s, t = _LEA[id(img)]
    return [int(x) for x in s[t == target]]


def fields(img, begin, end, lo=0x28, hi=0x180):
    """(disp, mnemonic, op_str, address) for memory operands on non-stack registers in [lo, hi)."""
    out = []
    for ins in md.disasm(img.rd(begin, end - begin), begin):
        for op in ins.operands:
            if op.type == X.X86_OP_MEM and op.mem.base not in STACK and lo <= op.mem.disp < hi:
                out.append((op.mem.disp, ins.mnemonic, ins.op_str, ins.address))
    return out


def report(tag, img):
    print('=' * 20, tag)
    for cls in ('.?AVCommonNPCScheduleStage@pa@@', '.?AVServerNPCScheduleStage@pa@@'):
        vts = vtables(img, cls)
        print(cls, [hex(v) for v in vts])
        for vt in vts[:2]:
            slots = [struct.unpack('<Q', img.rd(vt + 8 * s, 8))[0] - B for s in range(16)]
            print('  slots 0..15', ' '.join('%X' % s for s in slots))
            ctors = sorted({img.func_of(a) for a in lea_sites(img, vt)} - {None})
            for fb, fe in ctors[:6]:
                fl = fields(img, fb, fe)
                print('  stores the vtable: function 0x%X..0x%X, %d field operands' % (fb, fe, len(fl)))
                for disp, mn, ops, at in fl[:60]:
                    print('     +0x%-4X %-8s %-40s @0x%X' % (disp, mn, ops, at))
            f13 = img.func_of(slots[13])
            if f13:
                fl = fields(img, *f13)
                by = defaultdict(int)
                for disp, *_ in fl:
                    by[disp] += 1
                print('  vf13 0x%X..0x%X field displacements:' % f13, ' '.join('+0x%X x%d' % kv for kv in sorted(by.items())))


for tag in ('3576', '3732'):
    report(tag, Image(ARCHIVE + 'CrimsonDesert-1.0.0.%s.exe' % tag))
