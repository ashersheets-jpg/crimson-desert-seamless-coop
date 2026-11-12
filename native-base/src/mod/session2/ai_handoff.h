#pragma once
// Co-op+ (2026-11-11, build 1.0.0.3576): an NPC's AI goes to the player who can still see it.
//
// Measured (ff3): the host draws its bandits, then goes 108 m away; the guest stays among them. From that moment
// every bandit keeps exactly its position and pose and the guest takes no damage. Cause (research notes, local):
// - an NPC's controller is a user key at [actor+0x90], written ONCE when the actor is created; only the controller's
//   client runs its AI, and the server accepts AI / movement requests from the controller only;
// - the server shows a user only the actors in the 3x3 block of 64 m sectors around its character; outside it the
//   client gets RemoveActorAck and no longer has the actor at all.
// So an NPC whose controller walked out of its block is driven by nobody, although another player stands next to it.
//
// What this does, on the host's server thread once a second: for every NPC with no owner user whose controller's
// character is outside the NPC's 3x3 block (or whose controller is gone) while another player's character is inside
// it, for two checks in a row: [actor+0x90] = that player's user key, then the game's own "AI restart" steps (what its
// AiControlChangeCheatReq handler runs: AI session + 1, move session + 1, action reset). The new controller's client is
// told with a private form of ChangeAiControlAck (2660) that its receive hook turns into: client actor+0x90 = my user
// key, control mode recomputed (exe+0x5AF410), then the game's own 2660 (action reset, "OnAi"). The same rule hands
// the NPC back when that player leaves and the first one is there.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cdmp::session2 {

// [session2] ai_handoff = 0 | probe | 1. probe: counted and logged as "would", nothing written. The client part (the
// receive hook's answer to the private 2660) is on in both roles whenever the mode is not 0.
void ai_handoff_start(std::string_view mode, bool guest);
// Any thread, the host: asks for the server-thread check once a second while a second player is in the world.
void ai_handoff_tick() noexcept;
// The SERVER thread (trade_probe's entry 0xC0DE0008, the host's own request).
void ai_handoff_server_tick() noexcept;
// The client's packet thread, both roles, before the game reads a server packet (the sniffer's OnRecvPacket hook, the
// mod's own injected packets included). True = the game must not see this packet.
bool ai_handoff_client_recv(const uint8_t* packet, size_t size) noexcept;
std::string ai_handoff_describe();

}  // namespace cdmp::session2
