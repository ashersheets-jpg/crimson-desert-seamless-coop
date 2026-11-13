# Message numbers 1.0.0.3576 -> 1.0.0.3663 (2026-11-13): 1066 of the 1175 messages got another number (4 are new).
# Table: build/port3663/troc_3576_3663.json (troc63.py, read from both exes by message name).
# Every 4-digit number in CODE (comments and strings left alone) that is a 3576 message number is classed:
#   named   the line (its comment included) names that very message                          -> rewritten
#   const   `k<Name> = N` whose name holds the message's name (kOpX, kX, kXAck ...)           -> rewritten
#   syntax  the number itself stands in opcode syntax (op == N, case N:, kOp.. = N,
#           packet(N / make(N, a list of three or more such numbers); in TESTS: every
#           number on a line that does not speak of time or sizes                             -> rewritten
#   time    the code part speaks of time / sizes / distances                                  -> left, listed
#   weak    opcode words on the line, the number not in opcode syntax                         -> left, listed
#   review  none of these                                                                     -> left, listed
# ONE SHOT: a second --apply would map 3663 numbers again. A stamp file stops it.
#   python op63.py <tree> [--apply]       tree: native-src | native-tests | layer-src | layer-tests
# op63_hand.py FORCE: "file:line" -> class decided by hand after reading the dry run ('syntax' = rewrite, 'time' = leave).
import glob, json, os, re, sys

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/'
T = json.load(open(ROOT + 'build/port3663/troc_3576_3663.json'))
OLD, NEW = T['old'], T['new']
name_by_old = {v: k for k, v in OLD.items()}
DIRS = {
    'native-src': ROOT + 'upstream/cdmp-native/src/',
    'native-tests': ROOT + 'upstream/cdmp-native/tests/',
    'layer-src': ROOT + 'upstream/cd-coop-3446/src/',
    'layer-tests': ROOT + 'upstream/cd-coop-3446/tests/',
}
NUM = re.compile(r'(?<![\w.])([23]\d{3})(?![\w.])')
STR = re.compile(r'"(?:\\.|[^"\\])*"')
BLOCK = re.compile(r'/\*.*?\*/')
WEAK = re.compile(r'\bop(code)?\s*[!=<>]=|[!=]=\s*[23]\d{3}\b|\bcase\s+[23]\d{3}\s*:|packet\w*\(\s*[23]\d{3}|\bop\w*\s*=\s*[23]\d{3}|'
                  r'uint16_t\s+k\w+\s*=\s*[23]\d{3}|\{\s*[23]\d{3}\s*,|,\s*[23]\d{3}\s*\}|kOp|opcode|Opcode|\bop\b|is_op|troc|Troc')
TIME = re.compile(r'_ms\b|Ms\b|\bms\b|ms\)|timeout|Timeout|sleep|Sleep|wait|Wait|size|Size|len\b|bytes|_us\b|kb\b|radius|meters|'
                  r'\bm\b|milli|seconds|tick|Tick|delay|Delay|period|interval|budget|limit|Limit|max_|Max|count|Count|'
                  r'chrono|GetTickCount|elapsed|since|until|deadline|dist|height|width|year|\bhz\b|fps', re.I)
STRONG = re.compile(r'(\bop\w*\s*(==|!=)\s*|\bcase\s+|\bkOp\w*\s*=\s*|opcode\w*\s*(==|!=|=)\s*|'
                    r'\b(packet|make|dev_c2s|make_c2s|inject_c2s|inject_s2c|is_op|on_op|build_packet)\w*\(\s*)$')
AFTER = re.compile(r'^\s*(==|!=)\s*\w*op\w*\b')
SKIP_FILES = re.compile(r'offsets_(3424|3446|3460|3479|3527|3549|3576)\.h$|enemy_tables_2976\.h$')
try:
    from op63_hand import FORCE
except ImportError:
    FORCE = {}


def loose(s):
    return re.sub(r'[^a-z0-9]', '', s.lower())


def classify(line, masked, hits, tests):
    out = []
    op_list = len(hits) >= 3 and re.search(r'\bcase\b|\{', masked) and not TIME.search(masked)
    for m in hits:
        n = int(m.group(1))
        msg = name_by_old[n]
        pre, post = masked[:m.start(1)], masked[m.end(1):]
        if re.search(r'\b' + re.escape(msg) + r'\b', line):
            out.append('named')
            continue
        cm = re.search(r'\b(k\w+)\s*=\s*$', pre)
        if cm:
            cn = loose(cm.group(1)[1:])
            base = loose(msg)
            core = re.sub(r'(ack|req|nak|noti)$', '', base)
            if base in cn or (len(core) >= 6 and core in cn):
                out.append('const')
                continue
        if STRONG.search(pre) or AFTER.search(post) or op_list:
            out.append('syntax')
        elif TIME.search(masked):
            out.append('time')
        elif tests:
            out.append('syntax')
        elif WEAK.search(masked):
            out.append('weak')
        else:
            out.append('review')
    return out


def run(tree, apply):
    base = DIRS[tree]
    files = sorted(glob.glob(base + '**/*.h', recursive=True) + glob.glob(base + '**/*.cpp', recursive=True))
    stamp = ROOT + 'build/native-port/op63_%s.done' % tree.replace('-', '_')
    if apply and os.path.exists(stamp):
        sys.exit('already applied (%s): a second run would map the new numbers again' % stamp)
    stats, listed = {}, []
    for f in files:
        f = f.replace('\\', '/')
        if SKIP_FILES.search(f):
            continue
        lines = open(f, encoding='utf-8', errors='surrogateescape', newline='').read().split('\n')
        changed = False
        rel = f[len(base):]
        for i, line in enumerate(lines):
            cut = line.find('//')
            code = line if cut < 0 else line[:cut]
            masked = STR.sub(lambda m: ' ' * len(m.group(0)), code)
            masked = BLOCK.sub(lambda m: ' ' * len(m.group(0)), masked)
            hits = [m for m in NUM.finditer(masked) if int(m.group(1)) in name_by_old]
            if not hits:
                continue
            cls = classify(line, masked, hits, 'tests' in tree)
            forced = FORCE.get('%s:%s:%d' % (tree, rel, i + 1))
            if forced:
                cls = [forced] * len(hits)
            new = code
            for m, c in reversed(list(zip(hits, cls))):
                n = int(m.group(1))
                stats[c] = stats.get(c, 0) + 1
                if c in ('named', 'const', 'syntax'):
                    new = new[:m.start(1)] + str(NEW[name_by_old[n]]) + new[m.end(1):]
            tag = '/'.join(sorted(set(cls)))
            listed.append('%-7s %s:%d: %s   [%s]' % (tag, rel, i + 1, line.strip()[:170], ', '.join(
                '%s=%s->%s' % (m.group(1), name_by_old[int(m.group(1))], NEW[name_by_old[int(m.group(1))]]) for m in hits)))
            if new != code:
                lines[i] = new + line[len(code):]
                changed = True
        if apply and changed:
            open(f, 'w', encoding='utf-8', errors='surrogateescape', newline='').write('\n'.join(lines))
    out = ROOT + 'build/native-port/op63_%s.txt' % tree.replace('-', '_')
    open(out, 'w', encoding='utf-8', errors='replace').write('\n'.join(sorted(listed)))
    print(tree, dict(sorted(stats.items())), '->', out, '(applied)' if apply else '(dry run)')
    if apply:
        open(stamp, 'w').write('applied\n')


if __name__ == '__main__':
    run(sys.argv[1], '--apply' in sys.argv)
