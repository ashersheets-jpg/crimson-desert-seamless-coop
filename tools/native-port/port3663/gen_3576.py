"""Port 1.0.0.3549 -> 1.0.0.3576 (2026-11-06): writes offsets_3576.h (3549's table re-exported, the moved values
redefined), then points the source at build_3576 and moves the sites kept outside the header. Values: map_all.py
(map_3576.json), extra.py / tmpl5.py (template copies, vtable slots), rtti_vt.py (vtables), troc76.py (opcodes: all
1175 unchanged). Run once."""
import glob
import os
import re

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/cd-coop-3446/'
SRC = ROOT + 'src/mod/game/'
h49 = open(SRC + 'offsets_3549.h', encoding='utf-8').read()
names = re.findall(r'using build_3527::(\w+);', h49) + re.findall(r'inline constexpr \w+ (\w+)(?:\[\])? =', h49)

MOVED = [
    ('kPeTimestamp', 'uint32_t', '0x6AED2E85', '3549:0x6AEB7D59'),
    ('kImageBase', 'uintptr_t', '0x140000000', 'fixed, no ASLR'),
    ('kImageSize', 'uintptr_t', '0x13CC4000', '3549:0x13CFF000'),
    ('kWorldSystemSlot', 'uintptr_t', '0x71C0100', '3549:0x71BDEF0, 8/8 code references agree'),
    ('kCameraSlot', 'uintptr_t', '0x70DC130', '3549:0x70DA028, 2/2'),
    ('kStaticPlayerPos', 'uintptr_t', '0x71CF140', '3549:0x71CD0B0, 2/2'),
    ('kChunkOrigin', 'uintptr_t', '0x71AC2C0', '3549:0x71AA150, 8/8'),
    ('kCutscenePlaying', 'uintptr_t', '0x70DBF96', '3549:0x70D9F14, 2/2'),
    ('kFieldTimeClient', 'uintptr_t', '0x6EAD998', '3549:0x6EAB998, 6/6'),
    ('kMiniGameRoot', 'uintptr_t', '0x71C0158', '3549:0x71BDF48, 8/8'),
    ('kClientClockGlobal', 'uintptr_t', '0x70DBF2D', '3549:0x70D9E3D, 8/8 cmp sites'),
    ('kClientFrameClockMs', 'uintptr_t', '0x70DBF61', '= flag + 0x34'),
    ('kClientFrameClockUs', 'uintptr_t', '0x70DBF69', '= flag + 0x3C'),
    ('kClientClockBias', 'uintptr_t', '0x71C03A8', '= kWorldSystemSlot + 0x2A8 (8/8 references agree)'),
    ('kMinimapIconRoot', 'uintptr_t', '0x71C0498', '= kWorldSystemSlot + 0x398'),
    ('kMinimapIconRootAlt', 'uintptr_t', '0x71C04A0', '= kWorldSystemSlot + 0x3A0'),
    ('kStatusFlagsChanged', 'uintptr_t', '0x6EA030', '3549:0x6E99D0, head matches'),
    ('kVtNormalMovementMethod', 'uintptr_t', '0x595AD08', '3549:0x5958C10 (= RTTI); slot 2 = kMovementMethodStep'),
    ('kMovementMethodStep', 'uintptr_t', '0xBBDA10', '3549:0xBBD5F0, head matches'),
    ('kVtClientActorManager', 'uintptr_t', '0x5946C40', '3549:0x5944B38 (= RTTI)'),
    ('kVtControlledActor', 'uintptr_t', '0x5914D58', '3549:0x59127C8 (= RTTI ClientChildOnlyInGameActor)'),
    ('kVtCharacterControl', 'uintptr_t', '0x59459F0', '3549:0x59438F0 (= RTTI)'),
    ('kVtStatusComponent', 'uintptr_t', '0x591C7C0', '3549:0x591A710 (= RTTI)'),
    ('kVtWallUpMovementMethod', 'uintptr_t', '0x5926050', '3549:0x5923FA8 (= RTTI)'),
    ('kActionLayerAdvance', 'uintptr_t', '0x214F8D0', '3549:0x214EC90, head matches'),
    ('kActionLayersAdvance', 'uintptr_t', '0x2332070', '3549:0x2331320; one of 227 identical template copies: the '
     '64th in both builds (the whole block moved +0xD50, its callees too), 0x670 gap to kPending kept'),
    ('kMinimapIconDecide', 'uintptr_t', '0x9EA56D', '3549:0x9EA31D'),
    ('kVtClientEquipSlotComponent', 'uintptr_t', '0x594D478', '3549:0x594B388 (= RTTI)'),
    ('kItemInfoTableSlot', 'uintptr_t', '0x71C1080', '3549:0x71BEE70, decoded from the load below'),
    ('kItemInfoTableLoad', 'uintptr_t', '0x3DD8B5', '3549:0x3DD5D5'),
    ('kTrocInstancesBegin', 'uintptr_t', '0x6E90D10', '3549:0x6E8ED10, same span'),
    ('kTrocInstancesEnd', 'uintptr_t', '0x6EC4378', 'last + 8'),
    ('kGameplayInput', 'uintptr_t', '0x71A16C8', '3549:0x719F558, u8'),
    ('kUiModePush', 'uintptr_t', '0xF22247', '3549:0xF21B07; its movzx reads kGameplayInput'),
    ('kErrorLogger', 'uintptr_t', '0x15FCC70', '3549:0x15FC450, head matches'),
    ('kClientPostPacket', 'uintptr_t', '0x3A6310', '3549:0x3A6030, head matches'),
    ('kClientOnRecvPacket', 'uintptr_t', '0x3A66A0', '3549:0x3A63C0, head matches'),
    ('kPointClimbEnter', 'uintptr_t', '0x761070', '3549:0x760A10, head matches'),
    ('kRideOnEnter', 'uintptr_t', '0x750210', '3549:0x74FBB0, head matches'),
    ('kRideOffEnter', 'uintptr_t', '0x7511E0', '3549:0x750B80, head matches'),
]
HEADS = [('kMinimapIconDecideHead', '0x80, 0x7C, 0x24, 0x48, 0x00, 0x75, 0x6F, 0x8B, 0x05, 0x26, 0x08, 0x76, 0x06, 0x41, 0x89, 0x06',
          'its mov eax,[rip+d] moved with the data')]
