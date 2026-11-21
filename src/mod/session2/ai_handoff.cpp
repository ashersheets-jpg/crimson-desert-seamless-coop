// Co-op+ (2026-11-11, build 1.0.0.3576): ai_handoff.h.
#include "mod/session2/ai_handoff.h"

#include "mod/session2/anti_grief.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <format>
#include <unordered_map>
#include <vector>

#include "mod/game/actor_lookup.h"
#include "mod/log.h"
#include "mod/net/remote_puppet.h"
#include "mod/net/troc_sniffer.h"
#include "mod/session2/damage_range.h"
#include "mod/session2/lootr_logic.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

enum class Mode : uint8_t { Off, Probe, On };
Mode g_mode = Mode::Off;
bool g_guest = false;
bool g_fight = false;  // the fight rule (ai_handoff.h)

constexpr uint32_t kHostCharacter = 0xA0100001;
constexpr uint16_t kOpChangeAiControlAck = 2152;
constexpr uint32_t kPrivateMagic = 0xC0DEA1C7;   // our 12-byte form of 2660: magic | actor key | new controller
constexpr uint32_t kTickEntry = 0xC0DE0008;      // trade_probe's entry for the server-thread check
constexpr uintptr_t kServerActorMgrSlot = 0x724B860;
constexpr uintptr_t kClientWorldSlot = 0x724B350;
constexpr uintptr_t kFnPosition = 0x5D8880;      // (transform comp, buf)
constexpr uintptr_t kFnAiSessionBump = 0x2BCD790, kFnMoveSessionBump = 0x2BCF0D0, kFnActionReset = 0x229D880;
constexpr uintptr_t kFnClientControlMode = 0x5DAE40;  // (client actor): recomputes the control mode byte actor+0xC8
constexpr float kSector = 0.015625f;             // 1 / 64 m
constexpr float kFarM = 60.0f, kNearM = 30.0f;   // controller farther than kFarM, another player within kNearM
constexpr int kNeedChecks = 2;                   // the same answer on two checks in a row (about 2 s)
constexpr int kMaxPerTick = 48;
constexpr float kFightM = 20.0f;                   // the fight rule: the fighter within 20 m of the NPC
constexpr uint64_t kFightRecentMs = 4000, kFightQuietMs = 6000, kFightCooldownMs = 10000;

std::atomic<uint64_t> c_ticks{0}, c_npcs{0}, c_orphans{0}, c_handed{0}, c_would{0}, c_failed{0}, c_back{0};
std::atomic<uint64_t> c_client_took{0}, c_client_gave{0}, c_client_unknown{0}, c_client_dropped{0};
std::atomic<uint64_t> c_fight_out{0}, c_fight_back{0}, c_fight_seen{0};
// The departure sweep (2026-11-21): the far-controller rule is off in the release (mode 0) and the checks below run
// only while a second player is in the world, so an NPC the other player's game ran (a fight, a ride, a catch, a talk,
// or born near that player) when it LEFT (its goodbye under way, or its session gone: a lost link, a crash) or began
// a LOAD (fast travel, a respawn: its move phase in a real load) keeps a controller whose game no longer has it: it
// would stand still for the host. Then one server check gives every such NPC in the host's own 3x3 block of 64 m sectors (what the
// host's client surely has, ai_handoff.h) back to the host, kMaxPerTick a second; after a departure (not a load) a
// check every 10 s while the host is alone, at most 10 minutes, until none is left anywhere (the host may walk up to
// the others). Only the host's block: NPCs born under the other player at the destination of its load, given to a
// host whose game does not have them, would stand still for it when it arrives. The other rules never run while the
// other player is not in the world (as before the sweep).
std::atomic<bool> g_left_sweep{false}, g_left_watch{false};
std::atomic<uint64_t> c_left_back{0}, c_left_sweeps{0}, c_left_failed{0};
std::atomic<uint32_t> g_local_user{0};
// The ride rule's table (ai_handoff_note_ride): (mount key << 32) | rider key, 0 = free. Lock-free, any thread.
constexpr size_t kRides = 8;
std::array<std::atomic<uint64_t>, kRides> g_rides{};
std::atomic<uint64_t> c_ride_noted{0}, c_ride_out{0}, c_ride_full{0};
uint32_t ridden_by(uint32_t mount_key) noexcept {
    for (const auto& r : g_rides) {
        const uint64_t v = r.load(std::memory_order_relaxed);
        if (v && static_cast<uint32_t>(v >> 32) == mount_key) return static_cast<uint32_t>(v);
    }
    return 0;
}
// The catch rule's table (ai_handoff_note_catch): (NPC key << 32) | catcher key, and when it was noted (ms).
// Borque 18 Nov (host log): interactions 0xF42BE / 0xF42CD on small animals, accepted for user 2, the animal's AI
// still on the host 1-1.5 s later.
constexpr uint32_t kCatchInteractions[] = {0xF42BE, 0xF42CD};
constexpr uint64_t kCatchMs = 30000;
std::array<std::atomic<uint64_t>, kRides> g_catches{}, g_catch_ms{};
std::atomic<uint64_t> c_catch_noted{0}, c_catch_out{0}, c_catch_full{0};
// The talk rule's table (ai_handoff_note_catch, the interactions that are not catches): (NPC key << 32) | talker key,
// when it was noted (ms), and the controller the NPC had before the talker's game took it (0 = not handed yet).
constexpr uint64_t kTalkGraceMs = 4000, kTalkMaxMs = 600000;
constexpr float kTalkEndM = 6.0f;  // the talker this far from the NPC (after the grace): the talk is over
std::array<std::atomic<uint64_t>, kRides> g_talks{}, g_talk_ms{};
std::array<std::atomic<uint32_t>, kRides> g_talk_from{};
std::atomic<uint64_t> c_talk_noted{0}, c_talk_out{0}, c_talk_back{0}, c_talk_ended{0}, c_talk_full{0}, c_talk_skip{0};
std::atomic<bool> g_talk_on{true};
size_t talk_slot(uint32_t npc_key) noexcept {
    for (size_t i = 0; i < kRides; ++i) {
        const uint64_t v = g_talks[i].load(std::memory_order_acquire);
        if (v && static_cast<uint32_t>(v >> 32) == npc_key) return i;
    }
    return kRides;
}
// Passengers (ai_handoff_note_passenger): (vehicle key << 32) | rider key, 0 = free.
std::array<std::atomic<uint64_t>, kRides> g_passengers{};
std::atomic<uint64_t> c_pass_noted{0}, c_pass_skip{0}, c_pass_full{0};
uint32_t passenger_vehicle(uint32_t rider_key) noexcept {
    for (const auto& p : g_passengers) {
        const uint64_t v = p.load(std::memory_order_relaxed);
        if (v && static_cast<uint32_t>(v) == rider_key) return static_cast<uint32_t>(v >> 32);
    }
    return 0;
}
void drop_passenger(uint32_t rider_key) noexcept {
    for (auto& p : g_passengers) {
        uint64_t v = p.load(std::memory_order_relaxed);
        if (v && static_cast<uint32_t>(v) == rider_key) p.compare_exchange_strong(v, 0);
    }
}
constexpr float kPassengerM = 6.0f;  // a seated passenger is within this of its vehicle's origin
uint32_t caught_by(uint32_t npc_key, uint64_t now) noexcept {
    for (size_t i = 0; i < kRides; ++i) {
        const uint64_t v = g_catches[i].load(std::memory_order_acquire);
        if (v && static_cast<uint32_t>(v >> 32) == npc_key && now - g_catch_ms[i].load(std::memory_order_relaxed) < kCatchMs)
            return static_cast<uint32_t>(v);
    }
    return 0;
}

