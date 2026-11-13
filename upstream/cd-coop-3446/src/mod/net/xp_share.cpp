#include "mod/net/xp_share.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>

#include "mod/log.h"
#include "mod/net/local_capture.h"
#include "mod/net/prologue_hold.h"
#include "mod/net/quest_advance.h"

namespace cdmp::net {
namespace {

constexpr uint16_t kOpAddSubLevelExperienceAck = 2454;  // u32 player | i64 | u8 | u32 sub-level | u64 amount | u16
constexpr uint64_t kEchoWindowMs = 10000;
constexpr size_t kMaxQueued = 64;

std::atomic<bool> g_on{true};
std::mutex g_m;
// under g_m:
struct Gain {
    uint32_t key;
    uint64_t amount;
    uint64_t at;
};
std::deque<Gain> g_expected;  // our grants still to show up as AddSubLevelExperienceAck (not to send back)
std::deque<Gain> g_incoming;  // the partner's gains to grant here
std::map<uint32_t, uint64_t> g_learnt;  // enemy kind -> its kill's experience (our natural kills)
uint32_t g_last_kill_variant = 0;
uint64_t g_last_kill_at = 0;
uint32_t g_our_key = 521;               // the skill-point bar our kills fill (521 Kliff, 522 Oongka, 523 Damiane)
constexpr uint64_t kDefaultKillXp = 15;
std::wstring g_file;      // <game folder>cdmp_kill_xp.txt ("" until xp_share_load)
bool g_learnt_dirty = false;
std::vector<std::vector<uint8_t>> g_out;
uint64_t g_sent = 0, g_granted = 0, g_dropped = 0;

bool skill_points(uint32_t key) { return key >= 521 && key <= 523; }

}  // namespace

void xp_share_configure(bool on) {
    g_on = on;
    if (on) server_trigger_hook_install();
    log::info("Co-op: shared XP {}", on ? "on (every skill-point gain of one player is granted to the other)" : "off");
}

bool xp_share_enabled() { return g_on.load(); }

void xp_share_observe_s2c(const uint8_t* packet, size_t size) {
    if (!g_on || size < 5 + 25) return;
    uint16_t op = 0;
    std::memcpy(&op, packet, 2);
    if (op != kOpAddSubLevelExperienceAck) return;
    const uint8_t* p = packet + 5;
    uint32_t player = 0, key = 0;
    uint64_t amount = 0;
    std::memcpy(&player, p, 4);
    std::memcpy(&key, p + 13, 4);
    std::memcpy(&amount, p + 17, 8);
    if (player != capture_own_player_key() || !skill_points(key) || amount == 0 || amount > 100000) return;
    const uint64_t now = GetTickCount64();
    std::lock_guard lock(g_m);
    g_our_key = key;
    if (g_last_kill_variant && now - g_last_kill_at <= 500) {  // this gain is that natural kill's
        if (g_learnt.emplace(g_last_kill_variant, amount).second) {
            log::info("XP: an enemy of kind {} gives {} experience (learnt from our kill)", g_last_kill_variant, amount);
            g_learnt_dirty = true;
        }
        g_last_kill_variant = 0;
    }
    while (!g_expected.empty() && now - g_expected.front().at > kEchoWindowMs) g_expected.pop_front();
    for (auto it = g_expected.begin(); it != g_expected.end(); ++it) {
        if (it->key == key && it->amount == amount) {  // our own grant of the partner's gain: not sent back
            g_expected.erase(it);
            return;
        }
    }
    if (prologue_held()) return;  // apart for the prologue: one's own
    std::vector<uint8_t> m{kMsgSharedXp};
    for (int i = 0; i < 4; ++i) m.push_back(static_cast<uint8_t>(key >> (8 * i)));
    for (int i = 0; i < 8; ++i) m.push_back(static_cast<uint8_t>(amount >> (8 * i)));
    if (g_out.size() < kMaxQueued) g_out.push_back(std::move(m));
    if (++g_sent <= 20 || g_sent % 50 == 0) {
        log::info("XP: we gained {} (sub-level {}); shared with the partner ({} so far)", amount, key, g_sent);
    }
}

std::vector<std::vector<uint8_t>> xp_share_take_outgoing() {
    std::lock_guard lock(g_m);
    std::vector<std::vector<uint8_t>> out;
    out.swap(g_out);
    return out;
}

void xp_share_on_message(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 13 || bytes[0] != kMsgSharedXp) return;
    uint32_t key = 0;
    uint64_t amount = 0;
    std::memcpy(&key, bytes.data() + 1, 4);
    std::memcpy(&amount, bytes.data() + 5, 8);
    if (!skill_points(key) || amount == 0 || amount > 100000) return;
    std::lock_guard lock(g_m);
    if (g_incoming.size() < kMaxQueued) g_incoming.push_back(Gain{key, amount, GetTickCount64()});
}

