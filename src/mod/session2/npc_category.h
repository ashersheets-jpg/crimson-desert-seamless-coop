#pragma once
// Co-op+ (2026-11-08, build 1.0.0.3576): the NPCs' quest-dialog categories, shared from the host
// (npc_category_logic.h says what and why). [session2] npc_category_share = 1 (default) | 0, both roles.
//
//   host    every second (the network thread) its own client's table ([local character+0x68]+0xF8) is read into a
//           key -> category map; sent to the guest as control state 16 on a change (or every 5 s); the same map
//           serves the host's SERVER when it judges user 2's character.
//   guest   the host's map from state 16 (dropped after 15 s without one).
//   both    an inline hook on the category getter 0x1C358E0 (table, NPC key): for a table of the guest's character
//           (on the guest's client: any player character; on the host's server: server_user2_player_key) a 0 answer
//           (the NPC's own entry missing or 0) becomes the host's category. Its callers: CheckQuestDialogCategory and
//           HasQuestDialog's dialog search (which gives up at 0 before it reads the guest's OWN dialogs, tcB86).
//           Nothing in the game is written.

#include <cstdint>
#include <span>
#include <string>

namespace cdmp::troc {
class TrocLink;
}

namespace cdmp::session2 {

// The late-init thread, once (after the game's sections are readable). guest: this PC plays on the host.
void npc_category_install(bool on, bool guest);
// Host: the network loop, every pass (paced inside).
void npc_category_host_tick(troc::TrocLink& link, uint32_t now_ms);
// Guest: a control payload of the host (opcode 1). True: a state-16 frame (consumed).
bool npc_category_guest_on_control(std::span<const uint8_t> payload);
std::string npc_category_describe();

}  // namespace cdmp::session2
