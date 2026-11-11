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

// bodies: enemy / animal body loot per player too (the second looter gets the game's own second roll).
void lootr_start(bool on, bool probe, bool guest, bool bodies);
// [dev] lootr_kill_hits (tests only, host): whatever the host hits dies at once.
void lootr_set_kill_hits(int mode);
void lootr_host_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept;
// server.cpp, a packet about to be queued for the live session 2: true = held back whole; `p` may be rewritten (a
// RemoveActorAck without the kept gimmick).
bool lootr_on_guest_out(std::vector<uint8_t>& p) noexcept;
// session2.cpp offer(), user 2's request: 1 = answered here (dropped), 0 = pass.
int lootr_on_guest_req(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
// Network thread: requests to hand the server as user 2's (the add of its copy).
void lootr_tick(std::vector<std::vector<uint8_t>>& to_server) noexcept;
std::string lootr_describe();
// lootr_probe (host, the SERVER thread: trade_probe's entry 0xC0DE0006): the gimmicks around the host that have a Drop
// event (chests, boxes, breakables) are logged with name, position and rolls left. build/decomp/notes_find_chests.md.
void lootr_chest_scan(uintptr_t host_actor) noexcept;
// Phase 2a (host's client, OnRecvPacket, the game's thread): what the host is told about a gimmick the GUEST took
// first is blanked in place (gimmick key 0 / zero save key), so the host still sees it; the originals are kept.
void lootr_host_recv(uint8_t* packet, uint16_t size) noexcept;

}  // namespace cdmp::session2
