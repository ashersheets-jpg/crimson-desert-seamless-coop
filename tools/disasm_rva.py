"""Disassemble the archived game exe at an RVA (capstone), for checking a hook site by hand.

    python tools/disasm_rva.py 0x225CD3E 40          40 instructions from exe+0x225CD3E
    python tools/disasm_rva.py 0x225CD7D 12 --exe <path>

Local research only (the exe is the user's own copy in build/exe-archive; nothing of it is published).
"""
import struct
import sys

import capstone

EXE = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/build/exe-archive/CrimsonDesert-1.0.0.3576.exe'
BASE = 0x140000000


def sections(data):
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    nsec = struct.unpack_from('<H', data, pe + 6)[0]
    opt = struct.unpack_from('<H', data, pe + 20)[0]
    off = pe + 24 + opt
    out = []
    for i in range(nsec):
        name, vsize, va, rsize, raw = struct.unpack_from('<8sIIII', data, off + i * 40)
        out.append((name.rstrip(b'\0').decode(errors='replace'), va, max(vsize, rsize), raw, rsize))
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    exe = EXE
    if '--exe' in sys.argv:
        exe = sys.argv[sys.argv.index('--exe') + 1]
        args = [a for a in args if a != exe]
    rva = int(args[0], 16)
    count = int(args[1]) if len(args) > 1 else 24
    data = open(exe, 'rb').read()
    for name, va, size, raw, rsize in sections(data):
        if va <= rva < va + size:
            fo = raw + (rva - va)
            code = data[fo:fo + count * 15]
            md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
            n = 0
            for ins in md.disasm(code, BASE + rva):
                print(f'exe+0x{ins.address - BASE:X}  {ins.bytes.hex(" "):<32} {ins.mnemonic} {ins.op_str}')
                n += 1
                if n >= count:
                    break
            return 0
    print('rva not in any section')
    return 1


if __name__ == '__main__':
    sys.exit(main())
