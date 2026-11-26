# Port 3732 -> 3840 (2026-11-26): the native unit tests that still failed after tests84.py + testfix84.py (the first
# build): captured packets that carry 3732's message numbers, the save paths' protected return addresses (pins84.py
# moved them in src; tests84 maps only the auto map's literals), the RefreshSlots thunk's target, kCodeEnd's test
# address, and one line op84.py renumbered although it holds 2976 numbers (sched_load's capture translation). ONE SHOT.
import pathlib

T = pathlib.Path('C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/cdmp-native/tests')


def sub(name, pairs):
    p = T / name
    s = p.read_bytes().decode('utf-8')
    for a, b, n in pairs:
        assert s.count(a) == n, (name, a, s.count(a))
        s = s.replace(a, b)
    p.write_bytes(s.encode('utf-8'))
    print('  ', name)


sub('optd_guest_save_tests.cpp', [
    ("// 3732's (guest_save_logic.h: kCallSites + 5)", "// 3840's (guest_save_logic.h: kCallSites + 5; pins84.py)", 1),
    ('gsv::load_caller(0xE274364)', 'gsv::load_caller(0xE2874B5)', 1),        # 3732 0xE274364
    ('gsv::save_caller(0xB6A4AA5)', 'gsv::save_caller(0xB7D9D98)', 1),        # 3732 0xB6A4AA5
    ('gsv::save_caller(0xE214B65)', 'gsv::save_caller(0xE23E2F5)', 1),        # 3732 0xE214B65
    ('gsv::flush_caller_name(0xE584ACD)', 'gsv::flush_caller_name(0xE5A257D)', 1),  # 3732 0xE584ACD
])
sub('optd_slot_refresh_tests.cpp', [
    ('== 0xB6AAD70);  // 3732 (3663: 0xBB0CBC0; 3576: 0xB8AB410)',
     '== 0xB7DB640);  // 3840 (3732: 0xB6AAD70; 3663: 0xBB0CBC0; 3576: 0xB8AB410)', 1),
])
sub('optd_remote_motion_tests.cpp', [
    ('CHECK(rmo::check_data_byte(secs, 0x5620000) == rmo::ByteSite::NotWritable);  // 3663: kCodeEnd is 0x5613000',
     'CHECK(rmo::check_data_byte(secs, 0x5800000) == rmo::ByteSite::NotWritable);  // 3840: kCodeEnd is 0x57A5000 (3663: 0x5613000)', 1),
])
sub('optd_clock_push_tests.cpp', [
    ('0x49, 0x0C, 0xFF, 0x23, 0x00,', '0x5E, 0x08, 0xFF, 0x23, 0x00,', 1),
    ('// header: opcode 3145 (2976: 2816)', '// header: opcode 2142 (3732: 3145; 2976: 2816)', 1),
])
sub('optd_docked_gimmicks_tests.cpp', [
    ('CHECK(dgl::withhold_index(packet(2556, upd), keys) == -1);  // MoveActorReq: never withheld',
     'CHECK(dgl::withhold_index(packet(2561, upd), keys) == -1);  // MoveActorReq (3840 2561; 3732 2790): never withheld', 1),
    ('CHECK(dgl::withhold_index(packet(3067, upd), keys) == -1);  // RegionInOutReq',
     'CHECK(dgl::withhold_index(packet(2222, upd), keys) == -1);  // RegionInOutReq (3840 2222; 3732 3140)', 1),
    ('CHECK(dgl::withhold_index(packet(2556, req), keys) == -1);',
     'CHECK(dgl::withhold_index(packet(2561, req), keys) == -1);  // MoveActorReq', 1),
])
sub('optd_sched_load_tests.cpp', [
    ('op == 2552 ? gsl::kOpScheduleCreate : op == 2446 ? gsl::kOpInitSchedule : op;',
     'op == 2335 ? gsl::kOpScheduleCreate : op == 2874 ? gsl::kOpInitSchedule : op;  // 2976 numbers (op84 must not renumber them)', 1),
])
sub('optd_user2_shops_tests.cpp', [
    ('0xF5, 0x03, 0xFF, 0x05, 0, 0, 0, 0x54, 0x0A,', '0xF5, 0x03, 0xFF, 0x05, 0, 0, 0, 0xA6, 0x0A,', 1),  # 2726 (3732 2644)
])
sub('optd_scene_level_share_tests.cpp', [
    ('"a00a0008', '"23090008', 2),   # kOpLoad 2339 (3732 2720)
    ('"840b000a', '"b908000a', 2),   # kOpUnload 2233 (3732 2948)
    ('"3f0a0059', '"3b090059', 1),   # kOpResync 2363 (3732 2623)
    ("// 3732's (the logic's kOpLoad 2720 = a00a, kOpUnload 2948 = 840b, kOpResync 2623 = 3f0a; 2976: 2598 / 2834 / 2104),",
     "// 3732's (the logic's kOpLoad 2720 = a00a, kOpUnload 2948 = 840b, kOpResync 2623 = 3f0a; 2976: 2598 / 2834 / 2104),\r\n"
     "// 2026-11-26 to 3840's (kOpLoad 2339 = 2309, kOpUnload 2233 = b908, kOpResync 2363 = 3b09),", 1),
])
print('test fixes b: done')
