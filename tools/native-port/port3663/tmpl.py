import portmap as p, re
def uniq(site, back_starts, length=96):
    for st in back_starts:
        rx, used = p.pattern(p.OLD, st, length)
        hits = sorted(p.find(p.NEW, rx), key=lambda h: abs(h - st))
        print(hex(site), 'from', hex(st), 'len', used, 'hits', len(hits), [hex(h + site - st) for h in hits[:4]])
uniq(0x2331320, [0x2331302, 0x2331302 - 0x40, 0x2331302 - 0x80], 160)
uniq(0x2331990, [0x2331990 - 0x6E, 0x2331990 - 0xB0, 0x2331990 - 0x100], 200)
# what does the function containing these look like: find its start (int3 padding before) and the caller
d = p.OLD
a = 0x2331320
while d.rd(a - 1, 1) != b'\xcc': a -= 1
print('fn start old ~', hex(a))
