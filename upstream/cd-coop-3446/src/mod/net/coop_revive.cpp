#include "mod/net/coop_revive.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <format>
#include <mutex>

#include "mod/input/actions.h"
#include "mod/log.h"
#include "mod/net/coop_activity.h"
#include "mod/net/revive_prompt.h"
#include "mod/net/session_summary.h"

namespace cdmp::net {
namespace {

constexpr uint8_t kReviveYouType = 18;  // Activity event type (no data)
constexpr uint32_t kBleedOutMs = 30000;
constexpr uint32_t kHoldMs = 3000;
constexpr float kReachM = 3.0f;

std::atomic<bool> g_on{true}, g_test{false};
std::atomic<bool> g_revived_by_partner{false};  // set by the event, consumed by the tick (network thread)
std::mutex g_mutex;
uint32_t g_down_since = 0;          // we are down since (0 = not)
uint32_t g_partner_down_since = 0;  // the other player is down since
uint32_t g_hold_since = 0;          // our hold on the revive action (0 = not holding)
bool g_sent = false;                // our revive for this downing was sent

}  // namespace

void revive_partner_set(bool on, bool test) {
    g_on = on;
    g_test = test;
}

void revive_partner_on_event() { g_revived_by_partner = true; }

std::atomic<bool> g_partner_present{false};
bool revive_partner_present() { return g_partner_present.load(std::memory_order_relaxed); }

ReviveTick revive_partner_tick(bool connected, float hp, float hp_max, const proto::Vec3& pos,
                               const std::optional<ReviveRemote>& partner, const std::string& name, bool focused,
                               uint32_t now) {
    ReviveTick out;
    g_partner_present.store(connected && partner.has_value(), std::memory_order_relaxed);
    std::lock_guard lock(g_mutex);
    const std::string who = name.empty() ? std::string("Your friend") : name;
    const bool we_down = hp_max > 0 && hp <= 0;
    const bool partner_down = partner && partner->hp_max > 0 && partner->hp <= 0;
    // Downed side.
    if (!we_down) {
        if (g_down_since && g_revived_by_partner.load()) out.toast = std::format("Co-op\n{} revived you", who);
        g_down_since = 0;
        g_revived_by_partner = false;
    } else {
        if (!g_down_since) g_down_since = now ? now : 1;
        const uint32_t down_ms = now - g_down_since;
        if (g_revived_by_partner.exchange(false)) {
            if (g_on && connected && down_ms < kBleedOutMs + 2000 && revive_in_place(now)) {
                log::info("Revive: {} revived us", who);
                summary_note(SummaryStat::TheyRevivedUs);
            }
        } else if (g_on && connected && partner && !partner_down && down_ms < kBleedOutMs) {
            out.line = std::format("{} can revive you ({} s)", who, (kBleedOutMs - down_ms + 999) / 1000);
        }
    }
    // Reviver side: the other player is down near us.
    if (!partner_down) {
        g_partner_down_since = 0;
        g_hold_since = 0;
        g_sent = false;
        return out;
    }
    if (!g_partner_down_since) g_partner_down_since = now ? now : 1;
    const bool can_revive = g_on && connected && (!we_down || g_test.load()) &&
                            now - g_partner_down_since < kBleedOutMs && !g_sent;
    const float dx = partner->pos.x - pos.x, dz = partner->pos.z - pos.z;
    const bool in_reach = dx * dx + dz * dz <= kReachM * kReachM;
    if (!can_revive || !in_reach) {
        g_hold_since = 0;
        if (can_revive && out.line.empty()) out.line = std::format("{} is down: go to them to revive them", who);
        return out;
    }
    // B17 (2026-11-09): revive by interacting -- the game's interact key (E) held next to the downed friend counts
    // too, as well as the ReviveFriend action (Shift+F7, pad View + LB).
    const bool interact = focused && (GetAsyncKeyState('E') & 0x8000) != 0;
    const bool holding = interact || input::action_down(input::Action::ReviveFriend, focused);
    if (!holding) {
        g_hold_since = 0;
        if (out.line.empty()) out.line = std::format("Hold E or {{ReviveFriend}} to revive {}", who);
        return out;
    }
    if (!g_hold_since) g_hold_since = now ? now : 1;
    const uint32_t held = now - g_hold_since;
    if (held < kHoldMs) {
        out.line = std::format("Reviving {}... {}%", who, held * 100 / kHoldMs);
        return out;
    }
    g_sent = true;
    g_hold_since = 0;
    activity_send(kReviveYouType, {});
    out.toast = std::format("Co-op\nYou revived {}", who);
    log::info("Revive: we revived {}", who);
    summary_note(SummaryStat::WeRevived);
    return out;
}

}  // namespace cdmp::net
