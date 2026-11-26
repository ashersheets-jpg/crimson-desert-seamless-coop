#pragma once
// Piece 2 of host-authoritative enemies: our enemies hit the other player.
// With aggro_set_copy_targetable our enemies attack the other player's copy;
// each blow is a DoFrameEventReq whose target list names the copy, and our
// server, which does not know the copy, computes no damage. So:
//   sender: learns how much each attack hurts from the blows OUR player takes
//           (enemy variant + action hash -> HP lost), and sends a PlayerHit
//           event with that amount (0 when not learnt yet) for every blow on
//           the copy;
//   receiver: applies it to its own player through its own server
//           (VaryStatByAccInputReq), 5% of its max HP when the amount is unknown,
//           and reacts to it like the sender's player did (hits_set_react).
// combate-coop.md §1.3-1.4 (formats), §4.3 (c2).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/learnt_file.h"
#include "common/protocol.h"

namespace cdmp::net {

// Sniffer writer thread: every packet (header included).
void hits_observe_c2s(const uint8_t* packet, size_t size);
void hits_observe_s2c(const uint8_t* packet, size_t size);

// Co-op (network thread): PlayerHit events to send.
std::vector<proto::GameEvent> hits_take_events();
// Co-op (network thread): the other player's enemies hit us.
void hits_on_event(const proto::GameEvent& ev);

void hits_set_enabled(bool on);
// Our HP and maximum from the game's own reading (local player), for when no VaryHPAck has given them yet.
void hits_note_own_health(float hp, float hp_max);
// Co-op+ dev ([dev] kill_key=1, Ctrl+Shift+F9): our player dies the way a lethal relayed blow kills it
// (HP to 1, then a collision from ourselves). For repeatable death tests on one PC.
bool hits_dev_kill_self(int64_t hp_x1000);
// [coop] player_hit_react: PlayerHits carry how the sender's player reacts to
// that attack, and we react the same (stagger, knock-down, push).
// [coop] player_hit_lethal: a blow that takes all our HP kills us (a normal
// death); otherwise we stay at 1 HP.
// TEST ONLY (dev scripts' duelhit): one duel hit on our own player through the real path; a line for the log.
std::string hits_dev_duel_hit();
void hits_set_react(bool on, bool lethal);
// Our player's HP x1000 from its last VaryHPAck (-1 unknown), and its maximum (0 unknown).
int64_t hits_own_hp();
int64_t hits_own_hp_max();

// Co-op+ duel (coop_duel.h). Our blow landed on the other player's character: the payload for them (a PlayerHit
// payload whose attacker is our character over there, with a stagger reaction learnt from our own fights).
std::vector<uint8_t> hits_make_pvp(uint32_t attack_hash, int64_t damage_x1000);
// Their blow on us (hits_make_pvp's payload): applied through our own server with the reaction, never below
// `floor_x1000` HP (a duel is lost, not died in). Returns the damage applied (x1000).
int64_t hits_on_pvp(const std::vector<uint8_t>& payload, int64_t floor_x1000);
// Changes our player's HP by `delta_x1000` through our server (the duel's heal back after a round).
bool hits_vary_own_hp(int64_t delta_x1000);
// cdmp_learnt.txt (learnt_store.cpp): what the enemies' attacks do to us.
// Learnt whether or not co-op hits are on, so playing alone counts too.
void hits_learnt_get(LearntCombat& out);
void hits_learnt_put(const LearntCombat& in);

}  // namespace cdmp::net
