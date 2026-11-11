// Co-op+ (2026-11-11, build 1.0.0.3576): ai_handoff.h.
#include "mod/session2/ai_handoff.h"

#include <windows.h>

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

constexpr uint32_t kHostCharacter = 0xA0100001;
constexpr uint16_t kOpChangeAiControlAck = 2660;
constexpr uint32_t kPrivateMagic = 0xC0DEA1C7;   // our 12-byte form of 2660: magic | actor key | new controller
constexpr uint32_t kTickEntry = 0xC0DE0008;      // trade_probe's entry for the server-thread check
constexpr uintptr_t kServerActorMgrSlot = 0x71C0608;
constexpr uintptr_t kClientWorldSlot = 0x71C0100;
constexpr uintptr_t kFnPosition = 0x5ACE70;      // (transform comp, buf)
constexpr uintptr_t kFnAiSessionBump = 0x2B64FD0, kFnMoveSessionBump = 0x2B66830, kFnActionReset = 0x222C920;
constexpr uintptr_t kFnClientControlMode = 0x5AF410;  // (client actor): recomputes the control mode byte actor+0xC8
constexpr float kSector = 0.015625f;             // 1 / 64 m
constexpr float kFarM = 60.0f, kNearM = 30.0f;   // controller farther than kFarM, another player within kNearM
constexpr int kNeedChecks = 2;                   // the same answer on two checks in a row (about 2 s)
constexpr int kMaxPerTick = 48;

std::atomic<uint64_t> c_ticks{0}, c_npcs{0}, c_orphans{0}, c_handed{0}, c_would{0}, c_failed{0}, c_back{0};
std::atomic<uint64_t> c_client_took{0}, c_client_gave{0}, c_client_unknown{0}, c_client_dropped{0};
std::atomic<uint32_t> g_local_user{0};

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

// The server's actor list: the players (out) and every NPC nobody who controls it can see while another player can.
// Plain data only (SEH). Returns the number of candidates; n_players and n_npcs are filled.
int scan(PlayerAt* pl, int pl_cap, int& n_players, Cand* out, int cap, int& n_npcs, int& n_orphans) noexcept {
    int n = 0;
    n_players = n_npcs = n_orphans = 0;
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
        for (uint32_t i = 0; i < cnt && n < cap; ++i) {  // pass 2: the NPCs
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
            if (a >= 0 || b < 0) continue;  // its controller is near enough, or nobody else is close
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

void ai_handoff_start(std::string_view mode, bool guest) {
    g_mode = mode == "1" ? Mode::On : mode == "probe" ? Mode::Probe : Mode::Off;
    g_guest = guest;
    log::info("AiHandoff {} (ai_handoff={}{}): {}", g_mode == Mode::On ? "ON" : g_mode == Mode::Probe ? "probe" : "off",
              mode, guest ? ", guest" : "",
              g_mode == Mode::Off ? "an NPC keeps the controller it was born with"
              : guest             ? "this client takes over an NPC's AI when the host's server says so"
                                  : "an NPC whose controller's player is more than 60 m away goes to a player within 30 m "
                                    "of it (two checks in a row)");
}

void ai_handoff_tick() noexcept {
    if ((g_mode == Mode::Off && !damage_range_on()) || g_guest) return;
    try {
        static uint64_t next = 0;
        const uint64_t now = GetTickCount64();
        if (now < next || !server_user2_in_world()) return;
        next = now + 1000;
        std::vector<uint8_t> pl(30, 0);  // the host's own trade request with our entry: the server thread runs the check
        std::memcpy(&pl[14], &kHostCharacter, 4);
        std::memcpy(&pl[18], &kHostCharacter, 4);
        std::memcpy(&pl[26], &kTickEntry, 4);
        net::sniffer_inject_c2s(lrl::make(3111, pl, 0x00));
    } catch (...) {
    }
}

void ai_handoff_server_tick() noexcept {
    if ((g_mode == Mode::Off && !damage_range_on()) || g_guest) return;
    try {
        static std::unordered_map<uint32_t, std::pair<uint32_t, int>> pending;  // actor key -> (to, checks in a row)
        static std::unordered_map<uint32_t, uint32_t> born;                     // actor key -> its first controller
        PlayerAt pl[4];
        static Cand cand[512];
        int n_players = 0, n_npcs = 0, n_orphans = 0;
        const int n = scan(pl, 4, n_players, cand, 512, n_npcs, n_orphans);
        {  // damage_range.h: where the users' characters are now
            float xyz[4 * 3];
            for (int k = 0; k < n_players && k < 4; ++k) {
                xyz[k * 3] = pl[k].x;
                xyz[k * 3 + 1] = pl[k].y;
                xyz[k * 3 + 2] = pl[k].z;
            }
            damage_range_players(xyz, n_players);
        }
        if (g_mode == Mode::Off) return;
        c_ticks.fetch_add(1);
        c_npcs.store(static_cast<uint64_t>(n_npcs));
        c_orphans.store(static_cast<uint64_t>(n_orphans));
        uint32_t host_user = 0;
        for (int k = 0; k < n_players; ++k) {
            if (pl[k].key == kHostCharacter) host_user = pl[k].user;
        }
        if (n_players < 2 || !host_user) {
            pending.clear();
            return;
        }
        // Facts, every 10 s (the first 40 times): the 10 NPCs nearest to the other player, as the server has them.
        static uint64_t next_dump = 0;
        static int dumps = 0;
        if (const uint64_t now = GetTickCount64(); now >= next_dump && dumps < 40) {
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

std::string ai_handoff_describe() {
    if (g_mode == Mode::Off && !c_client_took.load() && !c_client_gave.load() && !c_client_unknown.load()) return {};
    return std::format("AiHandoff: {} | server checks {} | NPCs looked at {} (controller far, another player close: {}) | "
                       "handed over {} (of them back to the first controller {}) would {} failed {} | this client: took {} "
                       "noted {} not loaded {} notices dropped {}",
                       g_mode == Mode::On ? "on" : g_mode == Mode::Probe ? "probe" : "client part only", c_ticks.load(), c_npcs.load(), c_orphans.load(), c_handed.load(),
                       c_back.load(), c_would.load(), c_failed.load(), c_client_took.load(), c_client_gave.load(),
                       c_client_unknown.load(), c_client_dropped.load());
}

}  // namespace cdmp::session2