// The departure sweep: the other player's entries in the ride / catch / passenger tables (their low 32 bits are the
// player's character key). A load or a departure ends a ride, a catch and a seat, and a stale ride entry would hand a
// mount nobody rides to its game again when it comes back with the same key. Talks are left to the talk rule (its
// "the talker left" / "walked off" ends them, and after a short load the talk's NPC can still go back). Any thread.
int forget_other_player() noexcept {
    int n = 0;
    for (size_t i = 0; i < kRides; ++i) {
        uint64_t v = g_rides[i].load(std::memory_order_acquire);
        if (v && static_cast<uint32_t>(v) != kHostCharacter && g_rides[i].compare_exchange_strong(v, 0)) ++n;
        v = g_catches[i].load(std::memory_order_acquire);
        if (v && static_cast<uint32_t>(v) != kHostCharacter && g_catches[i].compare_exchange_strong(v, 0)) ++n;
        v = g_passengers[i].load(std::memory_order_acquire);
        if (v && static_cast<uint32_t>(v) != kHostCharacter && g_passengers[i].compare_exchange_strong(v, 0)) ++n;
    }
    return n;
}

struct PlayerAt {
    uint32_t key = 0, user = 0;
    int sx = 0, sz = 0;
    float x = 0, y = 0, z = 0;
};
// Facts for the log (2026-11-11: the sector rule was a guess; what do the NPCs around the second player look like on
// the server): the NPCs nearest to one player.
struct Near {
    uint32_t key = 0, ctrl = 0;
    uint8_t type = 0, dead = 0;
    float d_here = 0, d_host = 0, x = 0, z = 0;
};
struct Cand {
    uintptr_t actor = 0;
    uint32_t key = 0, from = 0, to = 0;
    uint8_t type = 0;
    float x = 0, z = 0;
};
// The fight rule's view of an NPC near a player: who drives it, how far the host and the other player are.
struct Fight {
    uintptr_t actor = 0;
    uint32_t key = 0, ctrl = 0, other_user = 0;
    uint8_t type = 0;
    float x = 0, z = 0, d_host = 1e9f, d_other = 1e9f;
};

int sector(float v) { return static_cast<int>(std::floor(v * kSector)); }

bool actor_xyz(uintptr_t base, uintptr_t actor, float& x, float& y, float& z) noexcept {
    const uintptr_t comps = *reinterpret_cast<uintptr_t*>(actor + 0x68);
    const uintptr_t ts = comps ? *reinterpret_cast<uintptr_t*>(comps + 0x1A0) : 0;
    if (!ts) return false;
    alignas(16) uint8_t buf[0x80] = {};
    reinterpret_cast<void* (__fastcall*)(uintptr_t, void*)>(base + kFnPosition)(ts, buf);
    const float* f = reinterpret_cast<const float*>(buf);
    x = f[0] + 1000.0f * static_cast<float>(*reinterpret_cast<int16_t*>(buf + 0xC));
    y = f[1];
    z = f[2] + 1000.0f * static_cast<float>(*reinterpret_cast<int16_t*>(buf + 0xE));
    return true;
}

bool actor_xz(uintptr_t base, uintptr_t actor, float& x, float& z) noexcept {
    const uintptr_t comps = *reinterpret_cast<uintptr_t*>(actor + 0x68);
    const uintptr_t ts = comps ? *reinterpret_cast<uintptr_t*>(comps + 0x1A0) : 0;
    if (!ts) return false;
    alignas(16) uint8_t buf[0x80] = {};
    reinterpret_cast<void* (__fastcall*)(uintptr_t, void*)>(base + kFnPosition)(ts, buf);
    const float* f = reinterpret_cast<const float*>(buf);
    x = f[0] + 1000.0f * static_cast<float>(*reinterpret_cast<int16_t*>(buf + 0xC));
    z = f[2] + 1000.0f * static_cast<float>(*reinterpret_cast<int16_t*>(buf + 0xE));
    return true;
}

// The departure sweep's list: NPCs (0xB, no owner user, a controller that is not `host_user`), with what the AI restart
// needs. Plain data only (SEH).
struct Left {
    uintptr_t actor = 0;
    uint32_t key = 0, ctrl = 0;
    float d = 0;  // from the host's character
};
int scan_left(uint32_t host_user, float hx, float hz, int hsx, int hsz, Left* out, int cap, int& n_far,
              bool& ok) noexcept {
    int n = 0;
    n_far = 0;
    ok = true;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        const uintptr_t slot = *reinterpret_cast<uintptr_t*>(base + kServerActorMgrSlot);
        const uintptr_t sam = slot ? *reinterpret_cast<uintptr_t*>(slot) : 0;
        const uint32_t cnt = sam ? *reinterpret_cast<uint32_t*>(sam + 0x9C) : 0;
        const uintptr_t arr = sam ? *reinterpret_cast<uintptr_t*>(sam + 0xB0) : 0;
        if (!sam || !arr || cnt > 200000) {
            ok = false;  // no list to judge by: the watch goes on
            return 0;
        }
        for (uint32_t i = 0; i < cnt && n < cap; ++i) {
            const uintptr_t e = *reinterpret_cast<uintptr_t*>(arr + 8ull * i);
            const uintptr_t actor = e ? *reinterpret_cast<uintptr_t*>(e + 8) : 0;
            if (!actor) continue;
            const uint32_t key = *reinterpret_cast<uint32_t*>(actor + 0x60);
            if ((key >> 28) != 0xB || *reinterpret_cast<uintptr_t*>(actor + 0xA0)) continue;
            const uint32_t ctrl = *reinterpret_cast<uint32_t*>(actor + 0x90);
            if (!ctrl || ctrl == host_user) continue;
            const uintptr_t info = *reinterpret_cast<uintptr_t*>(actor + 0x88);
            const uint8_t type = info ? *reinterpret_cast<uint8_t*>(info + 1) : 0;
            if (type != 3 && type != 4 && type != 5 && type != 6 && type != 8 && type != 9) continue;
            const uintptr_t comps = *reinterpret_cast<uintptr_t*>(actor + 0x68);
            if (!comps) continue;
            const uintptr_t status = *reinterpret_cast<uintptr_t*>(comps + 0x20);
            const uintptr_t aic = *reinterpret_cast<uintptr_t*>(comps + 0x40);
            const uintptr_t ai = *reinterpret_cast<uintptr_t*>(comps + 0x58);
            const uintptr_t mv = *reinterpret_cast<uintptr_t*>(comps + 0x1A0);
            if (!status || !aic || !ai || !mv || !*reinterpret_cast<uintptr_t*>(aic + 0x40)) continue;
            if (*reinterpret_cast<uint8_t*>(status + 0x293)) continue;  // a dead body
            float x = 0, z = 0;
            if (!actor_xz(base, actor, x, z)) continue;
            const int sx = sector(x), sz = sector(z);
            if (sx < hsx - 1 || sx > hsx + 1 || sz < hsz - 1 || sz > hsz + 1) {
                ++n_far;
                continue;
            }
            const float d = std::sqrt((x - hx) * (x - hx) + (z - hz) * (z - hz));
            out[n++] = Left{actor, key, ctrl, d};
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;  // a partial list: the watch must not end on it
        return n;
    }
    return n;
}

