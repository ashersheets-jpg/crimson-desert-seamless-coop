"""Source literals that point into the copy-protection section (beyond the code and data the port map covers).
The map never held them, so each one is either ported by hand or stale. For each: the 16 bytes at that address on the
old and on the new build (equal = an unlikely accident, since the section is rebuilt), and where the source has it.
   python protlit.py           (both trees; lines of the old offsets_*.h headers are skipped)"""
import glob, re
import portmap as p

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/'
LO, HI = 0x7600000, 0x14500000
SKIP = re.compile(r'offsets_(3424|3446|3460|3479|3527|3549|3576)\.h$|enemy_tables_2976\.h$|owner_fanout_writers\.h$')
found = {}
for tree, d in (('native', 'upstream/cdmp-native/src/'), ('layer', 'upstream/cd-coop-3446/src/')):
    for f in glob.glob(ROOT + d + '**/*.*', recursive=True):
        f = f.replace('\\', '/')
        if not f.endswith(('.cpp', '.h')) or SKIP.search(f):
            continue
        for n, line in enumerate(open(f, encoding='utf-8', errors='replace').read().split('\n')):
            code = line.split('//')[0]
            code = re.sub(r'"(?:\\.|[^"\\])*"', '""', code)
            for m in re.finditer(r'\b0x([0-9A-Fa-f]{7,8})\b', code):
                v = int(m.group(1), 16)
                if LO <= v < HI:
                    found.setdefault(v, []).append('%s %s:%d' % (tree, f[len(ROOT + d):], n + 1))


def rd(img, a):
    try:
        return img.rd(a, 16).hex(' ')
    except Exception as e:
        return '(unreadable: %s)' % e


for v in sorted(found):
    ob, nb = rd(p.OLD, v), rd(p.NEW, v)
    print('0x%X  %s' % (v, 'SAME BYTES' if ob == nb else 'differs'))
    print('     old %s' % ob)
    print('     new %s' % nb)
    for at in found[v][:4]:
        print('     ' + at)
print(len(found), 'literals')