void xp_share_note_our_kill(uint32_t variant) {
    std::lock_guard lock(g_m);
    g_last_kill_variant = variant;
    g_last_kill_at = GetTickCount64();
}

void xp_share_partner_finished(uint32_t variant) {
    if (!g_on || prologue_held()) return;
    uint64_t amount = kDefaultKillXp;
    uint32_t key = 521;
    bool learnt = false;
    {
        std::lock_guard lock(g_m);
        if (const auto it = g_learnt.find(variant); it != g_learnt.end()) amount = it->second, learnt = true;
        key = g_our_key;
    }
    // Our own grant: its AddSubLevelExperienceAck is NOT in g_expected, so it is shared like a kill of ours.
    const bool ok = quest_advance_grant_xp(key, amount);
    log::info("XP: the other player's kill (kind {}) finished here: {} experience granted ({}){}", variant, amount,
              learnt ? "learnt for that kind" : "default", ok ? "" : " - NOT queued");
}

void xp_share_load(const std::wstring& dir) {
    std::lock_guard lock(g_m);
    g_file = dir + L"cdmp_kill_xp.txt";
    FILE* f = _wfopen(g_file.c_str(), L"r");
    if (!f) return;
    unsigned long variant = 0;
    unsigned long long amount = 0;
    size_t n = 0;
    while (fscanf(f, "%lu %llu", &variant, &amount) == 2) {
        if (variant && amount && amount <= 100000) g_learnt[static_cast<uint32_t>(variant)] = amount, ++n;
    }
    fclose(f);
    log::info("XP: {} enemy kinds' kill experience known from earlier sessions", n);
}

void xp_share_tick(bool linked) {
    {
        std::lock_guard lock(g_m);  // the learnt amounts to disk (a new kind was learnt)
        if (g_learnt_dirty && !g_file.empty()) {
            g_learnt_dirty = false;
            const std::wstring tmp = g_file + L".tmp";
            if (FILE* f = _wfopen(tmp.c_str(), L"w")) {
                for (const auto& [v, a] : g_learnt) fprintf(f, "%u %llu\n", v, static_cast<unsigned long long>(a));
                fclose(f);
                MoveFileExW(tmp.c_str(), g_file.c_str(), MOVEFILE_REPLACE_EXISTING);
            }
        }
    }
    std::deque<Gain> todo;
    {
        std::lock_guard lock(g_m);
        if (g_incoming.empty()) return;
        todo.swap(g_incoming);
    }
    for (const Gain& g : todo) {
        if (!g_on || !linked || prologue_held()) {
            ++g_dropped;
            continue;
        }
        {
            std::lock_guard lock(g_m);
            g_expected.push_back(Gain{g.key, g.amount, GetTickCount64()});
        }
        const bool ok = quest_advance_grant_xp(g.key, g.amount);
        if (!ok) {
            std::lock_guard lock(g_m);
            if (!g_expected.empty()) g_expected.pop_back();
        }
        if (++g_granted <= 20 || g_granted % 50 == 0) {
            log::info("XP: the partner gained {} (sub-level {}); granted here{}", g.amount, g.key, ok ? "" : " - NOT queued");
        }
    }
}

}  // namespace cdmp::net
