#include "mod/net/weather_sync.h"

#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <utility>

#include "mod/log.h"
#include "mod/net/local_capture.h"
#include "mod/net/remote_puppet.h"
#include "mod/net/troc_sniffer.h"

namespace cdmp::net {
namespace {

constexpr uint16_t kOpPrepareSequencerAck = 2356;  // 3479
constexpr uint16_t kOpDeleteSequencerAck = 2659;   // 3479
constexpr uint16_t kOpStageBranchStateAck = 2752;     // u8 4 | u32 player | u64 sequencer | u32 branch | u32 previous |
                                                      // u32 hash | u32 0 | u32 hash | u32 counter
constexpr uint16_t kOpStageBranchStateReq = 2644;     // u64 sequencer | u32 branch | u16 0 (every ~60 s)
constexpr uint16_t kOpStageOnBranchEventAck = 2369;   // u8 4 | u32 player | u64 sequencer | u32 branch | u8 0
constexpr uint16_t kOpUpdateStageChartEventReq = 2200;  // u64 sequencer | u32 branch | u32 index | u8 value
constexpr uint16_t kOpUpdateStageChartEventAck = 2105;  // u8 4 | u32 player | + the request
constexpr uint8_t kDeleteReason = 7;               // what the server sends when a stage ends normally
constexpr uint64_t kFakeIdBase = 0x7E000000;       // our own sequencer ids for the host's stages (the server's: ~0x200)
constexpr uint32_t kSendEveryMs = 5000;
constexpr uint32_t kHostStaleMs = 20000;           // no report for this long: our own weather comes back
constexpr size_t kMaxStages = 8;
constexpr size_t kPrepKeyAt = 19, kPrepIdAt = 9, kPrepPosAt = 35, kPrepMin = 23;

// stage_names.json (NattKh), every Weather_* stage but the Wayward Woods quest one.
constexpr std::pair<uint32_t, const char*> kWeatherStages[] = {
    {1001373, "Rainy_Desert"},      {1001492, "Sunny"},             {1001600, "Windy_Degree45"},
    {1001619, "Windy_Degree90"},    {1001621, "Windy_Degree135"},   {1001624, "Windy_Degree180"},
    {1001625, "Windy_Degree225"},   {1001836, "Windy_Degree270"},   {1001887, "Windy_Degree315"},
    {1002670, "Windy_Speed08"},     {1002671, "Windy_Speed07"},     {1002672, "Windy_Speed06"},
    {1002673, "Windy_Speed05"},     {1002674, "Windy_Speed04"},     {1002675, "Windy_Speed03"},
    {1002676, "Cloudy_85"},         {1002677, "Cloudy_80"},         {1002678, "Cloudy_75"},
    {1002679, "Cloudy_70"},         {1002680, "Cloudy_65"},         {1002681, "Cloudy_60"},
    {1002682, "Cloudy_55"},         {1002683, "Cloudy_45"},         {1004464, "Rainy_Jungle"},
    {19990011, "Rainy_Short"},      {19990020, "Rainy_Normal"},     {19990030, "Rainy_Hard"},
};

// A weather PrepareSequencerAck seen live (3479, Weather_Rainy_Hard), the template for [dev] weather_test.
constexpr uint8_t kTemplate[103] = {
    0x04, 0x01, 0x00, 0x10, 0xA0, 0x01, 0x00, 0x10, 0xA0, 0x60, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x0E, 0x06, 0x31, 0x01, 0x00, 0x00, 0x00, 0x00, 0x17, 0x37, 0xB0, 0x18, 0x0E, 0x06, 0x31, 0x01, 0xB3,
    0xB5, 0x21, 0xC6, 0xD6, 0xE0, 0x05, 0x44, 0x68, 0xFB, 0x74, 0xC5, 0xC7, 0x3A, 0x02, 0x40, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
    0x7F, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x0D, 0x01, 0x00, 0x00, 0x20, 0x01, 0x00, 0x10, 0xA0, 0x05, 0x00, 0x00,
};

using Bytes = std::vector<uint8_t>;

std::atomic<bool> g_enabled{true};
std::atomic<uint32_t> g_test_key{0};
std::atomic<bool> g_follow{false};  // read by the S>C hook: drop our server's weather stages

std::mutex g_mutex;
std::map<uint64_t, Bytes> g_own;    // our server's weather stages: sequencer id -> PrepareSequencerAck payload
bool g_own_changed = false;
uint32_t g_seq_out = 1, g_seq_in = 0;
uint32_t g_last_send = 0;
std::map<uint32_t, Bytes> g_host;   // the host's stages by stage key (payloads)
uint32_t g_host_ms = 0;
bool g_host_fresh = false;     // a report came in; the tick stamps it with its own clock
bool g_host_new = false;
std::map<uint32_t, uint64_t> g_started;  // host stages running in our client: stage key -> our sequencer id
uint64_t g_next_fake = kFakeIdBase;
bool g_own_ended = false;
bool g_was_loading = false;
struct Branch {
    uint32_t branch = 0, previous = 0xFFFFFFFF, counter = 0;
};
std::map<uint64_t, Branch> g_branch;  // our stand-in for the server's branch state of the host's stages in our client
uint32_t g_hash1 = 0x22B6EC91, g_hash2 = 0xDAD73925;  // every weather stage's (captures 2026-11-02)

template <class T>
T rd(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

bool is_weather(uint32_t key) {
    for (const auto& [k, n] : kWeatherStages) {
        if (k == key) return true;
    }
    return false;
}

Bytes branch_state_payload(uint32_t player, uint64_t id, const Branch& b) {
    Bytes p(37, 0);
    p[0] = 4;
    std::memcpy(p.data() + 1, &player, 4);
    std::memcpy(p.data() + 5, &id, 8);
    std::memcpy(p.data() + 13, &b.branch, 4);
    std::memcpy(p.data() + 17, &b.previous, 4);
    std::memcpy(p.data() + 21, &g_hash1, 4);
    std::memcpy(p.data() + 29, &g_hash2, 4);
    std::memcpy(p.data() + 33, &b.counter, 4);
    return p;
}

Bytes delete_payload(uint32_t player, uint64_t id, uint32_t key) {
    Bytes b(17, 0);
    std::memcpy(b.data(), &player, 4);
    std::memcpy(b.data() + 4, &id, 8);
    std::memcpy(b.data() + 12, &key, 4);
    b[16] = kDeleteReason;
    return b;
}

// The host's payload as one of ours: our player, our sequencer id, where we stand.
Bytes prepare_payload(Bytes p, uint32_t player, uint64_t id, const std::optional<std::array<float, 3>>& me) {
    const uint32_t theirs = rd<uint32_t>(p.data() + 1);
    for (size_t i = 0; i + 4 <= p.size(); ++i) {
        if (rd<uint32_t>(p.data() + i) == theirs) std::memcpy(p.data() + i, &player, 4);
    }
    std::memcpy(p.data() + kPrepIdAt, &id, 8);
    if (me && p.size() >= kPrepPosAt + 12) std::memcpy(p.data() + kPrepPosAt, me->data(), 12);
    return p;
}

std::string names_of(const std::map<uint32_t, Bytes>& m) {
    std::string s;
    for (const auto& [k, p] : m) s += std::string(s.empty() ? "" : ", ") + weather_stage_name(k);
    return s.empty() ? "none" : s;
}

}  // namespace

const char* weather_stage_name(uint32_t key) {
    for (const auto& [k, n] : kWeatherStages) {
        if (k == key) return n;
    }
    return "";
}

void weather_set_enabled(bool on) { g_enabled = on; }

void weather_set_test(uint32_t key) {
    g_test_key = key;
    if (key) log::info("Weather (dev): acting as a guest whose host has {} ({})", weather_stage_name(key), key);
}

bool weather_filter_s2c(const uint8_t* packet, size_t size) {
    if (size < 5) return false;
    const uint16_t op = rd<uint16_t>(packet);
    const uint8_t* p = packet + 5;
    const size_t n = size - 5;
    if (op == kOpPrepareSequencerAck && n >= kPrepMin) {
        if (!is_weather(rd<uint32_t>(p + kPrepKeyAt))) return false;
        std::lock_guard lock(g_mutex);
        g_own[rd<uint64_t>(p + kPrepIdAt)] = Bytes(p, p + n);
        g_own_changed = true;
        return g_follow.load();
    }
    if (op == kOpStageBranchStateAck && n >= 37) {  // the branch hashes, should a build change them
        std::lock_guard lock(g_mutex);
        if (g_own.count(rd<uint64_t>(p + 5))) {
            g_hash1 = rd<uint32_t>(p + 21);
            g_hash2 = rd<uint32_t>(p + 29);
        }
        return false;
    }
    if (op == kOpDeleteSequencerAck && n >= 16) {
        std::lock_guard lock(g_mutex);
        if (g_own.erase(rd<uint64_t>(p + 4)) == 0) return false;
        g_own_changed = true;
        return g_follow.load();
    }
    return false;
}

void weather_observe_c2s(const uint8_t* packet, size_t size) {
    if (size < 5 + 8) return;
    const uint16_t op = rd<uint16_t>(packet);
    if (op != kOpStageBranchStateReq && op != kOpUpdateStageChartEventReq) return;
    const uint8_t* p = packet + 5;
    const uint64_t id = rd<uint64_t>(p);
    if (id < kFakeIdBase || id >= kFakeIdBase + 0x1000000) return;
    const uint32_t player = capture_own_player_key();
    if (!player) return;
    std::vector<std::pair<uint16_t, Bytes>> inject;
    Bytes head(13);
    head[0] = 4;
    std::memcpy(head.data() + 1, &player, 4);
    std::memcpy(head.data() + 5, &id, 8);
    if (op == kOpUpdateStageChartEventReq && size >= 5 + 17) {
        Bytes a = head;
        a.insert(a.end(), p + 8, p + 17);
        inject.push_back({kOpUpdateStageChartEventAck, std::move(a)});
    } else if (op == kOpStageBranchStateReq && size >= 5 + 12) {
        // As the server answers a weather stage: the event for the branch the client is in, then the state "in branch
        // 1, came from that one" (0 -> 1, then 1 -> 1 every minute).
        const uint32_t b = rd<uint32_t>(p + 8);
        Bytes ev = head;
        ev.insert(ev.end(), {static_cast<uint8_t>(b), static_cast<uint8_t>(b >> 8), static_cast<uint8_t>(b >> 16),
                             static_cast<uint8_t>(b >> 24), 0});
        inject.push_back({kOpStageOnBranchEventAck, std::move(ev)});
        std::lock_guard lock(g_mutex);
        auto it = g_branch.find(id);
        if (it == g_branch.end()) return;  // ended meanwhile
        it->second.previous = b;
        it->second.branch = b == 0 ? 1 : b;
        ++it->second.counter;
        inject.push_back({kOpStageBranchStateAck, branch_state_payload(player, id, it->second)});
        static int logged = 0;
        if (logged++ < 10) log::info("Weather: answered our client's branch request for 0x{:X} (branch {})", id, b);
    }
    puppet_inject_s2c_batch(inject);
}

void weather_on_event(const std::vector<uint8_t>& b) {
    if (b.size() < 5) return;
    const uint32_t seq = rd<uint32_t>(b.data());
    std::map<uint32_t, Bytes> stages;
    size_t at = 5;
    for (int i = 0; i < b[4] && at + 2 <= b.size(); ++i) {
        const uint16_t len = rd<uint16_t>(b.data() + at);
        at += 2;
        if (len < kPrepMin || at + len > b.size()) return;  // malformed: keep what we had
        Bytes p(b.data() + at, b.data() + at + len);
        at += len;
        const uint32_t key = rd<uint32_t>(p.data() + kPrepKeyAt);
        if (is_weather(key)) stages[key] = std::move(p);
    }
    std::lock_guard lock(g_mutex);
    if (seq <= g_seq_in && seq + 1000 > g_seq_in) return;  // a repeat or older (a restart of theirs starts at 1)
    g_seq_in = seq;
    g_host_fresh = true;
    if (stages != g_host) {
        g_host = std::move(stages);
        g_host_new = true;
    }
}

void weather_tick(bool connected, bool host, bool in_world, std::optional<std::array<float, 3>> me, uint32_t now) {
    std::vector<std::pair<uint16_t, Bytes>> inject;
    std::string log_line;
    Bytes send;
    {
        std::lock_guard lock(g_mutex);
        const uint32_t test = g_test_key.load();
        if (test) {  // [dev] a simulated host with exactly that stage
            if (g_host.size() != 1 || !g_host.count(test)) {
                Bytes p(std::begin(kTemplate), std::end(kTemplate));
                std::memcpy(p.data() + kPrepKeyAt, &test, 4);
                std::memcpy(p.data() + kPrepKeyAt + 12, &test, 4);
                g_host = {{test, std::move(p)}};
                g_host_new = true;
            }
            g_host_fresh = true;
        }
        if (g_host_fresh) {
            g_host_fresh = false;
            g_host_ms = now;
        }
        // Host: our stages to the other player, on change and every few seconds.
        if (g_enabled && connected && host && (g_own_changed || now - g_last_send >= kSendEveryMs)) {
            g_own_changed = false;
            g_last_send = now;
            send.resize(5);
            const uint32_t seq = g_seq_out++;
            std::memcpy(send.data(), &seq, 4);
            uint8_t count = 0;
            for (const auto& [id, p] : g_own) {
                if (count == kMaxStages || send.size() + 2 + p.size() > 1000) break;
                const auto len = static_cast<uint16_t>(p.size());
                send.insert(send.end(), reinterpret_cast<const uint8_t*>(&len), reinterpret_cast<const uint8_t*>(&len) + 2);
                send.insert(send.end(), p.begin(), p.end());
                ++count;
            }
            send[4] = count;
        }
        // Guest: follow the host's stages.
        const uint32_t player = capture_own_player_key();
        const bool loading = sniffer_world_loading();
        // Our position goes away in a minigame or a cutscene: that does not stop following.
        const bool follow = g_enabled && player && (in_world || g_follow.load()) && !loading && connected && (test || !host) &&
                            g_host_ms && now - g_host_ms < kHostStaleMs;
        if (loading && !g_was_loading && !g_started.empty()) {
            // A loading screen: the client may drop its sequencers. Ours start again under new ids after it (the
            // old ids are ended first, in case it kept them).
            for (const auto& [key, id] : g_started) inject.push_back({kOpDeleteSequencerAck, delete_payload(player, id, key)});
            g_started.clear();
            g_branch.clear();
        }
        g_was_loading = loading;
        if (follow) {
            if (!g_follow.exchange(true) || !g_own_ended) {
                for (const auto& [id, p] : g_own) {
                    inject.push_back({kOpDeleteSequencerAck, delete_payload(player, id, rd<uint32_t>(p.data() + kPrepKeyAt))});
                }
                g_own_ended = true;
                log_line = "Weather: following the host's (" + names_of(g_host) + "); ours held back";
            }
            for (auto it = g_started.begin(); it != g_started.end();) {
                if (g_host.count(it->first)) {
                    ++it;
                    continue;
                }
                inject.push_back({kOpDeleteSequencerAck, delete_payload(player, it->second, it->first)});
                g_branch.erase(it->second);
                it = g_started.erase(it);
            }
            for (const auto& [key, p] : g_host) {
                if (g_started.count(key)) continue;
                const uint64_t id = g_next_fake++;
                g_started[key] = id;
                inject.push_back({kOpPrepareSequencerAck, prepare_payload(p, player, id, me)});
                g_branch[id] = Branch{};
                inject.push_back({kOpStageBranchStateAck, branch_state_payload(player, id, g_branch[id])});
            }
            if (g_host_new && log_line.empty()) log_line = "Weather: the host's is now " + names_of(g_host);
            g_host_new = false;
        } else if (g_follow.exchange(false)) {
            // Back to our own weather: the host's end, ours start again (the stages our server holds now).
            for (const auto& [key, id] : g_started) {
                if (player) inject.push_back({kOpDeleteSequencerAck, delete_payload(player, id, key)});
            }
            g_started.clear();
            g_branch.clear();
            if (player) {
                for (const auto& [id, p] : g_own) inject.push_back({kOpPrepareSequencerAck, prepare_payload(p, player, id, std::nullopt)});
            }
            g_own_ended = false;
            g_host.clear();
            g_host_ms = 0;
            log_line = "Weather: our own again (" + std::to_string(g_own.size()) + " stages)";
        }
    }
    if (!send.empty()) capture_emit_weather(std::move(send));
    if (!inject.empty()) puppet_inject_s2c_batch(inject);
    if (!log_line.empty()) log::info("{}", log_line);
}

size_t weather_host_stages_running() {
    std::lock_guard lock(g_mutex);
    return g_started.size();
}

}  // namespace cdmp::net
