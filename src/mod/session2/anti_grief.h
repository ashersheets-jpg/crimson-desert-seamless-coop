#pragma once
// Co-op+ (2026-11-09, build 1.0.0.3576), ROADMAP B19: the guest cannot hurt the people of the host's world.
// [session2] anti_grief = 0 | 1 (HOST). With 1, the guest's DoFrameEventReq whose targets include an NPC the host
// talks to (any SetQuestDialogAck 3086 the host's client got: townsfolk, merchants, quest givers) never reaches the
// host's server: no damage, no crime, no broken quest (anti_grief_logic.h). Enemies and animals are not touched.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cdmp::session2 {

void anti_grief_start(bool on, bool guest);
// The host client's packets (sniffer tap): the NPCs with a dialog list are recorded.
void anti_grief_host_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept;
// A Req of user 2 about to be handed to the host's server: true = drop it.
bool anti_grief_drop_user2_req(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
// A guest hit with protected and other targets: the payload without the protected ones (the swing still hits the
// enemies); empty when nothing is to remove or every target is protected (then the drop above decides).
std::vector<uint8_t> anti_grief_strip_user2_req(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
std::string anti_grief_describe();

}  // namespace cdmp::session2
