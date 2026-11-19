"""The scene Req sites of notes_host_unload_crash_3663.md 4.3 on 3732: the instructions after each `call [rax+0x48]`
(is rax tested before use?) and the first instructions of the callee each passes it to (does it test rcx?)."""
import portmap as p
import fmap

SITES = {2644: (0x2D510A9, None), 2654: (0x2D51490, 0x2B54380), 3141: (0x2D51BAF, 0x2B54A60), 2672: (0x2D50C0A, None),
         2678: (0x2D4FF4A, None), 2563: (0x2D530AF, 0x2B57A00), 2313: (0x2D529F9, 0x2B55700), 2546: (0x2D52219, 0x2B551F0),
         2924: (0x2D52609, 0x2B55470), 2125: (0x2D50863, 0x2B58350)}


def show(img, a, n):
    return [('%X' % i.address, i.mnemonic, i.op_str) for i in img.insns(a, n * 8)[:n]]


for op, (site, callee) in SITES.items():
    ns, how = fmap.addr(site)
    print('== %d site 3663 0x%X -> 3732 0x%X (%s)' % (op, site, ns or 0, how))
    for a, m, o in show(p.NEW, ns, 10):
        print('     %s %s %s' % (a, m, o))
    if callee:
        nc, how2 = fmap.addr(callee)
        print('   callee 3663 0x%X -> 3732 0x%X (%s):' % (callee, nc or 0, how2))
        for a, m, o in show(p.NEW, nc, 12):
            print('     %s %s %s' % (a, m, o))
