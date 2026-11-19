import portmap as p, capstone
def refs(img, f, n=0x200):
    out = []
    for i in img.insns(f, n):
        if i.mnemonic == 'int3': break
        if i.mnemonic in ('call', 'jmp') and i.operands and i.operands[0].type == capstone.x86.X86_OP_IMM:
            out.append((i.address - f, i.mnemonic, i.operands[0].imm))
        for op in i.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                out.append((i.address - f, 'rip', i.address + i.size + op.mem.disp))
    return out
for of, nf in ((0x23312D0, 0x2330030 - 0x50), (0x2331940, 0x23306a0 - 0x50)):
    a = refs(p.OLD, of); b = refs(p.NEW, nf)
    print('old fn', hex(of), 'new fn', hex(nf), 'new pad before', p.NEW.rd(nf - 2, 2).hex())
    for x, y in zip(a, b):
        print('  +%X %s %X | +%X %s %X  d=%+X' % (x[0], x[1], x[2], y[0], y[1], y[2], y[2] - x[2]))
