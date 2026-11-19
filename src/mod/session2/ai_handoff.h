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
// fight (2026-11-16, B18, [session2] ai_handoff_fight, default 1): the FIGHT rule, on its own or with the mode above. Measured
// (cs40a / cs40b): the host's quest bandits are driven by the host's client and go for the host, even while only the
// guest fights them (27 blows on the idle host, 3 on the guest). So: an NPC the other player hit in the last 4 s,
// that the host has not hit for 6 s, with that player within 20 m and nearer than the host, goes to that player;
// the same the other way back. At most one hand-over per NPC in 10 s.
void ai_handoff_start(std::string_view mode, bool guest, bool fight = false);
// Any thread, the host: asks for the server-thread check once a second while a second player is in the world.
void ai_handoff_tick() noexcept;
// The SERVER thread (trade_probe's entry 0xC0DE0008, the host's own request).
void ai_handoff_server_tick() noexcept;
// The client's packet thread, both roles, before the game reads a server packet (the sniffer's OnRecvPacket hook, the
// mod's own injected packets included). True = the game must not see this packet.
bool ai_handoff_client_recv(const uint8_t* packet, size_t size) noexcept;
// The RIDE rule (2026-11-18, Borque: the joined player's horse-taming minigame never ends; its cart is not seen by
// the host): an NPC mount or vehicle (no owner user) that a player rides runs on that player's game while ridden, as
// the server accepts a mount's AI / movement requests from its controller only. mount_sync calls this from the host's
// SERVER-realm vf53 / RideOff (any thread; atomics only): on = mounted (rider's character key, the NPC's key), off =
// the rider left it. The host's server tick hands the NPC over; the fight rule leaves a ridden NPC alone.
void ai_handoff_note_ride(uint32_t rider_key, uint32_t mount_key, bool on) noexcept;
// The catch rule (2026-11-18, Borque: "catching small animals plays the animation but catches nothing"): the host's
// server accepted the other player's catch interaction (2671 result 0, shop_probe's C point) on an NPC. In single
// player the catcher's own game runs the animal while it is caught; with its AI left on the host the guest's catch
// never ends. The next server tick hands that NPC to the catcher's game for kCatchMs (the fight rule leaves it alone
// meanwhile). Host role only; the other player's character and the catch interactions only.
void ai_handoff_note_catch(uint32_t catcher_key, uint32_t npc_key, uint32_t interaction) noexcept;
// [session2] ai_handoff_catch (default 1): 0 turns the catch rule off (cs65's control run).
void ai_handoff_set_catch(bool on) noexcept;
std::string ai_handoff_describe();

}  // namespace cdmp::session2
