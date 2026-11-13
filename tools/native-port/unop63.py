# Port 3576 -> 3663, a correction (2026-11-13): op63.py rewrote message numbers on EVERY line of the native src. Lines
# never touched since the base author's commit (3cbb03c) hold 2976 numbers of modules that were never ported (their
# tests, with 2976 fixtures, are consistent with them); read as 3576 numbers they were "mapped" to nonsense and their
# tests broke (remote_rope). This puts back, on exactly those lines, the text of the commit before the port
# (c212b93) when the line differs from it only in 4-digit decimal numbers.
#   python unop63.py [--dry]
import difflib, glob, re, subprocess, sys

REPO = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/cdmp-native'
BASE, PRE = '3cbb03c', 'c212b93'
NUM = re.compile(r'(?<![\w.])[23]\d{3}(?![\w.])')
dry = '--dry' in sys.argv


def git(*a):
    return subprocess.run(['git', '-C', REPO] + list(a), capture_output=True).stdout


added = {}
cur, ln = None, 0
for line in git('diff', BASE, PRE, '-U0', '--', 'src').decode('utf-8', 'replace').splitlines():
    if line.startswith('+++ '):
        cur = line[6:] if line.startswith('+++ b/') else None
    elif line.startswith('@@'):
        m = re.search(r'\+(\d+)', line)
        ln = int(m.group(1)) if m else 0
    elif line.startswith('+') and cur:
        added.setdefault(cur, set()).add(ln)
        ln += 1
total = 0
for path in sorted(glob.glob(REPO + '/src/**/*.*', recursive=True)):
    path = path.replace('\\', '/')
    if not path.endswith(('.cpp', '.h')):
        continue
    rel = path[len(REPO) + 1:]
    old_raw = git('show', '%s:%s' % (PRE, rel))
    if not old_raw:
        continue
    old = old_raw.decode('utf-8', 'surrogateescape').split('\n')
    now = open(path, encoding='utf-8', errors='surrogateescape', newline='').read().split('\n')
    o_cmp = [x.rstrip('\r') for x in old]
    n_cmp = [x.rstrip('\r') for x in now]
    if o_cmp == n_cmp:
        continue
    ad = added.get(rel, set())
    n = 0
    sm = difflib.SequenceMatcher(None, o_cmp, n_cmp, autojunk=False)
    for tag, a0, a1, b0, b1 in sm.get_opcodes():
        if tag != 'replace' or a1 - a0 != b1 - b0:
            continue
        for k in range(a1 - a0):
            if (a0 + k + 1) in ad:
                continue                       # a line the ports wrote: 3576 numbers, rightly mapped
            if NUM.sub('#', o_cmp[a0 + k]) == NUM.sub('#', n_cmp[b0 + k]):
                cr = '\r' if now[b0 + k].endswith('\r') else ''
                now[b0 + k] = o_cmp[a0 + k] + cr
                n += 1
    if n:
        total += n
        print('%4d  %s' % (n, rel))
        if not dry:
            open(path, 'w', encoding='utf-8', errors='surrogateescape', newline='').write('\n'.join(now))
print(total, 'base-era lines put back', '(dry run)' if dry else '')
