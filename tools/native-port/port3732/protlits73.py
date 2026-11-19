"""Literals in both trees that fall inside the 3663 protection (0x7AC4000..0x14046000), minus the native base author's
2976 values (present in the same file at commit 3cbb03c), with each one's lines."""
import glob, re, subprocess
ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/'
LO, HI = 0x7AC4000, 0x14046000
base_cache = {}


def base_lits(rel):
    if rel not in base_cache:
        r = subprocess.run(['git', '-C', ROOT + 'cdmp-native', 'show', '3cbb03c:' + rel], capture_output=True, text=True,
                           encoding='utf-8', errors='replace')
        base_cache[rel] = {int(x, 16) for x in re.findall(r'\b0x([0-9A-Fa-f]{5,8})\b', r.stdout)} if r.returncode == 0 else set()
    return base_cache[rel]


hits = {}
for tree, sub in (('cdmp-native', 'src'), ('cd-coop-3446', 'src/mod')):
    for path in glob.glob(ROOT + tree + '/' + sub + '/**/*.*', recursive=True):
        path = path.replace('\\', '/')
        if not path.endswith(('.cpp', '.h')) or re.search(r'offsets_3(424|446|460|479|527|576)\.h$', path):
            continue
        rel = path[len(ROOT + tree) + 1:]
        for i, line in enumerate(open(path, encoding='utf-8', errors='replace').read().split('\n')):
            code = line.split('//')[0]
            for m in re.finditer(r'\b0x([0-9A-Fa-f]{7,8})\b', code):
                v = int(m.group(1), 16)
                if not (LO <= v < HI) or v in (0x10000000, 0x14000000, 0xFFFFFFF):
                    continue
                if tree == 'cdmp-native' and v in base_lits(rel):
                    continue
                hits.setdefault(v, []).append('%s/%s:%d' % (tree, rel, i + 1))
for v in sorted(hits):
    print('0x%X' % v, ' '.join(hits[v][:4]))
