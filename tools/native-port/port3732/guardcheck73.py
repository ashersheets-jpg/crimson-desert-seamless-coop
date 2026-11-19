"""For a hook at `site` (patch `plen` bytes) redirected to `exit_`: (1) every instruction of the function that writes one
of the registers the exit relies on, between the entry and the site (linear); (2) every branch in the function whose
target lies inside the patched bytes (site+1 .. site+plen-1); (3) the patched instructions' bytes."""
import capstone
import portmap as p

p.md.detail = True
CASES = {
    '2678': dict(fn=0x2D54840, end=0x2D54C04, site=0x2D549DA, plen=11, exit_=0x2D5498D, regs={'r12', 'r13', 'r15', 'rbx', 'rbp'}),
    '3141': dict(fn=0x2D56490, end=0x2D568C6, site=0x2D5663F, plen=12, exit_=0x2D56606, regs={'r12', 'r13', 'r15', 'rdi', 'rbp'}),
    '2808': dict(fn=None, end=None, site=0x2C5884E, plen=11, exit_=0x2C5880E, regs=set()),
}
for name, c in CASES.items():
    if not c['fn']:
        continue
    insns = list(p.md.disasm(p.NEW.rd(c['fn'], c['end'] - c['fn']), c['fn']))
    writes, into = [], []
    for ins in insns:
        if ins.address < c['site']:
            _r, w = ins.regs_access()
            names = {ins.reg_name(x) for x in w}
            # 32-bit writes zero-extend into the 64-bit register
            names |= {{'r12d': 'r12', 'r13d': 'r13', 'r15d': 'r15', 'edi': 'rdi', 'ebx': 'rbx'}.get(n, n) for n in names}
            hit = names & c['regs']
            if hit and ins.mnemonic not in ('push',):
                writes.append('%X %s %s (%s)' % (ins.address, ins.mnemonic, ins.op_str, ','.join(sorted(hit))))
        if (ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL)) and ins.operands and \
                ins.operands[0].type == capstone.x86.X86_OP_IMM:
            t = ins.operands[0].imm
            if c['site'] < t < c['site'] + c['plen']:
                into.append('%X %s 0x%X' % (ins.address, ins.mnemonic, t))
    patched = [('%X' % i.address, i.mnemonic, i.op_str, i.size) for i in insns if c['site'] <= i.address < c['site'] + c['plen']]
    print('==', name, 'site 0x%X exit 0x%X' % (c['site'], c['exit_']))
    print('   writes before the site:', writes)
    print('   branches into the patch:', into or 'none')
    print('   patched:', patched, 'bytes', p.NEW.rd(c['site'], c['plen']).hex(' '))
    print('   exit bytes:', p.NEW.rd(c['exit_'], 12).hex(' '))
    print('   call before site:', p.NEW.rd(c['site'] - 10, 10).hex(' '))