// The server's actor list: the players (out) and every NPC nobody who controls it can see while another player can.
// Plain data only (SEH). Returns the number of candidates; n_players and n_npcs are filled.
int scan(PlayerAt* pl, int pl_cap, int& n_players, Cand* out, int cap, int& n_npcs, int& n_orphans, Fight* fout,
         int fcap, int& n_fight) noexcept {
    int n = 0;
    n_players = n_npcs = n_orphans = n_fight = 0;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        const uintptr_t slot = *reinterpret_cast<uintptr_t*>(base + kServerActorMgrSlot);
        const uintptr_t sam = slot ? *reinterpret_cast<uintptr_t*>(slot) : 0;
        if (!sam) return 0;
        const uint32_t cnt = *reinterpret_cast<uint32_t*>(sam + 0x9C);
        const uintptr_t arr = *reinterpret_cast<uintptr_t*>(sam + 0xB0);
        if (!arr || cnt > 200000) return 0;
        for (uint32_t i = 0; i < cnt && n_players < pl_cap; ++i) {  // pass 1: the users' characters
            const uintptr_t e = *reinterpret_cast<uintptr_t*>(arr + 8ull * i);
            const uintptr_t actor = e ? *reinterpret_cast<uintptr_t*>(e + 8) : 0;
            if (!actor) continue;
            const uint32_t key = *reinterpret_cast<uint32_t*>(actor + 0x60);
            const uintptr_t user = *reinterpret_cast<uintptr_t*>(actor + 0xA0);
            if ((key >> 28) != 0xA || !user) continue;
            float x = 0, y = 0, z = 0;
            if (!actor_xyz(base, actor, x, y, z)) continue;
            PlayerAt p;
            p.key = key;
            p.user = *reinterpret_cast<uint32_t*>(user + 0x60);
            p.sx = sector(x);
            p.sz = sector(z);
            p.x = x;
            p.y = y;
            p.z = z;
            if (p.user) pl[n_players++] = p;
        }
        if (n_players < 2) return 0;
        for (uint32_t i = 0; i < cnt; ++i) {  // pass 2: the NPCs
            const uintptr_t e = *reinterpret_cast<uintptr_t*>(arr + 8ull * i);
            const uintptr_t actor = e ? *reinterpret_cast<uintptr_t*>(e + 8) : 0;
            if (!actor) continue;
            const uint32_t key = *reinterpret_cast<uint32_t*>(actor + 0x60);
            if ((key >> 28) != 0xB || *reinterpret_cast<uintptr_t*>(actor + 0xA0)) continue;  // an NPC nobody owns
            const uint32_t ctrl = *reinterpret_cast<uint32_t*>(actor + 0x90);
            if (!ctrl) continue;
            const uintptr_t info = *reinterpret_cast<uintptr_t*>(actor + 0x88);
            const uint8_t type = info ? *reinterpret_cast<uint8_t*>(info + 1) : 0;
            // the kinds the game's own AI restart accepts, without players (1) and gimmicks (7)
            if (type != 3 && type != 4 && type != 5 && type != 6 && type != 8 && type != 9) continue;
            const uintptr_t comps = *reinterpret_cast<uintptr_t*>(actor + 0x68);
            if (!comps) continue;
            const uintptr_t status = *reinterpret_cast<uintptr_t*>(comps + 0x20);
            const uintptr_t aic = *reinterpret_cast<uintptr_t*>(comps + 0x40);
            const uintptr_t ai = *reinterpret_cast<uintptr_t*>(comps + 0x58);
            const uintptr_t mv = *reinterpret_cast<uintptr_t*>(comps + 0x1A0);
            // everything the restart steps touch must be there; not a dead body
            if (!status || !aic || !ai || !mv || !*reinterpret_cast<uintptr_t*>(aic + 0x40)) continue;
            if (*reinterpret_cast<uint8_t*>(status + 0x293)) continue;
            float x = 0, z = 0;
            if (!actor_xz(base, actor, x, z)) continue;
            ++n_npcs;
            if (n_fight < fcap) {  // the fight rule (ai_handoff.h): every NPC with a player within kFightM
                Fight f;
                for (int k = 0; k < n_players; ++k) {
                    const float d = std::sqrt((x - pl[k].x) * (x - pl[k].x) + (z - pl[k].z) * (z - pl[k].z));
                    if (pl[k].key == kHostCharacter) {
                        f.d_host = d;
                    } else if (d < f.d_other) {
                        f.d_other = d;
                        f.other_user = pl[k].user;
                    }
                }
                if ((f.d_host <= kFightM || f.d_other <= kFightM) && f.other_user) {
                    f.actor = actor;
                    f.key = key;
                    f.ctrl = ctrl;
                    f.type = type;
                    f.x = x;
                    f.z = z;
                    fout[n_fight++] = f;
                }
            }
            // ff4 (2026-11-11): the sector rule was wrong as a test of "driven": the host's client still moves NPCs two
            // sectors (105-119 m) away. What matters to players is WHO the NPC's AI plays against: it runs on the
            // controller's client, around that client's own character. So: the controller's character is far from
            // the NPC (more than kFarM) and another player's is close (within kNearM). The gap between the two
            // distances keeps an NPC from going back and forth.
            int a = -1, b = -1;
            float best = kNearM;
            for (int k = 0; k < n_players; ++k) {
                const float d = std::sqrt((x - pl[k].x) * (x - pl[k].x) + (z - pl[k].z) * (z - pl[k].z));
                if (pl[k].user == ctrl) {
                    a = d <= kFarM ? k : -2;  // -2: the controller is here but far
                } else if (d <= best) {
                    best = d;
                    b = k;
                }
            }
            if (a >= 0 || b < 0 || n >= cap) continue;  // its controller is near enough, or nobody else is close
            ++n_orphans;
            Cand c;
            c.actor = actor;
            c.key = key;
            c.from = ctrl;
            c.to = pl[b].user;
            c.type = type;
            c.x = x;
            c.z = z;
            out[n++] = c;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return n;
    }
    return n;
}

// The `cap` NPCs (no owner user) nearest to (x, z), nearest first. Plain data only (SEH).
int nearest(float px, float pz, float hx, float hz, Near* out, int cap) noexcept {
    int n = 0;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        const uintptr_t slot = *reinterpret_cast<uintptr_t*>(base + kServerActorMgrSlot);
        const uintptr_t sam = slot ? *reinterpret_cast<uintptr_t*>(slot) : 0;
        if (!sam) return 0;
        const uint32_t cnt = *reinterpret_cast<uint32_t*>(sam + 0x9C);
        const uintptr_t arr = *reinterpret_cast<uintptr_t*>(sam + 0xB0);
        if (!arr || cnt > 200000) return 0;
        for (uint32_t i = 0; i < cnt; ++i) {
            const uintptr_t e = *reinterpret_cast<uintptr_t*>(arr + 8ull * i);
            const uintptr_t actor = e ? *reinterpret_cast<uintptr_t*>(e + 8) : 0;
            if (!actor) continue;
            const uint32_t key = *reinterpret_cast<uint32_t*>(actor + 0x60);
            if ((key >> 28) != 0xB || *reinterpret_cast<uintptr_t*>(actor + 0xA0)) continue;
            const uintptr_t info = *reinterpret_cast<uintptr_t*>(actor + 0x88);
            const uint8_t type = info ? *reinterpret_cast<uint8_t*>(info + 1) : 0;
            if (type == 7 || type == 0) continue;  // gimmicks
            float x = 0, z = 0;
            if (!actor_xz(base, actor, x, z)) continue;
            Near r;
            r.key = key;
            r.ctrl = *reinterpret_cast<uint32_t*>(actor + 0x90);
            r.type = type;
            const uintptr_t comps = *reinterpret_cast<uintptr_t*>(actor + 0x68);
            const uintptr_t status = comps ? *reinterpret_cast<uintptr_t*>(comps + 0x20) : 0;
            r.dead = status ? *reinterpret_cast<uint8_t*>(status + 0x293) : 2;
            r.x = x;
            r.z = z;
            r.d_here = std::sqrt((x - px) * (x - px) + (z - pz) * (z - pz));
            r.d_host = std::sqrt((x - hx) * (x - hx) + (z - hz) * (z - hz));
            int at = n < cap ? n : cap - 1;  // insertion sort by d_here, the farthest falls out
            if (n == cap && r.d_here >= out[cap - 1].d_here) continue;
            while (at > 0 && out[at - 1].d_here > r.d_here) {
                out[at] = out[at - 1];
                --at;
            }
            out[at] = r;
            if (n < cap) ++n;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return n;
    }
    return n;
}

