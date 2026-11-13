#include "mod/net/stage_cinema.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <format>
#include <map>
#include <mutex>
#include <string>
#include <utility>

#include "mod/log.h"
#include "mod/net/coop_activity.h"
#include "mod/net/enemy_relay.h"
#include "mod/net/local_capture.h"
#include "mod/net/remote_puppet.h"
#include "mod/net/troc_sniffer.h"

namespace cdmp::net {
namespace {

// 3479 opcodes (layouts: weather_sync.cpp, NOTES/LOG.md 2026-11-02 17:10).
constexpr uint16_t kOpPrepareSequencerAck = 2356;
constexpr uint16_t kOpDeleteSequencerAck = 2659;
constexpr uint16_t kOpStageBranchStateAck = 2752;
constexpr uint16_t kOpStageOnBranchEventAck = 2369;
constexpr uint16_t kOpUpdateStageChartEventReq = 2200;
constexpr uint16_t kOpUpdateStageChartEventAck = 2105;
constexpr uint16_t kOpSetAvoidByCutSceneAck = 2698;  // u8 4 | u32 player | u64 stage | u8 on | u32 player
constexpr uint64_t kFakeIdBase = 0x7D000000;
constexpr float kNearM = 100.0f;          // the user's cutscene range
constexpr uint32_t kMaxMs = 180000;       // a copy never outlives this
constexpr uint32_t kPickWindowMs = 5000;  // the stage named / changed this long before the cutscene started
constexpr uint32_t kTestDelayMs = 3000;
constexpr size_t kPrepKeyAt = 19, kPrepIdAt = 9, kPrepMin = 23, kPrepPosAt = 35, kBindingsFrom = 86;
enum : uint8_t { kStart = 7, kEnd = 8, kBranch = 9 };

using Bytes = std::vector<uint8_t>;

std::atomic<bool> g_enabled{true};
std::atomic<bool> g_test{false};

struct Stage {
    Bytes prepare, branch, on_branch;  // payloads, as our server sent them
    uint32_t changed_ms = 0;           // GetTickCount of the last branch message
};

std::mutex g_mutex;
// Host side (our server's stages).
std::map<uint64_t, Stage> g_stages;
uint64_t g_avoid_id = 0;
uint32_t g_avoid_ms = 0;
uint64_t g_relayed = 0;           // the stage we told the other player about (0 = none)
uint32_t g_relayed_changed = 0;
bool g_last_cut = false;
uint32_t g_cut_since = 0;
bool g_test_started = false;
// Guest side.
struct Copy {
    bool pending = false, active = false;
    Bytes prepare, branch, on_branch;
    std::array<float, 3> where{};
    uint64_t id = 0;
    uint32_t key = 0, since = 0;
    uint32_t active_key = 0;  // the stage of the copy running now (key may already name the next one)
} g_copy;
uint64_t g_next_fake = kFakeIdBase;

template <class T>
T rd(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

uint32_t ticks() { return static_cast<uint32_t>(GetTickCount64()); }

void put_blob(Bytes& out, const Bytes& b) {
    const auto n = static_cast<uint16_t>(b.size());
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(&n), reinterpret_cast<const uint8_t*>(&n) + 2);
    out.insert(out.end(), b.begin(), b.end());
}

bool get_blob(const uint8_t*& p, const uint8_t* end, Bytes& b) {
    if (end - p < 2) return false;
    const uint16_t n = rd<uint16_t>(p);
    p += 2;
    if (end - p < n) return false;
    b.assign(p, p + n);
    p += n;
    return true;
}

// The stage's world actors (node bindings after the fixed part) as the other player's copies of them.
Bytes for_guest(Bytes prepare) {
    for (size_t i = kBindingsFrom; i + 4 <= prepare.size(); ++i) {
        const uint32_t k = rd<uint32_t>(prepare.data() + i);
        if ((k & 0xFFF00000u) != 0xB0100000u) continue;
        if (const uint32_t copy = enemy_copy_of(k)) {
            std::memcpy(prepare.data() + i, &copy, 4);
            i += 3;
        }
    }
    return prepare;
}

// A payload of ours with the other player's key and the server's sequencer id replaced (ours / our copy's id).
Bytes as_ours(Bytes p, uint32_t player, uint64_t id, size_t id_at) {
    if (p.size() < id_at + 8 || p.size() < 5) return p;
    const uint32_t theirs = rd<uint32_t>(p.data() + 1);
    for (size_t i = 0; i + 4 <= p.size(); ++i) {
        if (rd<uint32_t>(p.data() + i) == theirs) std::memcpy(p.data() + i, &player, 4);
    }
    std::memcpy(p.data() + id_at, &id, 8);
    return p;
}

void send(uint8_t type, const Bytes& data) { activity_send(type, data); }

}  // namespace

uint32_t cinema_stage_key(uint64_t id) {
    std::lock_guard lock(g_mutex);
    const auto it = g_stages.find(id);
    return it == g_stages.end() || it->second.prepare.size() < kPrepMin ? 0
                                                                         : rd<uint32_t>(it->second.prepare.data() + kPrepKeyAt);
}

namespace {
std::optional<std::array<float, 3>> stage_pos(const Stage& s) {
    if (s.prepare.size() < kPrepPosAt + 12) return std::nullopt;
    std::array<float, 3> p{};
    std::memcpy(p.data(), s.prepare.data() + kPrepPosAt, 12);
    if (p[0] == 0.0f && p[2] == 0.0f) return std::nullopt;
    return p;
}
float dist_xz(const std::array<float, 3>& a, const std::array<float, 3>& b) { return std::hypot(a[0] - b[0], a[2] - b[2]); }
}  // namespace

uint32_t cinema_minigame_near(const std::array<float, 3>& pos, float max_m, const uint32_t* keys, size_t n) {
    std::lock_guard lock(g_mutex);
    uint32_t best = 0;
    float best_m = max_m;
    for (const auto& [id, s] : g_stages) {
        if (s.prepare.size() < kPrepMin) continue;
        const uint32_t key = rd<uint32_t>(s.prepare.data() + kPrepKeyAt);
        if (std::find(keys, keys + n, key) == keys + n) continue;
        const auto p = stage_pos(s);
        if (p && dist_xz(*p, pos) <= best_m) best_m = dist_xz(*p, pos), best = key;
    }
    return best;
}

void cinema_set_enabled(bool on) { g_enabled = on; }

static std::atomic<bool> g_spoiler_guard{false};  // Co-op+ (T39)
void cinema_set_spoiler_guard(bool on) { g_spoiler_guard = on; }
void cinema_set_test(bool on) { g_test = on; }

void cinema_observe_s2c(const uint8_t* packet, size_t size) {
    if (size < 5 + 12) return;  // DeleteSequencerAck is 12 bytes (fix from Plitos, 2026-11-07)
    const uint16_t op = rd<uint16_t>(packet);
    const uint8_t* p = packet + 5;
    const size_t n = size - 5;
    if (op != kOpPrepareSequencerAck && op != kOpDeleteSequencerAck && op != kOpStageBranchStateAck &&
        op != kOpStageOnBranchEventAck && op != kOpSetAvoidByCutSceneAck) {
        return;
    }
    std::lock_guard lock(g_mutex);
    if (op == kOpPrepareSequencerAck && n >= kPrepMin) {
        Stage& s = g_stages[rd<uint64_t>(p + kPrepIdAt)];
        s = Stage{};
        s.prepare.assign(p, p + n);
    } else if (op == kOpDeleteSequencerAck && n >= 12) {
        g_stages.erase(rd<uint64_t>(p + 4));
    } else if ((op == kOpStageBranchStateAck || op == kOpStageOnBranchEventAck) && n >= 13) {
        auto it = g_stages.find(rd<uint64_t>(p + 5));
        if (it == g_stages.end()) return;
        (op == kOpStageBranchStateAck ? it->second.branch : it->second.on_branch).assign(p, p + n);
        it->second.changed_ms = ticks();
    } else if (op == kOpSetAvoidByCutSceneAck && n >= 14 && p[13]) {
        g_avoid_id = rd<uint64_t>(p + 5);
        g_avoid_ms = ticks();
    }
}

void cinema_observe_c2s(const uint8_t* packet, size_t size) {
    if (size < 5 + 17 || rd<uint16_t>(packet) != kOpUpdateStageChartEventReq) return;
    const uint8_t* p = packet + 5;
    const uint64_t id = rd<uint64_t>(p);
    if (id < kFakeIdBase || id >= kFakeIdBase + 0x1000000) return;
    const uint32_t player = capture_own_player_key();
    if (!player) return;
    Bytes a(13);
    a[0] = 4;
    std::memcpy(a.data() + 1, &player, 4);
    std::memcpy(a.data() + 5, &id, 8);
    a.insert(a.end(), p + 8, p + 17);
    puppet_inject_s2c_batch({{kOpUpdateStageChartEventAck, std::move(a)}});
}

void cinema_on_event(uint8_t type, const uint8_t* d, size_t size) {
    const uint8_t* end = d + size;
    std::lock_guard lock(g_mutex);
    if (type == kStart && size >= 4 + 12 && g_spoiler_guard.load()) {
        log::info("Cinema: the other player's story scene 0x{:08X} not started here (spoiler guard)", rd<uint32_t>(d));
        return;
    }
    if (type == kStart && size >= 4 + 12) {
        Copy c;
        c.key = rd<uint32_t>(d);
        std::memcpy(c.where.data(), d + 4, 12);
        const uint8_t* p = d + 16;
        if (!get_blob(p, end, c.prepare) || !get_blob(p, end, c.branch) || !get_blob(p, end, c.on_branch)) return;
        if (c.prepare.size() < kPrepMin) return;
        c.pending = true;
        c.since = ticks();
        c.active = g_copy.active;  // a copy still running ends first
        c.id = g_copy.id;
        c.active_key = g_copy.active_key;
        g_copy = std::move(c);
        log::info("Cinema: the other player's cutscene is in stage {}", g_copy.key);
    } else if (type == kBranch && g_copy.active) {
        const uint8_t* p = d;
        Bytes b, ob;
        if (!get_blob(p, end, b) || !get_blob(p, end, ob)) return;
        const uint32_t player = capture_own_player_key();
        std::vector<std::pair<uint16_t, Bytes>> inject;
        if (!b.empty()) inject.push_back({kOpStageBranchStateAck, as_ours(b, player, g_copy.id, 5)});
        if (!ob.empty()) inject.push_back({kOpStageOnBranchEventAck, as_ours(ob, player, g_copy.id, 5)});
        puppet_inject_s2c_batch(inject);
    } else if (type == kEnd) {
        g_copy.pending = false;
        g_copy.since = 0;  // the tick ends an active copy
    }
}

void cinema_tick(bool connected, bool host, bool cut, std::optional<std::array<float, 3>> me,
                 std::optional<std::array<float, 3>> partner, uint32_t now) {
    if (!g_enabled) return;
    const uint32_t t = ticks();
    const bool loading = sniffer_world_loading();
    const uint32_t player = capture_own_player_key();
    std::vector<std::pair<uint16_t, Bytes>> inject;
    std::vector<std::pair<uint8_t, Bytes>> out;
    std::string note;
    {
        std::lock_guard lock(g_mutex);
        // ---- our cutscene: which stage, and tell the other player
        if (cut != g_last_cut) {
            g_last_cut = cut;
            g_cut_since = t;
            g_test_started = false;
            if (cut && !loading) {
                uint64_t pick = 0;
                if (g_avoid_id && t - g_avoid_ms < kPickWindowMs && g_stages.count(g_avoid_id)) {
                    pick = g_avoid_id;
                } else {
                    uint32_t best = kPickWindowMs;
                    for (const auto& [id, s] : g_stages) {
                        if (s.changed_ms && t - s.changed_ms < best) best = t - s.changed_ms, pick = id;
                    }
                }
                g_relayed = pick;
                if (pick) {
                    const Stage& s = g_stages[pick];
                    g_relayed_changed = s.changed_ms;
                    const uint32_t key = rd<uint32_t>(s.prepare.data() + kPrepKeyAt);
                    note = "Cinema: our cutscene is in stage " + std::to_string(key) + " (sequencer 0x" +
                           std::format("{:X}", pick) + ")";
                    if (connected) {
                        Bytes b(16);
                        std::memcpy(b.data(), &key, 4);
                        const std::array<float, 3> here = me.value_or(std::array<float, 3>{});
                        std::memcpy(b.data() + 4, here.data(), 12);
                        put_blob(b, for_guest(s.prepare));
                        put_blob(b, s.branch);
                        put_blob(b, s.on_branch);
                        if (b.size() <= 1000) out.push_back({kStart, std::move(b)});
                        else note += "; too big to send";
                    }
                } else {
                    note = "Cinema: our cutscene is not in a stage we know (not shared)";
                }
            } else if (!cut && g_relayed) {
                if (connected) out.push_back({kEnd, {}});
                g_relayed = 0;
            }
        } else if (cut && g_relayed && connected) {
            // Branch changes of the stage while its cutscene plays.
            const auto it = g_stages.find(g_relayed);
            if (it != g_stages.end() && it->second.changed_ms != g_relayed_changed) {
                g_relayed_changed = it->second.changed_ms;
                Bytes b;
                put_blob(b, it->second.branch);
                put_blob(b, it->second.on_branch);
                out.push_back({kBranch, std::move(b)});
            }
        }
        // [dev] cinema_test: our own stage as if the host sent it, kTestDelayMs into the cutscene.
        if (g_test && cut && g_relayed && !g_test_started && t - g_cut_since > kTestDelayMs) {
            g_test_started = true;
            const Stage& s = g_stages[g_relayed];
            Copy c;
            c.key = rd<uint32_t>(s.prepare.data() + kPrepKeyAt);
            c.where = me.value_or(std::array<float, 3>{});
            c.prepare = s.prepare;
            c.branch = s.branch;
            c.on_branch = s.on_branch;
            c.pending = true;
            c.since = t;
            c.active = g_copy.active;
            c.id = g_copy.id;
            c.active_key = g_copy.active_key;
            g_copy = std::move(c);
            note = "Cinema: [dev] cinema_test: starting a copy of our own stage " + std::to_string(g_copy.key);
        }
        if (g_test && !cut && g_copy.active && t - g_cut_since > 1000) g_copy.since = 0;  // test: ends with ours

        // ---- guest: run the copy
        Copy& c = g_copy;
        const bool linked = connected || g_test;
        const bool expired = c.since == 0 || t - c.since > kMaxMs || !linked || loading;
        if (c.active && (expired || c.pending)) {  // ended, or replaced by a new one
            if (player) inject.push_back({kOpDeleteSequencerAck, [&] {
                Bytes d(17, 0);
                std::memcpy(d.data(), &player, 4);
                std::memcpy(d.data() + 4, &c.id, 8);
                std::memcpy(d.data() + 12, &c.active_key, 4);
                d[16] = 7;
                return d;
            }()});
            c.active = false;
            note = "Cinema: the copy of stage " + std::to_string(c.active_key) + " ended";
        }
        if (c.pending && !expired && player && (!host || g_test) && (g_test || !cut)) {
            const std::array<float, 3> here = me.value_or(c.where);
            const bool in_range = std::hypot(here[0] - c.where[0], here[2] - c.where[2]) <= kNearM;
            (void)partner;
            if (in_range) {
                c.pending = false;
                c.active = true;
                c.id = g_next_fake++;
                c.active_key = c.key;
                inject.push_back({kOpPrepareSequencerAck, as_ours(c.prepare, player, c.id, kPrepIdAt)});
                if (!c.branch.empty()) inject.push_back({kOpStageBranchStateAck, as_ours(c.branch, player, c.id, 5)});
                if (!c.on_branch.empty()) {
                    inject.push_back({kOpStageOnBranchEventAck, as_ours(c.on_branch, player, c.id, 5)});
                }
                note = std::format("Cinema: playing the other player's cutscene (stage {} as 0x{:X}, {} bytes)", c.key,
                                   c.id, c.prepare.size());
            }
        }
    }
    for (auto& [type, b] : out) send(type, b);
    if (!inject.empty()) puppet_inject_s2c_batch(inject);
    if (!note.empty()) log::info("{}", note);
    (void)now;
}

bool cinema_copy_active() {
    std::lock_guard lock(g_mutex);
    return g_copy.pending || g_copy.active;
}

}  // namespace cdmp::net
