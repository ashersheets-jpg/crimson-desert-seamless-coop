#pragma once
// Co-op+ (2026-11-09, build 1.0.0.3576): the guest's own shop scene, on the guest's client only (shop_twin_logic.h
// says what and why). [session2] shop_twin = 0 | 1 (guest). Called from guest.cpp: every packet of the host before it
// is queued for the game (twins prepared / initialised / deleted around it), every packet of the client before it goes
// to the host (twin requests withheld, answered locally).

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cdmp::troc {
class TrocLink;
}

namespace cdmp::session2 {

void shop_twin_start(bool on, bool guest);
// [session2] shop_twin_again (guest, default 0; 2026-11-21, research): a twin this client's own manager lost (a load
// or far teleport ends its scenes) is prepared again with its stored packets.
void shop_twin_set_again(bool on);
// [session2] twin_watch (HOST, research, default empty): hex resource hashes whose scenes are watched like the shop
// scenes, every packet naming one logged ("ShopTwin [watch]", 600 lines at most).
void shop_twin_set_watch(const std::string& hex_list);
// [session2] twin_extra (GUEST, research, default empty): hex resource hashes whose host scenes get a twin like the
// shops (B14: 94FEA6AE, the Hernand inn's arm-wrestling table), answered from a table of the host server's answers.
void shop_twin_set_extra(const std::string& hex_list);
// Guest: a packet of the client about to go to the host (wire form): a twin_extra scene's minigame request
// (SpawnMiniGameCharacterReq / UnspawnMiniGameCharacterReq) names the host's scene instead of the twin.
void shop_twin_rewrite_out(std::vector<uint8_t>& packet) noexcept;
// [session2] dialog_share_all (both roles, B02a experiment): every non-greeting dialog group of the host's NPCs shared.
void shop_twin_share_all(bool on);
// Co-op+ 2026-11-15, [session2] dialog_redeliver (guest, default 0): when a host frame changes the host's choices for an
// NPC whose dialog list (3086) this client already has, that list is given to the client again with them added.
void shop_twin_redeliver(bool on);
// A packet of the host's server (wire form) about to be queued for the game: `before` / `after` get packets to queue
// around it (in order).
void shop_twin_on_host(std::vector<uint8_t>& packet, std::vector<std::vector<uint8_t>>& before,
                       std::vector<std::vector<uint8_t>>& after) noexcept;
// A packet of the client (wire form) about to go to the host: true = withhold it; `answers` get packets to queue for
// the game instead (the server's answers for a twin).
bool shop_twin_on_out(std::span<const uint8_t> packet, std::vector<std::vector<uint8_t>>& answers) noexcept;
// Host: every packet its client sees (the sniffer tap): the shop choices of its 3086s collected.
void shop_twin_host_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept;
// Host: the network loop: the shop choices to the guest (control state 17) on a change, or every 10 s.
void shop_twin_host_tick(troc::TrocLink& link);
// Guest: a control payload of the host: true = a state-17 frame (consumed).
bool shop_twin_guest_on_control(std::span<const uint8_t> payload);
std::string shop_twin_describe();

}  // namespace cdmp::session2
