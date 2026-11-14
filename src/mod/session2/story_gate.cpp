#include "mod/session2/story_gate.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <deque>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#include "mod/log.h"
#include "mod/net/local_capture.h"
#include "mod/net/troc_sniffer.h"
#include "mod/session2/server.h"
#include "mod/session2/story_gate_logic.h"
#include "mod/session2/user2_stages.h"

namespace cdmp::session2 {
namespace {

// 3663: SequencerTriggerVolumeInOutReq 2808 and StageBranchStateReq 2644 both start with the u64 sequencer id (an
// id of the sender's own stage manager: the host's and user 2's ids are separate spaces).
constexpr uint16_t kOpTriggerVolume = 2808, kOpStageBranch = 2644;
constexpr size_t kHeader = 5;

std::atomic<int> g_mode{0};  // 0 off, 1 probe, 2 hold

// (player, id) -> scene, a small open-addressing table (writes from server threads, reads from any: a slot is filled
// once per key; the name is copied before the key is published).
struct Slot {
    std::atomic<uint64_t> key{0};
    uint32_t hash = 0;
    char name[44] = {};
};
constexpr size_t kSlots = 1024;
Slot g_slots[kSlots];
std::atomic<int> g_said{0}, g_raw{0};

uint64_t key_of(bool user2, uint64_t id) { return (id & 0x7FFFFFFFFFFFFFFFull) | (user2 ? 0x8000000000000000ull : 0); }
size_t slot_of(uint64_t key) { return static_cast<size_t>((key * 0x9E3779B97F4A7C15ull) >> 54) % kSlots; }

const Slot* find(uint64_t key) {
    size_t i = slot_of(key);
    for (size_t k = 0; k < 32; ++k, i = (i + 1) % kSlots) {
        const uint64_t v = g_slots[i].key.load(std::memory_order_acquire);
        if (v == key) return &g_slots[i];
        if (v == 0) return nullptr;
    }
    return nullptr;
}

uint64_t rd64(const uint8_t* p) {
    uint64_t v = 0;
    std::memcpy(&v, p, 8);
    return v;
}

std::string hex(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; ++i) s += std::format("{:02X}{}", p[i], i + 1 < n && i % 4 == 3 ? " | " : i + 1 < n ? " " : "");
    return s;
}

const char* who_name(int who) { return who == sgl::kHost ? "the host" : "user 2"; }

// The hold (mode 2). g_mu is a leaf lock: nothing of the game and no other lock under it.
std::mutex g_mu;
sgl::Gate g_gate;
std::vector<std::vector<uint8_t>> g_held[2];  // each player's held Reqs, whole wire packets, in order
std::deque<std::vector<uint8_t>> g_release_u2;  // user 2's released Reqs: offered first by the tunnel (session2.cpp)
std::atomic<bool> g_release_u2_any{false};
char g_name[2][44] = {};
bool g_was_linked = false;

uint32_t own_key(int who) { return who == sgl::kHost ? net::capture_own_player_key() : server_user2_player_key(); }

// Releases the players of `mask` (caller holds g_mu): the host's Reqs go to the host client's send queue (posted before
// its next own Req, sniffer_inject_c2s), user 2's to the tunnel's release list.
std::vector<std::vector<uint8_t>> release_locked(int mask, const char* why) {
    std::vector<std::vector<uint8_t>> host;
    for (int w = 0; w < 2; ++w) {
        if (!(mask & (1 << w))) continue;
        log::info("StoryGate: {}'s story trigger '{}' released ({}; {} Req(s))", who_name(w), g_name[w], why, g_held[w].size());
        if (w == sgl::kHost) {
            for (auto& p : g_held[w]) host.push_back(std::move(p));
        } else {
            for (auto& p : g_held[w]) g_release_u2.push_back(std::move(p));
            g_release_u2_any.store(!g_release_u2.empty());
        }
        g_held[w].clear();
        g_name[w][0] = 0;
    }
    return host;
}

void post_host(std::vector<std::vector<uint8_t>> host) {
    for (auto& p : host) {
        if (!net::sniffer_inject_c2s(std::move(p))) log::warn("StoryGate: the host's released Req could not be queued");
    }
}

// A Req of a player (whole wire packet). True: taken by the gate (held, or dropped with its held entry).
bool decide(int who, const uint8_t* packet, size_t size) {
    if (g_mode.load(std::memory_order_relaxed) != 2 || size < kHeader + 8) return false;
    const uint16_t op = static_cast<uint16_t>(packet[0] | packet[1] << 8);
    const uint8_t* d = packet + kHeader;
    const size_t n = size - kHeader;
    std::vector<std::vector<uint8_t>> host;
    bool taken = false;
    {
        std::lock_guard lock(g_mu);
        const bool linked = server_user2_in_world();
        if (g_was_linked && !linked) host = release_locked(g_gate.unlink(), "the friend left the world");
        g_was_linked = linked;
        if (op == kOpTriggerVolume) {
            sgl::Volume v;
            const uint32_t own = own_key(who);
            if (sgl::parse_volume(d, n, v) && own && v.actor == own) {
                const Slot* s = find(key_of(who == sgl::kUser2, v.seq));
                if (s && sgl::is_story_name(s->name) && !sgl::is_gate_name(s->name) && v.in) {
                    static std::atomic<int> said{0};
                    if (said.fetch_add(1) < 40)
                        log::info("StoryGate: {} entered the story scene '{}' (not a step's main scene): it goes on", who_name(who), s->name);
                }
                if (s && sgl::is_gate_name(s->name)) {
                    if (v.in) {
                        const bool was = g_gate.p[who].held;
                        if (g_gate.on_enter(who, v.seq, s->hash, linked, GetTickCount64()) == sgl::Verdict::Hold) {
                            g_held[who].emplace_back(packet, packet + size);
                            taken = true;
                            if (!was) {
                                std::memcpy(g_name[who], s->name, sizeof g_name[who]);
                                log::info("StoryGate: {} reached the story trigger '{}' (sequencer 0x{:X}, hash 0x{:08X}): "
                                          "it waits{}", who_name(who), s->name, v.seq, s->hash,
                                          g_gate.together() ? "; both players are here" : " for the friend (or to go on alone)");
                            }
                        }
                    } else if (g_gate.on_leave(who, v.seq) == sgl::Verdict::Drop) {
                        log::info("StoryGate: {} left the story trigger '{}' before it started: the wait is off ({} held "
                                  "Req(s) dropped with the exit)", who_name(who), g_name[who], g_held[who].size());
                        g_held[who].clear();
                        g_name[who][0] = 0;
                        taken = true;
                    }
                }
            }
        } else if (op == kOpStageBranch && g_gate.on_branch(who, rd64(d)) == sgl::Verdict::Hold) {
            g_held[who].emplace_back(packet, packet + size);
            taken = true;
        }
    }
    if (!host.empty()) post_host(std::move(host));
    return taken;
}

}  // namespace

