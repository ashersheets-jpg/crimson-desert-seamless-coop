"""verify-port.ps1: 3549 -> 3576 (run once; asserts every anchor exists exactly once)."""
import sys

p = sys.argv[1]
raw = open(p, encoding='utf-8', newline='').read()
crlf = '\r\n' in raw
s = raw.replace('\r\n', '\n')


def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old, s.count(old))
    s = s.replace(old, new)


# signature sites -> the globals they load (sites mapped by portmap.code, one hit each)
rep("0x0C9D74B, 3, 0x71BDEF0", "0x0C9DB6B, 3, 0x71C0100")
rep("0x03AB4C6, 3, 0x71BDEF0", "0x03AB7A6, 3, 0x71C0100")
rep("0x2FC2147, 3, 0x70DA028", "0x2FC2CD7, 3, 0x70DC130")
rep("0x0A30E9A, 4, 0x71CD0B0", "0x0A310EA, 4, 0x71CF140")
rep("0x0A30E9A, 11, 0x71CD0B8", "0x0A310EA, 11, 0x71CF148")
rep("0x397CF01, 25, 0x71AA150", "0x397DCB1, 25, 0x71AC2C0")
# measured deltas (previous build -> this one)
rep("""    ('kClientFrameClockMs', 0x70D2D75 + 0x34, 0x70D9E3D + 0x34),
    ('kClientFrameClockUs', 0x70D2D75 + 0x3C, 0x70D9E3D + 0x3C),
    ('kClientClockBias',    0x71B6EF8, 0x71BE198),
    ('kFieldTimeClient',    0x6EA4998, 0x6EAB998),
    ('kTrocInstancesBegin', 0x6E87D10, 0x6E8ED10),
    ('kTrocInstancesEnd',   0x6EBB378, 0x6EC2378),
    ('kMinimapIconRoot',    0x71B6FE8, 0x71BE288),
    ('kMinimapIconRootAlt', 0x71B6FF0, 0x71BE290),""",
    """    ('kClientFrameClockMs', 0x70D9E3D + 0x34, 0x70DBF2D + 0x34),
    ('kClientFrameClockUs', 0x70D9E3D + 0x3C, 0x70DBF2D + 0x3C),
    ('kClientClockBias',    0x71BE198, 0x71C03A8),
    ('kFieldTimeClient',    0x6EAB998, 0x6EAD998),
    ('kTrocInstancesBegin', 0x6E8ED10, 0x6E90D10),
    ('kTrocInstancesEnd',   0x6EC2378, 0x6EC4378),
    ('kMinimapIconRoot',    0x71BE288, 0x71C0498),
    ('kMinimapIconRootAlt', 0x71BE290, 0x71C04A0),""")
rep("(new - 0x70D9E3D) in (0x34, 0x3C), 'flag+0x%X' % (new - 0x70D9E3D))",
    "(new - 0x70DBF2D) in (0x34, 0x3C), 'flag+0x%X' % (new - 0x70DBF2D))")
rep("_clock_target = 0x70D9E3D", "_clock_target = 0x70DBF2D")
rep("_clock_sites = [0x562E95, 0x2F14947, 0x15F9B20, 0x2E4DA59]  # 3479: 0x562575 0x2F0EFD7 0x15F8610 0x2E480E9",
    "_clock_sites = [0x5634F5, 0x2F155B7, 0x15FA340, 0x2E4E6C9]  # 3549: 0x562E95 0x2F14947 0x15F9B20 0x2E4DA59")
