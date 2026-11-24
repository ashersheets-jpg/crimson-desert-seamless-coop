#pragma once
// Co-op+ (2026-11-09, build 1.0.0.3576): merchants hidden in co-op (merchant_unhide_logic.h says what and why).
// [session2] merchant_unhide = 0 | 1, both roles:
//   the shop scenes' bindings are noted from this client's 2380 PrepareSequencerAck handler point 0xD3414E (the
//   host: an own mid hook; the guest: guest_seq_route's hook of the same point calls merchant_unhide_note_2380);
//   a worker thread checks those NPCs every 500 ms and, when one keeps hide reason bit 9 for 4 s, calls the game's
//   setter 0x18CD090(info, 0x200, clear, 0.5 s fade, first mask) as the NPC's AI does solo.

#include <cstdint>
#include <span>
#include <string>

namespace cdmp::troc {
class TrocLink;
}

namespace cdmp::session2 {

// diag_only (merchant_unhide=2): the state snapshots only, nothing written.
void merchant_unhide_start(bool on, bool guest, bool diag_only = false, bool guard = false);
// A raw 2380 packet (the Troc frame: 5-byte header, then the payload) seen at 0xD3414E on this client.
void merchant_unhide_note_2380(uintptr_t raw) noexcept;
std::string merchant_unhide_describe();
// Diagnostics: is this NPC key bound to a shop scene on this client (any thread)?
bool merchant_unhide_is_shop_npc(uint32_t key) noexcept;
// The hide mirror (merchant_unhide_logic.h, state 18; [session2] hide_mirror, default 1): set before start.
void merchant_unhide_set_mirror(bool on);
// Host: the network loop: the visible NPCs of unlisted scenes to the guest (every 2 s on a change, else 6 s).
void merchant_unhide_host_tick(troc::TrocLink& link);
// Guest: the host's list (guest.cpp, state 18); false when the payload is not one.
bool merchant_unhide_guest_on_control(std::span<const uint8_t> payload);

}  // namespace cdmp::session2
