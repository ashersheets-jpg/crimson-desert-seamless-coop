"""gimmick73.py, round 2: the points the first pass left open (T23).
   python gimmick73b.py   -> build/native-port/gimmick73b.txt"""
import re, struct
import portmap as p
import fmap
import gimmick73 as g

NEW = p.NEW
OUT = []


def say(*a):
    s = ' '.join(str(x) for x in a)
    OUT.append(s)
    print(s)


def dis(rva, n=30, size=None):
    out = []
    for ins in p.md.disasm(NEW.rd(rva, size or n * 15), rva):
        out.append('    %X: %s %s' % (ins.address, ins.mnemonic, ins.op_str))
        if len(out) >= n:
            break
    return out


def calls_in(rva, size):
    return [(ins.address, int(ins.op_str, 16)) for ins in p.md.disasm(NEW.rd(rva, size), rva)
            if ins.mnemonic in ('call', 'jmp') and ins.op_str.startswith('0x')]


def fn_bounds(rva):
    i = None
    import bisect
    starts = [b for b, _e in fmap.B]
    k = bisect.bisect_right(starts, rva) - 1
    if k >= 0 and fmap.B[k][0] <= rva < fmap.B[k][1]:
        return fmap.B[k]
    return None


def funcs_between(lo, hi):
    return [(b, e) for b, e in fmap.B if lo <= b < hi]


def main():
    # 1. the controller constants: maybe already 3732's
    say('== controller write / block / field-owner read at their CURRENT values, in 3732')
    for name, (rva, b) in g.CTL.items():
        say('  %s 0x%X: 3732 bytes %s' % (name, rva, 'MATCH' if NEW.rd(rva, len(b)) == b else 'differ'))
    # 2. the error hash anywhere in code
    say('\n== 0xD65F8D70 (eErrNoActorNotExist) as an immediate in code')
    imm = b'\x70\x8D\x5F\xD6'
    hits = g.find_all(imm, 0, 0x7FFFFFFF)
    say('  %d hit(s)' % len(hits))
    for a in hits[:12]:
        # the instruction that holds it: try starts up to 10 bytes back
        for back in range(1, 11):
            ins = list(p.md.disasm(NEW.rd(a - back, 16), a - back))
            if ins and ins[0].address + ins[0].size > a and ins[0].address <= a:
                i0 = ins[0]
                tgt = ''
                for op in i0.operands:
                    if op.type == p.capstone.x86.X86_OP_MEM and op.mem.base == p.capstone.x86.X86_REG_RIP:
                        tgt = ' -> global 0x%X' % (i0.address + i0.size + op.mem.disp)
                say('  0x%X: %s %s%s' % (i0.address, i0.mnemonic, i0.op_str, tgt))
                OUT.extend(dis(i0.address, 4))
                break
    # 3. the second client requester: 2976 callers were vf +0x7E0 and another; with the +8 slot shift, +0x7E8
    say('\n== the second client request (2976 0x8E5F60 = request + 0x590; called from vf +0x7E0)')
    vt = g.primary_vtable('ClientGimmickActorComponent')
    req = 0xA00E40
    for off in (0x7E0, 0x7E8, 0x7F0):
        t = g.slot(vt, off // 8)
        fb = fn_bounds(t)
        size = (fb[1] - t) if fb else 0x400
        cs = calls_in(t, min(size, 0x2000))
        say('  vf +0x%X 0x%X (%s bytes): calls %s' % (off, t, size, ['0x%X@%X' % (c, a) for a, c in cs][:20]))
        for a, c in cs:
            hd = NEW.rd(c, 20)
            say('     -> 0x%X head %s' % (c, hd.hex()))
    say('  functions from the request to +0x1000:')
    for b, e in funcs_between(req, req + 0x1000):
        say('    0x%X..0x%X (%X) head %s' % (b, e, e - b, NEW.rd(b, 20).hex()))
    # 4. the docking spawns through their messages' handlers
    say('\n== the docking spawns: candidates and who calls them')
    cands = [0x2B734B0, 0x2B73CF0, 0x2B741D0, 0x2B69C20]
    for c in cands:
        callers = fmap.callers(NEW, c, 20)
        say('  0x%X: %d caller(s) %s' % (c, len(callers), ['0x%X' % x for x in callers]))
        for x in callers[:4]:
            fb = fn_bounds(x)
            say('     caller 0x%X in function 0x%X' % (x, fb[0] if fb else 0))
    for cls in ('OnDemandEquipDockingGimmickSpawnReq', 'OnDemandDockingGimmickSpawnReq', 'UpdateGimmickReq',
                'GimmickExecuteFrameEventReq', 'TrocTrOnDemandEquipDockingGimmickSpawnReq',
                'TrocTrOnDemandDockingGimmickSpawnReq', 'TrocTrUpdateGimmickReq', 'TrocTrGimmickExecuteFrameEventReq'):
        vt2 = g.primary_vtable(cls)
        if not vt2:
            say('  %s: no vtable' % cls)
            continue
        for n in (1, 2, 3):
            t = g.slot(vt2, n)
            if not t or not g.in_code(NEW, t):
                continue
            fb = fn_bounds(t)
            size = (fb[1] - t) if fb else 0x600
            cs = calls_in(t, min(size, 0x3000))
            say('  %s vt 0x%X vf%d 0x%X (%X bytes) calls %s' % (cls, vt2, n, t, size,
                                                              ['0x%X@%X' % (c, a) for a, c in cs][:24]))
    # 5. the server's Update: just before ExecuteFrameEvent (2976: 0xAE0 before it)
    say('\n== the server gimmick Update (2976 0x2AC0790, 0xAE0 before ExecuteFrameEvent 0x2AC1270)')
    ex = 0x2DBEBA0
    for b, e in funcs_between(ex - 0x2000, ex + 0x100):
        body = '\n'.join(dis(b, 400, e - b))
        marks = [m for m in ('0xa0]', '0x60]', '0x90]') if m in body]
        say('    0x%X..0x%X (%X) head %s %s' % (b, e, e - b, NEW.rd(b, 21).hex(), marks))
    say('  ExecuteFrameEvent 0x%X, first instructions:' % ex)
    OUT.extend(dis(ex, 60))
    open(g.ROOT + 'build/native-port/gimmick73b.txt', 'w').write('\n'.join(OUT))


if __name__ == '__main__':
    main()
