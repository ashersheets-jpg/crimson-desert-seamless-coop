#include "mod/net/kill_feed.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <deque>
#include <format>
#include <mutex>

#include "mod/log.h"
#include "mod/net/coop_activity.h"
#include "mod/net/coop_hunt.h"
#include "mod/net/enemy_relay.h"
#include "mod/net/guest_hits.h"
#include "mod/net/local_capture.h"
#include "mod/net/loot_twin.h"
#include "mod/net/native_suppress.h"
#include "mod/net/xp_share.h"
#include "mod/net/session_summary.h"

namespace cdmp::net {
namespace {

constexpr uint8_t kType = 23;
constexpr uint16_t kOpNotifyHandleFatalAck = 2863, kOpNotifyHandleDeadAck = 2316;  // coop_hunt.cpp
constexpr uint64_t kOursWithinMs = 3000;
constexpr uint32_t kShowMs = 6000;
constexpr size_t kMaxLines = 4;

struct Line {
    uint32_t at;  // 0 = not stamped yet (the tick does it)
    std::string name;
    bool ours;
};

std::mutex g_mutex;
std::atomic<bool> g_on{false};
uint32_t g_last_victim = 0;
std::deque<Line> g_lines;
std::deque<std::string> g_to_send;

std::string with_article(const std::string& name) {
    if (name.empty()) return "an enemy";
    return (std::strchr("AEIOU", name[0]) ? "an " : "a ") + name;
}

}  // namespace

void kill_feed_observe_s2c(const uint8_t* packet, size_t size) {
    if (!g_on.load() || size < 5 + 8) return;
    uint16_t op;
    std::memcpy(&op, packet, 2);
    if (op != kOpNotifyHandleFatalAck && op != kOpNotifyHandleDeadAck) return;
    uint32_t victim, killer;
    std::memcpy(&victim, packet + 5, 4);
    std::memcpy(&killer, packet + 9, 4);
    const uint32_t own = capture_own_player_key();
    if (!own || victim == own) return;
    if (killer != own && !guest_hits_we_hit(victim, kOursWithinMs)) return;
    {
        std::lock_guard lock(g_mutex);
        if (victim == g_last_victim) return;  // the two notices of one death (fatal, then dead)
        g_last_victim = victim;
    }
    // Co-op+ (2026-11-07): the other player's killing blow, finished by the mod in our world (they count it), and our
    // own hidden enemies the mod kills for loot (the copy's death already counted it) are not our kills.
    if (suppress_is_hidden(victim) || loot_is_twin(victim)) {
        log::info("Kill feed: 0x{:08X} died but is not our kill (our hidden twin, killed for loot)", victim);
        return;
    }
    if (guest_hits_finished_for_partner(victim, 30000)) {
        log::info("Kill feed: 0x{:08X} died but is not our kill (the other player's blow, finished here)", victim);
        xp_share_partner_finished(enemy_variant(victim));  // its experience: granted here, shared (xp_share.h)
        return;
    }
    if (killer == own) xp_share_note_our_kill(enemy_variant(victim));  // learns this kind's kill experience
    const char* name = hunt_animal_name(enemy_variant(victim));
    std::lock_guard lock(g_mutex);
    g_lines.push_back({0, name ? name : "", true});
    g_to_send.push_back(name ? name : "");
    log::info("Kill feed: we took down 0x{:08X} ({})", victim, name ? name : "an enemy");
    summary_note(SummaryStat::OurKill);
}

void kill_feed_on_event(const uint8_t* d, size_t n) {
    std::string name(reinterpret_cast<const char*>(d), std::min<size_t>(n, 48));
    for (char& c : name) {
        if (static_cast<unsigned char>(c) < 0x20) c = ' ';
    }
    std::lock_guard lock(g_mutex);
    g_lines.push_back({0, std::move(name), false});
    summary_note(SummaryStat::TheirKill);
}

std::vector<FeedLine> kill_feed_tick(bool connected, const std::string& partner_name, uint32_t now) {
    g_on = connected;
    std::vector<FeedLine> out;
    std::deque<std::string> send;
    {
        std::lock_guard lock(g_mutex);
        if (!connected) {
            g_lines.clear();
            g_to_send.clear();
            return out;
        }
        send.swap(g_to_send);
        for (auto& l : g_lines) {
            if (!l.at) l.at = now ? now : 1;
        }
        while (!g_lines.empty() && (now - g_lines.front().at > kShowMs || g_lines.size() > kMaxLines)) g_lines.pop_front();
        const std::string who = partner_name.empty() ? std::string("Your friend") : partner_name;
        for (const auto& l : g_lines) out.push_back({(l.ours ? std::string("You") : who) + " defeated " + with_article(l.name), l.ours});
    }
    for (const auto& s : send) activity_send(kType, std::vector<uint8_t>(s.begin(), s.end()));
    return out;
}

}  // namespace cdmp::net
