#include "mod/net/revive_prompt.h"

#include <windows.h>

#include <array>
#include <iterator>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <safetyhook.hpp>

#include "common/clock.h"
#include "mod/game/actor_lookup.h"
#include "mod/input/actions.h"
#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/game/ui_state.h"
#include "mod/net/boss_safe.h"
#include "mod/log.h"
#include "mod/net/local_capture.h"
#include "mod/net/remote_puppet.h"
#include "mod/net/coop_revive.h"
#include "mod/native_layer.h"
#include "mod/net/troc_sniffer.h"

namespace cdmp::net {
namespace {
namespace off = game::build_3732;

constexpr uint16_t kOpReviveReq = 2869;             // u32 player | u8 type | u8 x3
constexpr uint16_t kOpReviveLoadingStartReq = 2972;  // 04 | u8 type | 00
constexpr uint16_t kOpReviveLoadingReq = 2887;       // u8 type
constexpr uint16_t kOpGameLoadingStartReq = 2350;    // empty: the client starts loading a field
constexpr uint16_t kOpLoadingFieldDataAck = 2286;    // 04 | u32 player | ... | +28 vec3, quat, scale
constexpr uint16_t kOpNotifyHandleDeadAck = 2316;   // u32 dead | u32 killer | type | ...
constexpr uint16_t kOpDoTeleportAck = 2379;          // u32 actor | vec3 | 8 bytes (handler reads 4, 12, 8)
constexpr uint8_t kInstant = 1, kCheckpoint = 2;
constexpr uint32_t kWaitForGameMs = 4000;   // the game's own screen shows ~2 s after the death
constexpr uint32_t kLoadingStepMs = 1000;   // the game's gap between its two checkpoint requests
constexpr uint32_t kRetryAfterMs = 5000;    // still dead this long after a choice: offer it again
constexpr uint32_t kTeleportCheckMs = 1500; // after the revive: still farther than kTeleportNearM -> again
constexpr float kTeleportNearM = 10.0f;
constexpr int kTeleportTries = 8;  // ~12 s: a teleport during the get-up animation is ignored (live tests)
constexpr uint64_t kBossFightWindowMs = 30000;  // a boss whose HP changed this recently: the fight is on
constexpr uint32_t kBossReviveRetryMs = 4000;

uint32_t g_dead_since = 0;
uint32_t g_chosen_at = 0;
uint32_t g_loading_req_at = 0;
uint32_t g_revive_req_at = 0;  // checkpoint step 3 (GameLoadingStartReq + ReviveReq), unless the game loads
bool g_q_was = false, g_u_was = false;
// Checkpoint revive: the server moved us (LoadingFieldDataAck) but the client, not loading, stays put.
std::atomic<bool> g_checkpoint_asked{false}, g_teleport_due{false};
float g_checkpoint[3] = {};
uint32_t g_teleport_at = 0;
int g_teleport_tries = 0;
// A9 (boss-fight deaths).
bool g_boss_wait = true;
bool g_boss_hold = false;       // we died during a boss fight and wait for the other player
uint32_t g_boss_revive_at = 0;  // the boss is down: we were sent up; checked again at this time

// The status component keeps the revive type the client itself asked for at +0x471 (16 = none);
// its ReviveLoadingStartAck handler (exe+0x6F33D0) drops an answer of another type. Set as the
// game's screen would before asking. Not enough for its loading screen; kept because the one
// teleport that took had it (live tests 2026-11-02).
constexpr uint32_t kStatus_PendingReviveType = 0x471;

bool write_byte(uintptr_t addr, uint8_t v) {
    __try {
        *reinterpret_cast<volatile uint8_t*>(addr) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool expect_revive(uint32_t own, uint8_t type) {
    const auto actor = game::find_actor(own);
    const auto table = actor ? game::read_ptr(*actor + off::kActor_ComponentTable) : std::nullopt;
    const auto status = table ? game::read_ptr(*table + off::kComponents_Status) : std::nullopt;
    const auto before = status ? game::read<uint8_t>(*status + kStatus_PendingReviveType) : std::nullopt;
    const bool ok = before && write_byte(*status + kStatus_PendingReviveType, type);
    log::info("Revive: pending revive type {} -> {}{}", before ? static_cast<int>(*before) : -1, type,
              ok ? "" : " (NOT set)");
    return ok;
}

// Teleports: DoTeleportAck moves our player only up to ~50 m (30 and 51 m took; 80, 104, 150, 400 m and 5 km were
// ignored, live tests 2026-11-02), standing or walking, and only to a point near the ground (24 m up: refused).
// Longer moves go in hops of up to kHopM from where we actually are, one every kHopMs, at our current height; a hop
// that did not take (we did not move) is tried again a little higher or lower (the ground rises and falls), then
// higher still, shorter, and to the side (a cliff wall stopped a 4 km trip 1.8 km short with heights alone, travel1
// 2026-11-02); after all of kHopTries miss in a row the move stops where it got. Any thread may ask; the network loop
// hops.
constexpr float kHopM = 40.0f;
constexpr uint32_t kHopMs = 450;
struct HopTry {
    float dy;    // added to the height
    float side;  // metres to the right (+) or left (-) of the way to the goal
    float len;   // hop length (at most kHopM)
};
constexpr HopTry kHopTries[] = {
    {0, 0, 40},    {3, 0, 40},    {-3, 0, 40},   {8, 0, 40},     {-8, 0, 40},    {15, 0, 40},   {-15, 0, 40},
    {25, 0, 40},   {-25, 0, 40},  {40, 0, 40},   {60, 0, 40},    {90, 0, 40},    {130, 0, 40},  {180, 0, 40},
    {5, 0, 15},    {30, 0, 15},   {5, 30, 30},   {5, -30, 30},   {40, 30, 30},   {40, -30, 30}, {90, 40, 25},
    {90, -40, 25}, {5, 40, 0},    {5, -40, 0},   {60, 40, 0},    {60, -40, 0},
};
constexpr int kHopFailLimit = static_cast<int>(std::size(kHopTries));
constexpr float kArrivedM = 3.0f;
std::mutex g_hop_mutex;
std::optional<std::array<float, 3>> g_hop_target;  // newly asked
std::optional<std::array<float, 3>> g_hop_goal;    // in progress
std::atomic<float> g_arrive_next{kArrivedM};  // the next request's "arrived" radius (revive_teleport_to)
float g_arrive_m = kArrivedM;                 // the one in progress (network loop only)
std::array<float, 3> g_hop_last{};                 // where we were at the last hop
int g_hop_fails = 0;
uint32_t g_hop_at = 0;

// FALL GUARD (2026-11-25, Brycecco + Arcticwolf on 614591d0, the first two-PC run of the game's start). Right after
// "Into the Fog" the host's story took it up to an Abyss island (the prologue's Abyss tutorial, ~2008 m up). That
// island is a level of the HOST's story: its server loaded it for the host only (SceneLevelShare "owner native", never
// "ready for user 2"), so the joined player's game had no ground there. Go to my friend's far travel arrived at
// 2007.9 m, the game held the character in the air ~25 s, then it fell straight down through the world (1650 m in
// ~45 s), died on landing, was revived at the island's checkpoint (the server's choice) and fell again; the next tries
// to reach the friend could not take. So, after a move of ours arrives, for kFallWatchMs: our height kFallDropM below
// the move's goal, falling at least kFallSpeed over the last second, still within kFallNearM of the goal (a straight
// drop: not a walk off an edge, a dive or a lift), while the friend stands up there = no ground under us in our game.
// Then: back to where the move started (far travel or hops, as any move), a note, and the spot is remembered for
// kBadSpotMs: a move to within kBadSpotM of it, not more than kBadSpotDy below it, is refused with a note (until we
// stand there ourselves: the place is in our game by then).
constexpr uint32_t kFallWatchMs = 120000;
constexpr uint32_t kFallSampleMs = 250;
constexpr float kFallDropM = 30.0f;
constexpr float kFallSpeed = 12.0f;                            // metres per second, over the last second
constexpr float kFallNearM = 7.0f;                             // from the goal (3 m of arrival radius, some drift)
constexpr float kFallFriendM = 200.0f, kFallFriendDy = 15.0f;  // the friend still up there
constexpr uint32_t kBadSpotMs = 300000;
constexpr float kBadSpotM = 150.0f, kBadSpotDy = 60.0f;
uint32_t g_fall_watch_until = 0;     // network loop only, like the rest of the fall guard but g_bad_spot
std::array<float, 3> g_fall_goal{};  // the arrived move's goal
std::array<float, 3> g_move_from{};  // where the move in progress started (its first position read)
bool g_move_from_set = false;
std::array<float, 3> g_fall_back{};  // where the arrived move started
bool g_fall_back_set = false;
uint32_t g_fall_sample_at = 0;
float g_fall_y[5] = {};  // the last heights, kFallSampleMs apart (g_fall_n % 5)
int g_fall_n = 0;
struct BadSpot {
    std::array<float, 3> p;
    uint32_t at;
};
std::optional<BadSpot> g_bad_spot;  // under g_hop_mutex
uint32_t g_bad_refused_at = 0;      // the last refusal's note (under g_hop_mutex)
uint32_t g_bad_stand_since = 0;     // we stand on the bad spot since (network loop)
// The friend's last position (mod.cpp, every network tick while linked and the friend is alive; cdmp::now_ms).
std::atomic<float> g_friend_x{0.0f}, g_friend_y{0.0f}, g_friend_z{0.0f};
std::atomic<uint32_t> g_friend_at{0};
std::atomic<bool> g_fall_test{false};  // TEST ONLY ([dev] fall_guard_test): no "the friend stands up there" check

void say(std::string text);

void request_move(float x, float y, float z, bool check_bad = true) {
    std::lock_guard lock(g_hop_mutex);
    if (check_bad && g_bad_spot) {
        const BadSpot& b = *g_bad_spot;
        const uint32_t now = cdmp::now_ms();
        const float dx = x - b.p[0], dz = z - b.p[2];
        if (now - b.at >= kBadSpotMs) {
            g_bad_spot.reset();
        } else if (dx * dx + dz * dz <= kBadSpotM * kBadSpotM && y >= b.p[1] - kBadSpotDy) {
            if (!g_bad_refused_at || now - g_bad_refused_at >= 10000) {
                g_bad_refused_at = now | 1;
                log::info("Teleport: a move to ({:.0f}, {:.0f}, {:.0f}) is refused: {:.0f} m from where our game had no "
                          "ground {:.0f} s ago", x, y, z, std::sqrt(dx * dx + dz * dz), (now - b.at) / 1000.0);
                say("Co-op\nYour friend is somewhere your game has no ground yet (likely a place from their story, "
                    "like an Abyss island). Go to my friend works again once they leave it");
            }
            return;
        }
    }
    g_hop_target = std::array<float, 3>{x, y, z};
}

// FAR TRAVEL (2026-11-12). Two testers: going to the friend over 3 km took 2 and 3 minutes of being dragged across
// the map in hops and once stopped short. The game's own far move is a pending reload (local notes
// build/decomp/notes_far_teleport.md, game build 3576): our client sends ReloadStageStartReq 3074 with the position
// (u32 0 | u32 FieldInfoKey | u32 0 | vec3 pos | quat | vec3 scale | u8 1; with actor 0 the server takes the position
// as it is), the server keeps it as our destination, and TeleportRevivePointAck (3732: 3072; 3576: 3071) (u32 our key
// | u32 0) starts our
// client's loading screen, which asks the server for the load by itself. A short loading screen, then we stand there.
// The hops stay for short moves, for the last metres, and as the way out when this does not take.
// The FieldInfoKey of the field we are in: on the host from the native base (cdmp_field_info_key), on the guest from
// the host (coop_lobby's kMsgFieldKey: both are in the host's field). 0 = not known: hops as before.
constexpr uint16_t kOpReloadStageStartReq = 3018;
constexpr uint16_t kOpTeleportRevivePointAck = 3072;
// Farther than this: the game's travel. 250 m, not 80 (pp10-revive, 2026-11-12): the travel is a load of about 9 s on
// the server whatever the distance (the traveller cannot be hit, or revived, while it lasts: the step's kill came 7 s
// after a 112 m travel and did nothing), and a few hops cover 250 m in less than that.
constexpr float kFarM = 250.0f;
constexpr float kFarLandedM = 40.0f;      // this close after the load: it took us there
constexpr uint32_t kFarAckDelayMs = 300;  // the server has our destination before our client starts loading
constexpr uint32_t kFarTimeoutMs = 45000;
std::atomic<uint32_t> g_field_key_from_host{0};
bool g_far_try = false;     // a newly asked move: far travel is considered once
uint32_t g_far_ack_at = 0;  // when the 3071 goes to our client (0 = sent / none)
uint32_t g_far_until = 0;   // far travel in progress until (0 = none)
uint32_t g_far_since = 0;
// nb5-lowsave (2026-11-12): the join's move to the host's side (1321 m) was asked 4 s after the guest entered the
// world; the server did the travel, the guest stood at the host's side for a moment, and was then put back to its
// save's place: its login's own move into the host's field was still finishing and ended after ours. So a far
// travel is looked at again a few seconds after it arrived: back near where we started and far from the goal =
// the same travel again (twice at most; a player cannot get that far by walking in that time).
constexpr uint32_t kFarConfirmMs = 4000;
// nb6-lowsave: the server did the travel and our client never moved (45 s of waiting, then 1.3 km of hops). When we
// still stand where we started 8 s after asking, the two packets go out once more; the hops only after that.
constexpr uint32_t kFarStuckMs = 8000;
constexpr float kFarMovedM = 20.0f;
bool g_far_again = false;       // the request was sent a second time for this move
uint32_t g_far_key = 0;
bool g_far_used = false;                 // this move went by far travel
std::array<float, 3> g_far_from{};       // where we stood when it began
std::array<float, 3> g_confirm_goal{};
uint32_t g_confirm_at = 0;               // when to look again (0 = nothing to confirm)
int g_confirm_tries = 2, g_confirm_waits = 0;
// 2026-11-25 (Borque, 527e283a): the host's server took no input for 30 s, 26 s and 153 s right after the joined
// player came into its field 600 m from the host. The join's move went by far travel: the request waited in the
// stalled server, our loading screen (the 3072) waited for it, the 8 s rule asked again (in his first attempt: two
// reloads back to back), and the 45 s timeout went on to hops, which cannot move a client in a loading screen: it never
// ended. So while the server is stalled (the native's stall watch, > 1 s: on the host from the native base, on the guest
// from the host, coop_lobby's kMsgServerBusy, trusted for kHostBusyFreshMs): a far travel not started yet waits (no
// request, no loading screen; the player can move meanwhile) and says so; one under way neither asks again nor times
// out (it ends when the server takes it, or as before once the server answers again). At most kFarWaitMaxMs in all.
std::atomic<uint32_t> g_host_busy_ms{0}, g_host_busy_at{0};  // the host's word (the guest), on cdmp::now_ms()
constexpr uint32_t kHostBusyFreshMs = 6000;
constexpr uint32_t kFarWaitMaxMs = 300000;
constexpr uint32_t kFarWaitToastMs = 30000;
uint32_t g_far_wait_since = 0;     // a far travel not started yet held back since (0 = none)
uint32_t g_far_wait_toast_at = 0;  // the last "it waits" toast
uint32_t g_far_stuck_from = 0;     // the 8 s rule counts from here (the request, or the end of a stall)
bool g_far_in_stall = false;       // the server is stalled during this far travel (said once per stall)
std::mutex g_toast_mutex;
std::string g_toast;  // revive_take_toast

// When the last stall seen here ended (either source; cdmp::now_ms(), 0 = none). cs127b (2026-11-25): a far travel
// that went right after a 25 s stall took 9 s (the server worked through its backlog and the game's autosave that had
// waited for it), so the 8 s rule asked again and the host's server ran a second reload trip over the first (it
// finished there; in Borque's first attempt the two broke the load). Within kAfterStallMs of a stall: one request only.
std::atomic<uint32_t> g_stall_ended_at{0};
constexpr uint32_t kAfterStallMs = 30000;
bool stall_ended_recently(uint32_t now_ms) {
    const uint32_t e = g_stall_ended_at.load(std::memory_order_relaxed);
    return e && now_ms - e < kAfterStallMs;
}

// This PC's server (the host): the native base's stall watch (0 = no stall, or no server here: the guest).
uint32_t native_stalled_ms() {
    using Fn = uint32_t (*)();
    static Fn fn = [] {
        Fn f = nullptr;
        if (HMODULE m = GetModuleHandleW(L"cdmp.asi")) f = reinterpret_cast<Fn>(GetProcAddress(m, "cdmp_server_stalled_ms"));
        return f;
    }();
    const uint32_t ms = fn ? fn() : 0;
    static std::atomic<bool> was{false};
    if (was.exchange(ms != 0, std::memory_order_relaxed) && !ms) g_stall_ended_at.store(cdmp::now_ms() | 1);
    return ms;
}
bool server_stall_is_ours() { return native_stalled_ms() != 0; }

// How long the server has been stalled, in ms (0 = it takes requests, or nobody knows): this PC's own (the host) or the
// host's word (the guest).
uint32_t server_busy_ms(uint32_t now_ms) {
    if (const uint32_t ms = native_stalled_ms()) return ms;
    const uint32_t at = g_host_busy_at.load(std::memory_order_relaxed);
    if (!at || now_ms - at >= kHostBusyFreshMs) return 0;
    const uint32_t ms = g_host_busy_ms.load(std::memory_order_relaxed);
    return ms ? ms + (now_ms - at) : 0;
}

void say(std::string text) {
    std::lock_guard lock(g_toast_mutex);
    g_toast = std::move(text);
}

enum class FarWait { Go, Wait, GiveUp };
// A far travel not started yet, while the server is stalled: Wait (looked at again in a second), GiveUp after
// kFarWaitMaxMs (hops from where we are), else Go. Said when it starts waiting, every kFarWaitToastMs on screen, and
// when it ends.
FarWait far_wait_for_server(uint32_t now_ms) {
    const uint32_t busy = server_busy_ms(now_ms);
    if (!busy) {
        if (g_far_wait_since) {
            log::info("Teleport: the server takes requests again; the far travel waited {:.1f} s and goes now",
                      (now_ms - g_far_wait_since) / 1000.0);
            g_far_wait_since = 0;
        }
        return FarWait::Go;
    }
    if (!g_far_wait_since) {
        g_far_wait_since = now_ms | 1;
        g_far_wait_toast_at = 0;
        log::warn("Teleport: the {} server has not taken requests for {} ms: the far travel waits for it (no loading "
                  "screen meanwhile)", server_stall_is_ours() ? "our" : "host's", busy);
    } else if (now_ms - g_far_wait_since >= kFarWaitMaxMs) {
        log::warn("Teleport: the server is still stalled after {} s: the far travel is given up (hops from here)",
                  kFarWaitMaxMs / 1000);
        g_far_wait_since = 0;
        return FarWait::GiveUp;
    }
    if (!g_far_wait_toast_at || now_ms - g_far_wait_toast_at >= kFarWaitToastMs) {
        g_far_wait_toast_at = now_ms | 1;
        say(server_stall_is_ours() ? "Co-op\nYour game is busy right now: the trip to your friend waits until it answers"
                                   : "Co-op\nThe host's game is busy right now: the trip to your friend waits until it "
                                     "answers. You can move meanwhile");
    }
    return FarWait::Wait;
}

uint32_t own_field_key() {
    using Fn = uint32_t (*)();
    static Fn fn = [] {
        Fn f = nullptr;
        if (HMODULE m = GetModuleHandleW(L"cdmp.asi")) f = reinterpret_cast<Fn>(GetProcAddress(m, "cdmp_field_info_key"));
        return f;
    }();
    const uint32_t k = fn ? fn() : 0;
    return k ? k : g_field_key_from_host.load();
}

bool send(uint16_t op, const std::vector<uint8_t>& body);

bool far_travel_start(uint32_t key, const std::array<float, 3>& p) {
    std::vector<uint8_t> b(53, 0);
    const float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f}, scale[3] = {1.0f, 1.0f, 1.0f};
    std::memcpy(b.data() + 4, &key, 4);
    std::memcpy(b.data() + 12, p.data(), 12);
    std::memcpy(b.data() + 24, quat, 16);
    std::memcpy(b.data() + 40, scale, 12);
    b[52] = 1;
    return send(kOpReloadStageStartReq, b);
}

void teleport(uint32_t /*own*/) { request_move(g_checkpoint[0], g_checkpoint[1], g_checkpoint[2]); }

void send_hop(uint32_t own, const std::array<float, 3>& p) {
    std::vector<uint8_t> b(24, 0);
    std::memcpy(b.data(), &own, 4);
    std::memcpy(b.data() + 4, p.data(), 12);
    puppet_inject_s2c_batch({{kOpDoTeleportAck, b}});
}

// The fall guard (above request_move): a move arrived at `goal`; watch our height for a while.
void fall_watch_start(uint32_t now_ms, const std::array<float, 3>& goal) {
    g_fall_watch_until = (now_ms + kFallWatchMs) | 1;
    g_fall_goal = goal;
    g_fall_back = g_move_from;
    g_fall_back_set = g_move_from_set;
    g_fall_sample_at = now_ms;
    g_fall_n = 0;
}

// Network loop, every hop_tick: the watch after an arrival (one sample every kFallSampleMs), and the bad spot's end.
void fall_watch(uint32_t now_ms, uint32_t own) {
    // The bad spot ends early once we stand on it ourselves for 5 s (the place is in our game now).
    static uint32_t bad_next = 0;
    static float bad_last_y = 0.0f;
    if (static_cast<int32_t>(now_ms - bad_next) >= 0) {
        bad_next = now_ms + 1000;
        std::optional<std::array<float, 3>> p;
        {
            std::lock_guard lock(g_hop_mutex);
            if (g_bad_spot) p = g_bad_spot->p;
        }
        const auto at = p && own ? game::actor_world_position(own) : std::nullopt;
        if (!p) {
            g_bad_stand_since = 0;
        } else if (at) {
            const float dx = at->x - (*p)[0], dz = at->z - (*p)[2];
            const bool on = dx * dx + dz * dz <= 40.0f * 40.0f && at->y >= (*p)[1] - 10.0f &&
                            std::fabs(at->y - bad_last_y) < 1.0f;
            bad_last_y = at->y;
            if (!on) {
                g_bad_stand_since = 0;
            } else if (!g_bad_stand_since) {
                g_bad_stand_since = now_ms | 1;
            } else if (now_ms - g_bad_stand_since >= 5000) {
                g_bad_stand_since = 0;
                std::lock_guard lock(g_hop_mutex);
                g_bad_spot.reset();
                log::info("Teleport: we stand where our game had no ground before: it has it now; moves there are "
                          "allowed again");
            }
        }
    }
    if (!g_fall_watch_until) return;
    if (g_hop_goal || static_cast<int32_t>(now_ms - g_fall_watch_until) >= 0) {  // another move, or the time is up
        g_fall_watch_until = 0;
        return;
    }
    if (static_cast<int32_t>(now_ms - g_fall_sample_at) < 0) return;
    g_fall_sample_at = now_ms + kFallSampleMs;
    const auto at = own ? game::actor_world_position(own) : std::nullopt;
    if (!at) return;  // loading: no sample
    const float dx = at->x - g_fall_goal[0], dz = at->z - g_fall_goal[2];
    if (dx * dx + dz * dz > kFallNearM * kFallNearM) {
        g_fall_watch_until = 0;  // we moved off the spot: whatever happens from here is a normal move of ours
        return;
    }
    // While the game still holds us at the goal's height (its hold until our load is done on the host's server: cs130a
    // 8 s, the testers 25 s, a stalled host minutes), the window starts again: kFallWatchMs from the end of the hold.
    if (std::fabs(at->y - g_fall_goal[1]) < 2.0f) g_fall_watch_until = (now_ms + kFallWatchMs) | 1;
    const float y_1s = g_fall_n >= 4 ? g_fall_y[(g_fall_n - 4) % 5] : at->y;  // 4 samples = a second ago
    g_fall_y[g_fall_n % 5] = at->y;
    ++g_fall_n;
    const float drop = g_fall_goal[1] - at->y, speed = y_1s - at->y;
    if (drop < kFallDropM || speed < kFallSpeed) return;
    // The friend stands up there still (its word within 3 s): it has ground where we have none. A friend that dives or
    // falls with us is no sign of missing ground; the watch goes on.
    const uint32_t f_at = g_friend_at.load(std::memory_order_relaxed);
    const float fx = g_friend_x.load(std::memory_order_relaxed) - g_fall_goal[0];
    const float fz = g_friend_z.load(std::memory_order_relaxed) - g_fall_goal[2];
    const float fy = g_friend_y.load(std::memory_order_relaxed);
    const bool test = g_fall_test.load(std::memory_order_relaxed);
    if (!test && (!f_at || cdmp::now_ms() - f_at >= 3000 || fx * fx + fz * fz > kFallFriendM * kFallFriendM ||
                  fy < g_fall_goal[1] - kFallFriendDy)) {
        return;
    }
    g_fall_watch_until = 0;
    {
        std::lock_guard lock(g_hop_mutex);
        g_bad_spot = BadSpot{g_fall_goal, cdmp::now_ms() | 1};
        g_bad_refused_at = 0;
    }
    g_bad_stand_since = 0;
    g_confirm_at = 0;  // the far travel's "back where we started" check must not send us up again,
    g_teleport_at = 0;  // nor the revive's checkpoint checks
    g_teleport_due = false;
    log::warn("Teleport: {:.0f} m below the move's goal ({:.0f}, {:.0f}, {:.0f}) and falling {:.0f} m/s straight down "
              "while the friend stands up there ({:.0f} m up{}): our game has no ground there (a place of the friend's "
              "story?): {}", drop, g_fall_goal[0], g_fall_goal[1], g_fall_goal[2], speed, fy,
              test ? "; TEST ONLY: [dev] fall_guard_test, not checked" : "",
              g_fall_back_set ? std::format("back to where the move started ({:.0f}, {:.0f}, {:.0f})", g_fall_back[0],
                                            g_fall_back[1], g_fall_back[2])
                              : std::string("where the move started is not known: left as it is"));
    if (g_fall_back_set) request_move(g_fall_back[0], g_fall_back[1], g_fall_back[2], /*check_bad=*/false);
    say("Co-op\nYour game has no ground where your friend is (likely a place from their story, like an Abyss island), "
        "so you were brought back");
}

// Network loop: start a newly asked move, and send the next hop when due.
void hop_tick(uint32_t now_ms) {
    {
        std::lock_guard lock(g_hop_mutex);
        if (g_hop_target) {
            g_hop_goal = g_hop_target;
            g_hop_target.reset();
            g_arrive_m = g_arrive_next.exchange(kArrivedM);
            g_hop_fails = 0;
            g_hop_at = now_ms;
            g_hop_last = {1e9f, 0.0f, 1e9f};
            g_far_try = true;
            g_far_until = g_far_ack_at = 0;
            g_far_used = false;
            g_far_in_stall = false;
            g_far_wait_since = 0;
            g_confirm_at = 0;
            g_confirm_tries = 2;
            g_confirm_waits = 0;
            g_move_from_set = false;  // the first position read for this move (below)
            g_fall_watch_until = 0;   // a new move ends the last one's watch
        }
    }
    const uint32_t own = capture_own_player_key();
    fall_watch(now_ms, own);
    if (!g_hop_goal) {
        if (!g_confirm_at || static_cast<int32_t>(now_ms - g_confirm_at) < 0) return;
        const auto here = own ? game::actor_world_position(own) : std::nullopt;
        if (!here) {  // loading: look again in a moment (not for ever)
            g_confirm_at = ++g_confirm_waits <= 15 ? (now_ms + 2000) | 1 : 0;
            return;
        }
        g_confirm_at = 0;
        const auto flat = [&](const std::array<float, 3>& p) {
            const float x = p[0] - here->x, z = p[2] - here->z;
            return std::sqrt(x * x + z * z);
        };
        const float to_goal = flat(g_confirm_goal), to_start = flat(g_far_from);
        if (to_goal > kFarM && to_start < kFarM && g_confirm_tries > 0) {
            --g_confirm_tries;
            log::warn("Teleport: {} s after the far travel we are back where we started ({:.0f} m from the goal; a load "
                      "that was still finishing put us back): the same travel again ({} more after this)",
                      kFarConfirmMs / 1000, to_goal, g_confirm_tries);
            g_hop_goal = g_confirm_goal;
            g_hop_fails = 0;
            g_hop_at = now_ms;
            g_hop_last = {1e9f, 0.0f, 1e9f};
            g_far_try = true;
            g_far_used = false;
        } else if (to_goal > kFarM) {
            log::warn("Teleport: {} s after the far travel we are {:.0f} m from the goal and {:.0f} m from where we "
                      "started: left as it is", kFarConfirmMs / 1000, to_goal, to_start);
        }
        return;
    }
    // Far travel under way: our client's loading starts a moment after the request, and while it loads there may be
    // no position to read.
    if (g_far_until) {
        if (g_far_ack_at && static_cast<int32_t>(now_ms - g_far_ack_at) >= 0) {
            g_far_ack_at = 0;
            std::vector<uint8_t> b(8, 0);
            std::memcpy(b.data(), &own, 4);
            puppet_inject_s2c_batch({{kOpTeleportRevivePointAck, b}});
            log::info("Teleport: far travel: our loading screen is started ({})", kOpTeleportRevivePointAck);
        }
        // The server stalled while we travel (Borque 25 Nov): no second request and no timeout while it lasts (our
        // request waits in it; the loading screen ends when it takes it), counted again from when it answers.
        if (const uint32_t busy = server_busy_ms(now_ms); busy && now_ms - g_far_since < kFarWaitMaxMs) {
            if (!g_far_in_stall) {
                g_far_in_stall = true;
                log::warn("Teleport: far travel under way and the server has not taken requests for {} ms: no second "
                          "request and no timeout until it answers", busy);
            }
            g_far_until = (now_ms + kFarTimeoutMs) | 1;
        } else if (g_far_in_stall) {
            // It answers again, with a backlog to work through first (Borque's host: 26,353 Reqs queued): our request
            // is in it, so no second one for this travel (his first attempt's two reloads back to back); the timeout
            // counts from now, then hops as before.
            g_far_in_stall = false;
            g_far_again = true;
            g_far_stuck_from = now_ms;
            log::info("Teleport: the server takes requests again during the far travel ({:.1f} s after asking): our "
                      "request is in its queue, no second one", (now_ms - g_far_since) / 1000.0);
        }
        if (static_cast<int32_t>(now_ms - g_far_until) >= 0) {
            log::warn("Teleport: far travel did not take us there in {} s: hops from where we are",
                      kFarTimeoutMs / 1000);
            g_far_until = 0;
            g_hop_fails = 0;
            g_hop_last = {1e9f, 0.0f, 1e9f};
            g_hop_at = now_ms;
        }
    }
    if (static_cast<int32_t>(now_ms - g_hop_at) < 0) return;
    const auto at = own ? game::actor_world_position(own) : std::nullopt;
    if (!at) return;
    if (!g_move_from_set) {
        g_move_from = {at->x, at->y, at->z};
        g_move_from_set = true;
    }
    const auto& goal = *g_hop_goal;
    const float dx = goal[0] - at->x, dz = goal[2] - at->z;
    const float left = std::sqrt(dx * dx + dz * dz);
    if (g_far_until) {
        if (left > kFarLandedM) {  // still on the way (the loading screen), or it did not start
            const float sx = at->x - g_far_from[0], sz = at->z - g_far_from[2];
            if (!g_far_again && !g_far_in_stall && now_ms - g_far_stuck_from >= kFarStuckMs &&
                sx * sx + sz * sz < kFarMovedM * kFarMovedM) {
                g_far_again = true;
                if (far_travel_start(g_far_key, goal)) g_far_ack_at = (now_ms + kFarAckDelayMs) | 1;
                log::warn("Teleport: {} s after asking we still stand where we started: the game's travel is asked "
                          "for once more", (now_ms - g_far_since) / 1000);
            }
            return;
        }
        log::info("Teleport: far travel took {:.1f} s; {:.0f} m left for the hops", (now_ms - g_far_since) / 1000.0, left);
        g_far_until = 0;
        g_hop_fails = 0;
        g_hop_last = {1e9f, 0.0f, 1e9f};
    }
    if (g_far_try) {
        uint32_t key = left > kFarM ? own_field_key() : 0;
        bool gave_up = false;
        if (key) {
            const FarWait w = far_wait_for_server(now_ms);
            if (w == FarWait::Wait) {  // the server is stalled: nothing is sent yet; looked at again in a second
                g_hop_at = now_ms + 1000;
                return;
            }
            gave_up = w == FarWait::GiveUp;  // hops from here (said by far_wait_for_server)
            if (gave_up) key = 0;
        }
        g_far_try = false;
        if (key && far_travel_start(key, goal)) {
            g_far_used = true;
            // Right after a stall the server still has a backlog: one request only (cs127b's second reload trip).
            g_far_again = stall_ended_recently(now_ms);
            g_far_in_stall = false;
            g_far_key = key;
            g_far_from = {at->x, at->y, at->z};
            g_far_since = now_ms;
            g_far_stuck_from = now_ms;
            g_far_ack_at = (now_ms + kFarAckDelayMs) | 1;
            g_far_until = (now_ms + kFarTimeoutMs) | 1;
            log::info("Teleport: {:.0f} m: the game's own travel (field 0x{:08X}, to ({:.0f}, {:.0f}, {:.0f})){}", left, key,
                      goal[0], goal[1], goal[2], g_far_again ? "; right after a stall of the server: one request only" : "");
            return;
        }
        if (left > kFarM && !gave_up) {
            log::info("Teleport: {:.0f} m in hops (the field is not known{})", left, key ? ", or the request was not sent" : "");
        }
    }
    if (left <= g_arrive_m) {
        log::info("Teleport: arrived ({:.0f}, {:.0f})", at->x, at->z);
        if (g_far_used) {
            g_confirm_goal = goal;
            g_confirm_at = (now_ms + kFarConfirmMs) | 1;
            g_confirm_waits = 0;
        }
        fall_watch_start(now_ms, goal);
        g_hop_goal.reset();
        return;
    }
    const float mx = at->x - g_hop_last[0], mz = at->z - g_hop_last[2];
    if (g_hop_last[0] < 1e8f && mx * mx + mz * mz < 1.0f) {
        if (++g_hop_fails >= kHopFailLimit) {
            log::warn("Teleport: stopped {:.0f} m short (hops did not take)", left);
            g_hop_goal.reset();
            return;
        }
    } else {
        g_hop_fails = 0;
    }
    const HopTry& t = kHopTries[g_hop_fails];
    const float step = std::min(t.len, left) / left;
    const float ux = dx / left, uz = dz / left;  // the way to the goal; (uz, -ux) is to its right
    const float y = (left <= kHopM ? goal[1] : at->y) + t.dy;
    send_hop(own, {at->x + dx * step + uz * t.side, y, at->z + dz * step - ux * t.side});
    g_hop_last = {at->x, at->y, at->z};
    g_hop_at = now_ms + kHopMs;
}

bool send(uint16_t op, const std::vector<uint8_t>& body);

// T26 (2026-11-22, docs/research/t26-death-penalty.md): we get up by ourselves after a death (the prompt's instant or
// checkpoint choice; never a friend's revive or a boss's "we get up"). The host's server applies ITS death penalty
// to our own money (the native's trade_probe worker, entry 0xC0DE0009; native death_penalty_logic.h's request_body):
// a 3104 on our own connection, giver = receiver = our character. On a host with the penalty off it only logs.
constexpr uint16_t kOpMoveItem = 3104;
constexpr uint32_t kDeathPenaltyEntry = 0xC0DE0009;
uint32_t g_own_deaths = 0;
std::atomic<int> g_penalty_shown{0};
void ask_death_penalty(uint32_t own) {
    if (!kNativeLayer || !revive_partner_present()) return;
    ++g_own_deaths;
    std::vector<uint8_t> b(30, 0);
    const uint16_t none = 0xFFFF;
    const uint64_t one = 1;
    std::memcpy(&b[0], &g_own_deaths, 4);
    std::memcpy(&b[4], &none, 2);
    std::memcpy(&b[6], &one, 8);
    std::memcpy(&b[14], &own, 4);
    std::memcpy(&b[18], &own, 4);
    std::memcpy(&b[22], &none, 2);
    std::memcpy(&b[24], &none, 2);
    std::memcpy(&b[26], &kDeathPenaltyEntry, 4);
    const bool ok = send(kOpMoveItem, b);
    log::info("Revive: death {}: the host's death penalty asked for our own money{}", g_own_deaths, ok ? "" : " (NOT sent)");
}

}  // namespace

void revive_set_death_penalty(int percent) {
    const int p = percent == 10 || percent == 25 || percent == 50 || percent == 100 ? percent : 0;
    if (g_penalty_shown.exchange(p) != p) log::info("Revive: the host's death penalty this session: {} %", p);
}
int revive_death_penalty() { return g_penalty_shown.load(); }

namespace {

// The checkpoint revive (see the header). `move`: also move the client to the checkpoint.
void ask_checkpoint(uint32_t own, uint32_t now_ms, bool move) {
    expect_revive(own, kCheckpoint);
    const bool ok = send(kOpReviveLoadingStartReq, {0x04, kCheckpoint, 0x00});
    g_chosen_at = now_ms ? now_ms : 1;
    g_loading_req_at = (now_ms + kLoadingStepMs) | 1;
    g_checkpoint_asked = move;
    log::info("Revive: checkpoint revive{}, step 1{}", move ? "" : " (staying here)", ok ? "" : " (NOT sent)");
}

bool send(uint16_t op, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> pkt(5 + body.size());
    const uint16_t len = static_cast<uint16_t>(body.size());
    std::memcpy(pkt.data(), &op, 2);
    pkt[2] = 0;
    std::memcpy(pkt.data() + 3, &len, 2);
    if (!body.empty()) std::memcpy(pkt.data() + 5, body.data(), body.size());
    return sniffer_inject_c2s(std::move(pkt));
}

// The revive choices: Q / U on a keyboard, X / Y (Square / Triangle) on a pad (input/actions.h).
bool pressed(int vk, bool& was, bool focused) {
    const bool down = input::action_down(vk == 'Q' ? input::Action::ReviveNow : input::Action::ReviveCheckpoint, focused);
    const bool edge = down && !was;
    was = down;
    return edge;
}

}  // namespace

void revive_set_boss_wait(bool on) { g_boss_wait = on; }

bool revive_in_place(uint32_t now_ms) {
    const uint32_t own = capture_own_player_key();
    if (!own || !g_dead_since) return false;
    // Co-op+ (2026-11-05 playtest): the friend's revive arrived while the host's own checkpoint respawn was under way
    // (its loading burst); the two sequences together left the host with no HUD, no pause menu and no Ride prompt.
    // One respawn at a time: once we chose (or a load is on), the friend's revive is not applied.
    if (g_chosen_at || g_loading_req_at || g_revive_req_at || sniffer_world_loading()) {
        log::info("Revive: the other player revived us, but our own respawn is already under way; not applied");
        return false;
    }
    ask_checkpoint(own, now_ms, /*move=*/false);
    log::info("Revive: the other player revived us: up where we fell");
    return true;
}

std::atomic<bool> g_death_withheld{false};
std::atomic<uint64_t> g_death_withheld_at{0};

// ---- the downed pose (2026-11-11; research: build/decomp/notes_downed_pose.md, local) ----
// Keeping our own death notice (2985) from the client keeps the HUD and menus, but the character then STANDS at 0 HP
// on its own player's screen. The client's "on dead" (exe+0x6E7640) does two things: the actor part (the event
// "OnBattle_HandleDeadSelf" = the fall, collision, the dead flag) and, for the locally controlled character only,
// one call of the 5-byte jmp stub exe+0xBA3700, which puts the UI into its "gameover" sub-phase (HUD hidden, menus
// locked, the game's own death screen). With the stub skipped while we are down in co-op, the notice can go to the
// client: the character falls and stays down, the UI stays ours. The game's two revive packets stand it up again.
constexpr uintptr_t kGameOverStub = 0xBDBCD0;
constexpr uint8_t kGameOverStubBytes[5] = {0xE9, 0x5B, 0x67, 0xDF, 0x08};  // build 1.0.0.3576
SafetyHookInline g_gameover;
std::atomic<bool> g_pose_down{false};
std::atomic<uint32_t> g_gameover_skipped{0};

void hooked_gameover(void* field) {
    if (g_pose_down.load(std::memory_order_relaxed)) {
        const uint32_t n = g_gameover_skipped.fetch_add(1) + 1;
        if (n <= 5) log::info("Revive: the game's own game-over state is skipped (we are down in co-op; {} so far)", n);
        return;
    }
    g_gameover.call<void>(field);
}

bool revive_death_withheld() { return g_death_withheld.load(); }

RevivePrompt revive_prompt_tick(bool connected, float hp, float hp_max, std::optional<float> partner_hp, bool focused,
                                uint32_t now_ms) {
    // Up again (the notice is kept a moment before our HP reads 0, so the first second does not count), or the
    // friend is gone.
    if (g_death_withheld.load() &&
        (!connected || (hp > 0.0f && GetTickCount64() - g_death_withheld_at.load() > 1500))) {
        g_death_withheld = false;
        if (g_pose_down.exchange(false)) log::info("Revive: up again: the game-over skip is off");
    }
    hop_tick(now_ms);
    // The checkpoint revive's second step goes in on time even if we are no longer "dead" by then.
    if (g_loading_req_at && static_cast<int32_t>(now_ms - g_loading_req_at) >= 0) {
        g_loading_req_at = 0;
        const bool ok = send(kOpReviveLoadingReq, {kCheckpoint});
        g_revive_req_at = (now_ms + 400) | 1;
        log::info("Revive: checkpoint revive, step 2{}", ok ? "" : " (NOT sent)");
    }
    if (g_revive_req_at && static_cast<int32_t>(now_ms - g_revive_req_at) >= 0) {
        g_revive_req_at = 0;
        if (sniffer_world_loading()) {
            log::info("Revive: checkpoint revive: the game is loading the checkpoint itself");
            return RevivePrompt::None;
        }
        std::vector<uint8_t> b(8, 0);
        const uint32_t own = capture_own_player_key();
        std::memcpy(b.data(), &own, 4);
        b[4] = kCheckpoint;
        // Queued together: once GameLoadingStartReq is seen, the sniffer holds anything new of ours.
        const bool ok = own && send(kOpGameLoadingStartReq, {}) && send(kOpReviveReq, b);
        log::info("Revive: checkpoint revive, step 3{}", ok ? "" : " (NOT sent)");
    }
    const bool dead = hp_max > 0.0f && hp <= 0.0f;
    if (!dead && g_teleport_due.exchange(false)) {
        g_teleport_at = (now_ms + kTeleportCheckMs) | 1;
        g_teleport_tries = 1;
    }
    if (g_teleport_at && static_cast<int32_t>(now_ms - g_teleport_at) >= 0) {
        g_teleport_at = 0;
        const uint32_t own = capture_own_player_key();
        const auto at = own ? game::actor_world_position(own) : std::nullopt;
        const float dx = at ? at->x - g_checkpoint[0] : 1e9f, dz = at ? at->z - g_checkpoint[2] : 1e9f;
        if (dx * dx + dz * dz <= kTeleportNearM * kTeleportNearM) {
            if (g_teleport_tries) log::info("Revive: at the checkpoint");
        } else if (own && g_teleport_tries++ < kTeleportTries) {
            teleport(own);
            g_teleport_at = (now_ms + kTeleportCheckMs) | 1;
            log::info("Revive: to the checkpoint again ({:.1f}, {:.1f}, {:.1f}) (try {})", g_checkpoint[0],
                      g_checkpoint[1], g_checkpoint[2], g_teleport_tries);
        } else {
            log::warn("Revive: still not at the checkpoint after {} tries; left where we are", kTeleportTries);
        }
    }
    if (!dead) {
        if (g_dead_since && g_chosen_at) log::info("Revive: we are alive again");
        g_dead_since = g_chosen_at = g_boss_revive_at = 0;
        g_boss_hold = false;
        g_q_was = g_u_was = false;
        return RevivePrompt::None;
    }
    if (!g_dead_since) {
        g_dead_since = now_ms ? now_ms : 1;
        // A9: died during a boss fight -> wait for the other player (decided once, at the death).
        g_boss_hold = g_boss_wait && connected && partner_hp && boss_fight_active(kBossFightWindowMs);
        if (g_boss_hold) log::info("Revive: we died in a boss fight: waiting for the other player");
    }
    // Only where the game's screen is known to stay away, and never over it or another menu.
    // Co-op+ (2026-11-05): once a copy has been loaded the game's own screen never comes again, connected or not (a
    // death after the friend left had no screen at all), so the prompt is for that whole session.
    // Shared world (2026-11-11, tcB385): no copy is ever spawned there, so this test never passed and a downed player
    // whose friend did not revive them in time had no way up at all. There the kept death notice is the sign that the
    // game's own screen will not come.
    (void)connected;
    const bool no_game_screen = puppet_ever_spawned() || (kNativeLayer && revive_death_withheld());
    if (!no_game_screen || game::ui_menu_open()) return RevivePrompt::None;
    if (now_ms - g_dead_since < kWaitForGameMs) return RevivePrompt::None;
    const uint32_t own = capture_own_player_key();
    if (!own) return RevivePrompt::None;

    bool checkpoint_only = false;
    if (g_boss_hold) {
        if (g_boss_revive_at) {
            // The boss is down and we were sent back up; still dead after a while: ask once more.
            if (static_cast<int32_t>(now_ms - g_boss_revive_at) >= 0) {
                g_boss_revive_at = (now_ms + kBossReviveRetryMs) | 1;
                ask_checkpoint(own, now_ms, /*move=*/false);
                log::info("Revive: the boss is down: we get up (again)");
            }
            return RevivePrompt::None;
        }
        if (!partner_hp) {
            g_boss_hold = false;
            log::info("Revive: the other player is gone: revive as usual");
        } else if (boss_killed_within(now_ms - g_dead_since + 2000)) {
            // Up where we fell, full health: the server's checkpoint revive without the move.
            ask_checkpoint(own, now_ms, /*move=*/false);
            g_boss_revive_at = (now_ms + kBossReviveRetryMs) | 1;
            log::info("Revive: the other player beat the boss: we get up");
            return RevivePrompt::None;
        } else if (*partner_hp <= 0.0f) {
            checkpoint_only = true;  // everyone is down: the fight starts again from the checkpoint
        } else if (!boss_fight_active(kBossFightWindowMs)) {
            g_boss_hold = false;
            log::info("Revive: the boss fight is over without a kill: revive as usual");
        } else {
            return RevivePrompt::WaitBoss;
        }
    }
    if (g_chosen_at && now_ms - g_chosen_at < kRetryAfterMs) return RevivePrompt::None;
    if (!checkpoint_only && pressed('Q', g_q_was, focused)) {
        std::vector<uint8_t> b(8, 0);
        std::memcpy(b.data(), &own, 4);
        b[4] = 0;
        b[5] = kInstant;
        ask_death_penalty(own);
        const bool ok = send(kOpReviveReq, b);
        g_chosen_at = now_ms ? now_ms : 1;
        log::info("Revive: instant revive asked{}", ok ? "" : " (NOT sent)");
        return RevivePrompt::None;
    }
    if (pressed('U', g_u_was, focused)) {
        ask_death_penalty(own);
        ask_checkpoint(own, now_ms, /*move=*/true);
        return RevivePrompt::None;
    }
    return checkpoint_only ? RevivePrompt::CheckpointOnly : RevivePrompt::Choice;
}

void revive_set_field_key(uint32_t key) {
    if (g_field_key_from_host.exchange(key) != key) log::info("Teleport: the host's field is 0x{:08X} (for far travel)", key);
}
uint32_t revive_own_field_key() { return own_field_key(); }

uint32_t revive_server_stalled_ms() { return native_stalled_ms(); }

// 2026-11-25 (cs127c): the joined player's own load on the host's server. The host reads its native base's tracker; the
// guest keeps the host's word (coop_lobby's kMsgGuestLoad, every 3 s and on a change).
namespace {
std::atomic<int> g_own_load{-1};
std::atomic<uint32_t> g_own_load_at{0};
}  // namespace
int revive_user2_load_settled() {
    using Fn = int (*)();
    static Fn fn = [] {
        Fn f = nullptr;
        if (HMODULE m = GetModuleHandleW(L"cdmp.asi")) f = reinterpret_cast<Fn>(GetProcAddress(m, "cdmp_user2_load_settled"));
        return f;
    }();
    return fn ? fn() : -1;
}
void revive_set_own_load_settled(int state) {
    const int was = g_own_load.exchange(state, std::memory_order_relaxed);
    g_own_load_at.store(cdmp::now_ms() | 1, std::memory_order_relaxed);
    if (was != state) {
        log::info("Teleport: the host says our own load on its server is {}",
                  state == 1 ? "done" : state == 0 ? "under way" : "not seen yet");
    }
}
bool revive_own_load_settled() {
    // No word at all: as before. Otherwise the last word counts, however old (cs127d: a 10 s freshness rule let the
    // trip go during the stall although the host's tracker still said "under way"; the join's own 90 s cap is the
    // safety net).
    if (!g_own_load_at.load(std::memory_order_relaxed)) return true;
    return g_own_load.load(std::memory_order_relaxed) != 0;
}
void revive_set_host_server_busy(uint32_t ms) {
    const uint32_t was = g_host_busy_ms.exchange(ms, std::memory_order_relaxed);
    g_host_busy_at.store(ms ? (cdmp::now_ms() | 1) : 0, std::memory_order_relaxed);
    if (was && !ms) g_stall_ended_at.store(cdmp::now_ms() | 1, std::memory_order_relaxed);
    if ((was != 0) != (ms != 0)) {
        if (ms) log::warn("Teleport: the host says its server has not taken requests for {} ms", ms);
        else log::info("Teleport: the host says its server takes requests again");
    }
}
std::string revive_take_toast() {
    std::lock_guard lock(g_toast_mutex);
    return std::exchange(g_toast, std::string());
}

void revive_set_fall_test(bool on) {
    g_fall_test.store(on, std::memory_order_relaxed);
    if (on) log::warn("Config: TEST ONLY [dev] fall_guard_test=1: the fall guard does not ask whether the friend stands up there");
}

void revive_note_friend(float x, float y, float z) {
    g_friend_x.store(x, std::memory_order_relaxed);
    g_friend_y.store(y, std::memory_order_relaxed);
    g_friend_z.store(z, std::memory_order_relaxed);
    g_friend_at.store(cdmp::now_ms() | 1, std::memory_order_relaxed);
}

bool revive_teleport_to(float x, float y, float z, float arrive_m) {
    if (!capture_own_player_key()) return false;
    g_arrive_next.store(arrive_m > 0.1f ? arrive_m : kArrivedM);
    request_move(x, y, z);
    return true;
}

void revive_dev_teleport(float x, float y, float z) {
    request_move(x, y, z);
    log::info("Dev: teleport to ({:.1f}, {:.1f}, {:.1f})", x, y, z);
}

bool revive_filter_s2c(const uint8_t* packet, size_t size) {
    // Co-op+ (2026-11-05 playtest: the host's HUD, pause menu and Ride prompt were gone after a death). Our own
    // NotifyHandleDeadAck (2985: u32 dead | u32 killer | ...) puts the client into its death state: HUD hidden, menus
    // locked, until the game's death screen ends it. Once the other player's copy has been loaded that screen never
    // opens (see the header), so nothing ever ended it, whichever revive followed. While our prompt stands in for the
    // screen, the notice is kept from the client: the server has us dead as before (our prompt, the friend's revive
    // and the server's revive all work), the client keeps its HUD and menus. Verified on one PC (dt9, dt11: HUD
    // through the death, the menu opens after the revive). Alone (no copy ever loaded) nothing changes.
    // On the native base (the layer, tc50) no copy is ever spawned by us: a friend connected and in reach counts.
    const bool friend_with_us = puppet_ever_spawned() || (kNativeLayer && revive_partner_present());
    if (!packet || size < 9 || (packet[0] | packet[1] << 8) != kOpNotifyHandleDeadAck || !friend_with_us) {
        return false;
    }
    uint32_t dead = 0;
    std::memcpy(&dead, packet + 5, 4);
    if (!dead || dead != capture_own_player_key()) return false;
    g_death_withheld_at = GetTickCount64();
    g_death_withheld = true;
    if (kNativeLayer && g_gameover) {
        // The downed pose: the notice goes through, only the UI's game-over state is skipped (above).
        g_pose_down = true;
        log::info("Revive: our death notice goes to the client (the character falls); its game-over state is skipped, "
                  "our revive screen stands in");
        return false;
    }
    log::info("Revive: our death notice is kept from the client (its death state would lock the HUD and menus)");
    return true;
}

bool revive_downed_pose_install(bool on) {
    if (!on || !kNativeLayer) {
        log::info("Revive: downed pose off{}", on ? " (not the shared world)" : " ([coop] downed_pose=0)");
        return false;
    }
    if (g_gameover) return true;
    const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto* stub = reinterpret_cast<uint8_t*>(exe + kGameOverStub);
    uint8_t have[5]{};
    if (!game::safe_read(reinterpret_cast<uintptr_t>(stub), have, sizeof have) ||
        std::memcmp(have, kGameOverStubBytes, sizeof have) != 0) {
        log::warn("Revive: downed pose off: the game-over stub at exe+0x{:X} is not the expected code (another game "
                  "build?): a downed player keeps standing, everything else works", kGameOverStub);
        return false;
    }
    g_gameover = safetyhook::create_inline(stub, reinterpret_cast<void*>(hooked_gameover));
    log::info("Revive: downed pose {} (exe+0x{:X}: the UI's game-over state is skipped while we are down in co-op)",
              g_gameover ? "ready" : "NOT hooked", kGameOverStub);
    return static_cast<bool>(g_gameover);
}

bool revive_pose_down() { return g_pose_down.load(std::memory_order_relaxed); }

void revive_observe_s2c(const uint8_t* packet, size_t size) {
    if (size < 5 + 40 || (packet[0] | packet[1] << 8) != kOpLoadingFieldDataAck || !g_checkpoint_asked) return;
    uint32_t who = 0;
    std::memcpy(&who, packet + 6, 4);
    if (!who || who != capture_own_player_key()) return;
    std::memcpy(g_checkpoint, packet + 5 + 28, 12);
    g_checkpoint_asked = false;
    // At once, with the revive (best effort: revive_prompt.h).
    teleport(who);
    log::info("Revive: to the checkpoint at ({:.1f}, {:.1f}, {:.1f})", g_checkpoint[0], g_checkpoint[1], g_checkpoint[2]);
    g_teleport_due = true;  // checked (and tried again) once we are alive
}

}  // namespace cdmp::net
