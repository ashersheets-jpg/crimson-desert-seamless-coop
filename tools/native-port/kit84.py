# Port 3732 -> 3840 (2026-11-26): the installer's game check and the READMEs' game version. ONE SHOT (asserts).
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


sub('kit/coopplus/install.ps1', [
    ("$expectedExeVersion = '1.0.0.3732'", "$expectedExeVersion = '1.0.0.3840'"),
    ("$expectedExeStamp = 0x6AFDC0CF   # the PE build stamp of 1.0.0.3732 (3663: 0x6AF5D198)",
     "$expectedExeStamp = 0x6B07E5E9   # the PE build stamp of 1.0.0.3840 (3732: 0x6AFDC0CF, 3663: 0x6AF5D198)"),
    ("$expectedExeImage = 0x13C7E000   # its SizeOfImage (3663: 0x144E9000; the 3663 port had left 3576's 0x13CC4000 here)",
     "$expectedExeImage = 0x13D05000   # its SizeOfImage (3732: 0x13C7E000, 3663: 0x144E9000)"),
])
for f in ('kit/coopplus/README.txt', 'kit/coopplus/README-shared-world.txt'):
    sub(f, [('game version 1.0.0.3732 (the current', 'game version 1.0.0.3840 (the current')])
sub('README.md', [('joined from the title screen, for game build 1.0.0.3732.', 'joined from the title screen, for game build 1.0.0.3840.')])
print('kit and READMEs: 1.0.0.3840')
