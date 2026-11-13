# Port 3576 -> 3663: four native tests that pin what the port changed on purpose (2026-11-13). ONE SHOT.
import re
ROOT = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/cdmp-native/tests/'


def rw(p, fn):
    s = open(ROOT + p, encoding='utf-8', errors='surrogateescape', newline='').read()
    t = fn(s)
    assert t != s, p
    open(ROOT + p, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(t)
    print('  ', p)


def one(old, new):
    def f(s):
        assert s.count(old) == 1, (old, s.count(old))
        return s.replace(old, new)
    return f


rw('optd_guest_actor_tests.cpp', one(
    'CHECK(is_denuvo_name(".tls$") && is_denuvo_name(".xpdata") && is_denuvo_name(".xtls") && !is_denuvo_name(".rsrc"));',
    '// 3663: the protection is ".data1"; ".tls$" is a small data section there, so the old names are no longer taken.\n'
    '    CHECK(is_denuvo_name(".data1") && !is_denuvo_name(".tls$") && !is_denuvo_name(".rsrc"));'))
rw('optd_remote_motion_tests.cpp', one(
    'CHECK(rmo::check_data_byte(secs, 0x5600000) == rmo::ByteSite::NotWritable);',
    'CHECK(rmo::check_data_byte(secs, 0x5620000) == rmo::ByteSite::NotWritable);  // 3663: kCodeEnd is 0x5613000'))
rw('optd_slot_refresh_tests.cpp', one(
    'CHECK(kSaveMgrRefreshSlots + 5 + static_cast<int64_t>(j) == 0xB8AB410);',
    'CHECK(kSaveMgrRefreshSlots + 5 + static_cast<int64_t>(j) == 0xBB0CBC0);  // 3663 (3576: 0xB8AB410)'))

# the load machine's states: one more from 0x0B on (0x11 -> 0x12, 0x12 -> 0x13, 0x13 -> 0x14, 0x17 -> 0x18, 0x18 -> 0x19)
MAP = {'0x11': '0x12', '0x12': '0x13', '0x13': '0x14', '0x14': '0x15', '0x15': '0x16', '0x16': '0x17', '0x17': '0x18', '0x18': '0x19',
       '0x0B': '0x0C', '0x0C': '0x0D', '0x0D': '0x0E', '0x0E': '0x0F', '0x0F': '0x10', '0x10': '0x11'}


def states(s):
    def call(m):
        args = m.group(2).split(',')
        for k in (0, 1):
            a = args[k].strip()
            if a in MAP:
                args[k] = args[k].replace(a, MAP[a])
        return m.group(1) + ','.join(args) + ')'
    return re.sub(r'(on_state\()([^()]*)\)', call, s)


rw('optd_reload_wait_tests.cpp', states)
