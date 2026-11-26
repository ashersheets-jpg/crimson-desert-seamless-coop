# Hand decisions for p84.py (3732 -> 3840), with the evidence in a comment.
# SKIP: numbers that look like addresses but are not (port-3732.md: "the mistake that mattered"): left as they are.
_NOT_ADDRESSES = {
    0xF42BE, 0xF42CD,      # ai_handoff kCatchInteractions: interaction keys
    0xF4321,               # trade_probe: the shop interaction key
    0xF4440,               # shop_twin_logic kShopDialogGroup: a dialog group key
    0xF4240,               # the HP stat id (layer: enemy scaling, hits, capture)
    0xFFFFFF,              # masks (Steam app id, photo mode)
    0x1000000,             # fake id ranges
    0x10F000,              # load_trace
    0x100004,              # puppet_visibility kHideBit (a bit mask)
    0xC0C0A7,              # a dwExtraInfo key
    0x6000000,             # troc_sniffer's size bound
    0x7120000,             # trade_probe: a scan range for the decomp, not in use
    # found by the hand tables (2026-11-26): the auto map rewrote these, the helpers revert them
    0x2065780,             # guest_s3.cpp:1203: the mod DLL's span (G3)
    0xCB0A77,              # layer offsets_3840.h kPhotoModeId: the low bits of a float, not an address (G4)
    0x4A00000,             # layer main_menu.cpp: a code-range bound for a [dev] log (G4)
}
OVERRIDES = {'native': {}, 'layer': {}}
SKIP = {'native': set(_NOT_ADDRESSES), 'layer': set(_NOT_ADDRESSES)}


# The hand tables (build/port3840/hand/G1..G4.md, evidence there): every address a helper changed in the source,
# "0xOLD 0xNEW" per line in G*_pairs.txt (G1-G3 native, G4 layer). A clash (one old address, two new) stops here.
def _load_pairs():
    import glob, os, re
    hand = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'build', 'port3840', 'hand')
    for path in sorted(glob.glob(os.path.join(hand, 'G[1-4]_pairs.txt'))):
        tree = 'layer' if os.path.basename(path).startswith('G4') else 'native'
        for line in open(path, encoding='utf-8'):
            m = re.match(r'\s*0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)', line)
            if not m:
                continue
            old, new = int(m.group(1), 16), int(m.group(2), 16)
            have = OVERRIDES[tree].get(old)
            assert have in (None, new), (path, hex(old), hex(have), hex(new))
            OVERRIDES[tree][old] = new


_load_pairs()
