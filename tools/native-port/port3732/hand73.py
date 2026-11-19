"""For an old (3663) code address the map could not place: score the new functions around the map's guess.
   python hand73.py <old hex> [<old hex> ...]
Each candidate (the guessed function and 12 on each side) gets a similarity ratio of its masked listing against the
old function's (difflib on mnemonic+operands, code addresses blanked), plus its size; the address is then placed at
the same masked instruction inside the best function (the k-th equal instruction text), or by offset."""
import difflib, re, sys, bisect
import portmap as p
import fmap


def listing(img, b, e, cap=1500):
    out = []
    for ins in p.md.disasm(img.rd(b, e - b), b):
        s = '%s %s' % (ins.mnemonic, ins.op_str)
        if ins.mnemonic.startswith('j') or ins.mnemonic == 'call':
            s = re.sub(r'0x[0-9a-f]+', 'ADDR', s)
        s = re.sub(r'rip [+-] 0x[0-9a-f]+', 'rip + D', s)
        out.append((ins.address, s))
        if len(out) >= cap:
            break
    return out


def best(old):
    i = fmap.func(old)
    if i is None:
        return None
    ob, oe = fmap.A[i]
    la = listing(p.OLD, ob, oe)
    ta = [s for _a, s in la]
    n_guess, how = fmap.addr(old)
    if n_guess is None:
        return None
    j = bisect.bisect_right([b for b, _e in fmap.B], n_guess) - 1
    cands = []
    for k in range(max(0, j - 12), min(len(fmap.B), j + 13)):
        nb, ne = fmap.B[k]
        lb = listing(p.NEW, nb, ne)
        r = difflib.SequenceMatcher(None, ta, [s for _a, s in lb], autojunk=False).ratio()
        cands.append((r, nb, ne, lb))
    cands.sort(reverse=True)
    r, nb, ne, lb = cands[0]
    second = cands[1][0] if len(cands) > 1 else 0
    # place the address: the instruction at `old` in the old listing -> the matching one in the new listing
    idx = next((q for q, (a, _s) in enumerate(la) if a == old), None)
    placed = None
    if old == ob:
        placed = nb
    elif idx is not None:
        sm = difflib.SequenceMatcher(None, ta, [s for _a, s in lb], autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag == 'equal' and i1 <= idx < i2:
                placed = lb[j1 + idx - i1][0]
    return dict(old_fn=ob, old_size=oe - ob, new_fn=nb, new_size=ne - nb, ratio=r, second=second, placed=placed,
                guess=n_guess, how=how, at_insn=la[idx][1] if idx is not None else None)


if __name__ == '__main__':
    for s in sys.argv[1:]:
        old = int(s, 16)
        b = best(old)
        if not b:
            print('0x%X: no function' % old)
            continue
        print('0x%X -> %s | fn 0x%X (%d B) -> 0x%X (%d B) ratio %.3f (next %.3f) | map guess 0x%X %s | insn: %s' % (
            old, ('0x%X' % b['placed']) if b['placed'] else 'NOT PLACED', b['old_fn'], b['old_size'], b['new_fn'],
            b['new_size'], b['ratio'], b['second'], b['guess'], b['how'], b['at_insn']))
