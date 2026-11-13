# Decisions made by hand after reading op63.py's dry run (2026-11-13). 'syntax' = an opcode, rewrite; 'time' = leave.
FORCE = {
    # inter_common(g_h_2663, 2663, ...): the number is the opcode the hook stands on
    'native-src:mod/session2/mount_sync.cpp:1034': 'syntax',
    'native-src:mod/session2/mount_sync.cpp:1038': 'syntax',
    # pop == 3111 (the client's own MoveItemInventoryToOtherInventoryReq)
    'native-src:mod/net/troc_sniffer.cpp:305': 'syntax',
    'native-src:mod/net/troc_sniffer.cpp:306': 'syntax',
    'native-src:mod/net/troc_sniffer.cpp:309': 'syntax',
    # g_req_ops.at(3008), .at(3073), .at(2887)
    'native-src:mod/session2/user2_funnels.cpp:1870': 'syntax',
    # const uint16_t op = 2565, len = 16;  /  op = 2150, len = ...
    'native-src:mod/net/piece4_test.cpp:146': 'syntax',
    'native-src:mod/net/piece4_test.cpp:164': 'syntax',
    'layer-src:mod/net/piece4_test.cpp:146': 'syntax',
    'layer-src:mod/net/piece4_test.cpp:164': 'syntax',
    # st.op = 3111;   op = 2899, len = 9
    'layer-src:mod/net/dev_script.cpp:466': 'syntax',
    'layer-src:mod/net/quest_advance.cpp:253': 'syntax',
    'layer-src:mod/net/quest_advance.cpp:281': 'syntax',
    'native-src:mod/net/quest_advance.cpp:253': 'syntax',
    'native-src:mod/net/quest_advance.cpp:281': 'syntax',
    # left on purpose: owner_fanout_writers.h:29 / :106 / :285 (the base author's 2976 numbers, never ported: their
    # names are not in the newer tables), observer_seq_copy.cpp:175 and user2_shops.cpp:252 (labels in a log line)
}
