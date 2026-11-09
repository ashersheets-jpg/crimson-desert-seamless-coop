#pragma once
// Co-op+ (2026-11-09, build 1.0.0.3576): merchants hidden in co-op (merchant_unhide_logic.h says what and why).
// [session2] merchant_unhide = 0 | 1, both roles:
//   the shop scenes' bindings are noted from this client's 2380 PrepareSequencerAck handler point 0xD3414E (the
//   host: an own mid hook; the guest: guest_seq_route's hook of the same point calls merchant_unhide_note_2380);
//   a worker thread checks those NPCs every 500 ms and, when one keeps hide reason bit 9 for 4 s, calls the game's
//   setter 0x18CD090(info, 0x200, clear, 0.5 s fade, first mask) as the NPC's AI does solo.

#include <cstdint>
#include <string>

namespace cdmp::session2 {

// diag_only (merchant_unhide=2): the state snapshots only, nothing written.
void merchant_unhide_start(bool on, bool guest, bool diag_only = false, bool guard = false);
// A raw 2380 packet (the Troc frame: 5-byte header, then the payload) seen at 0xD3414E on this client.
void merchant_unhide_note_2380(uintptr_t raw) noexcept;
std::string merchant_unhide_describe();

}  // namespace cdmp::session2