void story_gate_set_mode(const std::string& mode) {
    g_mode = mode == "probe" ? 1 : mode == "hold" ? 2 : 0;
    if (g_mode.load() == 1) log::warn("StoryGate: [session2] story_gate=probe: story triggers of both players are logged (read only)");
    if (g_mode.load() == 2) log::warn("StoryGate: [session2] story_gate=hold: the ready check is ON (a story trigger waits for both players)");
}

bool story_gate_on() noexcept { return g_mode.load(std::memory_order_relaxed) != 0; }

void story_gate_note_prepared(uintptr_t mgr, uint64_t id, uint32_t hash, const char* name, size_t n) noexcept {
    if (!story_gate_on() || !id || !name || !n) return;
    const uintptr_t u2 = user2_stages_user2_manager();
    const uint64_t key = key_of(u2 && mgr == u2, id);
    if (find(key)) return;
    size_t i = slot_of(key);
    for (size_t k = 0; k < 32; ++k, i = (i + 1) % kSlots) {
        if (g_slots[i].key.load(std::memory_order_acquire) != 0) continue;
        const size_t m = n < sizeof g_slots[i].name - 1 ? n : sizeof g_slots[i].name - 1;
        std::memcpy(g_slots[i].name, name, m);
        g_slots[i].name[m] = 0;
        g_slots[i].hash = hash;
        uint64_t expected = 0;
        if (g_slots[i].key.compare_exchange_strong(expected, key, std::memory_order_acq_rel)) return;
    }
}

