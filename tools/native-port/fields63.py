# Port 3576 -> 3663: fields that moved INSIDE the game's structures (2026-11-13). ONE SHOT (asserts).
# How they were found (build/port3663): structdiff.py reads every game function our addresses lie in next to its new
# twin and counts the memory operands whose displacement differs; the "family" of one object (all displacements on
# the same register in those functions) shows where the unchanged part ends:
#   the stage manager M (ClientSequencerStageManager): unchanged up to +0x168, everything from +0x170 on is +8
#       (0x170 0x178 0x180 0x259 0x5F8 0x688 0x6A8 0x6AC 0x6B8 0x6B9 0x6BA 0x6D0 all seen at +8)
#   the character control's inner object: +0x10 (CC+0x140 still leads to it: `mov r,[r+0x140]` is followed by
#       +0x2D0 46 times and +0x2E1 27 times in 3663, by +0x2C0 44 times and +0x2D1 25 times in 3576)
#   the movement controller: unchanged up to +0x110, +0x58 from +0x120 on (0x120 0x140 0x1B8 0x208 .. 0x268)
#   the category table of a character (comps+0xF8): its hash part lost 0x18 (+0x68/+0x6C/+0x78/+0x80 are now
#       +0x50/+0x54/+0x60/+0x68)
#   the loading object P: +0xF0 from +0x80 to +0x15E (0xB8 -> 0x1A8, 0x15C -> 0x24C), reshuffled after that
# The actor's component table is UNCHANGED (the pairs `[r+0x68]` then `[r+slot]` have the same counts per slot in
# both builds).
import re

ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/'
done = []


