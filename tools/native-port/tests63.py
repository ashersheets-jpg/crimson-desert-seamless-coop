# Port 3576 -> 3663: the native TESTS pin some addresses too (static_assert / CHECK against a literal). ONE SHOT.
# Every hex literal on a line of upstream/cdmp-native/tests added since the base commit that is an address the src
# map (p63.py, tree native) moved is rewritten with the same mapping; literals the map does not know are listed.
import json, re, subprocess, sys
sys.path.insert(0, 'C:/Users/asher/CrimsonDesert-SeamlessCoop/build/port3663')
import p63

REPO = p63.TREES['native']['repo']
mp, known = p63.mapping('native')
diff = subprocess.run(['git', '-C', REPO, 'diff', p63.TREES['native']['base'], 'HEAD', '-U0', '--', 'tests'],
                      capture_output=True, text=True, encoding='utf-8', errors='replace').stdout
want, f, ln = {}, None, 0
for line in diff.splitlines():
    if line.startswith('+++ '):
        f = line[6:] if line.startswith('+++ b/') else None
    elif line.startswith('@@'):
        m = re.search(r'\+(\d+)', line)
        ln = int(m.group(1)) if m else 0
    elif line.startswith('+') and f:
        want.setdefault(f, set()).add(ln)
        ln += 1
dry = '--dry' in sys.argv
total, unknown = 0, {}
for f, lines in sorted(want.items()):
    path = REPO + '/' + f
    try:
        rows = open(path, encoding='utf-8', errors='surrogateescape', newline='').read().split('\n')
    except FileNotFoundError:
        continue
    n = 0
    for i in sorted(lines):
        if i - 1 >= len(rows):
            continue
        row = rows[i - 1]
        cut = row.find('//')
        code, rest = (row, '') if cut < 0 else (row[:cut], row[cut:])

        def sub(mm):
            global n
            v = int(mm.group(1), 16)
            if v in mp and mp[v] != v:
                n += 1
                return '0x%0*X' % (len(mm.group(1)), mp[v])
            if 0x200000 <= v < p63.DATA_HI and v not in mp and len(mm.group(1)) >= 6:
                unknown.setdefault(v, '%s:%d' % (f, i))
            return mm.group(0)
        rows[i - 1] = re.sub(r'\b0x([0-9A-Fa-f]{5,8})\b', sub, code) + rest
    if n:
        total += n
        print('%4d  %s' % (n, f))
        if not dry:
            open(path, 'w', encoding='utf-8', errors='surrogateescape', newline='').write('\n'.join(rows))
print(total, 'literals', '(dry run)' if dry else '')
print('literals in the tests that look like addresses and are not in the src map: %d' % len(unknown))
for v in sorted(unknown)[:40]:
    print('   0x%X  %s' % (v, unknown[v]))
