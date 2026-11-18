#pragma once
// The scene-Req guard (2026-11-18, Borque's host crash at 13:46:51, build\decomp\notes_host_unload_crash_3663.md,
// private): the server's handler of TrocTrSequencerTriggerVolumeInOutReq (2808, exe+0x2C53BB0 on 1.0.0.3663) asks the
// subject's field (ServerField vf48) for its owner's sequencer stage manager and takes the manager's lock with no null
// test. While the host loads (its stages close, "unload at the owner's load") the joined player's character can still
// stand in the host's field whose owner does not resolve: vf48 answers 0 and the host's game crashed reading 0x0.
// A mid hook at vf48's return site sends a 0 answer to the handler's own "not attached" exit (exe+0x2C53D7E), as if
// the subject had no field: the trigger volume enter / leave of that moment is dropped, nothing else.
#include <string>

namespace cdmp::session2 {

// Checks the code bytes first; false (and nothing hooked) on any other game build. Idempotent.
bool scene_req_guard_install();
std::string scene_req_guard_describe();

}  // namespace cdmp::session2
