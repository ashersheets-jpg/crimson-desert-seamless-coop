# Port 1.0.0.3732 -> 1.0.0.3840 (2026-11-26): the working folder build/port3840 as a copy of build/port3732's tools
# (not their outputs), with the build pair moved on by one (3663 -> 3732 becomes 3732 -> 3840 in names and labels),
# and p84.py / p84_hand.py made from p73.py the way make_p73.py made p73.py from p63.py.
import pathlib, re, shutil, sys

ROOT = pathlib.Path('C:/Users/asher/CrimsonDesert-SeamlessCoop')
src, dst = ROOT / 'build/port3732', ROOT / 'build/port3840'
if dst.exists():
    sys.exit('build/port3840 exists already')
dst.mkdir()
n = 0
for f in sorted(src.glob('*.py')):
    s = f.read_text(encoding='utf-8')
    # the newer number first, so that 3663 -> 3732 does not then become 3840
    s = s.replace('3732', '\0NEW\0').replace('3663', '3732').replace('\0NEW\0', '3840')
    # file names stay (prot73.py, near73.py ...): they import each other by name
    (dst / f.name).write_text(s, encoding='utf-8')
    n += 1
print('copied', n, 'tools to build/port3840')

here = ROOT / 'tools/native-port'
p = (here / 'p73.py').read_text(encoding='utf-8')


def rep(a, b, cnt=1):
    global p
    assert p.count(a) == cnt, (a, p.count(a))
    p = p.replace(a, b)


rep("# Port 1.0.0.3663 -> 1.0.0.3732 (2026-11-19; made from p63.py by make_p73.py) of BOTH trees",
    "# Port 1.0.0.3732 -> 1.0.0.3840 (2026-11-26; made from p73.py by make_port3840.py) of BOTH trees")
rep("sys.path.insert(0, ROOT + 'build/port3732')", "sys.path.insert(0, ROOT + 'build/port3840')")
rep("CODE_HI = 0x5613000              # 3663: the code section (.reloc 0x1000 + 0x5612000) ends here",
    "CODE_HI = 0x5618000              # 3732: the code section (0x1000 + 0x5617000) ends here (CHECK: section table)")
rep("DATA_HI = 0x7AC4000              # 3663: the copy protection's section (.data1) starts here",
    "DATA_HI = 0x7ACB000              # 3732: the copy protection's section (.text1) starts here")
rep("    import p73_hand  # OVERRIDES / SKIP decided by hand, kept apart so that this file stays the method\n    for t in TREES:\n        OVERRIDES[t].update(p73_hand.OVERRIDES.get(t, {}))\n        SKIP[t] |= p73_hand.SKIP.get(t, set())",
    "    import p84_hand  # OVERRIDES / SKIP decided by hand, kept apart so that this file stays the method\n    for t in TREES:\n        OVERRIDES[t].update(p84_hand.OVERRIDES.get(t, {}))\n        SKIP[t] |= p84_hand.SKIP.get(t, set())")
rep("build/native-port/map_3732_%s%s.json", "build/native-port/map_3840_%s%s.json")
rep("print('LEFT AS 3663 (no mapping):')", "print('LEFT AS 3732 (no mapping):')")
rep("listed bytes are not the 3663 bytes either", "listed bytes are not the 3732 bytes either")
if (here / 'p84.py').exists():
    sys.exit('p84.py exists already')
(here / 'p84.py').write_text(p, encoding='utf-8')
(here / 'p84_hand.py').write_text("# Hand decisions for p84.py (3732 -> 3840), with the evidence in a comment.\n"
                                  "OVERRIDES = {'native': {}, 'layer': {}}\nSKIP = {'native': set(), 'layer': set()}\n",
                                  encoding='utf-8')
print('p84.py, p84_hand.py made')