void story_gate_observe(bool user2, uint16_t opcode, const uint8_t* payload, size_t size) noexcept {
    if (!story_gate_on() || !payload) return;
    if (opcode != kOpTriggerVolume && opcode != kOpStageBranch) return;
    if (size < 8) return;
    const uint64_t id = rd64(payload);
    const Slot* s = find(key_of(user2, id));
    // The weather / contents scenes branch every ~17 s: only story scenes (or ones not known) are logged.
    if (s && std::strncmp(s->name, "cd_seq_quest", 12) != 0) return;
    // 3663 (cs7c): 2808 = u64 sequencer | u32 kind (0x2000xxxx, one per volume) | u8 in (1) / out (0) | u32 actor |
    // u8 ? ... At a field's load every volume reports the NPCs inside it (0xB0...): only a player's (0xA...) count.
    if (opcode == kOpTriggerVolume) {
        if (size < 17) return;
        uint32_t actor = 0;
        std::memcpy(&actor, payload + 13, 4);
        if ((actor >> 28) != 0xA) return;
        if (g_raw.fetch_add(1, std::memory_order_relaxed) >= 150) return;
    }
    if (opcode == kOpStageBranch && g_said.fetch_add(1, std::memory_order_relaxed) >= 200) return;
    log::info("StoryGate [probe] {} {} sequencer 0x{:X}{} | {}", user2 ? "user 2" : "host",
              opcode == kOpTriggerVolume ? "SequencerTriggerVolumeInOutReq 2808" : "StageBranchStateReq 2644", id,
              opcode == kOpTriggerVolume ? std::format(" ({} bytes: {})", size, hex(payload, size < 24 ? size : 24))
                                         : std::string(),
              s ? std::format("scene '{}' (hash 0x{:08X})", s->name, s->hash) : std::string("scene not known"));
}

bool story_gate_take_user2(const std::vector<uint8_t>& packet) noexcept {
    try {
        return decide(sgl::kUser2, packet.data(), packet.size());
    } catch (...) {
        return false;
    }
}

bool story_gate_take_host(const uint8_t* packet, size_t size) noexcept {
    try {
        return decide(sgl::kHost, packet, size);
    } catch (...) {
        return false;
    }
}

bool story_gate_next_release(std::vector<uint8_t>& out) noexcept {
    if (!g_release_u2_any.load(std::memory_order_acquire)) return false;
    std::lock_guard lock(g_mu);
    if (g_release_u2.empty()) return false;
    out = g_release_u2.front();
    return true;
}

void story_gate_release_done() noexcept {
    std::lock_guard lock(g_mu);
    if (!g_release_u2.empty()) g_release_u2.pop_front();
    g_release_u2_any.store(!g_release_u2.empty());
}

int story_gate_press(int who) noexcept {
    if (g_mode.load() != 2 || (who != sgl::kHost && who != sgl::kUser2)) return 0;
    std::vector<std::vector<uint8_t>> host;
    int mask = 0;
    {
        std::lock_guard lock(g_mu);
        const bool together = g_gate.together();
        mask = g_gate.press(who);
        if (g_gate.p[who].ready && !mask) log::info("StoryGate: {} is ready; waiting for the friend's press", who_name(who));
        if (mask) host = release_locked(mask, together ? "both players pressed" : "it went on alone");
    }
    if (!host.empty()) post_host(std::move(host));
    return mask;
}

int story_gate_state(uint8_t* out, int cap) noexcept {
    if (g_mode.load() != 2 || !out || cap < 52) return 0;
    std::lock_guard lock(g_mu);
    std::memset(out, 0, 52);
    out[0] = static_cast<uint8_t>(sgl::shown(g_gate, sgl::kHost));
    out[1] = static_cast<uint8_t>(sgl::shown(g_gate, sgl::kUser2));
    const int w = g_gate.p[sgl::kHost].held ? sgl::kHost : sgl::kUser2;
    std::memcpy(out + 4, &g_gate.p[w].hash, 4);
    std::memcpy(out + 8, g_name[w], sizeof g_name[w]);
    return 52;
}

}  // namespace cdmp::session2

// Co-op+ 2026-11-14 (the ready check, story_gate.h): read by the Co-op+ layer of the host.
//   cdmp_story_gate_state(out, 52): u8 shown (host) | u8 shown (user 2) | u16 0 | u32 scene hash | char name[44];
//   0 = the gate is off. shown: story_gate_logic.h Shown.
//   cdmp_story_gate_press(who): 0 the host, 1 user 2 pressed (a hold of Interact); returns the released players.
extern "C" __declspec(dllexport) int cdmp_story_gate_state(uint8_t* out, int cap) {
    return cdmp::session2::story_gate_state(out, cap);
}
extern "C" __declspec(dllexport) int cdmp_story_gate_press(int who) { return cdmp::session2::story_gate_press(who); }
