# ssal84.py ran three times (two stops on host_scene_diag.cpp's mixed line ends); its insertion into
# npc_activation_logic.h keeps its anchor, so cc_of_processor went in three times. Keep one. ONE SHOT.
import pathlib, re

P = pathlib.Path('C:/Users/asher/CrimsonDesert-SeamlessCoop/upstream/cdmp-native/src/mod/session2/npc_activation_logic.h')
s = P.read_bytes().decode('utf-8')
block = re.compile(r'// 1\.0\.0\.3840: the logic is slot 4 of ServerActorMoveSyncProcessor.*?\r?\n}\r?\n', re.S)
found = list(block.finditer(s))
assert len(found) == 3, len(found)
for m in reversed(found[1:]):
    s = s[:m.start()] + s[m.end():]
assert len(list(block.finditer(s))) == 1 and s.count('uintptr_t cc_of_processor(') == 1
P.write_bytes(s.encode('utf-8'))
print('one cc_of_processor left')
