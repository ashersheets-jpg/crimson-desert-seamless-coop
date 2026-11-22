#pragma once
// The SENDING side of a remote player: turns what the local player does into
// a stream of game events that another game can replay on its copy of us
// (remote_puppet.h). Used both by the single-PC mirror (events go straight to
// our own puppet) and by co-op (events go over the network).
//
// Sources, all on the sniffer's writer thread:
//   - the player's own client->server messages (MoveActorReq, StartActionReq,
//     RotateActorReq), passed on unchanged;
//   - moves synthesised from memory while the game sends none but the player
//     does move (vehicles...);
//   - the ledge climb-up path (UpdateWallUpData) read from memory, which the
//     single-player game never sends;
//   - equipment changes (OtherEquipItemAck), from a hook on the in-process
//     server's writer of that message.
// The player's ActorLoadDataAck packets are kept as templates for SpawnInfo.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "common/protocol.h"

namespace cdmp::net {

// Writer thread: every captured server->client packet (templates).
void capture_observe_s2c(const uint8_t* packet, size_t size);
// Writer thread: every captured client->server packet (the player's Reqs).
void capture_observe_c2s(const uint8_t* packet, size_t size);
// Writer thread, every loop: synthesised moves and the climb-up path.
void capture_tick();

// Where events go (called on the writer thread). Empty = capture is idle.
using EventSink = std::function<void(const proto::GameEvent&)>;
void capture_set_sink(EventSink sink);

// What another game needs to load a copy of the local player, positioned at
// `pos`. nullopt until a save was loaded with the sniffer running.
std::optional<proto::SpawnInfo> capture_spawn_info(const proto::Vec3& pos, const proto::Quat& rot);

// Hooks the server's OtherEquipItemAck writer (read-only, arguments only).
// Call once after the world exists.
bool capture_install_equip_hook();
// Changes whenever new templates are captured (a save or area was loaded).
uint32_t capture_template_generation();
// Our horse's key if its template is captured and it is in the world now
// (what capture_spawn_info would send), else 0.
uint32_t capture_horse_present();
// Co-op+ test kit: our own Moves name our horse as their parent (we ride it now; any thread).
bool capture_riding();
// Co-op+ (prologue_hold.h): our Moves have named one parent (a horse, ours or someone's; riding along) for at least
// `ms` without a break, and still do. `parent`: what carries us.
bool capture_carried_for(uint64_t ms, uint32_t* parent);

// Co-op+: Ctrl+F3 "ping": our position goes to the other player (EventKind::Waypoint, flag 2).
void capture_emit_ping(float x, float y, float z);
// Co-op+: an EventKind::Activity payload for the other player (coop_activity.h).
void capture_emit_activity(std::vector<uint8_t> payload);
// Co-op+: our weather stages to the other player (weather_sync.h).
void capture_emit_weather(std::vector<uint8_t> payload);
// Our own player actor's key (0xA01...), 0 until a save was loaded.
uint32_t capture_own_player_key();
// Our own body actor (the B0... actor the player template names; 0 until captured).
uint32_t capture_own_body_key();
// Co-op+ 2026-11-22 (shared world): the other player's character key from its character load on this client, 0 until
// seen (the dev script's killfriend).
uint32_t capture_other_player_key();
// Our own user key (0x901...), 0 until a save was loaded.
uint32_t capture_own_user_key();
// True for a world character load seen (NPCs and enemies; not our pet,
// horse or companions).
bool capture_is_enemy(uint32_t key);
// True once the game loaded an actor whose key falls in the range of the keys
// the mod invents (keys.h): copies must not be created any more.
bool capture_key_alarm();
// Last known HP (x1000, -1 unknown) and death of one of our world's enemies.
struct EnemyState {
    int64_t hp = -1;
    bool dead = false;
};
EnemyState capture_enemy_state(uint32_t key);

// This game's clock as used in the Req timestamps (ms): read from the game
// once that reading agrees with our Reqs (it stops in menus), else
// extrapolated from the player's latest own Req. 0 while unknown.
uint64_t capture_game_clock_ms();
// The sniffer's writer, before each capture_observe_*: when that packet was captured (QPC microseconds, the
// steady_clock's base). Our clock estimate uses it instead of when the writer got to the packet.
void capture_set_packet_time_us(uint64_t t_us);
// Co-op+ low latency: our moves also from our transform every frame ([coop] pose_stream, default on).
void capture_set_pose_stream(bool on);
// True once the game's own clock (game_clock.h) is known to agree with our Reqs.
bool capture_game_clock_trusted();
// The check of the game's clock read is over (trusted or estimated instead).
bool capture_game_clock_decided();

// Combat tests: the nearest world actor (0xB01 key, not ours) whose movement
// our client reported in the last 3 s, within max_m of `from`, AND that has
// hit points (the server sent a VaryHPAck for it, or the player hit it).
// Anything else (gimmicks: levers, doors...) crashed the server when given
// damage (live test, 2026-10-03).
struct NearbyActor {
    uint32_t key;
    proto::Vec3 pos;
    float distance;
};
std::optional<NearbyActor> capture_nearest_actor(const proto::Vec3& from, float max_m);
// Attack hash (+0x24) of the latest PostProcessAttackResult_AttackTargetAck
// where the local player was the attacker; 0 if none yet.
uint32_t capture_last_attack_hash();
// Key of the actor the local player hit last (0 if none).
uint32_t capture_last_attack_target();

// Mirrored damage (docs/research/combate-coop.md, option b). The sender emits
// a Damage event whenever its server reports HP lost by an enemy to the local
// player; the receiver calls this to apply it to its own matching enemy
// through its own server (VaryStatByAccInputReq, verified live). Writer or
// network thread. Returns false if no matching enemy was found.
bool apply_remote_damage(const std::vector<uint8_t>& event);
// [coop] damage=0 turns both directions off.
void capture_set_damage_enabled(bool on);

// Timestamp (sender's game clock, ms) inside a GameEvent payload; 0 if the
// kind carries none (WallUp) or the payload is malformed.
uint64_t event_timestamp(const proto::GameEvent& ev);

// Co-op+ 2026-11-15 (shared world): the friend's character actor. The host's is always 0xA0100001; the guest's
// varies with the world (0xA0100066 in the test world, others in New Co-op Saves), so the host finds it: the
// class-0xA actor (not ours) within 10 m of the friend's network position, at most every 3 s, kept while it stays
// within 25 m of it. Call update from the network loop (net_pos nullptr: no friend); key 0 = not known.
void partner_actor_update(const proto::Vec3* net_pos, bool we_host);
uint32_t partner_actor_key();

}  // namespace cdmp::net
