# Port 3576 -> 3663 by hand, small things (2026-11-13). Each pair is skipped when already applied.
# - the load machine P: its state byte moved (+0x15C -> +0x24C, Q +0xC0 -> +0x1B0) and one state was inserted at
#   0x0B, so every state from 0x0B on is one higher (the machine 0x855C50 -> 0x8825B0 wrote 02 05..18 and writes
#   02 05..0A 0C..19 now): own-2322 wait 0x11 -> 0x12, the wait list 0x12 -> 0x13, in the world 0x18 -> 0x19.
# - docked_gimmicks' controller write (ICreateServerActorDesc vf3): the field is in rdi (was r13).
# - user2_stages G2: the collector's frame is `sub rsp, 0x1C2C8` again (3576: 0x1D2A8); the three slots are the frame
#   + 0x28 / + 0x40 / + 0x50.
ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/cdmp-native/src/mod/session2/'


def sub(path, pairs):
    s = open(ROOT + path, encoding='utf-8', errors='surrogateescape', newline='').read()
    n_done = 0
    for old, new, count in pairs:
        n = s.count(old)
        if n == 0 and s.count(new) >= 1:
            continue
        assert n == count, (path, old, n, count)
        s = s.replace(old, new)
        n_done += n
    open(ROOT + path, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(s)
    print('  %-34s %d' % (path, n_done))


sub('guest_reload_wait_logic.h', [
    ('inline constexpr int kStateIdle = 0x18;', 'inline constexpr int kStateIdle = 0x19;  /* 3663 (3576 0x18): one state more from 0x0B on */', 1),
    ('inline constexpr int kStateWait = 0x12;', 'inline constexpr int kStateWait = 0x13;  /* 3663 (3576 0x12) */', 1),
    ('inline constexpr int kStateOwn2322 = 0x11;', 'inline constexpr int kStateOwn2322 = 0x12;  /* 3663 (3576 0x11) */', 1)])
sub('guest_x_veto_logic.h', [('constexpr uint8_t kStateInWorld = 0x18;', 'constexpr uint8_t kStateInWorld = 0x19;  /* 3663 (3576 0x18) */', 1)])
sub('guest_actor_diag.cpp', [('    if (state == 0x12) {', '    if (state == 0x13) {  /* 3663 (3576 0x12) */', 1)])
sub('guest.cpp', [('game::safe_read(p + 0xC0, &q, sizeof q)', 'game::safe_read(p + 0x1B0 /* 3663 (3576 0xC0) */, &q, sizeof q)', 1),
                  ('game::safe_read(p + 0x15C, &state, sizeof state)', 'game::safe_read(p + 0x24C /* 3663 (3576 0x15C) */, &state, sizeof state)', 2)])
sub('guest_stage_diag.cpp', [('kP_State = 0x15C;', 'kP_State = 0x24C;  /* 3663 (3576 0x15C) */', 1)])
sub('p8_probes.cpp', [('constexpr uint32_t kP_State = 0x15C, kP_Flag162 = 0x162, kP_Flag163 = 0x163, kP_Pos = 0x150;',
                       'constexpr uint32_t kP_State = 0x24C, kP_Flag162 = 0x251, kP_Flag163 = 0x252, kP_Pos = 0x240;  /* 3663: P grew by 0xF0 '
                       '(3576 0x15C, 0x162, 0x163, 0x150); the two flags by their neighbours, not confirmed (a probe) */', 1)])
sub('docked_gimmicks.cpp', [
    ('        user2_stages_npc_control(rax, ctx.rsi, ctx.r13, ctx.r14);',
     '        user2_stages_npc_control(rax, ctx.rsi, ctx.rdi /* 3663: the field is in rdi (3576: r13) */, ctx.r14);', 1),
    ('    if (const auto o = game::fast<uint32_t>(ctx.r13 + dgl::kField_Owner)) {',
     '    if (const auto o = game::fast<uint32_t>(ctx.rdi + dgl::kField_Owner)) {  // 3663: rdi (3576: r13)', 1)])
sub('user2_stages.cpp', [
    ('constexpr uint32_t kG2_CallerRsi = 0x1D2D0, kG2_Ret = 0x1D2E8, kG2_Field = 0x1D2F8, kG2_RetRva = 0x2B42994;  // 3576',
     'constexpr uint32_t kG2_CallerRsi = 0x1C2F0, kG2_Ret = 0x1C308, kG2_Field = 0x1C318, kG2_RetRva = 0x2B42994;  // 3663: the '
     'frame is sub rsp 0x1C2C8 again (3576 0x1D2A8: 0x1D2D0, 0x1D2E8, 0x1D2F8); the not-found case is mov rbx,r15', 1)])
