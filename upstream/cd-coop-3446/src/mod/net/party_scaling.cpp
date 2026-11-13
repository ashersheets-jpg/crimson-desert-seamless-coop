#include "mod/net/party_scaling.h"
#include "mod/native_layer.h"

#include <atomic>
#include <cstring>
#include <deque>
#include <format>
#include <mutex>
#include <vector>

#include "common/clock.h"
#include "mod/log.h"
#include "mod/net/boss_safe.h"
#include "mod/net/enemy_relay.h"
#include "mod/net/troc_sniffer.h"

namespace cdmp::net {
namespace {

// Co-op+: message numbers for game 1.0.0.3460, read from the live Troc table (every
// TrocTr handler instance: RTTI name, opcode). The values here before were 2976-era.
constexpr uint16_t kOpVaryStatCheatReq = 2136;  // u32 key | u32 stat | u8 mode | i32 delta | u8 | u8
constexpr uint32_t kStatHP = 0x000F4240;
constexpr size_t kMaxPending = 64;

struct GiveBack {
    uint32_t enemy;
    int32_t hp;
};

std::mutex g_mutex;
scaling::Settings g_settings;
scaling::InjectedLedger g_ledger;
std::deque<GiveBack> g_pending;
std::atomic<bool> g_enabled{false};  // fast path for the sniffer thread
std::atomic<int> g_players{1};
std::atomic<bool> g_host{false};
uint64_t g_given = 0, g_given_hp = 0, g_skipped_ours = 0, g_dropped = 0, g_failed = 0;

float multiplier_locked(bool boss = false) {
    return g_host.load() ? scaling::health_multiplier(scaling::for_boss(g_settings, boss), g_players.load()) : 1.0f;
}

std::vector<uint8_t> cheat_packet(uint32_t enemy, int32_t delta) {
    uint8_t b[15] = {};
    std::memcpy(b, &enemy, 4);
    std::memcpy(b + 4, &kStatHP, 4);
    // b[8] mode 0: the change is attributed to our player, as guest_hits does.
    std::memcpy(b + 9, &delta, 4);
    std::vector<uint8_t> p(5 + sizeof b);
    const uint16_t op = kOpVaryStatCheatReq, len = sizeof b;
    std::memcpy(p.data(), &op, 2);
    p[2] = 0;
    std::memcpy(p.data() + 3, &len, 2);
    std::memcpy(p.data() + 5, b, sizeof b);
    return p;
}

}  // namespace

void scaling_configure(const scaling::Settings& s) {
    std::lock_guard lock(g_mutex);
    g_settings = s;
    g_enabled = s.enabled;
    log::info("Scaling: enemy toughness with a guest {} (health x{:.2f}, bosses x{:.2f})",
              s.enabled ? "ON" : "off", scaling::health_multiplier(s, 2),
              scaling::health_multiplier(scaling::for_boss(s, true), 2));
}

void scaling_set_enabled(bool on) {
    std::lock_guard lock(g_mutex);
    g_settings.enabled = on;
    g_enabled = on;
}

void scaling_set_2p(float regular, float boss) {
    std::lock_guard lock(g_mutex);
    g_settings.health_2p = g_settings.health[2] = regular;
    g_settings.boss_health_2p = g_settings.boss_health[2] = boss;
}

void scaling_set_party(int players, bool host) {
    const int before = g_players.exchange(players);
    const bool was_host = g_host.exchange(host);
    if (before != players || was_host != host) {
        std::lock_guard lock(g_mutex);
        const float k = multiplier_locked();
        if (g_settings.enabled) {
            log::info("Scaling: {} player(s), {} -> enemies take 1/{:.2f} of their damage", players,
                      host ? "host" : "guest (the host scales)", k);
        }
        if (k <= 1.0f) g_pending.clear();
    }
}

float scaling_multiplier(bool boss) {
    std::lock_guard lock(g_mutex);
    return multiplier_locked(boss);
}

int32_t scaling_guest_damage(uint32_t enemy, int32_t damage) {
    return scaling::scale_damage(damage, scaling_multiplier(boss_is_boss(enemy)));
}

void scaling_note_injected(uint32_t enemy, int32_t loss_hp) {
    // Whenever scaling could apply at all: the regular and boss multipliers differ, so
    // "regular is x1" must not keep a boss's guest blow out of the ledger.
    if (!g_enabled || !g_host || g_players < 2) return;
    std::lock_guard lock(g_mutex);
    g_ledger.note(enemy, loss_hp, now_ms());
}

void scaling_observe_hp(uint32_t enemy, int64_t before_x1000, int64_t after_x1000, bool by_us) {
    if (!by_us || before_x1000 <= after_x1000 || !g_enabled || !g_host || g_players < 2) return;
    const bool boss = boss_is_boss(enemy);  // boss_safe's own lock, never ours
    std::lock_guard lock(g_mutex);
    const float k = multiplier_locked(boss);
    if (k <= 1.0f) return;
    if (g_ledger.consume(enemy, before_x1000 - after_x1000, now_ms())) {
        ++g_skipped_ours;  // the guest's blow, already scaled
        return;
    }
    const int32_t back = scaling::heal_back(before_x1000, after_x1000, k);
    if (back <= 0) return;
    if (g_pending.size() >= kMaxPending) {
        ++g_dropped;
        return;
    }
    g_pending.push_back({enemy, back});
}

void scaling_tick() {
    std::deque<GiveBack> due;
    {
        std::lock_guard lock(g_mutex);
        if (g_pending.empty()) return;
        due.swap(g_pending);
    }
    for (const auto& g : due) {
        // Only hostile enemies that are fighting: never the animals, civilians or
        // guards the host happens to hit. Checked here, on the network thread, so the
        // sniffer thread never waits on the relay's locks.
        if (!enemy_hostile_in_combat(g.enemy)) continue;
        const bool ok = sniffer_inject_c2s(cheat_packet(g.enemy, g.hp));
        std::lock_guard lock(g_mutex);
        if (!ok) {
            ++g_failed;
            continue;
        }
        if (g_given++ < 20) log::info("Scaling: gave enemy 0x{:08X} back {} HP", g.enemy, g.hp);
        g_given_hp += static_cast<uint64_t>(g.hp);
    }
}

std::string scaling_hud_text() {
    if (kNativeLayer) return {};  // native_layer.h: scaling needs the sniffer, which is off there
    std::lock_guard lock(g_mutex);
    const float k = multiplier_locked();
    if (k <= 1.0f) return {};
    return std::format("Enemies: health x{:.1f} ({} players)", k, g_players.load());
}

std::string scaling_describe() {
    std::lock_guard lock(g_mutex);
    return std::format("Scaling: x{:.2f} | gave back {} times, {} HP | guest blows skipped {} | dropped {} failed {}",
                       multiplier_locked(), g_given, g_given_hp, g_skipped_ours, g_dropped, g_failed);
}

}  // namespace cdmp::net
