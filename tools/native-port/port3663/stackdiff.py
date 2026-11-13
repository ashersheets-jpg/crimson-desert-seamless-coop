"""Stack slots and registers at OUR hook sites, 3576 against 3663.

For every source file that reads a hook context (ctx.rsp / ctx.rbp / ctx.r..): the game functions its addresses lie
in, old and new side by side:
  - the frame: pushes, `sub rsp,N` (or mov eax,N + the probe call), `lea rbp,[rsp+K]`;
  - every rsp / rbp based operand whose displacement differs between aligned instructions (a local that moved);
  - aligned instructions whose REGISTERS differ (the shape with numbers masked is the same up to register names).
Then the lines of the file that use `ctx.rsp + C` / `ctx.rbp +- C` with a C that moved, or the frame changed.
  python stackdiff.py native|layer [file filter]
"""
import difflib, json, re, sys
from collections import Counter, defaultdict

import capstone
import portmap as p
import fmap

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/'
REPO = {'native': ROOT + 'upstream/cdmp-native/', 'layer': ROOT + 'upstream/cd-coop-3446/'}
X = capstone.x86
_num = re.compile(r'0x[0-9a-f]+|\b\d+\b')
_reg = re.compile(r'\b(r(?:[abcd]x|[sd]i|[sb]p|8|9|1[0-5])[dwb]?|e(?:[abcd]x|[sd]i|[sb]p)|[abcd][lh]|[sd]il|[xyz]mm\d+)\b')


def read(img, b, e):
    out = []
    for ins in p.md.disasm(img.rd(b, e - b), b):
        mem = [(op.mem.base, op.mem.disp) for op in ins.operands if op.type == X.X86_OP_MEM]
        loose = (ins.mnemonic, _reg.sub('R', _num.sub('#', ins.op_str)))
        out.append((loose, mem, ins.mnemonic + ' ' + ins.op_str, ins.address))
    return out


def frame(ins):
    pushes, sub, lea = 0, None, None
    last_imm = None
    for loose, mem, text, _a in ins[:40]:
        if text.startswith('push'):
            pushes += 1
        m = re.match(r'sub rsp, (0x[0-9a-f]+|\d+)$', text)
        if m and sub is None:
            sub = int(m.group(1), 0)
        m = re.match(r'mov eax, (0x[0-9a-f]+|\d+)$', text)
        if m:
            last_imm = int(m.group(1), 0)
        if text == 'sub rsp, rax' and sub is None:
            sub = last_imm
        m = re.match(r'lea rbp, \[rsp ([+-]) (0x[0-9a-f]+|\d+)\]$', text)
        if m and lea is None:
            lea = int(m.group(2), 0) * (1 if m.group(1) == '+' else -1)
    return pushes, sub, lea


def diff(i):
    ob, oe = fmap.A[i]
    nb, ne = fmap.B[fmap.PAIRS[i]]
    a, b = read(p.OLD, ob, oe), read(p.NEW, nb, ne)
    sm = difflib.SequenceMatcher(None, [x[0] for x in a], [x[0] for x in b], autojunk=False)
    slots, regs = Counter(), []
    for tag, a0, a1, b0, b1 in sm.get_opcodes():
        if tag != 'equal':
            continue
        for k in range(a1 - a0):
            x, y = a[a0 + k], b[b0 + k]
            for (r1, d1), (r2, d2) in zip(x[1], y[1]):
                if r1 in (X.X86_REG_RSP, X.X86_REG_RBP) and r1 == r2 and d1 != d2:
                    slots[('rsp' if r1 == X.X86_REG_RSP else 'rbp', d1, d2)] += 1
            if _num.sub('#', x[2]) != _num.sub('#', y[2]) and not x[2].startswith(('push', 'pop')):
                regs.append((x[3], x[2], y[3], y[2]))
    return frame(a), frame(b), slots, regs


def main(tree, filt):
    m = json.load(open(ROOT + 'build/native-port/map_3663_%s.json' % tree))
    base = set(json.load(open(ROOT + 'build/native-port/map_3663_%s_base.json' % tree)))
    by_file = defaultdict(set)
    for k, e in m.items():
        if k in base or e['kind'] != 'code':
            continue
        i = fmap.func(int(k, 16))
        if i is None or i not in fmap.PAIRS or fmap.A[i][1] - fmap.A[i][0] > 60000:
            continue
        for at in e['at']:
            by_file[at.rsplit(':', 1)[0]].add(i)
    for f in sorted(by_file):
        if filt and filt not in f:
            continue
        try:
            src = open(REPO[tree] + f, encoding='utf-8', errors='replace').read()
        except FileNotFoundError:
            continue
        if not re.search(r'ctx\.r|ctx\.xmm|context\.r', src):
            continue
        out = []
        for i in sorted(by_file[f]):
            fo, fn, slots, regs = diff(i)
            if fo == fn and not slots and not regs:
                continue
            ob = fmap.A[i][0]
            nb = fmap.B[fmap.PAIRS[i]][0]
            line = '  fn 0x%X -> 0x%X' % (ob, nb)
            if fo != fn:
                line += '  FRAME pushes %d sub %s lea %s  ->  pushes %d sub %s lea %s' % (
                    fo[0], fo[1] is not None and hex(fo[1]), fo[2] is not None and hex(fo[2]),
                    fn[0], fn[1] is not None and hex(fn[1]), fn[2] is not None and hex(fn[2]))
            out.append(line)
            if slots:
                out.append('     slots: ' + ' '.join('[%s%+#x]->%+#x(x%d)' % (r, d1, d2, n) for (r, d1, d2), n in sorted(slots.items(), key=lambda t: (t[0][0], t[0][1]))[:40]))
            for oa, ot, na, nt in regs[:12]:
                out.append('     regs : %X %-44s | %X %s' % (oa, ot, na, nt))
            if len(regs) > 12:
                out.append('     regs : ... %d more' % (len(regs) - 12))
        if out:
            uses = sorted(set(re.findall(r'ctx\.(r\w+|xmm\d+)', src)))
            print('== %s   (context fields used: %s)' % (f.replace('src/mod/', ''), ' '.join(uses)))
            print('\n'.join(out))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else '')
