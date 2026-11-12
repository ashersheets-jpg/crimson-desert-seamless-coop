#pragma once
// Co-op+ (2026-11-09, build 1.0.0.3576), ROADMAP B04 "shared map", HOST only. [session2] fog_share = 0 | 1.
// The host's server holds both players' map fog; at the guest's login it sends the guest's own FogDataAck chunks
// through session 2 (take_frame). With fog_share=1 each of those chunks is OR-ed with the host's chunk of the same
// index (recorded from the host client's own FogDataAck at its login, the sniffer's tap), so the guest's map shows
// everything either player has explored (fog_share_logic.h). The guest's own fog on the server is not changed.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

void fog_share_start(bool on, bool guest);
// The host client's packets (sniffer tap): its own FogDataAck chunks are recorded.
void fog_share_host_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept;
// take_frame: a packet the server sends user 2 (whole wire packet, header 5 bytes); rewritten in place.
void fog_share_on_user2_out(std::span<uint8_t> packet) noexcept;
std::string fog_share_describe();

}  // namespace cdmp::session2
