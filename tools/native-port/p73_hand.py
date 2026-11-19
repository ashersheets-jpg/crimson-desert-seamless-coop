# Hand decisions for p73.py (3663 -> 3732, 2026-11-19), with the evidence (build/port3732: thunk73.py, hand73.py,
# near73.py, the vtable-neighbour check).
_TROC_BEGIN_NEW = 0x6F1AE10       # p73 judge: 0x6F14E10 -> 0x6F1AE10 (ok); the table has the same 1179 entries
_CLOCK_FLAG_NEW = 0x716616D       # p73 judge: kClientClockGlobal 0x71600AD -> 0x716616D (ok)

# Jump stubs into the protected section (jmp rel32 at the stub): the function map's gap guess, confirmed by every old
# caller's mapped call now targeting it (thunk73.py: "GUESS CONFIRMED", 1 to 40 callers each) ...
_STUBS_BY_CALLERS = {
    0x3D9570: 0x3D9980, 0x3DAE70: 0x3DB280, 0x6F41E0: 0x6F4550, 0xAF4E70: 0xAF5720, 0x1A18320: 0x1A19F90,
    0x2294C50: 0x2296870, 0x2313BE0: 0x2315860, 0x2470330: 0x2472600, 0x25BB470: 0x25BD900, 0x2A0F2A0: 0x2A11A20,
    0x2B3E530: 0x2B40C80, 0x2B3FF00: 0x2B42650, 0x2E7E1F0: 0x2E82950, 0xBDB280: 0xBDBCD0,
}
# ... or, with no direct caller, by the one vtable slot pointing at each in both builds (7-8 neighbouring slots map
# the same way; one table 5 of 7).
_STUBS_BY_VTABLE = {0xB84E80: 0xB857C0, 0x2C160B0: 0x2C1AB40, 0x971460: 0x971B80, 0xF56140: 0xF568B0,
                    0x25348D0: 0x2536D60}

OVERRIDES = {
    'native': {**_STUBS_BY_CALLERS, **_STUBS_BY_VTABLE},
    'layer': {**_STUBS_BY_CALLERS, **_STUBS_BY_VTABLE},
}
OVERRIDES['native'].update({
    0x6F487B8: _TROC_BEGIN_NEW + (0x6F487B8 - 0x6F14E10),   # kTrocInstancesEnd = begin + the same size
    # switch jump tables inside a function of the same size (0x9A5310 -> 0x9A5B90, 240 B; 0x19979EC's +0x1E90):
    # the six entries read 0x9A5C30.. / 0x1999864.. (each old entry + the function's shift)
    0x9A53E4: 0x9A5C64, 0x19979EC: 0x199987C,
    0x22EE49E: 0x22F00BE,    # horse_diag (a diagnostic): fn 0x22EE3E0 -> 0x22F0000, 449 B, identical listing (ratio 1.0)
})
OVERRIDES['layer'].update({
    0x6F487B8: _TROC_BEGIN_NEW + (0x6F487B8 - 0x6F14E10),
    0x71600E1: _CLOCK_FLAG_NEW + 0x34, 0x71600E9: _CLOCK_FLAG_NEW + 0x3C,   # "= flag + 0x34 / + 0x3C"
    0x100004: 0x1001F4,      # puppet_visibility: fn 0xFFF80 -> 0x100170 (244 B, identical listing), same offset
    0x5A4760: 0x5A4AA0,      # remote_puppet (off): fn 0x5A46B0 -> 0x5A49F0 (2028 B, ratio 0.983), same offset
})
SKIP = {
    'native': {0x7120000},                    # trade_probe: a scan range for the decomp, not an address in use
    'layer': {0xC0C0A7, 0x6000000,            # a dwExtraInfo key; troc_sniffer's size bound
              0x5725E48, 0x6D69190},          # remote_puppet (3446-era, off): no reference in either build
}
