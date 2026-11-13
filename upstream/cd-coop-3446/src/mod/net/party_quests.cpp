#include "mod/net/party_quests.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <format>
#include <iterator>
#include <mutex>
#include <unordered_map>

#include "mod/net/troc_sniffer.h"
#include <vector>

#include "mod/log.h"
#include "mod/net/coop_activity.h"

namespace cdmp::net {
namespace {

struct MissionName {
    uint32_t key;
    char kind;  // 'M' mission, 'C' challenge, 'O' other
    const char* name;
};
constexpr MissionName kNames[] = {
#include "mod/net/mission_names.inc"
};

constexpr uint16_t kOpUpdateMissionListAck = 2558;
constexpr uint8_t kQuestType = 13;  // Activity event type
constexpr uint8_t kUnderWay = 2, kDone = 5;

const MissionName* find(uint32_t key) {
    const auto it = std::lower_bound(std::begin(kNames), std::end(kNames), key,
                                     [](const MissionName& m, uint32_t k) { return m.key < k; });
    return it != std::end(kNames) && it->key == key ? it : nullptr;
}

std::mutex g_mutex;
std::unordered_map<uint32_t, uint8_t> g_state;  // our missions' last state
std::deque<uint32_t> g_out;                     // our completions to tell the other player
std::deque<uint32_t> g_in;                      // theirs, to show

uint32_t g_test_key = 0;
uint64_t g_connected_at = 0;

bool plausible_state(uint8_t s) { return s == 0 || s == 1 || s == kUnderWay || s == kDone || s == 0xFF; }

}  // namespace

void party_quests_set_test(uint32_t key) { g_test_key = key; }

std::string party_quests_name(uint32_t key) {
    const MissionName* m = find(key);
    return m ? m->name : "";
}

void party_quests_observe_s2c(const uint8_t* packet, size_t size) {
    if (size < 5 + 12) return;
    uint16_t op;
    std::memcpy(&op, packet, 2);
    if (op != kOpUpdateMissionListAck) return;
    const uint8_t* p = packet + 5;
    const size_t n = size - 5;
    // The records differ in length (25 to 55 bytes), so walk the keys: every u32 that is a named mission with a
    // plausible state after it. A key seen for the first time only sets its baseline (the login lists every mission).
    std::lock_guard lock(g_mutex);
    for (size_t i = 5; i + 6 <= n; ++i) {
        uint32_t key;
        std::memcpy(&key, p + i, 4);
        const MissionName* m = find(key);
        if (!m || m->kind == 'O' || !plausible_state(p[i + 4]) || !plausible_state(p[i + 5])) continue;
        const uint8_t s = p[i + 4];
        auto [it, fresh] = g_state.try_emplace(key, s);
        if (!fresh && it->second != s) {
            if (s == kDone && it->second == kUnderWay && g_out.size() < 16) {
                g_out.push_back(key);
                log::info("Party journal: we completed {} \"{}\"", key, m->name);
            }
            it->second = s;
        }
        i += 5;
    }
}

static std::atomic<bool> g_spoiler_guard{false};  // Co-op+ (T39)
void party_quests_set_spoiler_guard(bool on) { g_spoiler_guard = on; }

void party_quests_on_event(const uint8_t* d, size_t n) {
    if (n < 4) return;
    uint32_t key;
    std::memcpy(&key, d, 4);
    std::lock_guard lock(g_mutex);
    if (find(key) && g_in.size() < 16) g_in.push_back(key);
}

std::string party_quests_tick(bool connected, const std::string& who) {
    {
        // Co-op+ (quest sync): the mission list's totals once it settles after a load (how far this save's story is).
        static size_t logged = 0, last_size = 0;
        static uint64_t changed_at = 0;
        static bool was_loading = false;
        std::lock_guard lock(g_mutex);
        // A load (another save, a co-op world) starts a new baseline: its mission list is not "completions".
        const bool loading = sniffer_world_loading();
        if (loading && !was_loading) {
            g_state.clear();
            g_out.clear();
            logged = 0;
        }
        was_loading = loading;
        const uint64_t now = GetTickCount64();
        if (g_state.size() != last_size) last_size = g_state.size(), changed_at = now;
        if (last_size && last_size != logged && now - changed_at > 5000) {
            size_t done = 0;
            for (const auto& [k, st] : g_state) done += st == kDone;
            log::info("Party journal: {} missions known, {} done", last_size, done);
            logged = last_size;
        }
    }
    std::vector<uint32_t> out;
    uint32_t in = 0;
    bool ours_too = false, ours_done = false;
    {
        std::lock_guard lock(g_mutex);
        if (!connected) {
            g_out.clear();
            g_in.clear();
            g_connected_at = 0;
            return {};
        }
        const uint64_t now = GetTickCount64();
        if (!g_connected_at) g_connected_at = now;
        if (g_test_key && now - g_connected_at > 60000) {
            log::info("Party journal: [dev] quest_test: we completed {}", g_test_key);
            g_out.push_back(g_test_key);
            g_test_key = 0;
        }
        out.assign(g_out.begin(), g_out.end());
        g_out.clear();
        if (!g_in.empty()) {
            in = g_in.front();
            g_in.pop_front();
            const auto s = g_state.find(in);
            ours_too = s != g_state.end() && s->second == kUnderWay;
            ours_done = s != g_state.end() && s->second == kDone;
        }
    }
    for (uint32_t key : out) {
        std::vector<uint8_t> b(4);
        std::memcpy(b.data(), &key, 4);
        activity_send(kQuestType, b);
    }
    if (!in) return {};
    const MissionName* m = find(in);
    if (!m) return {};
    const bool hide = g_spoiler_guard.load() && !ours_too && !ours_done;  // Co-op+ (T39): not even in the log
    log::info("Party journal: {} completed {} \"{}\"{}", who, in, hide ? "(hidden)" : m->name,
              ours_too ? " (ours too)" : "");
    const std::string title = m->kind == 'C' ? std::format("{}: Challenge Complete", who)
                                             : std::format("{}: Quest Updated", who);
    std::string body = m->name;
    if (hide) {
        log::info("Party journal: that quest is not one of ours yet: its name is hidden (spoiler guard)");
        body = "A quest you have not reached yet (name hidden: spoiler guard)";
    } else if (ours_too) body += "   (you are on this quest too: finish it together)";
    else if (ours_done) body += "   (you have done this one)";
    return title + "\n" + body;
}

}  // namespace cdmp::net