moved = {m[0] for m in MOVED} | {h[0] for h in HEADS}
missing = sorted(moved - set(names))
assert not missing, missing

out = ['#pragma once',
       '// Addresses for CrimsonDesert.exe 1.0.0.3576 (Co-op+ port, 2026-11-06).',
       '//',
       '// Steam updated the game from 1.0.0.3549 (PE 0x6AEB7D59) to 1.0.0.3576 (PE 0x6AED2E85, 326,872,984 bytes) on',
       '// 2026-11-06. Unlike 3527 -> 3549 the code moved this time. Every value was mapped with both exes on disk',
       '// (build/port3576): code by its instructions with relative operands masked, data and vtables through the code',
       '// that addresses them, vtables checked against RTTI, the 227 identical template copies by their order; the Troc',
       '// opcode table is unchanged (1175 names, same ids).',
       '//',
       '// Shifts 3549 -> 3576 vary by region: code +0x250 (minimap decide) .. +0x2E0 (packet hooks, item load) ..',
       '// +0x370 (character control) .. +0x660 (status, ride, climb) .. +0x820 (error logger, load steps) .. +0xC40',
       '// (action layer, quests) .. +0xD50 (aggro, the action-layers template block); vtables +0x20A8 .. +0x2590; data',
       '// +0x2000 (Troc table, field time) .. +0x2220.',
       '',
       '#include "mod/game/offsets_3549.h"',
       '',
       'namespace cdmp::game::build_3576 {',
       '',
       '// Everything this patch did not move (opcodes, struct offsets, wire layout, heads that match).']
for n in names:
    if n not in moved:
        out.append(f'using build_3549::{n};')
