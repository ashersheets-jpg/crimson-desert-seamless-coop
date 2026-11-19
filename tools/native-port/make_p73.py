# Makes p73.py (port 3663 -> 3732, 2026-11-19) from p63.py: the same method, the new pair of builds, and a guard so
# that apply / heads / inline run once per tree (LESSONS 2026-11-13: heads is not repeatable).
import pathlib, sys
here = pathlib.Path(__file__).parent
dst = here / 'p73.py'
if dst.exists():
    sys.exit('p73.py exists already')
s = (here / 'p63.py').read_text(encoding='utf-8')

def rep(a, b, n=1):
    global s
    assert s.count(a) == n, (a, s.count(a))
    s = s.replace(a, b)

rep("# Port 1.0.0.3576 -> 1.0.0.3663 (2026-11-13) of BOTH trees",
    "# Port 1.0.0.3663 -> 1.0.0.3732 (2026-11-19; made from p63.py by make_p73.py) of BOTH trees")
rep("sys.path.insert(0, ROOT + 'build/port3663')", "sys.path.insert(0, ROOT + 'build/port3732')")
rep("CODE_HI = 0x5597000              # 3576: the code section ends here",
    "CODE_HI = 0x5613000              # 3663: the code section (.reloc 0x1000 + 0x5612000) ends here")
rep("DATA_HI = 0x7A44000              # 3576: the copy protection's section starts here",
    "DATA_HI = 0x7AC4000              # 3663: the copy protection's section (.data1) starts here")
rep("    import p63_hand  # OVERRIDES / SKIP decided by hand, kept apart so that this file stays the method\n    for t in TREES:\n        OVERRIDES[t].update(p63_hand.OVERRIDES.get(t, {}))\n        SKIP[t] |= p63_hand.SKIP.get(t, set())",
    "    import p73_hand  # OVERRIDES / SKIP decided by hand, kept apart so that this file stays the method\n    for t in TREES:\n        OVERRIDES[t].update(p73_hand.OVERRIDES.get(t, {}))\n        SKIP[t] |= p73_hand.SKIP.get(t, set())")
rep("build/native-port/map_3663_%s%s.json", "build/native-port/map_3732_%s%s.json")
rep("print('LEFT AS 3576 (no mapping):')", "print('LEFT AS 3663 (no mapping):')")
rep("listed bytes are not the 3576 bytes either", "listed bytes are not the 3663 bytes either")
rep("""    {'scan': lambda: scan(tree),""", """    if cmd in ('apply', 'heads', 'inline') and not dry:
        stamp = out_path(tree, '_done_' + cmd).replace('.json', '.txt')
        if os.path.exists(stamp):
            sys.exit('%s %s ran already (%s); it is not repeatable' % (cmd, tree, stamp))
        open(stamp, 'w').write('ran\\n')
    {'scan': lambda: scan(tree),""")
dst.write_text(s, encoding='utf-8')
(here / 'p73_hand.py').write_text("# Hand decisions for p73.py (3663 -> 3732), with the evidence in a comment.\nOVERRIDES = {'native': {}, 'layer': {}}\nSKIP = {'native': set(), 'layer': set()}\n", encoding='utf-8')
print('ok')
