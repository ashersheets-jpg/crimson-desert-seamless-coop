# Port 3576 -> 3663: the addresses decided by hand (2026-11-13), with the evidence. Read by p63.py.
STUBS = {  # 5-byte jmp stubs into the copy protection (no unwind entry, the jmp target changes every build): placed by
           # BOTH paired neighbour functions agreeing on the spot; a jmp at the old and the new place (look.py stubs)
    0xABF3E0: 0xAF4E70, 0xB4DCF0: 0xB84E80, 0x19B2CC0: 0x1A18320, 0x2225930: 0x2294C50, 0x22A34C0: 0x2313BE0,
    0x23FEA20: 0x2470330, 0x2549C10: 0x25BB470, 0x29ACC70: 0x2A0F2A0, 0x2ADA850: 0x2B3E530, 0x2ADC230: 0x2B3FF00,
    0x943C80: 0x971460, 0xBA3700: 0xBDB280, 0xF1B8F0: 0xF56140, 0x24C2B50: 0x25348D0,
}
OVERRIDES = {
    'native': {
        **STUBS,
        0x193254C: 0x19979EC,   # the is-local function's jump table (function + 0x4C; 0x1932500 -> 0x19979A0)
        0x9769A4: 0x9A53E4,     # the mode function's jump table (function 0x9768D0 -> 0x9A5310, same size, same offset;
                                # its entries 0x976970 -> 0x9A53B0)
        0x23C74BB: 0x2438D53,   # shop_guest: the one place in the image with these 40 bytes (function not paired)
        0x29CCBB1: 0x2A2F43B,   # mov [r14+0x90],eax after the field-info getter call (0x29CCBA6 -> 0x2A2F431 by its call);
                                # THE REGISTER CHANGED: the field is in rdi now (was r13): docked_gimmicks' hook
        0x2AF5570: 0x2B595B0,   # what its one placed caller calls now; the prologue has one more argument store
        0x2DF184F: 0x2E58D68,   # trade_probe: the only `cmp [rdx+0xA0],rax; je; mov edi,[rip]` in the paired function
        0x2E6B370: 0x2ED3360,   # MoveFieldCompleteCore: its paired function (the other whole-image match is a twin)
        0x6EC4378: 0x6F487B8,   # kTrocInstancesEnd = the last instance (0x6F487B0, 1179 messages) + 8
        0x2BB06B0: 0x2C160B0,   # ServerStatusActorComponent vf10 (RTTI: vtable 0x5EE9828 -> 0x5F63430, slot 10)
        # The 127 info-table getters were REWRITTEN in 3663 (found through their callers, unanimous votes): they now
        # take a POINTER to the u16 index in rcx (was: the index in edx) and never return null (a default record).
        0x3DD8A0: 0x3DAF10,     # item info (23 of 23 callers)
        0x3DE240: 0x3DAC70,     # (19 of 19)
        0x555DE0: 0x57B290,     # field info (37 of 37)
        0x5560E0: 0x57B410,     # (25 of 25)
        0x3D5930: 0x3D9570,     # (30 of 30)
        0x3D7E10: 0x3DAE70,     # (33 of 33)
    },
    'layer': {
        **STUBS,
        0x94FB60: 0x97D6D0,     # its paired function (the whole-image match elsewhere is a twin)
        0x6EC4378: 0x6F487B8,   # kTrocInstancesEnd
        0x70DBF61: 0x71600E1,   # kClientFrameClockMs = kClientClockGlobal (0x70DBF2D -> 0x71600AD) + 0x34
        0x70DBF69: 0x71600E9,   # kClientFrameClockUs = + 0x3C
        0x3DD8A0: 0x3DAF10, 0x5560E0: 0x57B410,   # the rewritten getters (see native)
        0x3DD8B5: 0x3DAF18,     # kItemInfoTableLoad: the getter's `mov rcx,[rip+slot]` (was `mov rbx,[rip+slot]` at +0x15)
        0xAC9210: 0xAFEE20,     # coop_give: what its 4 placed callers call now
        0x558E000: 0x5612000,   # load_trace: 'inside the code' bound (the code section ends at 0x5613000)
    },
}
SKIP = {
    'native': {
        0xF4440,                # kShopDialogGroup: a number, not an address
        0xF4321,                # trade_probe: a number
        0x7120000, 0x7160000,   # trade_probe's scan range (a probe; off)
        0x855D34,               # guest_actor_diag (a diagnostic): its function grew by 304 bytes, not found
        0x22EE49E,              # horse_diag (a diagnostic): not found
        0x2E62C28, 0x2E62D11,   # clock_gate's two mid hooks: the field moved from rdi to rsi, ported by hand
    },
    'layer': {
        0xF4240, 0x6000000,     # numbers, not addresses
        0x100004, 0x10F000, 0xC0C0A7, 0xFFFFFF, 0x1000000,   # a flag, a key mask, our own input tag, a mask, a span
        0x4A00000,              # main_menu: a rough upper bound of the code for a stack walk (still inside the code)
        # remote_puppet's rope / tree helpers: 3446-era values never ported, guarded, off
        0x491AC0, 0x50EA00, 0x5A40E0, 0x5A4760, 0x5725E48, 0x6D69190, 0x26D3940, 0x26D4B40,
    },
}
