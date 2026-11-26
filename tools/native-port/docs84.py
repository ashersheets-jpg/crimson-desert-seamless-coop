# Port 3732 -> 3840 (2026-11-26): the docs that state the CURRENT game version (not the compatibility review's
# dated snapshot, not the posted texts of earlier builds). ONE SHOT (asserts).
import pathlib

R = pathlib.Path('C:/Users/asher/CrimsonDesert-SeamlessCoop')


def sub(rel, pairs):
    p = R / rel
    s = p.read_bytes().decode('utf-8')
    for a, b in pairs:
        assert s.count(a) == 1, (rel, a, s.count(a))
        s = s.replace(a, b)
    p.write_bytes(s.encode('utf-8'))
    print('  ', rel)


sub('docs/release/FEATURES.md', [('Crimson Desert on Steam, game version 1.0.0.3732, in ONE shared world',
                                  'Crimson Desert on Steam, game version 1.0.0.3840, in ONE shared world')])
sub('docs/release/nexus-page.md', [('**Game version:** 1.0.0.3732 (Steam).', '**Game version:** 1.0.0.3840 (Steam).'),
                                   ('- Crimson Desert on Steam, game version 1.0.0.3732.', '- Crimson Desert on Steam, game version 1.0.0.3840.')])
print('docs: 1.0.0.3840')
