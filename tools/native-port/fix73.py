# Port 3663 -> 3732 (2026-11-19): numbers p73.py apply rewrote although they are NOT game addresses (found by
# build/port3732/review73.py and mapcheck.py). Put back. ONE SHOT (exact counts asserted).
ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/'


def sub(path, pairs):
    s = open(ROOT + path, encoding='utf-8', errors='surrogateescape', newline='').read()
    for old, new, n in pairs:
        assert s.count(old) == n, (path, old, s.count(old))
        s = s.replace(old, new)
    open(ROOT + path, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(s)
    print('  ', path)


N = 'cdmp-native/src/mod/'
L = 'cd-coop-3446/src/mod/'
# interaction keys / stat ids (game data, not code)
sub(N + 'session2/ai_handoff.cpp', [('kCatchInteractions[] = {0xF5DFD, 0xF44BD}', 'kCatchInteractions[] = {0xF42BE, 0xF42CD}', 1)])
sub(N + 'session2/trade_probe.cpp', [('rd<uint32_t>(c.r8) != 0x000F4511', 'rd<uint32_t>(c.r8) != 0x000F4321', 1),
                                     ('at < base + 0x71660D0', 'at < base + 0x7166000', 1),
                                     ('at = base + 0x7120000;', 'at = base + 0x7126000;', 1)])  # error-constant scan range: +0x6000 like the data around it
sub(N + 'session2/shop_twin_logic.h', [('kShopDialogGroup = 0x000F4630', 'kShopDialogGroup = 0x000F4440', 1)])
sub(N + 'session2/guest_s3.cpp', [('at < mod_base + 0x20673A0', 'at < mod_base + 0x2065780', 1)])   # our own module
for f in ('mod.cpp', 'net/guest_hits.cpp', 'net/local_capture.cpp', 'net/loot_twin.cpp', 'net/party_scaling.cpp',
          'net/piece4_test.cpp', 'net/player_hits.cpp'):
    sub(L + f, [('0x000F4430', '0x000F4240', 1)])        # the HP stat id
# masks and sizes
sub(L + 'game/ui_state.cpp', [('(a & 0x100077F)', '(a & 0xFFFFFF)', 1), ('(b & 0x100077F)', '(b & 0xFFFFFF)', 1)])
sub(L + 'net/steam_transport.cpp', [('(info.game_id & 0x100077F)', '(info.game_id & 0xFFFFFF)', 1)])
sub(L + 'net/stage_cinema.cpp', [('kFakeIdBase + 0x1000780', 'kFakeIdBase + 0x1000000', 1)])
sub(L + 'net/weather_sync.cpp', [('kFakeIdBase + 0x1000780', 'kFakeIdBase + 0x1000000', 1)])
sub(L + 'net/load_trace.cpp', [('0x0010F1F0', '0x0010F000', 2), ('g_base + 0x5616C40', 'g_base + 0x5617000', 1)])  # 3732 code size
sub(L + 'game/main_menu.cpp', [('base + 0x4A045A0', 'base + 0x4A00000', 1)])
print('done')
