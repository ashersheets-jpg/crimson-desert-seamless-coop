#include "mod/net/coop_hunt.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <iterator>
#include <mutex>
#include <vector>

#include "mod/log.h"
#include "mod/net/coop_activity.h"
#include "mod/net/enemy_relay.h"
#include "mod/net/guest_hits.h"
#include "mod/net/local_capture.h"
#include "mod/net/troc_sniffer.h"

namespace cdmp::net {
namespace {

struct Animal {
    uint32_t key;
    int points;
    const char* name;
};
constexpr Animal kAnimals[] = {
#include "mod/net/animal_names.inc"
};

const Animal* animal(uint32_t variant) {
    const auto it = std::lower_bound(std::begin(kAnimals), std::end(kAnimals), variant,
                                     [](const Animal& a, uint32_t k) { return a.key < k; });
    return it != std::end(kAnimals) && it->key == variant ? it : nullptr;
}

constexpr uint16_t kOpNotifyHandleFatalAck = 2225;  // +0 victim | +4 killer
constexpr uint16_t kOpNotifyHandleDeadAck = 2985;   // +0 victim | +4 (the victim again in the captures)
constexpr uint64_t kOursWithinMs = 8000;            // a death within this of our last blow on it is our kill
constexpr uint8_t kHuntType = 14;                   // Activity event type: u32 round | u16 score | u16 kills
constexpr uint32_t kCountdownMs = 3000;
constexpr uint32_t kRoundMs = 180000;
constexpr uint32_t kSettleMs = 1500;  // the last scores reach both sides before the result
constexpr uint32_t kOverMs = 8000;
enum Phase : uint8_t { kIdle, kCountdown, kHunt, kSettle, kOver };

struct State {
    Phase phase = kIdle;
    uint32_t round = 0, since = 0;
    int ours = 0, theirs = 0, kills = 0, their_kills = 0;
    int result = 0;
    bool reported = false, send = false;
    std::string last_kill;  // toast: "Red Deer +2"
    uint32_t last_victim = 0;
} g;
std::mutex g_mutex;
std::atomic<bool> g_test{false};

}  // namespace

void hunt_set_test(bool on) { g_test = on; }

std::atomic<uint32_t> g_dev_last_kill{0};

uint32_t hunt_dev_last_kill() { return g_dev_last_kill.load(); }

bool hunt_dev_kill() {
    static std::vector<uint32_t> tried;
    const auto loaded = enemy_loaded_variants();
    int animals = 0;
    for (const auto& [key, variant] : loaded) animals += animal(variant) != nullptr;
    log::info("Hunt: [dev] {} actors loaded, {} wild animals", loaded.size(), animals);
    int sent = 0;
    for (const auto& [key, variant] : loaded) {
        const Animal* a = animal(variant);
        // Each one once (a far one gets no answer: try the next).
        if (!a || std::find(tried.begin(), tried.end(), key) != tried.end()) continue;
        tried.push_back(key);
        std::vector<uint8_t> pkt(5 + 8, 0);
        const uint16_t op = 2210, len = 8;  // KillCheatReq: u32 key | u32 option
        std::memcpy(pkt.data(), &op, 2);
        std::memcpy(pkt.data() + 3, &len, 2);
        std::memcpy(pkt.data() + 5, &key, 4);
        const bool ok = sniffer_inject_c2s(std::move(pkt));
        log::info("Hunt: [dev] KillCheatReq on 0x{:08X} ({}){}", key, a->name, ok ? "" : " NOT sent");
        if (sent == 0) g_dev_last_kill = key;
        if (++sent == 1) return ok;  // one body (tests walk to it)
    }
    if (sent) return true;
    log::info("Hunt: [dev] no wild animal loaded");
    return false;
}

const char* hunt_animal_name(uint32_t variant) {
    const Animal* a = animal(variant);
    return a ? a->name : nullptr;
}

void hunt_observe_s2c(const uint8_t* packet, size_t size) {
    if (size < 5 + 8) return;
    uint16_t op;
    std::memcpy(&op, packet, 2);
    if (op != kOpNotifyHandleFatalAck && op != kOpNotifyHandleDeadAck) return;
    // NotifyHandleDeadAck (2985) is the one seen in captures, and it does not say who killed (both keys are the
    // victim): the kill is ours when one of our blows (or arrows) named it in the last kOursWithinMs.
    uint32_t victim, killer;
    std::memcpy(&victim, packet + 5, 4);
    std::memcpy(&killer, packet + 9, 4);
    const uint32_t own = capture_own_player_key();
    if (!own || victim == own) return;
    if (killer != own && !guest_hits_we_hit(victim, kOursWithinMs) && !g_test) return;
    const Animal* a = animal(enemy_variant(victim));
    std::lock_guard lock(g_mutex);
    if (g.phase != kHunt || victim == g.last_victim) return;  // the two notices of one death count once
    g.last_victim = victim;
    if (!a) {
        log::info("Hunt: we killed 0x{:08X} (variant 0x{:08X}): not game", victim, enemy_variant(victim));
        return;
    }
    g.ours += a->points;
    g.kills += 1;
    g.send = true;
    g.last_kill = std::format("{} +{}", a->name, a->points);
    log::info("Hunt: we brought down {} (+{}), score {}", a->name, a->points, g.ours);
}

void hunt_on_event(const uint8_t* d, size_t n) {
    if (n < 8 || g_test) return;
    uint32_t round;
    uint16_t score, kills;
    std::memcpy(&round, d, 4);
    std::memcpy(&score, d + 4, 2);
    std::memcpy(&kills, d + 6, 2);
    std::lock_guard lock(g_mutex);
    if (round != g.round || (g.phase != kHunt && g.phase != kSettle)) return;
    g.theirs = score;
    g.their_kills = kills;
}

HuntTick hunt_tick(bool on, bool connected, const std::string& who) {
    HuntTick out;
    const uint32_t now = GetTickCount();
    std::vector<uint8_t> msg;
    {
        std::lock_guard lock(g_mutex);
        if (!on || (!connected && !g_test)) {
            if (g.phase != kIdle) log::info("Hunt: over");
            g = State{};
            return out;
        }
        switch (g.phase) {
            case kIdle:
                g.phase = kCountdown, g.since = now, g.round = 1;
                break;
            case kCountdown:
                if (now - g.since >= kCountdownMs) {
                    g.phase = kHunt, g.since = now;
                    log::info("Hunt: round {} on", g.round);
                }
                break;
            case kHunt:
                if (now - g.since >= kRoundMs) g.phase = kSettle, g.since = now;
                break;
            case kSettle:
                if (now - g.since >= kSettleMs) {
                    g.phase = kOver, g.since = now;
                    g.result = g.ours > g.theirs ? 1 : g.ours < g.theirs ? 2 : 3;
                    log::info("Hunt: round {} ends {} - {}", g.round, g.ours, g.theirs);
                }
                break;
            case kOver:
                if (now - g.since >= kOverMs) {
                    const uint32_t next = g.round + 1;
                    g = State{};
                    g.phase = kCountdown, g.since = now, g.round = next;
                }
                break;
        }
        if (g.send) {
            g.send = false;
            msg.resize(8);
            const auto score = static_cast<uint16_t>(g.ours), kills = static_cast<uint16_t>(g.kills);
            std::memcpy(msg.data(), &g.round, 4);
            std::memcpy(msg.data() + 4, &score, 2);
            std::memcpy(msg.data() + 6, &kills, 2);
        }
        if (!g.last_kill.empty()) out.toast = "Hunt\n" + std::exchange(g.last_kill, {});
        if (g.phase == kOver && !g.reported) {
            g.reported = true;
            out.round_result = g.result;
        }
        if (g.phase == kHunt || g.phase == kSettle) {
            const uint32_t left = g.phase == kHunt ? (kRoundMs - std::min(kRoundMs, now - g.since)) / 1000 : 0;
            out.banner = std::format("Hunting contest vs {}   {}:{:02}   You {} - {} {}", who, left / 60, left % 60,
                                     g.ours, g.theirs, who);
        }
    }
    if (!msg.empty() && connected) activity_send(kHuntType, msg);
    return out;
}

HuntHud hunt_hud() {
    HuntHud h;
    std::lock_guard lock(g_mutex);
    const uint32_t t = GetTickCount() - g.since;
    if (g.phase == kCountdown) {
        h.title = std::to_string(std::max<uint32_t>(1, (kCountdownMs - std::min(kCountdownMs, t) + 999) / 1000));
        h.subtitle = "Hunting contest: bring down the most game in 3 minutes (big game counts more)";
        h.color = 0xFF82C8E8;
        h.alpha = 1.0f - static_cast<float>(t % 1000) / 1600.0f;
    } else if (g.phase == kHunt && t < 1200) {
        h.title = "Hunt!";
        h.color = 0xFF82C8E8;
        h.alpha = 1.0f - static_cast<float>(t) / 1200.0f;
    } else if (g.phase == kOver) {
        h.title = g.result == 1 ? "Victory" : g.result == 2 ? "Defeat" : "A Draw";
        h.subtitle = std::format("{} points ({} kills) to {} ({} kills)", g.ours, g.kills, g.theirs, g.their_kills);
        h.color = g.result == 2 ? 0xFF2830C4 : 0xFF82C8E8;
        h.alpha = t < kOverMs - 1000 ? 1.0f : std::max(0.0f, (kOverMs - t) / 1000.0f);
    }
    return h;
}

}  // namespace cdmp::net
