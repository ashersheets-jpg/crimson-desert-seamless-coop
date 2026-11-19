"""After `p73.py heads`: every byte array the commit changed, old bytes vs new bytes decoded: 'numbers' when the
instruction lengths and mnemonics are the same (only displacements / immediates moved), 'SHAPE' otherwise (the code
itself changed: the hook's patch length, registers or stack slots may need a hand port).
   python shape73.py <repo> [<base rev>]   (default: the working tree against HEAD)"""
import re, subprocess, sys
import capstone

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
repo = sys.argv[1]
rev = sys.argv[2] if len(sys.argv) > 2 else 'HEAD'
diff = subprocess.run(['git', '-C', repo, 'diff', rev, '-U0', '--', 'src'], capture_output=True, text=True,
                      encoding='utf-8', errors='replace').stdout


def dec(b):
    return [(i.size, i.mnemonic, i.op_str) for i in md.disasm(b, 0x1000)]


f = None
minus, plus = [], []


def flush():
    global minus, plus
    if minus or plus:
        ob = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})\b', ' '.join(minus)))
        nb = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})\b', ' '.join(plus)))
        if ob and nb and len(ob) == len(nb):
            a, b = dec(ob), dec(nb)
            same = [(s, m) for s, m, _o in a] == [(s, m) for s, m, _o in b]
            regs_a = [o for _s, _m, o in a]
            regs_b = [o for _s, _m, o in b]
            if same:
                print('numbers  %s  %d B' % (f, len(ob)))
            else:
                print('SHAPE    %s  %d B\n    old: %s\n    new: %s' % (
                    f, len(ob), ' ; '.join('%s %s' % (m, o) for _s, m, o in a)[:300],
                    ' ; '.join('%s %s' % (m, o) for _s, m, o in b)[:300]))
        elif ob or nb:
            print('other    %s  (%d -> %d bytes)' % (f, len(ob), len(nb)))
    minus, plus = [], []


for line in diff.splitlines():
    if line.startswith('+++ '):
        flush()
        f = line[6:]
    elif line.startswith('@@'):
        flush()
    elif line.startswith('-') and not line.startswith('---'):
        minus.append(line[1:])
    elif line.startswith('+'):
        plus.append(line[1:])
flush()
