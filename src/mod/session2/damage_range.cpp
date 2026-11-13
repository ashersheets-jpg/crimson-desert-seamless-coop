// Co-op+ (2026-11-11, build 1.0.0.3576): damage_range.h.
#include "mod/session2/damage_range.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <format>

#include <safetyhook.hpp>

#include "mod/game/safe_memory.h"
#include "mod/log.h"

namespace cdmp::session2 {
namespace {

constexpr uintptr_t kSite = 0x22CE3BE;  // vsubss xmm3, xmm11, xmm6 ; vsubss xmm4, xmm9, xmm12
constexpr uint8_t kSiteBytes[9] = {0xC5, 0xA2, 0x5C, 0xDE, 0xC4, 0xC1, 0x32, 0x5C, 0xE4};
constexpr int kMaxPlayers = 4;

SafetyHookMid g_hook;
std::atomic<bool> g_on{false};
std::atomic<int> g_count{0};
std::atomic<uint32_t> g_pos[kMaxPlayers][3];  // float bits: x, y, z
std::atomic<uint64_t> c_seen{0}, c_owner_far{0}, c_moved{0};

float load(const std::atomic<uint32_t>& a) {
    const uint32_t b = a.load(std::memory_order_relaxed);
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

void on_range(safetyhook::Context& ctx) {
    c_seen.fetch_add(1, std::memory_order_relaxed);
    // Which register holds what (asm exe+0x225CE7A..0x225CF30, read again on 2026-11-11 after of1): the VICTIM
    // (r13) is x xmm11, z xmm9, y xmm10 (computed first); the field owner's character (rdi) is x xmm6, z xmm12,
    // y xmm13. The first version of this hook paired them as (6, 9, 10) / (11, 12, 13): a point made of one actor's x
    // and the other's z. It passed its test at the dock / shrine pair only because the host stood 36 m away along z
    // (log: "another player is 37 m from the victim"); 484 m from the host it never counted a hit (of1, of2).
    const float vx = ctx.xmm11.f32[0], vz = ctx.xmm9.f32[0], vy = ctx.xmm10.f32[0];
    const float ox = ctx.xmm6.f32[0], oz = ctx.xmm12.f32[0], oy = ctx.xmm13.f32[0];
    // the limits as the game is about to use them (it adds the two actors' sizes: xmm8 / xmm7)
    const float lim_h = ctx.xmm0.f32[0] + ctx.xmm8.f32[0], lim_v = ctx.xmm1.f32[0] + ctx.xmm7.f32[0];
    if (!(lim_h > 0.0f) || !(lim_v > 0.0f) || !std::isfinite(vx) || !std::isfinite(vz) || !std::isfinite(vy)) return;
    const float oh2 = (ox - vx) * (ox - vx) + (oz - vz) * (oz - vz);
    if (oh2 < lim_h * lim_h && std::fabs(oy - vy) < lim_v) return;  // the game's own answer is "in range"
    c_owner_far.fetch_add(1, std::memory_order_relaxed);
    const int n = g_count.load(std::memory_order_relaxed);
    for (int i = 0; i < n && i < kMaxPlayers; ++i) {
        const float px = load(g_pos[i][0]), py = load(g_pos[i][1]), pz = load(g_pos[i][2]);
        const float h2 = (px - vx) * (px - vx) + (pz - vz) * (pz - vz);
        if (h2 < lim_h * lim_h && std::fabs(py - vy) < lim_v) {
            ctx.xmm6.f32[0] = px;  // this player's character stands in for the field owner's, for this comparison
            ctx.xmm12.f32[0] = pz;
            ctx.xmm13.f32[0] = py;
            const uint64_t m = c_moved.fetch_add(1, std::memory_order_relaxed) + 1;
            if (m <= 5 || m % 500 == 0)
                log::info("DamageRange: a hit {:.0f} m from the field owner's character counts: another player is {:.0f} m "
                          "from the victim ({} so far)", std::sqrt(oh2), std::sqrt(h2), m);
            return;
        }
    }
}

}  // namespace

void damage_range_start(bool on, bool guest) {
    if (!on || guest) {
        log::info("DamageRange off ({}): the game's rule: no damage 80 m or more from the field owner's character",
                  guest ? "the guest: the host's server decides" : "damage_range_players=0");
        return;
    }
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint8_t have[sizeof kSiteBytes]{};
    if (!game::fast_read(base + kSite, have, sizeof have) || std::memcmp(have, kSiteBytes, sizeof have) != 0) {
        log::warn("DamageRange off: exe+0x{:X} is not the expected code (another game build?): a player more than 80 m "
                  "from the host cannot hurt or be hurt, as before", kSite);
        return;
    }
    g_hook = safetyhook::create_mid(reinterpret_cast<void*>(base + kSite), &on_range);
    g_on = static_cast<bool>(g_hook);
    log::info("DamageRange {} (damage_range_players=1): exe+0x{:X}: a hit counts when the victim is in range of ANY "
              "player's character, not only the host's", g_on.load() ? "ON" : "NOT hooked", kSite);
}

bool damage_range_on() { return g_on.load(std::memory_order_relaxed); }

void damage_range_players(const float* xyz, int count) noexcept {
    if (count < 0) count = 0;
    if (count > kMaxPlayers) count = kMaxPlayers;
    for (int i = 0; i < count; ++i) {
        for (int k = 0; k < 3; ++k) {
            uint32_t b;
            std::memcpy(&b, &xyz[i * 3 + k], 4);
            g_pos[i][k].store(b, std::memory_order_relaxed);
        }
    }
    g_count.store(count, std::memory_order_relaxed);
}

std::string damage_range_describe() {
    if (!g_on.load()) return {};
    return std::format("DamageRange: on | players known {} | range checks {} | field owner out of range {} | counted "
                       "because another player was in range {}", g_count.load(), c_seen.load(), c_owner_far.load(),
                       c_moved.load());
}

}  // namespace cdmp::session2
