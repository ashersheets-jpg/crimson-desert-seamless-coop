# Port 3663 -> 3732 (2026-11-19): the native unit tests pin the same addresses as src. Every literal of the src map
# (p73.mapping, hand decisions included) is rewritten in tests/*.cpp|h, except the numbers fix73.py found are not
# addresses. Prints each change for review. ONE SHOT.
import glob, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import p73

NOT_ADDR = {0xF42BE, 0xF42CD, 0xF4321, 0xF4440, 0xF4240, 0x2065780, 0xFFFFFF, 0x1000000, 0x10F000, 0x4A00000,
            0x100004, 0xCB0A77, 0x7120000, 0x7160000, 0x5612000}
stamp = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/build/native-port/map_3732_native_done_tests.txt'
if os.path.exists(stamp):
    sys.exit('ran already')
open(stamp, 'w').write('ran\n')
new_of, _ = p73.mapping('native')
repo = p73.TREES['native']['repo']
total = 0
for path in glob.glob(repo + '/tests/**/*.*', recursive=True):
    if not path.endswith(('.cpp', '.h')):
        continue
    raw = open(path, encoding='utf-8', newline='').read()
    rows = raw.split('\n')
    n = 0
    for i, row in enumerate(rows):
        cut = row.find('//')
        code, rest = (row, '') if cut < 0 else (row[:cut], row[cut:])

        def sub(m):
            global n
            v = int(m.group(1), 16)
            if v in NOT_ADDR or v not in new_of or new_of[v] == v:
                return m.group(0)
            n += 1
            print('  %s:%d 0x%X -> 0x%X | %s' % (os.path.basename(path), i + 1, v, new_of[v], code.strip()[:110]))
            return '0x%0*X' % (len(m.group(1)), new_of[v])
        rows[i] = re.sub(r'\b0x([0-9A-Fa-f]{5,8})\b', sub, code) + rest
    if n:
        open(path, 'w', encoding='utf-8', newline='').write('\n'.join(rows))
        total += n
print(total, 'literals')
