# Port check 3 (2026-11-13): two runs' logs side by side, the old game version against the new one.
# Every WRN / ERR line and every line that speaks of a hook's fate (hooked, REFUSED, not the expected code, off,
# installed, ON ...) with its numbers masked, counted; printed: the kinds that are in one run only.
#   python logdiff.py <old run name> <new run name>        (build/native/<name>.{host,guest}.log and .coopplus.*.log)
import re, sys
from collections import Counter

D = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/build/native/'
KEY = re.compile(r' WRN | ERR |REFUSED|refused,|not the expected|not recognised|unsupported|forced OFF|stays off|cannot arm|'
                 r'hooked|installed| ON \(| ON:|: on \(|is off|: off|NOT |not hooked|could not')
NUM = re.compile(r'0x[0-9A-Fa-f]+|\b\d+(\.\d+)?\b')
SKIP = re.compile(r'Net: |Session2 guest: hooks on|Troc tunnel|summary|Timing:|Overlay: (installed|hooked)|Local: |co-op: |Scaling: ')


def kinds(path):
    c = Counter()
    try:
        f = open(path, encoding='utf-8', errors='replace')
    except FileNotFoundError:
        return c
    for line in f:
        if not KEY.search(line) or SKIP.search(line):
            continue
        t = re.sub(r'^\[[0-9:.]+\] *', '', line.strip())
        t = re.sub(r'\[T\d+\] ', '', t)
        # the first two sentences are the kind; the rest is detail
        t = NUM.sub('#', t)[:150]
        c[t] += 1
    return c


def main(old, new):
    for suffix in ('host.log', 'guest.log', 'coopplus.host.log', 'coopplus.guest.log'):
        a, b = kinds(D + old + '.' + suffix), kinds(D + new + '.' + suffix)
        only_new = sorted(k for k in b if k not in a)
        only_old = sorted(k for k in a if k not in b)
        print('==== %s: %d kinds in both | only in %s: %d | only in %s: %d' % (suffix, len(set(a) & set(b)), new, len(only_new), old, len(only_old)))
        for k in only_new:
            print('  NEW  x%-3d %s' % (b[k], k))
        for k in only_old:
            print('  GONE x%-3d %s' % (a[k], k))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
