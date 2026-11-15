// Co-op+ 2026-11-15: the story gate as [session2] story_gate=0 (its default) for the unit tests that link
// observer_hold_acks.cpp without story_gate.cpp (child_hold, s3, character_hold, pets). Every story question answers
// "no": the observer hold routes as it did before the ready check.
#include "mod/session2/story_gate.h"

namespace cdmp::session2 {

bool story_gate_story_hash(uint32_t) noexcept { return false; }
bool story_gate_user2_story(uint64_t) noexcept { return false; }
bool story_gate_keep_user2(uint16_t, std::span<const uint8_t>, const std::vector<uint8_t>&) noexcept { return false; }

}  // namespace cdmp::session2
