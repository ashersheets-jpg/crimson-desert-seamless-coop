"""A cross-check of the port map against the function pairing: every mapped CODE address must land inside the new
function that fmap pairs with the old address's function (same function, old and new). A miss is either a wrong
map entry or a wrong pairing; both need a look.     python mapcheck.py [native|layer]
The verdict file's hand decisions are applied first (map_3663_<tree>_verdict.json: key -> [state, new])."""
import json, sys
import fmap

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/'
for tree in (sys.argv[1:] or ['native', 'layer']):
    m = json.load(open(ROOT + 'build/native-port/map_3663_%s.json' % tree))
    try:
        verdict = json.load(open(ROOT + 'build/native-port/map_3663_%s_verdict.json' % tree))
    except FileNotFoundError:
        verdict = {}
    bad = same = nofn = 0
    rows = []
    for k, v in sorted(m.items(), key=lambda kv: int(kv[0], 16)):
        old = int(k, 16)
        new = v.get('new')
        if k in verdict and verdict[k][1]:
            new = verdict[k][1]
        if not new or v.get('kind') not in (None, 'code'):
            continue
        i = fmap.func(old)
        if i is None or i not in fmap.PAIRS:
            nofn += 1
            continue
        nb, ne = fmap.B[fmap.PAIRS[i]]
        if nb <= new < ne:
            same += 1
        else:
            bad += 1
            ob, oe = fmap.A[i]
            j = fmap.funcB(new) if hasattr(fmap, 'funcB') else None
            rows.append('  0x%X (+0x%X in old 0x%X) -> 0x%X, but the pair is new 0x%X..0x%X (%s) | %s' % (
                old, old - ob, ob, new, nb, ne, fmap.HOW.get(i), ', '.join(v.get('at', [])[:2])))
    print('%s: %d inside the paired function, %d OUTSIDE, %d without a pairing' % (tree, same, bad, nofn))
    print('\n'.join(rows))
