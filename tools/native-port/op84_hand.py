# Hand decisions for op84.py (3732 -> 3840): 'tree:file:line' -> class ('syntax' = rewrite, 'time' = leave).
# Read in the dry run of 2026-11-26 (build/native-port/op84_*.txt). Everything not listed keeps the tool's class:
# review / weak / time lines are timings, buffer sizes, ring sizes, thresholds, or 2976 numbers used as log labels
# (owner_fanout_writers.h's table, mount_sync's g_h_2663 / g_h_2916, observer_seq_copy's 2661 / 2810, user2_shops'
# 2671), all left.
FORCE = {
    # the TEST stall's default opcode: MoveFieldCompleteReq (3732 2723 -> 3840 2156)
    'native-src:mod/config.cpp:243': 'syntax',
    'native-src:mod/config.h:348': 'syntax',
    # user2_funnels_branch_probe_in(uint16_t opcode): StageBranchStateReq
    'native-src:mod/session2/server.cpp:497': 'syntax',
    # g_req_ops is indexed by opcode: the three interaction Reqs' counts
    'native-src:mod/session2/user2_funnels.cpp:1916': 'syntax',
    # the trade request (MoveItemInventoryToOtherInventoryReq) the mod injects for its server-thread checks
    'native-src:mod/net/troc_sniffer.cpp:344': 'syntax',
    'native-src:mod/net/troc_sniffer.cpp:345': 'syntax',
    'native-src:mod/net/troc_sniffer.cpp:348': 'syntax',
    # the layer's dev scripts: LootingDropFromDeadBodyReq and the trade request
    'layer-src:mod/net/dev_script.cpp:150': 'syntax',
    'layer-src:mod/net/dev_script.cpp:552': 'syntax',
    # "time" by the tool (the word len / a size on the line), opcodes all the same: piece4_test's VaryStatByAccInputReq
    # and AttackByCollisionReq, scene_req_guard's table (trigger volume, sequencer delete, chart delayed event), the
    # layer's quest_advance MissionTriggerInOutReq
    'native-src:mod/net/piece4_test.cpp:146': 'syntax',
    'native-src:mod/net/piece4_test.cpp:164': 'syntax',
    'native-src:mod/session2/scene_req_guard.cpp:53': 'syntax',
    'native-src:mod/session2/scene_req_guard.cpp:54': 'syntax',
    'native-src:mod/session2/scene_req_guard.cpp:55': 'syntax',
    'layer-src:mod/net/quest_advance.cpp:282': 'syntax',
    'layer-src:mod/net/quest_advance.cpp:310': 'syntax',
    # native tests: opcodes passed to the logic whose kOp constants this port rewrites (every number on these lines is
    # an opcode). NOT here (by hand after apply): optd_shop_twin_tests.cpp:260-262 / :265, whose 1500 / 3000 / 5000
    # are times on the same line as the opcode 2105.
    'native-tests:optd_character_hold_tests.cpp:975': 'syntax',
    'native-tests:optd_pets_tests.cpp:736': 'syntax',
    'native-tests:optd_pets_tests.cpp:747': 'syntax',
    'native-tests:optd_pets_tests.cpp:759': 'syntax',
    'native-tests:optd_shop_twin_tests.cpp:249': 'syntax',
    'native-tests:optd_shop_twin_tests.cpp:250': 'syntax',
    'native-tests:optd_shop_twin_tests.cpp:252': 'syntax',
    'native-tests:optd_shop_twin_tests.cpp:268': 'syntax',
    'native-tests:optd_user2_shops_tests.cpp:152': 'syntax',
    'native-tests:optd_user2_shops_tests.cpp:153': 'syntax',
    'native-tests:optd_world_anchor_tests.cpp:96': 'syntax',
    'native-tests:optd_door_trace_tests.cpp:88': 'syntax',
    'native-tests:optd_door_trace_tests.cpp:90': 'syntax',
    'native-tests:optd_npc_probe_client_tests.cpp:38': 'syntax',
    'native-tests:optd_npc_probe_client_tests.cpp:39': 'syntax',
    'native-tests:optd_npc_probe_client_tests.cpp:40': 'syntax',
    'native-tests:optd_npc_probe_client_tests.cpp:41': 'syntax',
    'native-tests:optd_npc_probe_client_tests.cpp:43': 'syntax',
    'native-tests:optd_npc_probe_client_tests.cpp:44': 'syntax',
    'native-tests:optd_owner_fanout_tests.cpp:325': 'syntax',
    'native-tests:optd_owner_fanout_tests.cpp:326': 'syntax',
    'native-tests:optd_owner_fanout_tests.cpp:331': 'syntax',
    'native-tests:optd_owner_fanout_tests.cpp:332': 'syntax',
    'native-tests:optd_owner_fanout_tests.cpp:333': 'syntax',
    'native-tests:optd_owner_fanout_tests.cpp:372': 'syntax',
    'native-tests:optd_owner_fanout_tests.cpp:373': 'syntax',
}
