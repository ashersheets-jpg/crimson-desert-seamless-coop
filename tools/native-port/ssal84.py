# Port 3732 -> 3840 (2026-11-26, found by pp66-fresh: "NpcActivation: sondas entrada accion=false H1b=false" -> the
# containment cannot act and U-e stops giving new NPCs to user 2; on 3732 all three probes were true, pp65b).
# ServerStartActionLogic (3732 0x2D73B60, 2976 0x2A78CA0) became slot 4 of ServerActorMoveSyncProcessor's vtable
# (RTTI .?AVServerActorMoveSyncProcessor@gameServerScript@pa@@, vtable 0x61099C8): 3840 0x30A69C0. StartActionReq's vf2
# (0x2D83BE0) now calls it through [[comps+0x1A0]+0x510] (call [rax+0x20]) with `this` = the processor, the other five
# arguments as before (rdx &res, r8 layers, r9 context, [rsp+0x20] block B, [rsp+0x28] extra). The body is the same
# with other registers: this rbx (3732 the control in r14), actor rsi (rbx), res r14 (rsi), context r12 (r13), layers
# rdi (r12), block B r13 (r15), and the character control r15 = [[[this+8]+0x68]+0x40] (0x30A6A2B..33); the
# OnServer out flag [rbp+0x108] (3732 [rbp+0xF0]). build/port3840/hand/ssal84*.py, ssal84f.txt. ONE SHOT.
import pathlib

S = pathlib.Path('C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/cdmp-native/src/mod/session2')


def sub(name, pairs):
    # Line ends as the file has them at that place (host_scene_diag.cpp mixes LF and CRLF lines); a pair already
    # applied (its new text there, the old gone) is skipped, so a run stopped half way can be run again.
    import re
    p = S / name
    s = p.read_bytes().decode('utf-8')

    def rx(t):
        return re.compile('\r?\n'.join(re.escape(x) for x in t.split('\n')))

    for a, b in pairs:
        found = list(rx(a).finditer(s))
        if len(found) == 1:
            m = found[0]
            nl = '\r\n' if '\r\n' in m.group(0) else '\n'
            s = s[:m.start()] + b.replace('\n', nl) + s[m.end():]
        elif not found and len(list(rx(b).finditer(s))) == 1:
            pass  # applied already
        else:
            raise AssertionError((name, a[:80], len(found)))
    p.write_bytes(s.encode('utf-8'))
    print('  ', name)