def sub(path, pairs):
    s = open(ROOT + path, encoding='utf-8', errors='surrogateescape', newline='').read()
    nl = '\r\n' if s.count('\r\n') * 2 > s.count('\n') else '\n'
    for old, new, count in pairs:
        old, new = old.replace('\n', nl), new.replace('\n', nl)
        n = s.count(old)
        if n == 0 and s.count(new) == count:
            continue                      # already applied (the first run stopped half way on a CRLF file)
        assert n == count, (path, old, n, count)
        s = s.replace(old, new)
    for attempt in range(5):
        try:
            open(ROOT + path, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(s)
            break
        except OSError:
            import time
            time.sleep(1)
    done.append(path)


N = 'cdmp-native/src/mod/'
T = ' /* 3663 (3576 %s) */'
# ---- the stage manager, +8 from 0x170
sub(N + 'session2/guest_reload_wait.cpp', [('kMgr_Ready = 0x259 /*', 'kMgr_Ready = 0x261 /* 3663 (3576 0x259);', 1),
                                           ('kP_State = 0x15C, kP_Flags = 0x160, kP_290 = 0x290, kP_Manager = 0xB8;',
                                            'kP_State = 0x24C, kP_Flags = 0x24F, kP_290 = 0x2B0, kP_Manager = 0x1A8;  /* 3663: P grew by 0xF0 '
                                            '(3576 0x15C, 0x160, 0x290, 0xB8); the flags and +0x290 feed log lines only and are a reading of '
                                            'the neighbours, not confirmed */', 1)])
sub(N + 'session2/guest_scene_diag.cpp', [('kMgr_Count = 0x174,', 'kMgr_Count = 0x17C' + T % '0x174' + ',', 1),
                                          ('kMgr_Ready = 0x259;', 'kMgr_Ready = 0x261;' + T % '0x259', 1)])
sub(N + 'session2/guest_sched_bind.cpp', [('kMgr_Sched = 0x170 /*', 'kMgr_Sched = 0x178 /* 3663;', 1),
                                          ('kMgr_State = 0x259;', 'kMgr_State = 0x261;' + T % '0x259', 1)])
sub(N + 'session2/guest_seq_play.cpp', [('kMgr_Ready = 0x259 /*', 'kMgr_Ready = 0x261 /* 3663;', 1),
                                        ('kMgr_Loading = 0x174;', 'kMgr_Loading = 0x17C;' + T % '0x174', 1)])
sub(N + 'session2/guest_seq_route.cpp', [('kMgr_Sched = 0x170 /*', 'kMgr_Sched = 0x178 /* 3663;', 1),
                                         ('kMgr_Ready = 0x259 /* 3549 (2976 0x231) */, kMgr_Count = 0x174, kMgr_Created = 0x17C;',
                                          'kMgr_Ready = 0x261 /* 3663 (3576 0x259) */, kMgr_Count = 0x17C, kMgr_Created = 0x184;  /* 3663: +8 */', 1),
                                         ('kMgr_List1B0 = 0x1B0, kMgr_List1C0 = 0x1C0;', 'kMgr_List1B0 = 0x1B8, kMgr_List1C0 = 0x1C8;  /* 3663: +8 */', 1),
                                         ('kMgr_N1D0 = 0x1D8, kMgr_NLive = 0x30, kMgr_NEngage = 0x194;',
                                          'kMgr_N1D0 = 0x1E0, kMgr_NLive = 0x30, kMgr_NEngage = 0x19C;  /* 3663: +8 */', 1)])
sub(N + 'session2/guest_stage_diag.cpp', [('kMgr_Ready = 0x259 /* 3549 (2976 0x231) */, kMgr_Count = 0x174;',
                                           'kMgr_Ready = 0x261 /* 3663 (3576 0x259) */, kMgr_Count = 0x17C;', 1),
                                          ('kP_Q = 0xC0', 'kP_Q = 0x1B0 /* 3663 (3576 0xC0) */', 1)])
sub(N + 'session2/shop_probe.cpp', [('kMgr_State = 0x6D0;  // 3576 +0x6D0', 'kMgr_State = 0x6D8;  // 3663 (3576 +0x6D0)', 1)])
sub(N + 'session2/user2_funnels.cpp', [('kMgrApplyByte = 0x6D0;', 'kMgrApplyByte = 0x6D8;' + T % '0x6D0', 1)])
sub(N + 'session2/user2_stages_logic.h', [('kMgr_Sched = 0x170, kMgr_CellX = 0x6A8,', 'kMgr_Sched = 0x178, kMgr_CellX = 0x6B0,  /* 3663: +8 */', 1),
                                          ('kMgr_CellZ = 0x6AC, kMgr_Preparing = 0x6B9, kMgr_PrepPending = 0x6BA, kMgr_State = 0x6D0;',
                                           'kMgr_CellZ = 0x6B4, kMgr_Preparing = 0x6C1, kMgr_PrepPending = 0x6C2, kMgr_State = 0x6D8;  /* 3663: +8 */', 1)])
sub(N + 'session2/guest_seq_present_logic.h', [('kMgr_InputBlock = 0x230,', 'kMgr_InputBlock = 0x238 /* 3663: +8 */,', 1)])
# ---- the character control's inner object, the movement controller
sub(N + 'game/offsets.h', [('inline constexpr uint32_t kControlInner_Physics = 0x2C0;',
                            'inline constexpr uint32_t kControlInner_Physics = 0x2D0;  /* 3663 (3576 0x2C0): the inner object grew by 0x10 */', 1),
                           ('inline constexpr uint32_t kMoveController_WallUp = 0x120;',
                            'inline constexpr uint32_t kMoveController_WallUp = 0x178;  /* 3663 (3576 0x120): +0x58 from +0x120 on */', 1)])
sub(N + 'session2/mount_sync.cpp', [('kMcMethods = 0x140,', 'kMcMethods = 0x198 /* 3663 (3576 0x140) */,', 1)])
sub(N + 'session2/remote_motion.cpp', [('constexpr uint32_t kMcMethods = 0x140;', 'constexpr uint32_t kMcMethods = 0x198;  /* 3663 (3576 0x140) */', 1)])
# ---- the category table (comps+0xF8)
sub(N + 'session2/npc_category.cpp', [
    ('    if (!rd(tab + 0x68, buckets) || !rd(tab + 0x6C, count) || !buckets || !count || !rd(tab + 0x78, bk) ||\n        !rd(tab + 0x80, ents) || !bk || !ents)',
     '    if (!rd(tab + 0x50, buckets) || !rd(tab + 0x54, count) || !buckets || !count || !rd(tab + 0x60, bk) ||  /* 3663: -0x18 */\n        !rd(tab + 0x68, ents) || !bk || !ents)', 2),
    ('    if (!tab || !rd(tab + 0x6C, count) || !rd(tab + 0x80, ents) || !ents) return out;',
     '    if (!tab || !rd(tab + 0x54, count) || !rd(tab + 0x68, ents) || !ents) return out;  /* 3663: -0x18 */', 1),
    ("// 0x1C358E0's lookup: buckets +0x68, count +0x6C, bucket array +0x78",
     "// 0x1C358E0's lookup (3663: every offset of this line is 0x18 lower): buckets +0x68, count +0x6C, bucket array +0x78", 1)])

L = 'cd-coop-3446/src/mod/'
p = ROOT + L + 'game/offsets_3663.h'
s = open(p, encoding='utf-8', newline='').read()
for u in ('using build_3549::kControlInner_Physics;\n', 'using build_3549::kMoveController_WallUp;\n'):
    assert s.count(u) == 1, u
    s = s.replace(u, '')
a = '// ---- addresses (3576 -> 3663: tools/native-port/p63.py) ---------------------------\n'
assert s.count(a) == 1
s = s.replace(a, '''// ---- fields that moved inside the game's structures (tools/native-port/fields63.py has the evidence)
inline constexpr uint32_t kControlInner_Physics = 0x2D0;   // 3576: 0x2C0 (the inner object grew by 0x10)
inline constexpr uint32_t kMoveController_WallUp = 0x178;  // 3576: 0x120 (the movement controller: +0x58 from +0x120)

''' + a)
open(p, 'w', encoding='utf-8', newline='').write(s)
done.append(L + 'game/offsets_3663.h')
print('\n'.join(done))
