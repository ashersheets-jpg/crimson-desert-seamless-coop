"""Which source lines name the game function that holds an OLD address?  python whonames.py <old hex> [<old hex> ...]"""
import json, sys
import fmap

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/'
maps = {t: json.load(open(ROOT + 'build/native-port/map_3663_%s.json' % t)) for t in ('native', 'layer')}
for a in sys.argv[1:]:
    old = int(a, 16)
    i = fmap.func(old)
    if i is None:
        print('0x%X: no function' % old)
        continue
    b, e = fmap.A[i]
    print('0x%X in old 0x%X..0x%X -> new 0x%X' % (old, b, e, fmap.B[fmap.PAIRS[i]][0] if i in fmap.PAIRS else 0))
    for t, m in maps.items():
        for k, v in m.items():
            if b <= int(k, 16) < e:
                print('    %s 0x%s -> 0x%X  %s' % (t, k[2:].upper() if k.startswith('0x') else k, v.get('new') or 0, ', '.join(v.get('at', [])[:4])))