sub('npc_activation_logic.h', [
    ('template <class Read> ActionEntry read_action_key(Read&& read, uintptr_t cc, const ActionOffsets& o = {}) noexcept {',
     '// 1.0.0.3840: the logic is slot 4 of ServerActorMoveSyncProcessor (this = the processor, its actor at +8); the\n'
     '// character control the readers want is [[actor+0x68]+0x40], what the game loads into r15 (0x30A6A2B..0x30A6A33).\n'
     '// 3732 and before: the first argument was the control itself.\n'
     'template <class Read> uintptr_t cc_of_processor(Read&& read, uintptr_t processor) noexcept {\n'
     '    const uintptr_t actor = read_ptr_with(read, processor + 0x08);\n'
     '    const uintptr_t comps = actor ? read_ptr_with(read, actor + 0x68) : 0;\n'
     '    return comps ? read_ptr_with(read, comps + 0x40) : 0;\n'
     '}\n'
     'template <class Read> ActionEntry read_action_key(Read&& read, uintptr_t cc, const ActionOffsets& o = {}) noexcept {'),
])
sub('npc_action_probe.cpp', [
    ('constexpr uint8_t kActionBytes[] = {0x48,0x89,0x5C,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,\n'
     '  0x48,0x8D,0xAC,0x24,0x50,0xFF,0xFF,0xFF,0x48,0x81,0xEC,0xB0,0x01,0x00,0x00,0x4D,0x8B,0xE9,0x4D,0x8B,0xE0,0x48,0x8B,0xF2};',
     '// 1.0.0.3840: 0x30A69C0 (ServerActorMoveSyncProcessor slot 4): mov [rsp+0x10],rbx ; mov [rsp+8],rcx ; 7 pushes ;\n'
     '// lea rbp,[rsp-0xB0] ; sub rsp,0x1B0 ; mov r12,r9 ; mov rdi,r8 ; mov r14,rdx (3732: no rcx home, r13/r12/rsi).\n'
     'constexpr uint8_t kActionBytes[] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x4C,0x24,0x08,0x55,0x56,0x57,0x41,0x54,0x41,\n'
     '  0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24,0x50,0xFF,0xFF,0xFF,0x48,0x81,0xEC,0xB0,0x01,0x00,0x00,0x4D,0x8B,0xE1,\n'
     '  0x49,0x8B,0xF8,0x4C,0x8B,0xF2};'),
    ('int* __fastcall action_detour(uintptr_t cc, int* result, uintptr_t layers, uintptr_t context,\n'
     '                             uintptr_t block, uintptr_t extra) noexcept {',
     'int* __fastcall action_detour(uintptr_t processor, int* result, uintptr_t layers, uintptr_t context,\n'
     '                             uintptr_t block, uintptr_t extra) noexcept {'),
    ('    const FastRead rd;\n    npcact::ActionEntry entry = npcact::read_action_key(rd, cc);',
     '    const FastRead rd;\n'
     '    // 1.0.0.3840: `this` is the actor\'s ServerActorMoveSyncProcessor; the readers (and H1b\'s mark) use the control.\n'
     '    const uintptr_t cc = npcact::cc_of_processor(rd, processor);\n'
     '    npcact::ActionEntry entry = npcact::read_action_key(rd, cc);'),
    ('    int* returned = call_original(&current, cc, result, layers, context, block, extra);',
     '    int* returned = call_original(&current, processor, result, layers, context, block, extra);'),
    ('int* call_original(npcact::PendingCall* current, uintptr_t cc, int* result, uintptr_t layers, uintptr_t context,\n'
     '                   uintptr_t block, uintptr_t extra) noexcept {',
     'int* call_original(npcact::PendingCall* current, uintptr_t processor, int* result, uintptr_t layers, uintptr_t context,\n'
     '                   uintptr_t block, uintptr_t extra) noexcept {'),
    ('        returned = g_action.unsafe_call<int*>(cc, result, layers, context, block, extra);',
     '        returned = g_action.unsafe_call<int*>(processor, result, layers, context, block, extra);'),
    ('    const bool action=install(base,sections,0x2D73B60,  // 3576 (2976 0x2A78CA0), same head',
     '    const bool action=install(base,sections,0x30A69C0,  // 3840: ServerActorMoveSyncProcessor slot 4 (3732 0x2D73B60; 2976 0x2A78CA0)'),
])
sub('host_scene_diag.cpp', [
    ('constexpr uintptr_t kH1 = 0x2D73BCD;\nconstexpr uint32_t kH1Patch = 8;\n'
     'constexpr uint8_t kH1Bytes[] = {0x41, 0x80, 0xBF, 0x79, 0x02, 0x00, 0x00, 0x00, 0x74, 0x11, 0x49, 0x8B, 0xCE, 0xE8, 0x51, 0x1A};',
     '// 1.0.0.3840: the logic is ServerActorMoveSyncProcessor slot 4, 0x30A69C0 (npc_action_probe.cpp): the same frame and\n'
     '// arguments in other registers (this rbx, actor rsi, res r14, context r12, layers rdi, block B r13, the control r15\n'
     '// = [[[this+8]+0x68]+0x40]); the OnServer out flag [rbp+0x108] (3732: r14 the control, rbx, rsi, r13, r12, r15, +0xF0).\n'
     'constexpr uintptr_t kH1 = 0x30A6A3E;  // 3840: cmp byte [r13+0x279],0 ; je (3732 0x2D73BCD, [r15+0x279])\n'
     'constexpr uint32_t kH1Patch = 8;\n'
     'constexpr uint8_t kH1Bytes[] = {0x41, 0x80, 0xBD, 0x79, 0x02, 0x00, 0x00, 0x00, 0x74, 0x12, 0x49, 0x8B, 0xCF, 0xE8, 0xA0, 0x0D};'),
    ('constexpr uintptr_t kH1Frame = 0x2D73B70;', 'constexpr uintptr_t kH1Frame = 0x30A69D5;  // 3840 (3732 0x2D73B70)'),
    ('constexpr uintptr_t kH1Args = 0x2D73B7F;\n'
     'constexpr uint8_t kH1ArgsBytes[] = {0x4D, 0x8B, 0xE9, 0x4D, 0x8B, 0xE0, 0x48, 0x8B, 0xF2, 0x4C, 0x8B, 0xF1};',
     '// 3840: mov r12,r9 (context) ; mov rdi,r8 (layers) ; mov r14,rdx (res) ; mov rbx,rcx (this) (3732 0x2D73B7F).\n'
     'constexpr uintptr_t kH1Args = 0x30A69E4;\n'
     'constexpr uint8_t kH1ArgsBytes[] = {0x4D, 0x8B, 0xE1, 0x49, 0x8B, 0xF8, 0x4C, 0x8B, 0xF2, 0x48, 0x8B, 0xD9};'),
    ('constexpr uintptr_t kH1Actor = 0x2D73B92;  // 3576: after the block load\nconstexpr uint8_t kH1ActorBytes[] = {0x48, 0x8B, 0x59, 0x08};',
     'constexpr uintptr_t kH1Actor = 0x30A69F0;  // 3840: mov rsi,[rcx+8] (3732 0x2D73B92 mov rbx,[rcx+8]; 3576: after the block load)\n'
     'constexpr uint8_t kH1ActorBytes[] = {0x48, 0x8B, 0x71, 0x08};'),
    ('constexpr uintptr_t kH1B = 0x2D73B8B;  // 3576: before the actor load\nconstexpr uint8_t kH1BBytes[] = {0x4C, 0x8B, 0xBD, 0x10, 0x01, 0x00, 0x00};',
     'constexpr uintptr_t kH1B = 0x30A6A37;  // 3840: mov r13,[rbp+0x110], after the control load (3732 0x2D73B8B mov r15,..)\n'
     'constexpr uint8_t kH1BBytes[] = {0x4C, 0x8B, 0xAD, 0x10, 0x01, 0x00, 0x00};'),
    ('constexpr uintptr_t kH1Local = 0x2D73BD5;\nconstexpr uint8_t kH1LocalBytes[] = {0x74, 0x11, 0x49, 0x8B, 0xCE, 0xE8, 0x51, 0x1A, 0x82, 0xFF,\n'
     '                                     0x33, 0xFF, 0x89, 0x3E, 0xE9, 0x70, 0x03, 0x00, 0x00};',
     '// 3840: je ; mov rcx,r15 ; call 0x26777F0 ; xor esi,esi ; mov [r14],esi ; jmp (3732 0x2D73BD5, r14 / [rsi]).\n'
     'constexpr uintptr_t kH1Local = 0x30A6A46;\n'
     'constexpr uint8_t kH1LocalBytes[] = {0x74, 0x12, 0x49, 0x8B, 0xCF, 0xE8, 0xA0, 0x0D, 0x5D, 0xFF,\n'
     '                                     0x33, 0xF6, 0x41, 0x89, 0x36, 0xE9, 0x17, 0x04, 0x00, 0x00};'),
    ('constexpr uintptr_t kH1b = 0x2D73F20;\nconstexpr uint32_t kH1bPatch = 7;\n'
     'constexpr uint8_t kH1bBytes[] = {0x80, 0xBD, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x75, 0x19, 0xC6, 0x44, 0x24, 0x20, 0x00, 0x4D, 0x8B};',
     'constexpr uintptr_t kH1b = 0x30A6E37;  // 3840: cmp byte [rbp+0x108],0 ; jne (3732 0x2D73F20, [rbp+0xF0])\n'
     'constexpr uint32_t kH1bPatch = 7;\n'
     'constexpr uint8_t kH1bBytes[] = {0x80, 0xBD, 0x08, 0x01, 0x00, 0x00, 0x00, 0x75, 0x19, 0xC6, 0x44, 0x24, 0x20, 0x00, 0x4D, 0x8B};'),
    ('constexpr uintptr_t kH1bOut = 0x2D73EB8;\nconstexpr uint8_t kH1bOutBytes[] = {0x48, 0x8D, 0x85, 0xF0, 0x00, 0x00, 0x00};',
     'constexpr uintptr_t kH1bOut = 0x30A6CC8;  // 3840: lea rax,[rbp+0x108] (3732 0x2D73EB8, +0xF0)\n'
     'constexpr uint8_t kH1bOutBytes[] = {0x48, 0x8D, 0x85, 0x08, 0x01, 0x00, 0x00};'),
    ('constexpr uintptr_t kH1bCall = 0x2D73F2E;\nconstexpr uint8_t kH1bCallBytes[] = {0x4D, 0x8B, 0xCF, 0x4C, 0x8D, 0x44, 0x24, 0x60, 0x48, 0x8D,\n'
     '                                     0x55, 0xC0, 0x49, 0x8B, 0xCE, 0xE8, 0x3E, 0x07, 0x00, 0x00};',
     '// 3840: mov r9,r13 ; lea r8,[rsp+0x60] ; lea rdx,[rbp-0x40] ; mov rcx,r15 ; call 0x2EB1AF0 (3732 0x2D73F2E, r15 / r14).\n'
     'constexpr uintptr_t kH1bCall = 0x30A6E45;\n'
     'constexpr uint8_t kH1bCallBytes[] = {0x4D, 0x8B, 0xCD, 0x4C, 0x8D, 0x44, 0x24, 0x60, 0x48, 0x8D,\n'
     '                                     0x55, 0xC0, 0x49, 0x8B, 0xCF, 0xE8, 0x97, 0xAC, 0xE0, 0xFF};'),
    ('    const uintptr_t actor = ctx.rbx, b = ctx.r15, layers = rdp(ctx.r12);',
     '    const uintptr_t actor = ctx.rsi, b = ctx.r13, layers = rdp(ctx.rdi);  // 3840 (3732: rbx, r15, r12)'),
    ('    t_h1 = PendingH1{ctx.r14, key, hash};', '    t_h1 = PendingH1{ctx.r15, key, hash};  // 3840: the control in r15 (3732: r14)'),
    ('    e.v[5] = ctx.r13;', '    e.v[5] = ctx.r12;  // 3840: the context (3732: r13)'),
    ('    npc_action_probe_applied(ctx.r14, key_of(rdp(ctx.r14 + kComp_Actor)));',
     '    npc_action_probe_applied(ctx.r15, key_of(rdp(ctx.r15 + kComp_Actor)));  // 3840: the control in r15 (3732: r14)'),
    ('    const uintptr_t comp = ctx.r14, actor = rdp(comp + kComp_Actor);\n    const uint32_t key = key_of(actor);\n'
     '    uint8_t out = 0;\n    const bool out_ok = rd(ctx.rbp + 0xF0, out);',
     '    const uintptr_t comp = ctx.r15, actor = rdp(comp + kComp_Actor);  // 3840 (3732: r14)\n    const uint32_t key = key_of(actor);\n'
     '    uint8_t out = 0;\n    const bool out_ok = rd(ctx.rbp + 0x108, out);  // 3840 (3732: +0xF0)'),
])
print('npc action probe / H1: done')
