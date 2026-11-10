#include "mod/net/coop_travel.h"

#include <windows.h>

#include <cmath>
#include <format>
#include <mutex>
#include <vector>

#include "mod/input/actions.h"
#include "mod/log.h"
#include "mod/native_layer.h"
#include "mod/net/coop_activity.h"

namespace cdmp::net {
namespace {

constexpr uint8_t kTravelType = 20;  // Activity event type: u8 kind
constexpr uint8_t kSummon = 1, kTravelled = 2;
constexpr uint32_t kRequestMs = 20000;
constexpr uint32_t kCooldownMs = 30000;
constexpr float kTravelM = 250.0f;     // covered within kTravelWindowMs = a fast travel or a long trip
constexpr uint32_t kTravelWindowMs = 5000;
constexpr float kArrivedM = 6.0f;      // the request is answered once we stand this close to them

std::mutex g_mutex;
uint8_t g_pending = 0;         // their request (0 = none)
uint32_t g_pending_at = 0;
uint32_t g_summoned_at = 0;    // our last summon
std::optional<proto::Vec3> g_anchor;  // our position kTravelWindowMs ago
uint32_t g_anchor_at = 0;

float dist2(const proto::Vec3& a, const proto::Vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return dx * dx + dz * dz;
}

}  // namespace

int travel_request() {
    std::lock_guard lock(g_mutex);
    return g_pending;
}

void travel_on_event(const uint8_t* d, size_t n) {
    if (n < 1 || (d[0] != kSummon && d[0] != kTravelled)) return;
    std::lock_guard lock(g_mutex);
    g_pending = d[0];
    g_pending_at = GetTickCount();
}

TravelTick travel_tick(bool connected, const std::optional<proto::Vec3>& me, const std::optional<proto::Vec3>& partner,
                       const std::string& name, bool focused, uint32_t now) {
    TravelTick out;
    const std::string who = name.empty() ? std::string("Your friend") : name;
    static input::Edge summon_key;
    const bool summon = summon_key(input::action_down(input::Action::Summon, focused));
    std::vector<uint8_t> send;
    {
        std::lock_guard lock(g_mutex);
        if (!connected) {
            g_pending = 0;
            g_anchor.reset();
            return out;
        }
        // Our summon.
        if (summon) {
            if (g_summoned_at && now - g_summoned_at < kCooldownMs) {
                out.toast = std::format("Co-op\nYou can call {} again in {} s", who,
                                        (kCooldownMs - (now - g_summoned_at) + 999) / 1000);
            } else if (partner && me && dist2(*partner, *me) < kArrivedM * kArrivedM) {
                out.toast = std::format("Co-op\n{} is already with you", who);
            } else {
                g_summoned_at = now ? now : 1;
                send = {kSummon};
                out.toast = std::format("Co-op\nYou asked {} to come to you", who);
                log::info("Travel: we summon {}", who);
            }
        }
        // Our own long trip.
        if (me) {
            if (!g_anchor || now - g_anchor_at > kTravelWindowMs) {
                if (g_anchor && dist2(*g_anchor, *me) > kTravelM * kTravelM && send.empty()) {
                    send = {kTravelled};
                    log::info("Travel: we travelled {:.0f} m: telling {}", std::sqrt(dist2(*g_anchor, *me)), who);
                }
                g_anchor = me;
                g_anchor_at = now;
            }
        }
        // Their request.
        if (g_pending) {
            const bool arrived = partner && me && dist2(*partner, *me) < kArrivedM * kArrivedM;
            if (arrived || now - g_pending_at > kRequestMs) {
                g_pending = 0;
            } else {
                if (kNativeLayer) {  // shared world: answered by holding interact (render/hud.cpp), no hotkey
                    out.line = g_pending == kSummon ? std::format("{} asks you to come to them", who)
                                                    : std::format("{} travelled far away", who);
                } else {
                    out.line = g_pending == kSummon
                                   ? std::format("{} asks you to come to them:   {{GoToFriend}} go", who)
                                   : std::format("{} travelled far away:   {{GoToFriend}} follow them", who);
                }
            }
        }
    }
    if (!send.empty()) activity_send(kTravelType, send);
    return out;
}

}  // namespace cdmp::net
