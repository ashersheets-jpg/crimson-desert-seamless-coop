"""Every literal the address rewrite changed (p73.py apply), old -> new, with its line; the ones whose line does not
clearly use a game address are printed for a hand check (interaction keys, stat ids, our own module's sizes ...).
   python review73.py <repo> <commit> [all]"""
import re, subprocess, sys
repo, commit = sys.argv[1], sys.argv[2]
show_all = len(sys.argv) > 3
d = subprocess.run(['git', '-C', repo, 'show', commit, '-U0'], capture_output=True, text=True, encoding='utf-8',
                   errors='replace').stdout
ADDR = re.compile(r'uintptr_t|base\s*\+|base1\s*\+|base0\s*\+|g_base|exe\+|rva\(|Site\b|Site\{|Body\{|ip_hook|create_mid|'
                  r'create_inline|MidHook|InlineHook|safe_read\(|Ret\b|Ret =|Fn\b|Fn =|Vt|vt\b|vtable|Vftable|Global|Slot|'
                  r'kSig|kHead|kAddr|kCall|kHook|kStub|kEntry|kSite|kAt\b|Callers?\b|callers?\b|kTable|game::', re.I)
f, minus, plus = None, [], []
out_sus, out_ok = [], 0


def flush():
    global minus, plus, out_ok
    for a, b in zip(minus, plus):
        ca, cb = a.split('//')[0], b.split('//')[0]
        va = re.findall(r'\b0x[0-9A-Fa-f]{5,8}\b', ca)
        vb = re.findall(r'\b0x[0-9A-Fa-f]{5,8}\b', cb)
        ch = [(x, y) for x, y in zip(va, vb) if x != y]
        if not ch:
            continue
        if ADDR.search(cb) and not show_all:
            out_ok += len(ch)
            continue
        out_sus.append('%s: %s | %s' % (f, ', '.join('%s->%s' % c for c in ch), cb.strip()[:170]))
    minus, plus = [], []


for line in d.splitlines():
    if line.startswith('+++ '):
        flush(); f = line[6:]
    elif line.startswith('@@'):
        flush()
    elif line.startswith('-') and not line.startswith('---'):
        minus.append(line[1:])
    elif line.startswith('+'):
        plus.append(line[1:])
flush()
print('clearly addresses:', out_ok, '| to check by hand:', len(out_sus))
for s in out_sus:
    print(s)