out += ['', '// ---- moved in 3576 ----------------------------------------------------------']
for n, t, v, c in MOVED:
    out.append(f'inline constexpr {t} {n} = {v};  // {c}')
for n, v, c in HEADS:
    out.append(f'inline constexpr uint8_t {n}[] = {{{v}}};  // {c}')
out += ['', '}  // namespace cdmp::game::build_3576', '']
open(SRC + 'offsets_3576.h', 'w', encoding='utf-8', newline='\n').write('\n'.join(out))
print(len(names), 'names in 3549;', len(moved), 'moved')

# ---- point the source at build_3576 ----
os.chdir(ROOT)
files = [f.replace(os.sep, '/') for f in glob.glob('src/**/*.*', recursive=True) if f.endswith(('.cpp', '.h'))]
n = 0
for f in files:
    if re.search(r'offsets_\d+\.h$', f):
        continue
    s = open(f, encoding='utf-8', errors='surrogateescape', newline='').read()
    t = s.replace('build_3549', 'build_3576')
    if f.endswith('game/offsets.h'):
        t = t.replace('#include "mod/game/offsets_3549.h"', '#include "mod/game/offsets_3576.h"')
    t = t.replace('Game reader enabled (build 1.0.0.3549 offsets)', 'Game reader enabled (build 1.0.0.3576 offsets)')
    t = t.replace('this mod was made for game version 1.0.0.3549, not', 'this mod was made for game version 1.0.0.3576, not')
    t = t.replace('Build 1.0.0.3549 only (each site is checked)', 'Build 1.0.0.3576 only (each site is checked)')
    if t != s:
        open(f, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(t)
        n += 1
print('files retargeted', n)


def sub(f, pairs):
    s = open(f, encoding='utf-8', errors='surrogateescape', newline='').read()
    for a, b in pairs:
        assert s.count(a) == 1, (f, a, s.count(a))
        s = s.replace(a, b)
    open(f, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(s)


sub('src/mod/net/horse_diag.cpp', [
    ('constexpr Site kCcUpdate = {0x943640,', 'constexpr Site kCcUpdate = {0x9439B0,'),
    ('constexpr Site kCcPost = {0x8624D0,', 'constexpr Site kCcPost = {0x862790,'),
    ('constexpr Site kRestore = {0x949B40,', 'constexpr Site kRestore = {0x949EB0,'),
    ('constexpr Site kPending = {0x2331990,', 'constexpr Site kPending = {0x23326E0,'),
    ('constexpr Site kCcTick = {0x944E70,', 'constexpr Site kCcTick = {0x9451E0,'),
    ('constexpr Site kAnimUpdate = {0x976800,', 'constexpr Site kAnimUpdate = {0x976B70,'),
    ('constexpr Site kCcLate = {0x94F7F0,', 'constexpr Site kCcLate = {0x94FB60,'),
    ('constexpr Site kLayerSample = {0x968F50,', 'constexpr Site kLayerSample = {0x9692C0,'),
    ('// 1.0.0.3527 (Co-op+): every site below moved',
     '// 1.0.0.3576 (Co-op+): every site below moved again (CC, restore, animation, layer sample +0x370, post +0x2C0,\n'
     '// pending +0xD50 with the action-layers template block); all heads unchanged. build/port3576.\n'
     '// 1.0.0.3527 (Co-op+): every site below moved'),
])
sub('src/mod/net/load_trace.cpp', [
    ('{{0x161A000, 2, 4}, {0x161A010, 4, 8}, {0x161A020, 8, 0x10}};  // 3527 (3479: -0x1510)',
     '{{0x161A820, 2, 4}, {0x161A830, 4, 8}, {0x161A840, 8, 0x10}};  // 3576 (3549: -0x820)'),
    ('constexpr uintptr_t kFinishTree = 0x5B2240;  // 3479: 0x5B1920',
     'constexpr uintptr_t kFinishTree = 0x5B28A0;  // 3549: 0x5B2240'),
    ('constexpr uintptr_t kComponentBusy = 0x5B17DF;  // 3479: 0x5B0EBF',
     'constexpr uintptr_t kComponentBusy = 0x5B1E3F;  // 3549: 0x5B17DF'),
    ('constexpr uintptr_t kCcSlot18 = 0x943910, kCcSlot24 = 0x943C60;  // 3527, vtable slots (3479: 0x942B20, 0x942E70)',
     'constexpr uintptr_t kCcSlot18 = 0x943C80, kCcSlot24 = 0x943FD0;  // 3576, vtable slots (3549: 0x943910, 0x943C60)'),
    ('constexpr uintptr_t kMethodStep = 0xBBD5F0;  // 3527 (3479: 0xBBC570)',
     'constexpr uintptr_t kMethodStep = 0xBBDA10;  // 3576 (3549: 0xBBD5F0)'),
    ('constexpr uintptr_t kDriverStep = 0x7819A9;  // 3479: 0x780F59',
     'constexpr uintptr_t kDriverStep = 0x782009;  // 3549: 0x7819A9'),
])
sub('src/mod/net/aggro_test.cpp', [
    ('constexpr uintptr_t kIsTargetable = 0x23E8580;    // (target comp, actor* candidate, u8 mode); 3479: 0x23E4C40',
     'constexpr uintptr_t kIsTargetable = 0x23E92D0;    // (target comp, actor* candidate, u8 mode); 3549: 0x23E8580'),
    ('constexpr uintptr_t kSetCurrentTarget = 0x23EBE00;', 'constexpr uintptr_t kSetCurrentTarget = 0x23ECB50;'),
    ('constexpr uintptr_t kCheckAIBattleType = 0x24C1C60;  // 3479: 0x24BE2F0 (slot 1 of the vtable, now 0x5C7CAD0)\n'
     'constexpr uint8_t kCheckAIBattleTypeHead[] = {0xE9, 0xCB, 0x62, 0xB8, 0x0A};',
     '// Co-op+ (3576): 3527 and 3549 hooked the WRONG function here. 0x24C1C60 is slot 1 of AICondition_IsKeepAggro\n'
     '// (vtable 0x5C7CAD0), not of CheckAIBattleType (0x5C7CA60, slot 1 0x24C1D80): both entries are a bare 5-byte jmp\n'
     '// into the protection, so a head match proves nothing. Now taken from the RTTI: AICondition_CheckAIBattleType\n'
     '// vtable 0x5C7CF78, slot 1 (build/port3576/hist.py checks every archived build).\n'
     'constexpr uintptr_t kCheckAIBattleType = 0x24C2B50;  // 3479: 0x24BE2F0 (RTTI-checked on 3479 and 3576)\n'
     'constexpr uint8_t kCheckAIBattleTypeHead[] = {0xE9, 0xDB, 0x8A, 0xBA, 0x0A};'),
])
sub('src/mod/net/quest_advance.cpp', [
    ('constexpr uintptr_t kSetMissionState = 0x1C3F0F0;', 'constexpr uintptr_t kSetMissionState = 0x1C3FD30;  // 3549: 0x1C3F0F0'),
    ('constexpr uintptr_t kAdvance = 0x2B95BE0;', 'constexpr uintptr_t kAdvance = 0x2B96850;  // 3549: 0x2B95BE0'),
    ('constexpr uintptr_t kTriggerHandler = 0x2EEE800;', 'constexpr uintptr_t kTriggerHandler = 0x2EEF470;  // 3549: 0x2EEE800'),
    ('constexpr uintptr_t kMissionTable = 0x71C1940;', 'constexpr uintptr_t kMissionTable = 0x71C3B60;  // 3549: 0x71C1940'),
])
sub('src/mod/net/quest_events.cpp', [
    ('constexpr uintptr_t kPostEvent = 0x19434D0;  // exe+0x19434D0(', 'constexpr uintptr_t kPostEvent = 0x1944110;  // 3549: 0x19434D0; exe+0x1944110('),
    ('constexpr uintptr_t kAdvance = 0x2B95BE0;', 'constexpr uintptr_t kAdvance = 0x2B96850;  // 3549: 0x2B95BE0'),
])
print('sites moved')
