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
}
OVERRIDES = {'native': {}, 'layer': {}}
SKIP = {'native': set(_NOT_ADDRESSES), 'layer': set(_NOT_ADDRESSES)}
