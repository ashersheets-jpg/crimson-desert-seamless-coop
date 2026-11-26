#pragma once
// Co-op+ (2026-11-26, build 1.0.0.3732), ROADMAP B10 "the host's story places", HOST only.
// [session2] level_state_share = 0 (default) | probe | 1.
// The server sends ChangeGameLevelStateAck (the world's game levels and their states) to the field owner only, and
// the joined player gets the whole list only at its own entry (level_state_share_logic.h). With 1, every one the
// host's client receives while user 2 is in the world goes to user 2 too, with user 2's character key in it (the
// sniffer's tap, then server_push_extra, as dye_share does), so a story step of the host that changes a game level's
// state changes it in the joined player's game as well. probe: the same decisions, logged, nothing sent.

#include <cstddef>
#include <cstdint>
#include <string>

namespace cdmp::session2 {

void level_state_share_start(const std::string& mode, bool guest);
// The host client's packets (sniffer tap, its writer thread).
void level_state_share_host_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept;
std::string level_state_share_describe();  // "" when off

}  // namespace cdmp::session2