rep("0x70D9E71 - _clock_target == 0x34,", "0x70DBF61 - _clock_target == 0x34,")
rep("'gap 0x%X' % (0x70D9E71 - _clock_target))", "'gap 0x%X' % (0x70DBF61 - _clock_target))")
rep("0x70D9E79 - _clock_target == 0x3C,", "0x70DBF69 - _clock_target == 0x3C,")
rep("'gap 0x%X' % (0x70D9E79 - _clock_target))", "'gap 0x%X' % (0x70DBF69 - _clock_target))")
rep("('kClientClockBias',    0x71BE198, True),", "('kClientClockBias',    0x71C03A8, True),")
rep("('kMinimapIconRoot',    0x71BE288, True),", "('kMinimapIconRoot',    0x71C0498, True),")
rep("('kMinimapIconRootAlt', 0x71BE290, True),", "('kMinimapIconRootAlt', 0x71C04A0, True),")
rep("('kTrocInstancesBegin', 0x6E8ED10, False),", "('kTrocInstancesBegin', 0x6E90D10, False),")
rep("('kTrocInstancesEnd',   0x6EC2378, False),", "('kTrocInstancesEnd',   0x6EC4378, False),")
rep("('kFieldTimeClient',    0x6EAB998, False),", "('kFieldTimeClient',    0x6EAD998, False),")
# vtables (RTTI)
rep("('WallUpMovementMethod', 0x5923FA8,", "('WallUpMovementMethod', 0x5926050,")
rep("('ClientActorManager', 0x5944B38,", "('ClientActorManager', 0x5946C40,")
rep("('ClientChildOnlyInGameActor', 0x59127C8,", "('ClientChildOnlyInGameActor', 0x5914D58,")
rep("('ClientCharacterControlActorComponent', 0x59438F0,", "('ClientCharacterControlActorComponent', 0x59459F0,")
rep("('ClientStatusActorComponent', 0x591A710,", "('ClientStatusActorComponent', 0x591C7C0,")
# head sites
rep("('kActionLayerAdvance',  0x214EC90,", "('kActionLayerAdvance',  0x214F8D0,")
rep("('kActionLayersAdvance', 0x2331320,", "('kActionLayersAdvance', 0x2332070,")
rep("""    ('kMinimapIconDecide', 0x9EA31D,
     '80 7C 24 48 00 75 6F 8B 05 56 E9 75 06 41 89 06',""", """    ('kMinimapIconDecide', 0x9EA56D,
     '80 7C 24 48 00 75 6F 8B 05 26 08 76 06 41 89 06',""")
rep("""    ('kUiModePush', 0xF21B07,
     '0F B6 05 4A DA 27 06 88 04 0A FF 43 08 C6 05 3D',""", """    ('kUiModePush', 0xF22247,
     '0F B6 05 7A F4 27 06 88 04 0A FF 43 08 C6 05 6D',""")
rep("    ('kClientPostPacket', 0x3A6030,\n", "    ('kClientPostPacket', 0x3A6310,\n")
rep("    ('kClientOnRecvPacket', 0x3A63C0,\n", "    ('kClientOnRecvPacket', 0x3A66A0,\n")
# Troc span
rep("(0x6EC2378 - 0x6E8ED10) == 0x33668, 'span 0x%X' % (0x6EC2378 - 0x6E8ED10))",
    "(0x6EC4378 - 0x6E90D10) == 0x33668, 'span 0x%X' % (0x6EC4378 - 0x6E90D10))")
rep("sec_of(0x6E8ED10) in _rw and sec_of(0x6EC2378 - 1) == sec_of(0x6E8ED10))",
    "sec_of(0x6E90D10) in _rw and sec_of(0x6EC4378 - 1) == sec_of(0x6E90D10))")
# code_addrs
rep("(injection: sends)', 0x3A6030,", "(injection: sends)', 0x3A6310,")
rep("(capture: receives)', 0x3A63C0,", "(capture: receives)', 0x3A66A0,")
rep("(minimap icon)',     0x9EA31D,", "(minimap icon)',     0x9EA56D,")
rep("(wall-climb fix)',   0x760A10,", "(wall-climb fix)',   0x761070,")
rep("(mount fix)',        0x74FBB0,", "(mount fix)',        0x750210,")
rep("(dismount fix)',     0x750B80,", "(dismount fix)',     0x7511E0,")
# containment
rep("('kCcUpdate', 0x943640,", "('kCcUpdate', 0x9439B0,")
rep("('kCcTick',   0x944E70,", "('kCcTick',   0x9451E0,")
rep("('kAnimUpdate', 0x976800,", "('kAnimUpdate', 0x976B70,")
rep("('kCcLate',   0x94F7F0,", "('kCcLate',   0x94FB60,")
rep("('kCcPost',   0x8624D0,", "('kCcPost',   0x862790,")
rep("('kRestore',  0x949B40,", "('kRestore',  0x949EB0,")
rep("('kPending',  0x2331990,", "('kPending',  0x23326E0,")
rep("# table (3549 -> 3527 -> 3479 -> ...), nearest first",
    "# table (3576 -> 3549 -> 3527 -> 3479 -> ...), nearest first")

out = s.replace('\n', '\r\n') if crlf else s
open(p, 'w', encoding='utf-8', newline='').write(out)
print('verify-port.ps1 retargeted to 3576')