// The hand-over on the server: the controller key, then the game's own restart steps. False = a fault (nothing more
// is done with this actor).
bool hand_over(uintptr_t actor, uint32_t key, uint32_t from, uint32_t to) noexcept {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        // still the same actor in the same state (the scan was a moment ago, on this thread)
        if (*reinterpret_cast<uint32_t*>(actor + 0x60) != key || *reinterpret_cast<uint32_t*>(actor + 0x90) != from ||
            *reinterpret_cast<uintptr_t*>(actor + 0xA0))
            return false;
        *reinterpret_cast<uint32_t*>(actor + 0x90) = to;
        reinterpret_cast<void (__fastcall*)(uintptr_t)>(base + kFnAiSessionBump)(actor);
        reinterpret_cast<void (__fastcall*)(uintptr_t)>(base + kFnMoveSessionBump)(actor);
        const uintptr_t comps = *reinterpret_cast<uintptr_t*>(actor + 0x68);
        const uintptr_t ai = comps ? *reinterpret_cast<uintptr_t*>(comps + 0x58) : 0;
        int out = 0;
        if (ai) reinterpret_cast<void (__fastcall*)(uintptr_t, int*)>(base + kFnActionReset)(ai, &out);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A server packet for one user's client: the host's own client through the injection queue (delivered on its packet
// thread), the second user's through the extra-packet queue of its session.
void tell(uint32_t user, uint32_t host_user, std::span<const uint8_t> payload) {
    if (user == host_user) {
        net::puppet_inject_s2c(kOpChangeAiControlAck, std::vector<uint8_t>(payload.begin(), payload.end()), 0);
    } else {
        const std::vector<uint8_t> p = lrl::make(kOpChangeAiControlAck, payload);
        server_push_extra(p);
    }
}

uint32_t local_user_key() noexcept {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        const uintptr_t ws = *reinterpret_cast<uintptr_t*>(base + kClientWorldSlot);
        const uintptr_t mgr = ws ? *reinterpret_cast<uintptr_t*>(ws + 0x30) : 0;
        const uintptr_t me = mgr ? *reinterpret_cast<uintptr_t*>(mgr + 0x50) : 0;
        return me ? *reinterpret_cast<uint32_t*>(me + 0x90) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// The client part: 1 = we took control, 2 = we noted another controller, 0 = a fault.
int client_apply(uintptr_t actor, uint32_t key, uint32_t now_ctrl, uint32_t me) noexcept {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        if (*reinterpret_cast<uint32_t*>(actor + 0x60) != key) return 0;
        *reinterpret_cast<uint32_t*>(actor + 0x90) = now_ctrl;
        if (now_ctrl != me) return 2;  // our AI task drops it by itself on its next run
        reinterpret_cast<void (__fastcall*)(uintptr_t)>(base + kFnClientControlMode)(actor);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

}  // namespace

void ai_handoff_start(std::string_view mode, bool guest, bool fight) {
    g_mode = mode == "1" ? Mode::On : mode == "probe" ? Mode::Probe : Mode::Off;
    g_guest = guest;
    g_fight = fight && !guest;
    if (g_fight)
        log::info("AiHandoff: the fight rule is ON (ai_handoff_fight=1): an enemy the other player fights (hit in the "
                  "last 4 s, the host not for 6 s, that player within 20 m and nearer) runs on that player's game; "
                  "the same back for the host; one hand-over per enemy in 10 s");
    log::info("AiHandoff {} (ai_handoff={}{}): {}", g_mode == Mode::On ? "ON" : g_mode == Mode::Probe ? "probe" : "off",
              mode, guest ? ", guest" : "",
              g_mode == Mode::Off ? "an NPC keeps the controller it was born with"
              : guest             ? "this client takes over an NPC's AI when the host's server says so"
                                  : "an NPC whose controller's player is more than 60 m away goes to a player within 30 m "
                                    "of it (two checks in a row)");
}

void ai_handoff_tick() noexcept {
    if ((g_mode == Mode::Off && !damage_range_on() && !g_fight) || g_guest) return;
    try {
        static uint64_t next = 0, next_state = 0, next_watch = 0, watch_until = 0;
        static bool in = false, was_present = false, was_loading = false, host_in = false, departed = false,
                    in_since = false;
        const uint64_t now = GetTickCount64();
        // The other player's state, four times a second (each read takes a lock and a copy in server.cpp).
        if (now >= next_state) {
            next_state = now + 250;
            // One phase read: "in the world" (server_user2_in_world's test), the load and the goodbye all from it.
            const MovePhase ph = server_user2_phase().phase();
            const bool present = server_user2_present();
            const bool logout = server_logout_requested();
            in = present && !logout && ph == MovePhase::Stable;
            // A real load: past the move's 2549 (move_phase_loading; a move can stop before it, and the phase is not
            // Stable at times with both players in the world, cs8a).
            const bool loading = present && move_phase_loading(ph);
            // Its goodbye under way (asked, or the host's for a lost link: the final save, then the 2001). A crash
            // reaches this ~10 s after the link went quiet, the session's end 15 s later.
            const bool leaving = present && logout;
            uintptr_t host = 0;
            host_in = server_host_user(host) && host;
            if (in || (!was_present && present)) departed = false;  // in the world again, or a new session
            if (in) in_since = true;
            const bool rules = g_mode != Mode::Off || g_fight;  // the hand-overs, and so the sweep
            if (rules && !departed && !in && ((was_present && !present) || leaving)) {
                departed = true;
                in_since = false;
                // Gone: its ride / catch / passenger entries first, then a server check, then the watch.
                if (const int f = forget_other_player())
                    log::info("AiHandoff: the other player left: {} ride / catch / passenger entr{} of its forgotten", f,
                              f == 1 ? "y" : "ies");
                g_left_sweep.store(true);
                g_left_watch.store(true);
                next_watch = now + 10000;
                watch_until = now + 600000;
            } else if (rules && loading && !was_loading && in_since) {
                // A load (fast travel, a respawn): a load ends a ride, a catch and a seat too; one check, no watch.
                in_since = false;
                if (const int f = forget_other_player())
                    log::info("AiHandoff: the other player is loading: {} ride / catch / passenger entr{} of its "
                              "forgotten", f, f == 1 ? "y" : "ies");
                g_left_sweep.store(true);
            }
            was_loading = loading;
            if (in) {
                g_left_sweep.store(false);  // back in the world: its game runs its NPCs again
                g_left_watch.store(false);
            } else if (g_left_watch.load() && now >= next_watch) {
                next_watch = now + 10000;
                if ((present && !leaving) || !host_in || now >= watch_until) {
                    g_left_watch.store(false);  // came back, or the host left its world too, or 10 minutes
                } else {
                    g_left_sweep.store(true);
                }
            }
            was_present = present;
        }
        if (now < next || (!in && !(g_left_sweep.load() && host_in))) return;
        next = now + 1000;
        std::vector<uint8_t> pl(30, 0);  // the host's own trade request with our entry: the server thread runs the check
        std::memcpy(&pl[14], &kHostCharacter, 4);
        std::memcpy(&pl[18], &kHostCharacter, 4);
        std::memcpy(&pl[26], &kTickEntry, 4);
        net::sniffer_inject_c2s(lrl::make(3104, pl, 0x00));
    } catch (...) {
    }
}

void ai_handoff_server_tick() noexcept {
    if ((g_mode == Mode::Off && !damage_range_on() && !g_fight) || g_guest) return;
    try {
        static std::unordered_map<uint32_t, std::pair<uint32_t, int>> pending;  // actor key -> (to, checks in a row)
        static std::unordered_map<uint32_t, uint32_t> born;                     // actor key -> its first controller
        PlayerAt pl[4];
        static Cand cand[512];
        static Fight fights[256];
        static std::unordered_map<uint32_t, uint64_t> moved;  // actor key -> when it was last handed over (ms)
        const bool sweep_now = g_left_sweep.exchange(false);  // taken here: no early return below leaves it set
        int n_players = 0, n_npcs = 0, n_orphans = 0, n_fight = 0;
        const int n_all = scan(pl, 4, n_players, cand, 512, n_npcs, n_orphans, fights, 256, n_fight);
        const int n = g_mode == Mode::Off ? 0 : n_all;  // the distance rule only with ai_handoff = probe / 1
        {  // damage_range.h: where the users' characters are now
            float xyz[4 * 3];
            for (int k = 0; k < n_players && k < 4; ++k) {
                xyz[k * 3] = pl[k].x;
                xyz[k * 3 + 1] = pl[k].y;
                xyz[k * 3 + 2] = pl[k].z;
            }
            damage_range_players(xyz, n_players);
        }
        if (g_mode == Mode::Off && !g_fight) return;
        c_ticks.fetch_add(1);
        c_npcs.store(static_cast<uint64_t>(n_npcs));
        c_orphans.store(static_cast<uint64_t>(n_orphans));
        uint32_t host_user = 0;
        for (int k = 0; k < n_players; ++k) {
            if (pl[k].key == kHostCharacter) host_user = pl[k].user;
        }
        const bool user2_in = server_user2_in_world();
        if (sweep_now && !host_user && !user2_in) g_left_sweep.store(true);  // the host's own load: the next check
        if (sweep_now && host_user && !user2_in) {
            float hx = 0, hz = 0;
            int hsx = 0, hsz = 0;
            for (int k = 0; k < n_players; ++k) {
                if (pl[k].key == kHostCharacter) {
                    hx = pl[k].x;
                    hz = pl[k].z;
                    hsx = pl[k].sx;
                    hsz = pl[k].sz;
                }
            }
            static Left left[512];
            int n_far = 0;
            bool ok = true;
            const int m = scan_left(host_user, hx, hz, hsx, hsz, left, 512, n_far, ok);
            int back = 0, failed = 0;
            std::string keys;  // the first few, for the log
            for (int i = 0; i < m && back < kMaxPerTick; ++i) {
                if (!hand_over(left[i].actor, left[i].key, left[i].ctrl, host_user)) {
                    ++failed;
                    continue;
                }
                if (++back <= 8)
                    keys += std::format(" 0x{:08X} (from 0x{:08X}, {:.0f} m)", left[i].key, left[i].ctrl, left[i].d);
                uint8_t priv[12];
                std::memcpy(priv, &kPrivateMagic, 4);
                std::memcpy(priv + 4, &left[i].key, 4);
                std::memcpy(priv + 8, &host_user, 4);
                uint8_t real[4];
                std::memcpy(real, &left[i].key, 4);
                // A player still listed (during its load) notes the new controller, as with the other rules.
                for (int k = 0; k < n_players; ++k) {
                    if (pl[k].user != host_user) tell(pl[k].user, host_user, priv);
                }
                tell(host_user, host_user, priv);
                tell(host_user, host_user, real);
                moved[left[i].key] = GetTickCount64();
            }
            if (m > back + failed) g_left_sweep.store(true);  // the rest at the next check (a second)
            if (ok && m == 0 && n_far == 0) g_left_watch.store(false);  // none anywhere: no more checks
            const uint64_t sw = c_left_sweeps.fetch_add(1) + 1;
            c_left_back.fetch_add(static_cast<uint64_t>(back));
            c_left_failed.fetch_add(static_cast<uint64_t>(failed));
            if (back || sw <= 3 || (failed && sw % 30 == 0))
                log::info("AiHandoff: departure sweep {}: the other player is not in the world; {} NPC(s) another game ran "
                          "back to the host's game ({} found in the host's 3x3 sectors, {} did not go through, {} farther "
                          "wait for the host to come near){}{}", sw, back, m, failed, n_far,
                          ok && m == 0 && n_far == 0 ? "; none left" : ok ? "" : "; the scan faulted",
                          keys.empty() ? "" : ":" + keys);
        }
        if (!user2_in) {  // the rules below only while the other player is in the world (as before the sweep)
            pending.clear();
            return;
        }
        if (n_players < 2 || !host_user) {
            pending.clear();
            return;
        }
        // Facts, every 10 s (the first 40 times): the 10 NPCs nearest to the other player, as the server has them.
        static uint64_t next_dump = 0;
        static int dumps = 0;
        if (const uint64_t now = GetTickCount64(); g_mode != Mode::Off && now >= next_dump && dumps < 40) {
            next_dump = now + 10000;
            ++dumps;
            float hx = 0, hz = 0;
            for (int k = 0; k < n_players; ++k) {
                if (pl[k].key == kHostCharacter) {
                    hx = pl[k].x;
                    hz = pl[k].z;
                }
            }
            for (int k = 0; k < n_players; ++k) {
                if (pl[k].key == kHostCharacter) continue;
                Near rows[10];
                const int m = nearest(pl[k].x, pl[k].z, hx, hz, rows, 10);
                std::string line;
                for (int i = 0; i < m; ++i)
                    line += std::format(" | 0x{:08X} t{} ctrl 0x{:08X}{} {:.0f} m (host {:.0f} m) at ({:.1f}, {:.1f})",
                                        rows[i].key, rows[i].type, rows[i].ctrl, rows[i].dead ? " DEAD" : "",
                                        rows[i].d_here, rows[i].d_host, rows[i].x, rows[i].z);
                log::info("AiHandoff facts: player 0x{:08X} (user 0x{:08X}) at ({:.1f}, {:.1f}) sector ({}, {}), the host at "
                          "({:.1f}, {:.1f}) sector ({}, {}), host user 0x{:08X}; nearest NPCs:{}",
                          pl[k].key, pl[k].user, pl[k].x, pl[k].z, pl[k].sx, pl[k].sz, hx, hz, sector(hx), sector(hz),
                          host_user, line);
            }
        }
        std::unordered_map<uint32_t, std::pair<uint32_t, int>> still;
        int done = 0;
        for (int i = 0; i < n; ++i) {
            const Cand& c = cand[i];
            int checks = 1;
            if (const auto it = pending.find(c.key); it != pending.end() && it->second.first == c.to)
                checks = it->second.second + 1;
            if (checks < kNeedChecks || done >= kMaxPerTick) {
                still[c.key] = {c.to, checks};
                continue;
            }
            if (g_mode == Mode::Probe) {
                const uint64_t w = c_would.fetch_add(1) + 1;
                if (w <= 40 || w % 200 == 0)
                    log::info("AiHandoff: [would] NPC 0x{:08X} (type {}) at ({:.0f}, {:.0f}): controller 0x{:08X} is far, "
                              "0x{:08X} is close ({} so far)", c.key, c.type, c.x, c.z, c.from, c.to, w);
                still[c.key] = {c.to, checks};  // counted once per stretch
                still[c.key].second = -100000;
                continue;
            }
            if (!hand_over(c.actor, c.key, c.from, c.to)) {
                const uint64_t f = c_failed.fetch_add(1) + 1;
                if (f <= 10) log::warn("AiHandoff: NPC 0x{:08X}: the hand-over did not go through (state changed or a fault)", c.key);
                continue;
            }
            ++done;
            moved[c.key] = GetTickCount64();
            uint8_t priv[12];
            std::memcpy(priv, &kPrivateMagic, 4);
            std::memcpy(priv + 4, &c.key, 4);
            std::memcpy(priv + 8, &c.to, 4);
            uint8_t real[4];
            std::memcpy(real, &c.key, 4);
            // The old controller's client (if it still has the actor) only notes the new one; the new controller's
            // takes over, then gets the game's own notice (action reset, "OnAi").
            for (int k = 0; k < n_players; ++k) {
                if (pl[k].user != c.to) tell(pl[k].user, host_user, priv);
            }
            tell(c.to, host_user, priv);
            tell(c.to, host_user, real);
            const auto [b, first] = born.try_emplace(c.key, c.from);
            const bool back = !first && b->second == c.to;
            if (back) c_back.fetch_add(1);
            const uint64_t h = c_handed.fetch_add(1) + 1;
            if (h <= 60 || h % 100 == 0)
                log::info("AiHandoff: NPC 0x{:08X} (type {}) at ({:.0f}, {:.0f}): controller 0x{:08X} -> 0x{:08X}{} ({} so far)",
                          c.key, c.type, c.x, c.z, c.from, c.to,
                          back ? " (back to its first controller)" : c.to == host_user ? " (the host)" : " (the other player)", h);
        }
        pending.swap(still);
        if (born.size() > 20000) born.clear();
        // The ride rule (ai_handoff.h, 2026-11-18): a ridden NPC runs on its rider's game.
        {
            const uint64_t now = GetTickCount64();
            for (int i = 0; i < n_fight && done < kMaxPerTick; ++i) {
                const Fight& f = fights[i];
                uint32_t rider = ridden_by(f.key);
                const bool caught = !rider && (rider = caught_by(f.key, now)) != 0;
                if (!rider) continue;
                uint32_t to = 0;
                for (int k = 0; k < n_players; ++k) {
                    if (pl[k].key == rider) to = pl[k].user;
                }
                if (!to || to == f.ctrl) continue;
                if (!hand_over(f.actor, f.key, f.ctrl, to)) {
                    const uint64_t fl = c_failed.fetch_add(1) + 1;
                    if (fl <= 10) log::warn("AiHandoff: ride rule: NPC 0x{:08X}: the hand-over did not go through", f.key);
                    continue;
                }
                ++done;
                moved[f.key] = now;
                uint8_t priv[12];
                std::memcpy(priv, &kPrivateMagic, 4);
                std::memcpy(priv + 4, &f.key, 4);
                std::memcpy(priv + 8, &to, 4);
                uint8_t real[4];
                std::memcpy(real, &f.key, 4);
                for (int k = 0; k < n_players; ++k) {
                    if (pl[k].user != to) tell(pl[k].user, host_user, priv);
                }
                tell(to, host_user, priv);
                tell(to, host_user, real);
                const uint64_t h = (caught ? c_catch_out : c_ride_out).fetch_add(1) + 1;
                if (h <= 60 || h % 100 == 0)
                    log::info("AiHandoff: {} rule: NPC 0x{:08X} (type {}) {} 0x{:08X} -> its game (0x{:08X} -> "
                              "0x{:08X}; {} so far)", caught ? "catch" : "ride", f.key, f.type,
                              caught ? "caught by" : "ridden by", rider, f.ctrl, to, h);
            }
        }
        // The talk rule (ai_handoff.h, 2026-11-21): a person the other player talks to runs on the talker's game.
        {
            const uint64_t now = GetTickCount64();
            // A talk whose NPC is no longer within kFightM of any player (both walked off) is over: dropped, the NPC
            // stays where it is (the far-controller rule above looks after it).
            for (size_t s = 0; s < kRides; ++s) {
                uint64_t v = g_talks[s].load(std::memory_order_acquire);
                if (!v || now - g_talk_ms[s].load(std::memory_order_relaxed) < kTalkGraceMs) continue;
                bool seen = false;
                for (int i = 0; i < n_fight && !seen; ++i) seen = fights[i].key == static_cast<uint32_t>(v >> 32);
                if (!seen && g_talks[s].compare_exchange_strong(v, 0)) {
                    g_talk_from[s].store(0, std::memory_order_relaxed);
                    c_talk_ended.fetch_add(1);
                }
            }
            for (int i = 0; i < n_fight && done < kMaxPerTick; ++i) {
                const Fight& f = fights[i];
                const size_t s = talk_slot(f.key);
                if (s == kRides) continue;
                const uint64_t v = g_talks[s].load(std::memory_order_acquire);
                const uint32_t talker = static_cast<uint32_t>(v);
                const uint64_t age = now - g_talk_ms[s].load(std::memory_order_relaxed);
                uint32_t talker_user = 0;
                float d = 1e9f;
                for (int k = 0; k < n_players; ++k) {
                    if (pl[k].key != talker) continue;
                    talker_user = pl[k].user;
                    d = std::sqrt((f.x - pl[k].x) * (f.x - pl[k].x) + (f.z - pl[k].z) * (f.z - pl[k].z));
                }
                const bool over = !talker_user || age >= kTalkMaxMs || (age >= kTalkGraceMs && d > kTalkEndM) ||
                                  ridden_by(f.key) || caught_by(f.key, now);
                const uint32_t from = g_talk_from[s].load(std::memory_order_relaxed);
                uint32_t to = 0;
                if (over) {
                    uint64_t want = v;
                    if (!g_talks[s].compare_exchange_strong(want, 0)) continue;
                    g_talk_from[s].store(0, std::memory_order_relaxed);
                    c_talk_ended.fetch_add(1);
                    // Back to the game that ran it before, if that player is still in the world and the talker's game
                    // has it now (a ride / catch / fight rule may have moved it since).
                    bool from_here = false;
                    float d_from = 1e9f;
                    for (int k = 0; k < n_players; ++k) {
                        if (pl[k].user != from) continue;
                        from_here = true;
                        d_from = std::sqrt((f.x - pl[k].x) * (f.x - pl[k].x) + (f.z - pl[k].z) * (f.z - pl[k].z));
                    }
                    const bool from_far = from_here && d_from > kFarM;
                    if (!from || !from_here || f.ctrl != talker_user || from == f.ctrl || from_far) {
                        log::info("AiHandoff: talk rule: the talk with NPC 0x{:08X} is over ({}); it stays with 0x{:08X}{}",
                                  f.key, !talker_user ? "the talker left" : age >= kTalkMaxMs ? "10 min" :
                                  d > kTalkEndM ? "the talker walked off" : "ridden / caught", f.ctrl,
                                  from_far ? std::format(" (the game that ran it before is {:.0f} m away)", d_from)
                                           : std::string());
                        continue;
                    }
                    to = from;
                } else {
                    if (f.type != 3) {  // a person (or an animal) only
                        uint64_t want = v;
                        g_talks[s].compare_exchange_strong(want, 0);
                        c_talk_skip.fetch_add(1);
                        continue;
                    }
                    if (f.ctrl == talker_user) continue;  // its game has it already
                    if (!from) g_talk_from[s].store(f.ctrl, std::memory_order_relaxed);
                    to = talker_user;
                }
                if (!hand_over(f.actor, f.key, f.ctrl, to)) {
                    const uint64_t fl = c_failed.fetch_add(1) + 1;
                    if (fl <= 10) log::warn("AiHandoff: talk rule: NPC 0x{:08X}: the hand-over did not go through", f.key);
                    continue;
                }
                ++done;
                moved[f.key] = now;
                uint8_t priv[12];
                std::memcpy(priv, &kPrivateMagic, 4);
                std::memcpy(priv + 4, &f.key, 4);
                std::memcpy(priv + 8, &to, 4);
                uint8_t real[4];
                std::memcpy(real, &f.key, 4);
                for (int k = 0; k < n_players; ++k) {
                    if (pl[k].user != to) tell(pl[k].user, host_user, priv);
                }
                tell(to, host_user, priv);
                tell(to, host_user, real);
                const uint64_t h = (over ? c_talk_back : c_talk_out).fetch_add(1) + 1;
                if (h <= 60 || h % 100 == 0)
                    log::info("AiHandoff: talk rule: NPC 0x{:08X} (type {}) {} (0x{:08X} -> 0x{:08X}; the talker 0x{:08X} "
                              "{:.1f} m from it; {} so far)", f.key, f.type,
                              over ? "back to the game that ran it before the talk" : "-> the talker's game", f.ctrl, to,
                              talker, d, h);
            }
        }
        // The fight rule (ai_handoff.h, 2026-11-16, B18).
        if (g_fight) {
            const uint64_t now = GetTickCount64();
            // Passengers (ai_handoff_note_passenger): still seated only while the vehicle is beside the player (this
            // server check's NPC list); otherwise the RideOff went by another route and the entry is dropped.
            uint32_t passenger_user = 0;
            for (int k = 0; k < n_players; ++k) {
                if (pl[k].key == kHostCharacter) continue;
                const uint32_t veh = passenger_vehicle(pl[k].key);
                if (!veh) continue;
                bool beside = false;
                for (int i = 0; i < n_fight; ++i) {
                    if (fights[i].key == veh && fights[i].d_other <= kPassengerM) beside = true;
                }
                if (beside) {
                    passenger_user = pl[k].user;
                } else {
                    drop_passenger(pl[k].key);
                    log::info("AiHandoff: passenger: 0x{:08X} is no longer beside vehicle 0x{:08X}: dropped", pl[k].key, veh);
                }
            }
            if (moved.size() > 8000) moved.clear();
            for (int i = 0; i < n_fight && done < kMaxPerTick; ++i) {
                const Fight& f = fights[i];
                if (ridden_by(f.key) || caught_by(f.key, now) || talk_slot(f.key) != kRides) continue;  // the ride / catch / talk rule's
                uint64_t hm = 0, gm = 0;
                if (!anti_grief_last_hits(f.key, hm, gm)) continue;
                c_fight_seen.fetch_add(1);
                const bool g_recent = gm && now - gm < kFightRecentMs, h_recent = hm && now - hm < kFightRecentMs;
                const bool g_quiet = !gm || now - gm >= kFightQuietMs, h_quiet = !hm || now - hm >= kFightQuietMs;
                uint32_t to = 0;
                if (f.ctrl == host_user && g_recent && h_quiet && f.d_other <= kFightM && f.d_other < f.d_host)
                    to = f.other_user;
                if (to && to == f.other_user) {  // not to a passenger: its frame events are boarding, not blows
                    if (to == passenger_user) {
                        const uint64_t s = c_pass_skip.fetch_add(1) + 1;
                        if (s <= 20 || s % 100 == 0)
                            log::info("AiHandoff: fight rule: NPC 0x{:08X} (type {}) stays: the other player is a passenger "
                                      "({} so far)", f.key, f.type, s);
                        continue;
                    }
                }
                else if (f.ctrl == f.other_user && h_recent && g_quiet && f.d_host <= kFightM && f.d_host < f.d_other)
                    to = host_user;
                if (!to) continue;
                if (const auto it = moved.find(f.key); it != moved.end() && now - it->second < kFightCooldownMs) continue;
                if (!hand_over(f.actor, f.key, f.ctrl, to)) {
                    const uint64_t fl = c_failed.fetch_add(1) + 1;
                    if (fl <= 10) log::warn("AiHandoff: fight rule: NPC 0x{:08X}: the hand-over did not go through", f.key);
                    continue;
                }
                ++done;
                moved[f.key] = now;
                uint8_t priv[12];
                std::memcpy(priv, &kPrivateMagic, 4);
                std::memcpy(priv + 4, &f.key, 4);
                std::memcpy(priv + 8, &to, 4);
                uint8_t real[4];
                std::memcpy(real, &f.key, 4);
                for (int k = 0; k < n_players; ++k) {
                    if (pl[k].user != to) tell(pl[k].user, host_user, priv);
                }
                tell(to, host_user, priv);
                tell(to, host_user, real);
                const bool back = to == host_user;
                const uint64_t h = (back ? c_fight_back : c_fight_out).fetch_add(1) + 1;
                if (h <= 60 || h % 100 == 0)
                    log::info("AiHandoff: fight rule: NPC 0x{:08X} (type {}) -> {} (0x{:08X} -> 0x{:08X}; host {:.0f} m, "
                              "other player {:.0f} m; last hits: host {} ms ago, other player {} ms ago)",
                              f.key, f.type, back ? "back to the host" : "the other player", f.ctrl, to, f.d_host, f.d_other,
                              hm ? now - hm : 0, gm ? now - gm : 0);
            }
        }
    } catch (...) {
    }
}

bool ai_handoff_client_recv(const uint8_t* packet, size_t size) noexcept {
    // Whatever our own setting says: the notice only comes when the host's server runs the hand-over, and the guest
    // has no say in that.
    if (!packet || size < lrl::kHeader + 4) return false;
    if (static_cast<uint16_t>(packet[0] | (packet[1] << 8)) != kOpChangeAiControlAck) return false;
    try {
        const uint8_t* pl = packet + lrl::kHeader;
        const size_t n = size - lrl::kHeader;
        if (n == 12 && lrl::rd32(pl) == kPrivateMagic) {
            const uint32_t key = lrl::rd32(pl + 4), now_ctrl = lrl::rd32(pl + 8);
            uint32_t me = g_local_user.load();
            if (!me) {
                me = local_user_key();
                g_local_user = me;
            }
            const auto actor = game::find_actor(key);
            const int r = actor && me ? client_apply(*actor, key, now_ctrl, me) : -1;
            if (r == 1) {
                const uint64_t t = c_client_took.fetch_add(1) + 1;
                if (t <= 40 || t % 100 == 0) log::info("AiHandoff: this client now runs NPC 0x{:08X} ({} so far)", key, t);
            } else if (r == 2) {
                c_client_gave.fetch_add(1);
            } else {
                c_client_unknown.fetch_add(1);
            }
            return true;  // never for the game
        }
        if (n == 4) {
            // The game's handler acts on whatever actor it looked up LAST when the key is unknown here: never let
            // that happen.
            if (!game::find_actor(lrl::rd32(pl))) {
                c_client_dropped.fetch_add(1);
                return true;
            }
        }
    } catch (...) {
    }
    return false;
}

std::atomic<bool> g_catch_on{true};
void ai_handoff_set_catch(bool on) noexcept {
    g_catch_on.store(on);
    if (!on) log::info("AiHandoff: catch rule OFF ([session2] ai_handoff_catch=0)");
}

void ai_handoff_set_talk(bool on) noexcept {
    g_talk_on.store(on);
    if (!on) log::info("AiHandoff: talk rule OFF ([session2] ai_handoff_talk=0)");
}

void ai_handoff_note_catch(uint32_t catcher_key, uint32_t npc_key, uint32_t interaction) noexcept {
    if (g_guest || !npc_key || (npc_key >> 28) != 0xB) return;
    if ((catcher_key >> 28) != 0xA || catcher_key == kHostCharacter) return;  // the other player's character only
    bool is_catch = false;
    for (uint32_t c : kCatchInteractions) is_catch = is_catch || c == interaction;
    if (is_catch && !g_catch_on.load(std::memory_order_relaxed)) return;
    if (!is_catch) {
        // The talk rule (ai_handoff.h): the same NPC again refreshes its time; else a free or stale slot.
        if (!g_talk_on.load(std::memory_order_relaxed)) return;
        const uint64_t t = GetTickCount64();
        const uint64_t want = (static_cast<uint64_t>(npc_key) << 32) | catcher_key;
        size_t slot = talk_slot(npc_key);
        if (slot != kRides) {
            g_talk_ms[slot].store(t, std::memory_order_relaxed);
            g_talks[slot].store(want, std::memory_order_release);
            return;
        }
        for (size_t i = 0; i < kRides && slot == kRides; ++i) {
            uint64_t v = g_talks[i].load(std::memory_order_relaxed);
            if (v && t - g_talk_ms[i].load(std::memory_order_relaxed) < kTalkMaxMs + 60000) continue;
            if (g_talks[i].compare_exchange_strong(v, 0)) slot = i;
        }
        if (slot == kRides) {
            c_talk_full.fetch_add(1);
            return;
        }
        g_talk_from[slot].store(0, std::memory_order_relaxed);
        g_talk_ms[slot].store(t, std::memory_order_relaxed);
        g_talks[slot].store(want, std::memory_order_release);
        const uint64_t n = c_talk_noted.fetch_add(1) + 1;
        if (n <= 40 || n % 100 == 0)
            log::info("AiHandoff: talk rule: the other player's 0x{:08X} interacts with NPC 0x{:08X} (interaction 0x{:X}): "
                      "a person near a player goes to the talker's game at the next server check ({} so far)", catcher_key, npc_key,
                      interaction, n);
        return;
    }
    const uint64_t now = GetTickCount64();
    const uint64_t want = (static_cast<uint64_t>(npc_key) << 32) | catcher_key;
    size_t slot = kRides;
    for (size_t i = 0; i < kRides && slot == kRides; ++i) {  // the same NPC again, else a free or expired slot
        const uint64_t v = g_catches[i].load(std::memory_order_relaxed);
        if (v && static_cast<uint32_t>(v >> 32) == npc_key) slot = i;
    }
    for (size_t i = 0; i < kRides && slot == kRides; ++i) {
        const uint64_t v = g_catches[i].load(std::memory_order_relaxed);
        if (!v || now - g_catch_ms[i].load(std::memory_order_relaxed) >= kCatchMs) slot = i;
    }
    if (slot == kRides) {
        c_catch_full.fetch_add(1);
        return;
    }
    g_catch_ms[slot].store(now, std::memory_order_relaxed);
    g_catches[slot].store(want, std::memory_order_release);
    const uint64_t n = c_catch_noted.fetch_add(1) + 1;
    if (n <= 40 || n % 100 == 0)
        log::info("AiHandoff: catch rule: the other player's 0x{:08X} catches NPC 0x{:08X} (interaction 0x{:X}): it goes "
                  "to the catcher's game at the next server check ({} so far)", catcher_key, npc_key, interaction, n);
}

uint32_t ai_handoff_ridden_by(uint32_t mount_key) noexcept { return mount_key ? ridden_by(mount_key) : 0; }

void ai_handoff_note_ride(uint32_t rider_key, uint32_t mount_key, bool on) noexcept {
    if (g_guest || !mount_key || (mount_key >> 28) != 0xB) return;
    if (!on) {
        for (auto& r : g_rides) {
            uint64_t v = r.load(std::memory_order_relaxed);
            if (v && static_cast<uint32_t>(v >> 32) == mount_key) r.compare_exchange_strong(v, 0);
        }
        return;
    }
    if ((rider_key >> 28) != 0xA || rider_key == kHostCharacter) return;  // the other player's character only
    const uint64_t want = (static_cast<uint64_t>(mount_key) << 32) | rider_key;
    for (auto& r : g_rides) {  // the same mount again: the new rider
        uint64_t v = r.load(std::memory_order_relaxed);
        if (v && static_cast<uint32_t>(v >> 32) == mount_key) {
            r.store(want, std::memory_order_relaxed);
            c_ride_noted.fetch_add(1);
            return;
        }
    }
    for (auto& r : g_rides) {
        uint64_t zero = 0;
        if (r.compare_exchange_strong(zero, want)) {
            c_ride_noted.fetch_add(1);
            return;
        }
    }
    c_ride_full.fetch_add(1);
}

void ai_handoff_note_passenger(uint32_t rider_key, uint32_t vehicle_key, bool on) noexcept {
    if (g_guest || (rider_key >> 28) != 0xA || rider_key == kHostCharacter) return;
    if (!on) {
        for (auto& p : g_passengers) {
            uint64_t v = p.load(std::memory_order_relaxed);
            if (v && static_cast<uint32_t>(v) == rider_key) p.compare_exchange_strong(v, 0);
        }
        return;
    }
    if (!vehicle_key || (vehicle_key >> 28) != 0xB) return;
    const uint64_t want = (static_cast<uint64_t>(vehicle_key) << 32) | rider_key;
    for (auto& p : g_passengers) {  // the same rider again: its new vehicle
        uint64_t v = p.load(std::memory_order_relaxed);
        if (v && static_cast<uint32_t>(v) == rider_key) {
            p.store(want, std::memory_order_relaxed);
            c_pass_noted.fetch_add(1);
            return;
        }
    }
    for (auto& p : g_passengers) {
        uint64_t zero = 0;
        if (p.compare_exchange_strong(zero, want)) {
            c_pass_noted.fetch_add(1);
            log::info("AiHandoff: passenger: 0x{:08X} rides in NPC vehicle 0x{:08X}: the fight rule gives it nothing meanwhile",
                      rider_key, vehicle_key);
            return;
        }
    }
    c_pass_full.fetch_add(1);
}

std::string ai_handoff_describe() {
    if (g_mode == Mode::Off && !g_fight && !c_client_took.load() && !c_client_gave.load() && !c_client_unknown.load())
        return {};
    if (g_mode == Mode::Off && g_fight)
        return std::format("AiHandoff: fight rule on | server checks {} | fought NPCs looked at {} | to the other player {} "
                           "back to the host {} failed {}",
                           c_ticks.load(), c_fight_seen.load(), c_fight_out.load(), c_fight_back.load(), c_failed.load()) +
               std::format(" | ride rule: rides noted {} handed to the rider {} table full {}", c_ride_noted.load(),
                           c_ride_out.load(), c_ride_full.load()) +
               std::format(" | catch rule: catches noted {} handed to the catcher {} table full {}", c_catch_noted.load(),
                           c_catch_out.load(), c_catch_full.load()) +
               std::format(" | talk rule{}: talks noted {} handed to the talker {} back {} ended {} not a person {} table "
                           "full {}", g_talk_on.load() ? "" : " (OFF)", c_talk_noted.load(), c_talk_out.load(),
                           c_talk_back.load(), c_talk_ended.load(), c_talk_skip.load(), c_talk_full.load()) +
               std::format(" | departures: sweeps {} NPCs back to the host {} failed {}", c_left_sweeps.load(),
                           c_left_back.load(), c_left_failed.load()) +
               std::format(" | passengers noted {} fight hand-overs skipped {} table full {}", c_pass_noted.load(),
                           c_pass_skip.load(), c_pass_full.load());
    return std::format("AiHandoff: {} | server checks {} | NPCs looked at {} (controller far, another player close: {}) | "
                       "handed over {} (of them back to the first controller {}) would {} failed {} | this client: took {} "
                       "noted {} not loaded {} notices dropped {}",
                       g_mode == Mode::On ? "on" : g_mode == Mode::Probe ? "probe" : "client part only", c_ticks.load(), c_npcs.load(), c_orphans.load(), c_handed.load(),
                       c_back.load(), c_would.load(), c_failed.load(), c_client_took.load(), c_client_gave.load(),
                       c_client_unknown.load(), c_client_dropped.load()) +
           std::format(" | ride rule: rides noted {} handed to the rider {} table full {}", c_ride_noted.load(),
                       c_ride_out.load(), c_ride_full.load()) +
           std::format(" | departures: sweeps {} NPCs back to the host {} failed {}", c_left_sweeps.load(),
                       c_left_back.load(), c_left_failed.load());
}

}  // namespace cdmp::session2
