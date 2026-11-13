"""Every address the mod uses, 3549 -> 3576 (portmap.py). Writes map_3576.json; prints a table."""
import json

import portmap as p

CODE = [
    ('kClientPostPacket', 0x3A6030),
    ('kClientOnRecvPacket', 0x3A63C0),
    ('kPointClimbEnter', 0x760A10),
    ('kRideOnEnter', 0x74FBB0),
    ('kRideOffEnter', 0x750B80),
    ('kStatusFlagsChanged', 0x6E99D0),
    ('kMovementMethodStep', 0xBBD5F0),
    ('kActionLayerAdvance', 0x214EC90),
    ('kActionLayersAdvance', 0x232DFB0),
    ('kMinimapIconDecide', 0x9EA31D),
    ('kItemInfoTableLoad', 0x3DD5D5),
    ('kUiModePush', 0xF21B07),
    ('kErrorLogger', 0x15FC450),
    ('hd.kCcUpdate', 0x943640),
    ('hd.kCcPost', 0x8624D0),
    ('hd.kRestore', 0x949B40),
    ('hd.kPending', 0x232E620),
    ('hd.kCcTick', 0x944E70),
    ('hd.kAnimUpdate', 0x976800),
    ('hd.kCcLate', 0x94F7F0),
    ('lt.step1', 0x161A000),
    ('lt.step2', 0x161A010),
    ('lt.step3', 0x161A020),
    ('lt.kFinishTree', 0x5B2240),
    ('lt.kComponentBusy', 0x5B17DF),
    ('lt.kCcSlot24', 0x943C60),
    ('lt.kDriverStep', 0x7819A9),
    ('rp.node', 0x5B1F20),
    ('rp.tree', 0x4922C0),
    ('rp.point', 0x5A4792),
    ('rp.bend', 0x349DA10),
    ('rp.target', 0x5A4112),
    ('rp.split', 0x50F270),
    ('rp.actor_update', 0x86B346),
    ('rp.rope_slot', 0x59E1BA),
    ('ag.kIsTargetable', 0x23E8580),
    ('ag.kSetCurrentTarget', 0x23EBE00),
    ('qa.kSetMissionState', 0x1C3F0F0),
    ('qa.kAdvance', 0x2B95BE0),
    ('qa.kTriggerHandler', 0x2EEE800),
    ('qe.kPostEvent', 0x19434D0),
    ('lt.kMethodStep', 0xBBD5F0),
]
DATA = [
    ('kWorldSystemSlot', 0x71BDEF0),
    ('kCameraSlot', 0x70DA028),
    ('kStaticPlayerPos', 0x71CD0B0),
    ('kChunkOrigin', 0x71AA150),
    ('kCutscenePlaying', 0x70D9F14),
    ('kFieldTimeClient', 0x6EAB998),
    ('kMiniGameRoot', 0x71BDF48),
    ('kClientClockGlobal', 0x70D9E3D),
    ('kClientClockBias', 0x71BE198),
    ('kMinimapIconRoot', 0x71BE288),
    ('kMinimapIconRootAlt', 0x71BE290),
    ('kVtNormalMovementMethod', 0x5958C10),
    ('kVtClientActorManager', 0x5944B38),
    ('kVtControlledActor', 0x59127C8),
    ('kVtCharacterControl', 0x59438F0),
    ('kVtStatusComponent', 0x591A710),
    ('kVtWallUpMovementMethod', 0x5923FA8),
    ('kVtClientEquipSlotComponent', 0x594B388),
    ('kItemInfoTableSlot', 0x71BEE70),
    ('kTrocInstancesBegin', 0x6E8ED10),
    ('kTrocInstancesLast', 0x6EC2370),
    ('kGameplayInput', 0x719F558),
    ('qa.kMissionTable', 0x71C1940),
]

out = {}
for n, old in CODE:
    new, hits, second = p.code(old)
    head_old = p.OLD.rd(old, 16).hex(' ')
    head_new = p.NEW.rd(new, 16).hex(' ') if new else ''
    out[n] = {'old': old, 'new': new, 'hits': hits, 'second': second, 'head_new': head_new,
              'head_same': head_old == head_new}
    print('%-26s %8X -> %s  hits %d%s  head %s' % (n, old, '%8X (%+X)' % (new, new - old) if new else '   ----   ', hits,
          '' if second is None else ' 2nd@%X' % second, 'same' if head_old == head_new else 'CHANGED'))
for n, old in DATA:
    new, votes, sites = p.data(old)
    out[n] = {'old': old, 'new': new, 'votes': votes, 'sites': sites}
    print('%-26s %8X -> %s  votes %d/%d' % (n, old, '%8X (%+X)' % (new, new - old) if new else '   ----   ', votes, sites))
json.dump(out, open('map_3576.json', 'w'), indent=1)
