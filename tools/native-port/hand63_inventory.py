# Port 3576 -> 3663 by hand (2026-11-13, found by the checklist's chest step): the static inventory manager.
#   info ([[slot]] + 8) holds it at +0x220 (3576: +0x240); its named inventory indices moved by -0x28:
#   the list +0x78.. (3576 +0xA0..), "Character" (the bag) +0x7A (+0xA2), "WareHouse" +0x8A (+0xB2), the next +0x8C (+0xB4).
#   Evidence: build/port3663/pairdiff.py 2D45EE0 (the game's own reads: [rdx+0x220], word [rax+0x7a] / [rax+0x8a]).
# ONE SHOT (asserts the old text is there exactly as often as expected).
ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/'


def rw(p, pairs):
    s = open(ROOT + p, encoding='utf-8', errors='surrogateescape', newline='').read()
    for old, new, n in pairs:
        assert s.count(old) == n, (p, old, s.count(old))
        s = s.replace(old, new)
    open(ROOT + p, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(s)
    print('  ', p)


rw('cdmp-native/src/mod/session2/lootr.cpp', [
    ('*reinterpret_cast<uintptr_t*>(info + 0x240);', '*reinterpret_cast<uintptr_t*>(info + 0x220);  // 3663 (3576 +0x240)', 1),
    ('*reinterpret_cast<uint16_t*>(mgr + 0xA2);', '*reinterpret_cast<uint16_t*>(mgr + 0x7A);  // 3663 (3576 +0xA2)', 1),
])
rw('cdmp-native/src/mod/session2/trade_probe.cpp', [
    ('const uintptr_t mgr = *reinterpret_cast<uintptr_t*>(info + 0x240);',
     'const uintptr_t mgr = *reinterpret_cast<uintptr_t*>(info + 0x220);  // 3663 (3576 +0x240)', 1),
    ('return info ? *reinterpret_cast<uintptr_t*>(info + 0x240) : 0;',
     'return info ? *reinterpret_cast<uintptr_t*>(info + 0x220) : 0;  // 3663 (3576 +0x240)', 1),
    ('bag = *reinterpret_cast<uint16_t*>(mgr + 0xA2);  // "Character", the main bag',
     'bag = *reinterpret_cast<uint16_t*>(mgr + 0x7A);  // "Character", the main bag (3663; 3576 +0xA2)', 1),
    ('wh = *reinterpret_cast<uint16_t*>(mgr + 0xB2);   // "WareHouse"',
     'wh = *reinterpret_cast<uint16_t*>(mgr + 0x8A);   // "WareHouse" (3663; 3576 +0xB2)', 1),
    ('game::fast_read(mgr + 0xA0 + i * 2, &idx, 2);', 'game::fast_read(mgr + 0x78 + i * 2, &idx, 2);  // 3663 (3576 +0xA0)', 1),
    ('for (const uintptr_t off : {uintptr_t{0xB4}, uintptr_t{0xB2}}) {',
     'for (const uintptr_t off : {uintptr_t{0x8C}, uintptr_t{0x8A}}) {  // 3663 (3576 +0xB4, +0xB2)', 1),
])
rw('cd-coop-3446/src/mod/net/coop_stash.cpp', [
    ('auto mgr = game::read_ptr(*info + 0x240);', 'auto mgr = game::read_ptr(*info + 0x220);  // 3663 (3576 +0x240)', 1),
    ('return game::safe_read(*mgr + 0xB2, &wh, sizeof wh) && wh != 0xFFFF;',
     'return game::safe_read(*mgr + 0x8A, &wh, sizeof wh) && wh != 0xFFFF;  // 3663 (3576 +0xB2)', 1),
])
