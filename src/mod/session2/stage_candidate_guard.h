#pragma once
// Co-op+ (2026-11-09, build 1.0.0.3576): a host crash seen in tcB117 (the user not touching the PC): 0xC0000005 in the
// server's TrocTrSyncStageCandidatePostionReq (3117) handler 0x2CEC840. At +0x3E8 it asks the scene it found for its
// candidate holder (vf48) and writes the positions into it ([holder+0x668..]); vf48 gave null right after all of the
// host's sequencers were deleted (a 2292 burst) while the host's own client still sent a 3117 for one of them.
// [session2] stage_candidate_guard = 1 (default) | 0. A mid hook after that call (0x2CECC2B): a null holder skips the
// write and goes on at the handler's own exit path 0x2CECCBF (the scope released, the request answered "done", r15 = 0
// as the normal path has it), the same as when the scene is not found. Nothing else changes.

#include <string>

namespace cdmp::session2 {

void stage_candidate_guard_start(bool on);
std::string stage_candidate_guard_describe();

}  // namespace cdmp::session2
