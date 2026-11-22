#include "mod/net/coop_race.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <mutex>
#include <vector>

#include "mod/log.h"
#include "mod/net/coop_activity.h"

namespace cdmp::net {
namespace {

constexpr uint8_t kRaceType = 24;  // Activity event type: u32 round | u32 finish ms
constexpr uint32_t kCountdownMs = 3000;
constexpr uint32_t kMaxMs = 600000;      // a round ends after 10 minutes whatever happens
constexpr uint32_t kWaitLoserMs = 60000;  // after the first finish, the other has this long (then it is a win)
constexpr uint32_t kOverMs = 8000;
constexpr float kFinishM = 12.0f;
constexpr float kNewFinishM = 30.0f;     // a waypoint this far from the last finish starts a new round
enum Phase : uint8_t { kIdle, kWait, kCountdown, kRace, kOver };

struct State {
    Phase phase = kIdle;
    uint32_t round = 0, since = 0;
    std::array<float, 3> finish{};
    std::optional<std::array<float, 3>> last_finish;
    uint32_t ours = 0, theirs = 0;  // finish times (ms from the start; 0 = not yet)
    uint32_t first_at = 0;          // when the first of us finished
    int result = 0;
    bool reported = false, send = false;
} g;
std::optional<std::array<float, 3>> g_own_wp, g_partner_wp;
std::mutex g_mutex;

float flat(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return std::hypot(a[0] - b[0], a[2] - b[2]);
}

std::string race_clock(uint32_t ms) { return std::format("{}:{:02}.{}", ms / 60000, ms / 1000 % 60, ms / 100 % 10); }

}  // namespace

void race_set_own_waypoint(std::optional<std::array<float, 3>> pos) {
    std::lock_guard lock(g_mutex);
    g_own_wp = pos;
}

void race_set_partner_waypoint(std::optional<std::array<float, 3>> pos) {
    std::lock_guard lock(g_mutex);
    g_partner_wp = pos;
}

std::optional<std::array<float, 3>> race_any_waypoint() {
    std::lock_guard lock(g_mutex);
    return g_own_wp ? g_own_wp : g_partner_wp;
}

void race_on_event(const uint8_t* d, size_t n) {
    if (n < 8) return;
    uint32_t round, ms;
    std::memcpy(&round, d, 4);
    std::memcpy(&ms, d + 4, 4);
    std::lock_guard lock(g_mutex);
    if (round != g.round || g.phase != kRace || g.theirs) return;
    g.theirs = std::max<uint32_t>(ms, 1);
    if (!g.first_at) g.first_at = GetTickCount();
    log::info("Race: the other player finished round {} in {}", round, race_clock(g.theirs));
}

RaceTick race_tick(bool on, bool connected, bool host, const std::string& who, std::optional<std::array<float, 3>> me,
                   std::optional<std::array<float, 3>> partner) {
    RaceTick out;
    const uint32_t now = GetTickCount();
    std::vector<uint8_t> msg;
    {
        std::lock_guard lock(g_mutex);
        if (!on || !connected) {
            if (g.phase != kIdle) log::info("Race: over");
            const auto keep = g.last_finish;
            g = State{};
            g.last_finish = keep;
            return out;
        }
        // The finish: the host's waypoint, else the guest's (the same on both sides).
        const auto& host_wp = host ? g_own_wp : g_partner_wp;
        const auto& guest_wp = host ? g_partner_wp : g_own_wp;
        const std::optional<std::array<float, 3>> finish = host_wp ? host_wp : guest_wp;
        switch (g.phase) {
            case kIdle:
                g.phase = kWait, g.since = now;
                break;
            case kWait:
                if (finish && (!g.last_finish || flat(*finish, *g.last_finish) > kNewFinishM)) {
                    g.phase = kCountdown, g.since = now, g.finish = *finish, ++g.round;
                    g.ours = g.theirs = g.first_at = 0, g.reported = false, g.result = 0;
                    log::info("Race: round {} to ({:.0f}, {:.0f})", g.round, g.finish[0], g.finish[2]);
                }
                break;
            case kCountdown:
                if (now - g.since >= kCountdownMs) g.phase = kRace, g.since = now;
                break;
            case kRace: {
                const uint32_t t = now - g.since;
                if (!g.ours && me && flat(*me, g.finish) < kFinishM) {
                    g.ours = std::max<uint32_t>(t, 1);
                    if (!g.first_at) g.first_at = now;
                    g.send = true;
                    log::info("Race: we finished round {} in {}", g.round, race_clock(g.ours));
                }
                const bool both = g.ours && g.theirs;
                const bool lapsed = g.first_at && now - g.first_at > kWaitLoserMs;
                if (both || lapsed || t > kMaxMs) {
                    if (g.ours && (!g.theirs || g.ours <= g.theirs)) g.result = 1;
                    else if (g.theirs) g.result = 2;
                    else g.result = 3;  // nobody made it
                    g.phase = kOver, g.since = now, g.last_finish = g.finish;
                    log::info("Race: round {} ends: us {} them {}", g.round, g.ours ? race_clock(g.ours) : "-",
                              g.theirs ? race_clock(g.theirs) : "-");
                }
                break;
            }
            case kOver:
                if (now - g.since >= kOverMs) g.phase = kWait, g.since = now;
                break;
        }
        if (g.send) {
            g.send = false;
            msg.resize(8);
            std::memcpy(msg.data(), &g.round, 4);
            std::memcpy(msg.data() + 4, &g.ours, 4);
        }
        if (g.phase == kOver && !g.reported) {
            g.reported = true;
            out.round_result = g.result == 3 ? 0 : g.result;
        }
        if (g.phase == kWait) {
            out.banner = finish ? std::format("Race vs {}: set a NEW waypoint on the map for the next race   "
                                              "({{Challenge}} ends)", who)
                                : std::format("Race vs {}: the host sets a waypoint on the map (right-click): "
                                              "that is the finish   ({{Challenge}} ends)", who);
        } else if (g.phase == kRace) {
            const uint32_t t = now - g.since;
            const float mine = me ? flat(*me, g.finish) : -1, theirs = partner ? flat(*partner, g.finish) : -1;
            out.banner = std::format("Race to the waypoint vs {}   {}   you {}   {} {}", who, race_clock(t),
                                     g.ours ? "finished " + race_clock(g.ours) : std::format("{:.0f} m", mine), who,
                                     g.theirs ? "finished " + race_clock(g.theirs)
                                     : theirs >= 0 ? std::format("{:.0f} m", theirs) : std::string("?"));
        }
    }
    if (!msg.empty() && connected) activity_send(kRaceType, msg);
    return out;
}

RaceHud race_hud() {
    RaceHud h;
    std::lock_guard lock(g_mutex);
    const uint32_t t = GetTickCount() - g.since;
    if (g.phase == kCountdown) {
        h.title = std::to_string(std::max<uint32_t>(1, (kCountdownMs - std::min(kCountdownMs, t) + 999) / 1000));
        h.subtitle = "Race to the waypoint: first within 12 m wins (horse, glider, anything goes)";
        h.color = 0xFF82C8E8;
        h.alpha = 1.0f - static_cast<float>(t % 1000) / 1600.0f;
    } else if (g.phase == kRace && t < 1200) {
        h.title = "Go!";
        h.color = 0xFF82C8E8;
        h.alpha = 1.0f - static_cast<float>(t) / 1200.0f;
    } else if (g.phase == kOver) {
        h.title = g.result == 1 ? "Victory" : g.result == 2 ? "Defeat" : "Nobody Finished";
        h.subtitle = std::format("You {}   them {}", g.ours ? race_clock(g.ours) : "-", g.theirs ? race_clock(g.theirs) : "-");
        h.color = g.result == 2 ? 0xFF2830C4 : 0xFF82C8E8;
        h.alpha = t < kOverMs - 1000 ? 1.0f : std::max(0.0f, (kOverMs - t) / 1000.0f);
    }
    return h;
}

}  // namespace cdmp::net
