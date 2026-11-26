# After op84.py --apply (2026-11-26, 3732 -> 3840): the lines the tool could not rewrite as a whole.
import sys

N = 'C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/cdmp-native/'


def fix(path, pairs):
    s = open(path, 'rb').read().decode('utf-8')
    for a, b in pairs:
        if b in s and a not in s:
            print('already:', path.rsplit('/', 1)[-1], b[:60])
            continue
        if s.count(a) != 1:
            sys.exit('%d x %r in %s' % (s.count(a), a, path))
        s = s.replace(a, b)
    open(path, 'wb').write(s.encode('utf-8'))
    print('fixed:', path.rsplit('/', 1)[-1])


# SetMissionNewAlarmFalseAck (3732 2334) is spelled SetMissionNewAlramFalseAck in 3840 (2778)
fix(N + 'src/mod/session2/owner_fanout_writers.h',
    [('{0x2A45AF2, 0x2A45A90, 2334, 1, "SetMissionNewAlarmFalseAck"},',
      '{0x2A45AF2, 0x2A45A90, 2778, 1, "SetMissionNewAlarmFalseAck"},  // 3840: "SetMissionNewAlramFalseAck" (3732 2334)')])
# UpdateStageChartEventAck 2105 -> 2308 on the lines whose 1500 / 3000 / 5000 are times
fix(N + 'tests/optd_shop_twin_tests.cpp',
    [('stl::chart_ack_to_twin(2105, other, pending, 1500, 3000)', 'stl::chart_ack_to_twin(2308, other, pending, 1500, 3000)'),
     ('stl::chart_ack_to_twin(2105, cack, pending, 5000, 3000)', 'stl::chart_ack_to_twin(2308, cack, pending, 5000, 3000)'),
     ('stl::chart_ack_to_twin(2105, cack, pending, 1500, 3000) == aid', 'stl::chart_ack_to_twin(2308, cack, pending, 1500, 3000) == aid'),
     ('stl::chart_ack_to_twin(2105, cack2, pending, 1500, 3000)', 'stl::chart_ack_to_twin(2308, cack2, pending, 1500, 3000)')])
