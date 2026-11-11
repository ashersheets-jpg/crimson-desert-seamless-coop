#pragma once
// Co-op+ (2026-11-10, user: "Lootr" -- every world item can be taken once by EACH player; it disappears only for the
// one who took it). Phase 1 (host side): an item the HOST takes stays for the guest. The host's real pickup runs as
// always (its save's cool time and the regrow stay right); what the server sends the guest about that gimmick (2214,
// 2760, 2096, 3067) is held back, so the guest still sees it; when the guest takes it (3008 / 3073), we answer like
// the server, add the same item to the guest's bag on the server thread (trade_probe entry 0xC0DE0005) and replay the
// held packets. The item = what the host's pickup added to its bag. Not yet: the guest taking first (the host's own
// client), chests and bodies, keeping the list across sessions. lootr_probe: logs the pickup worker and the host's
// packets after it. build/decomp/notes_lootr.md (local); recordings in PROGRESS.md (tcB324-326).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cdmp::session2 {

void lootr_start(bool on, bool probe, bool guest);
void lootr_host_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept;
// server.cpp, a packet about to be queued for the live session 2: true = held back whole; `p` may be rewritten (a
// RemoveActorAck without the kept gimmick).
bool lootr_on_guest_out(std::vector<uint8_t>& p) noexcept;
// session2.cpp offer(), user 2's request: 1 = answered here (dropped), 0 = pass.
int lootr_on_guest_req(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
// Network thread: requests to hand the server as user 2's (the add of its copy).
void lootr_tick(std::vector<std::vector<uint8_t>>& to_server) noexcept;
std::string lootr_describe();

}  // namespace cdmp::session2
