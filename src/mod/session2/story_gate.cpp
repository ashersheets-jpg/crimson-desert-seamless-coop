#include "mod/session2/story_gate.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <algorithm>
#include <deque>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#include "mod/log.h"
#include "mod/net/local_capture.h"
#include "mod/net/troc_sniffer.h"
#include "mod/session2/p8_probes.h"
#include "mod/session2/server.h"
#include "mod/session2/story_gate_logic.h"
#include "mod/session2/user2_stages.h"

namespace cdmp::session2 {
namespace {

// 3663: SequencerTriggerVolumeInOutReq 2808 and StageBranchStateReq 2644 both start with the u64 sequencer id (an
// id of the sender's own stage manager: the host's and user 2's ids are separate spaces).
constexpr uint16_t kOpTriggerVolume = 2808, kOpStageBranch = 2644;
constexpr uint16_t kOpGameLoadingStart = 2350;  // 3663 (a recording: "C>S GameLoadingStartReq 2350"): a player's load
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
void install_client();  // the host client's volume call (below)

// The hold (mode 2). g_mu is a leaf lock: nothing of the game and no other lock under it.
std::mutex g_mu;
sgl::Gate g_gate;
std::vector<std::vector<uint8_t>> g_held[2];  // each player's held Reqs, whole wire packets, in order
std::deque<std::vector<uint8_t>> g_release_u2;  // user 2's released Reqs: offered first by the tunnel (session2.cpp)
std::atomic<bool> g_release_u2_any{false};
char g_name[2][44] = {};
bool g_was_linked = false;

// cs8a / cs8b: capture_own_player_key() stopped naming the host's character once the guest had joined (the host's own
// entries were no longer taken). The stage machine's keys first; then, for the host, any player character that is not
// user 2's.
bool is_own(int who, uint32_t actor) {
    if ((actor >> 28) != 0xA) return false;
    const uint32_t host = user2_stages_host_key(), u2 = user2_stages_user2_key() ? user2_stages_user2_key() : server_user2_player_key();
    static std::atomic<uint64_t> last{~0ull};
    static std::atomic<int> said{0};
    if (const uint64_t now = static_cast<uint64_t>(host) << 32 | u2; last.exchange(now) != now && said.fetch_add(1) < 10)
        log::info("StoryGate: the players' characters: the host 0x{:08X} (capture 0x{:08X}), user 2 0x{:08X} (server 0x{:08X})",
                  host, net::capture_own_player_key(), u2, server_user2_player_key());
    if (who == sgl::kHost) return host ? actor == host : u2 && actor != u2;
    return u2 && actor == u2;
}

// Releases the players of `mask` (caller holds g_mu): the host's Reqs go to the host client's send queue (posted before
// its next own Req, sniffer_inject_c2s), user 2's to the tunnel's release list.
// The host client's own held volume call (hold mode; the hook at 0xE043E9, below): the call's arguments with a
// reference on the sequencer object; replayed on the game thread after a release (g_replay), or dropped when the host
// leaves the volume first. Under g_mu.
struct ClientHeld {
    uintptr_t seq = 0;
    uint8_t in = 0;
    uint32_t kind = 0;
    uintptr_t chr = 0;
    uint8_t facing = 0;
    uint64_t id = 0;
    bool valid = false;
};
std::vector<ClientHeld> g_client_calls;  // the main scene's call first, then the step's cinematic's (in order)
std::atomic<bool> g_replay{false};
std::vector<ClientHeld> g_client_replay;  // released calls, played by on_frame in order
std::vector<ClientHeld> g_client_drop;  // the host's held calls of a wait that ended in a load: released on the game thread
std::vector<uint64_t> g_extra[2];  // each player's held cinematic sequencers (their 2644s wait too)

std::vector<std::vector<uint8_t>> release_locked(int mask, const char* why) {
    std::vector<std::vector<uint8_t>> host;
    for (int w = 0; w < 2; ++w) {
        if (!(mask & (1 << w))) continue;
        log::info("StoryGate: {}'s story trigger '{}' released ({}; {} Req(s))", who_name(w), g_name[w], why, g_held[w].size());
        if (w == sgl::kHost) {
            for (auto& p : g_held[w]) host.push_back(std::move(p));
            if (!g_client_calls.empty()) {  // the host client's own volume calls: played on the game thread
                for (auto& c : g_client_calls) g_client_replay.push_back(c);
                g_client_calls.clear();
                g_replay.store(true);
            }
        } else {
            for (auto& p : g_held[w]) g_release_u2.push_back(std::move(p));
            g_release_u2_any.store(!g_release_u2.empty());
        }
        g_held[w].clear();
        g_extra[w].clear();
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
    if (g_mode.load(std::memory_order_relaxed) != 2 || size < kHeader) return false;
    const uint16_t op = static_cast<uint16_t>(packet[0] | packet[1] << 8);
    // A load (fast travel, death, a door): the player is leaving the scene it waits at, and a load may unload that
    // stage without a volume exit. Its wait ends; the held Reqs / calls are dropped (the friend's "ready" too).
    if (op == kOpGameLoadingStart) {
        std::lock_guard lock(g_mu);
        if (g_gate.p[who].held) {
            log::info("StoryGate: {} started a load while it waited at '{}': the wait is off ({} held Req(s) dropped)",
                      who_name(who), g_name[who], g_held[who].size());
            g_gate.on_leave(who, g_gate.p[who].seq);
            g_held[who].clear();
            g_extra[who].clear();
            g_name[who][0] = 0;
            if (who == sgl::kHost) {
                for (auto& c : g_client_calls) g_client_drop.push_back(c);
                g_client_calls.clear();
                if (!g_client_drop.empty()) g_replay.store(true);  // on_frame releases them
            }
        }
        return false;
    }
    if (size < kHeader + 8) return false;
    const uint8_t* d = packet + kHeader;
    const size_t n = size - kHeader;
    std::vector<std::vector<uint8_t>> host;
    bool taken = false;
    {
        std::lock_guard lock(g_mu);
        // cs8a: server_user2_in_world() (its move phase Stable) was false at the cabin with both players in the world;
        // logged in is enough here (a friend who left logs out: unlink below).
        const bool linked = server_user2_present();
        if (g_was_linked && !linked) host = release_locked(g_gate.unlink(), "the friend left the world");
        g_was_linked = linked;
        if (op == kOpTriggerVolume) {
            sgl::Volume v;
            if (sgl::parse_volume(d, n, v) && is_own(who, v.actor)) {
                const Slot* s = find(key_of(who == sgl::kUser2, v.seq));
                if (s && sgl::is_story_name(s->name) && !sgl::is_gate_name(s->name) && v.in) {
                    static std::atomic<int> said{0};
                    if (said.fetch_add(1) < 40)
                        log::info("StoryGate: {} entered the story scene '{}' (not a step's main scene): it goes on", who_name(who), s->name);
                }
                const bool extra = s && sgl::is_cinematic_name(s->name) && g_gate.p[who].held && sgl::same_step(g_name[who], s->name);
                if (extra) {
                    g_held[who].emplace_back(packet, packet + size);
                    taken = true;
                    if (std::find(g_extra[who].begin(), g_extra[who].end(), v.seq) == g_extra[who].end()) {
                        g_extra[who].push_back(v.seq);
                        log::info("StoryGate: {}'s step cinematic '{}' (sequencer 0x{:X}) waits with '{}'", who_name(who), s->name,
                                  v.seq, g_name[who]);
                    }
                }
                if (s && sgl::is_gate_name(s->name)) {
                    if (v.in) {
                        const bool was = g_gate.p[who].held;
                        const sgl::Verdict verdict = g_gate.on_enter(who, v.seq, s->hash, linked, GetTickCount64());
                        if (verdict == sgl::Verdict::Pass && !was) {
                            log::info("StoryGate: {} reached the story trigger '{}' (sequencer 0x{:X}): it goes on ({})",
                                      who_name(who), s->name, v.seq,
                                      !linked ? "the friend is not in the world"
                                      : g_gate.did(who, s->hash) ? "already passed" : "another wait is on");
                        }
                        if (verdict == sgl::Verdict::Hold) {
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
                        g_extra[who].clear();
                        g_name[who][0] = 0;
                        taken = true;
                    }
                }
            }
        } else if (op == kOpStageBranch &&
                   (g_gate.on_branch(who, rd64(d)) == sgl::Verdict::Hold ||
                    std::find(g_extra[who].begin(), g_extra[who].end(), rd64(d)) != g_extra[who].end())) {
            g_held[who].emplace_back(packet, packet + size);
            taken = true;
        }
    }
    if (!host.empty()) post_host(std::move(host));
    return taken;
}

}  // namespace

void story_gate_set_mode(const std::string& mode) {
    g_mode = mode == "probe" ? 1 : mode == "hold" ? 2 : mode == "client_probe" ? 3 : 0;
    if (g_mode.load() == 3) log::warn("StoryGate: [session2] story_gate=client_probe: this game's volume events of player characters are logged (read only)");
    if (g_mode.load() == 1) log::warn("StoryGate: [session2] story_gate=probe: story triggers of both players are logged (read only)");
    if (g_mode.load() == 2) log::warn("StoryGate: [session2] story_gate=hold: the ready check is ON (a story trigger waits for both players)");
    if (g_mode.load()) install_client();
}

bool story_gate_on() noexcept { return g_mode.load(std::memory_order_relaxed) != 0; }

void story_gate_note_prepared(uintptr_t mgr, uint64_t id, uint32_t hash, const char* name, size_t n) noexcept {
    if (!story_gate_on() || !id || !name || !n) return;
    const uintptr_t u2 = user2_stages_user2_manager();
    const uint64_t key = key_of(u2 && mgr == u2, id);
    if (find(key)) return;
    // Research (2026-11-14): which story scenes each player's manager prepares (user 2's client never reported the
    // comrades' scenes nor the cabin's cinematic _0310_dlc).
    if (n >= 13 && std::memcmp(name, "cd_seq_quest_", 13) == 0) {
        static std::atomic<int> said[2]{};
        const int w = u2 && mgr == u2 ? 1 : 0;
        if (said[w].fetch_add(1) < 80)
            log::info("StoryGate [prepared] {}'s manager 0x{:X}: sequencer 0x{:X} '{}'", w ? "user 2" : "the host", mgr, id,
                      std::string(name, n < 40 ? n : 40));
    }
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

namespace {
bool sg_read(void* dst, uint64_t src, size_t n) noexcept {
    __try {
        std::memcpy(dst, reinterpret_cast<const void*>(src), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// A return address: a call ends right at it (e8 rel32; ff /2 with disp32, disp8, or a register).
bool after_call(uint64_t a) noexcept {
    uint8_t b[7] = {};
    if (!sg_read(b, a - 7, 7)) return false;
    return b[2] == 0xE8 || (b[1] == 0xFF && (b[2] & 0x38) == 0x10) || (b[4] == 0xFF && (b[5] & 0x38) == 0x10) ||
           (b[5] == 0xFF && (b[6] & 0xF8) == 0xD0);
}
}  // namespace

namespace {
// Up to `cap` caller rips by RtlVirtualUnwind (a frame without unwind data: a leaf, [rsp] is its return address).
int sg_unwind(uint64_t rip, uint64_t rsp, const uint64_t nv[8], uint64_t* out, int cap) noexcept {
    int n = 0;
    __try {
        CONTEXT c{};
        c.ContextFlags = CONTEXT_FULL;
        c.Rip = rip;
        c.Rsp = rsp;
        c.Rbx = nv[0]; c.Rbp = nv[1]; c.Rsi = nv[2]; c.Rdi = nv[3];
        c.R12 = nv[4]; c.R13 = nv[5]; c.R14 = nv[6]; c.R15 = nv[7];
        for (; n < cap; ++n) {
            DWORD64 image = 0;
            PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(c.Rip, &image, nullptr);
            if (!fe) {
                c.Rip = *reinterpret_cast<const uint64_t*>(c.Rsp);
                c.Rsp += 8;
            } else {
                PVOID handler = nullptr;
                DWORD64 frame = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, c.Rip, fe, &c, &handler, &frame, nullptr);
            }
            if (!c.Rip) break;
            out[n] = c.Rip;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return n;
}
}  // namespace

// The host client's own volume handling (cs8f, RtlVirtualUnwind from the 2808's post): the frame-event dispatcher
// 0x29B1DC0 calls the delegate 0xE041E0 (a character in / out of a trigger volume), which finds the volume's
// sequencer (rdi, ref-counted: +0x10 count, +0x14 atomic) and at 0xE043E9 calls 0x6F41E0 (a jmp into protected code)
// (rcx sequencer, dl in, r8d the volume's kind, r9 the character, [rsp+0x20] u8 facing): it posts the 2808 AND starts
// the scene locally. 0x7025DD: right after the frame's event flush (0x162E570(.., 0x1C)), on the game thread.
namespace {
constexpr uintptr_t kVolCall = 0xE043E9;  // 3663
constexpr uint8_t kVolCallBytes[] = {0xE8, 0xF2, 0xFD, 0x8E, 0xFF};
constexpr uintptr_t kFrameAfter = 0x7025DD;  // 3663
constexpr uint8_t kFrameAfterBytes[] = {0x48, 0x8B, 0x47, 0x08, 0x48, 0x8B, 0x40, 0x60};
SafetyHookMid g_vol_hook, g_frame_hook, g_lookup_hook;
// 0xE04260 (3663): right after the delegate's sequencer lookup 0xE04460 (rsp+0xE0 the sequencer object or 0, rsp+0xD0
// the volume's kind, rsp+0x48 the character, r12b in / out). Research: does this game find a sequencer for a player's
// volume event (the guest's game never reported the cabin's cinematic)?
constexpr uintptr_t kLookupAfter = 0xE04260;
constexpr uint8_t kLookupAfterBytes[] = {0x48, 0x8B, 0xBC, 0x24, 0xE0, 0x00, 0x00, 0x00};
struct VolCall {
    uintptr_t seq = 0;
    uint8_t in = 0;
    uint32_t kind = 0;
    uintptr_t chr = 0;
    uint8_t facing = 0;
    bool valid = false;
};
thread_local VolCall t_vol;
std::atomic<uint64_t> g_vol_calls{0}, g_frames{0};

using VolFn = void(__fastcall*)(uintptr_t, uint8_t, uint32_t, uintptr_t, uint8_t);
thread_local bool t_replaying = false;

// The sequencer object's reference (+0x10 count, +0x14 1 = atomic), as 0xE043F4 releases it. The game thread only.
bool obj_ref(uintptr_t o, int d) noexcept {
    __try {
        auto* count = reinterpret_cast<volatile LONG*>(o + 0x10);
        const bool atomic = *reinterpret_cast<const uint8_t*>(o + 0x14) != 0;
        const LONG n = d > 0 ? (atomic ? InterlockedIncrement(count) : ++*count) : (atomic ? InterlockedDecrement(count) : --*count);
        if (n == 0) {
            using Dtor = void(__fastcall*)(uintptr_t, uint32_t);
            (*reinterpret_cast<Dtor* const*>(o))[0](o, 1);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool call_vol(uintptr_t fn, const ClientHeld& c) noexcept {
    __try {
        reinterpret_cast<VolFn>(fn)(c.seq, c.in, c.kind, c.chr, c.facing);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void on_vol_call(safetyhook::Context& ctx) {
    g_vol_calls.fetch_add(1, std::memory_order_relaxed);
    VolCall v;
    v.seq = ctx.rdi;
    v.in = static_cast<uint8_t>(ctx.rdx);
    v.kind = static_cast<uint32_t>(ctx.r8);
    v.chr = ctx.r9;
    sg_read(&v.facing, ctx.rsp + 0x20, 1);
    v.valid = true;
    t_vol = v;
    if (g_mode.load(std::memory_order_relaxed) != 2 || t_replaying) return;
    // cs8g: the sequencer id at +0x30 of the object, the character's key at +0x60.
    uint64_t id = 0;
    uint32_t key = 0;
    if (!sg_read(&id, v.seq + 0x30, 8) || !sg_read(&key, v.chr + 0x60, 4) || !is_own(sgl::kHost, key)) return;
    const Slot* s = find(key_of(false, id));
    if (!s) return;
    const bool gate = sgl::is_gate_name(s->name);
    if (!gate && !sgl::is_cinematic_name(s->name)) return;
    bool skip = false;
    std::vector<uintptr_t> dropped;
    {
        std::lock_guard lock(g_mu);
        const bool linked = server_user2_present();
        if (!gate) {
            // The step's cinematic while the host waits at that step's main scene: held with it (in and out, in order).
            if (g_gate.p[sgl::kHost].held && sgl::same_step(g_name[sgl::kHost], s->name)) {
                bool known = false;
                for (const auto& c : g_client_calls) known = known || c.id == id;
                if (!known) {
                    log::info("StoryGate: the host's step cinematic '{}' (sequencer 0x{:X}) waits with '{}' (not started)",
                              s->name, id, g_name[sgl::kHost]);
                }
                obj_ref(v.seq, +1);
                g_client_calls.push_back(ClientHeld{v.seq, v.in, v.kind, v.chr, v.facing, id, true});
                skip = true;
            }
        } else if (v.in) {
            const bool was = g_gate.p[sgl::kHost].held;
            const sgl::Verdict verdict = g_gate.on_enter(sgl::kHost, id, s->hash, linked, GetTickCount64());
            if (verdict == sgl::Verdict::Hold) {
                skip = true;
                if (!was) {
                    obj_ref(v.seq, +1);
                    g_client_calls.push_back(ClientHeld{v.seq, v.in, v.kind, v.chr, v.facing, id, true});
                    std::memcpy(g_name[sgl::kHost], s->name, sizeof g_name[sgl::kHost]);
                    log::info("StoryGate: the host reached the story trigger '{}' (sequencer 0x{:X}, hash 0x{:08X}): its "
                              "own client waits (the scene is not started){}", s->name, id, s->hash,
                              g_gate.together() ? "; both players are here" : " for the friend (or to go on alone)");
                }
            } else if (!was) {
                log::info("StoryGate: the host reached the story trigger '{}' (sequencer 0x{:X}): it goes on ({})", s->name, id,
                          !linked ? "the friend is not in the world" : g_gate.did(sgl::kHost, s->hash) ? "already passed" : "another wait is on");
            }
        } else if (g_gate.on_leave(sgl::kHost, id) == sgl::Verdict::Drop) {
            skip = true;
            for (const auto& c : g_client_calls) dropped.push_back(c.seq);
            g_client_calls.clear();
            log::info("StoryGate: the host left the story trigger '{}' before it started: the wait is off", g_name[sgl::kHost]);
            g_held[sgl::kHost].clear();
            g_extra[sgl::kHost].clear();
            g_name[sgl::kHost][0] = 0;
        }
    }
    for (uintptr_t o : dropped) obj_ref(o, -1);
    if (skip) ctx.rip = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + kVolCall + sizeof kVolCallBytes;
}

void on_lookup(safetyhook::Context& ctx) {
    static std::atomic<int> said{0};
    if (said.load(std::memory_order_relaxed) >= 300) return;
    uintptr_t chr = 0, seq = 0;
    uint32_t key = 0, kind = 0;
    if (!sg_read(&chr, ctx.rsp + 0x48, 8) || !chr || !sg_read(&key, chr + 0x60, 4) || (key >> 28) != 0xA) return;
    sg_read(&seq, ctx.rsp + 0xE0, 8);
    sg_read(&kind, ctx.rsp + 0xD0, 4);
    uint64_t id = 0;
    if (seq) sg_read(&id, seq + 0x30, 8);
    if (said.fetch_add(1) >= 300) return;
    log::info("StoryGate [volume] character 0x{:08X} {} a volume of kind 0x{:08X}: {}", key, static_cast<uint8_t>(ctx.r12) ? "in" : "out",
              kind, seq ? std::format("sequencer 0x{:X} (object 0x{:X})", id, seq) : std::string("NO sequencer found"));
}

void on_frame(safetyhook::Context&) {
    g_frames.fetch_add(1, std::memory_order_relaxed);
    if (!g_replay.load(std::memory_order_acquire)) return;
    std::vector<ClientHeld> calls, dropped;
    {
        std::lock_guard lock(g_mu);
        g_replay.store(false);
        dropped.swap(g_client_drop);
        calls.swap(g_client_replay);
    }
    for (const ClientHeld& c : dropped) obj_ref(c.seq, -1);
    const uintptr_t fn = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + 0x6F41E0;  // 3663: the call's target
    for (const ClientHeld& c : calls) {
        // A stage the game has let go of while the call waited (a load, a fast travel: no volume exit seen): only our
        // reference is left on the sequencer object; its call is not played, the object goes with our release.
        LONG count = 0;
        uint64_t id_now = 0;
        if (!sg_read(&count, c.seq + 0x10, 4) || count <= 1 || !sg_read(&id_now, c.seq + 0x30, 8) || id_now != c.id) {
            obj_ref(c.seq, -1);
            log::warn("StoryGate: the host's held volume call for sequencer 0x{:X} is NOT played: the game let go of that "
                      "scene while it waited (references {}, id now 0x{:X})", c.id, count, id_now);
            continue;
        }
        t_replaying = true;
        const bool ok = call_vol(fn, c);
        t_replaying = false;
        obj_ref(c.seq, -1);
        log::info("StoryGate: the host's own volume call ({} sequencer 0x{:X}) played now ({}; thread {})", c.in ? "into" : "out of",
                  c.id, ok ? "done" : "EXCEPTION", GetCurrentThreadId());
    }
}

void install_client() {
    static bool done = false;
    if (done) return;
    done = true;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::vector<gad::Section> secs;
    if (!p8_exe_sections(base, secs)) {
        log::error("StoryGate: the exe's sections are not readable: the client hooks are not installed");
        return;
    }
    p8_install_mid(base, secs, kVolCall, sizeof kVolCallBytes, kVolCallBytes, on_vol_call, g_vol_hook, "StoryGate",
                   "the client's volume call 0xE043E9 (0xE041E0 -> 0x6F41E0)");
    p8_install_mid(base, secs, kFrameAfter, sizeof kFrameAfterBytes, kFrameAfterBytes, on_frame, g_frame_hook, "StoryGate",
                   "after the frame's event flush 0x7025DD");
    if (g_mode.load() == 1 || g_mode.load() == 3) {
        p8_install_mid(base, secs, kLookupAfter, sizeof kLookupAfterBytes, kLookupAfterBytes, on_lookup, g_lookup_hook, "StoryGate",
                       "after the volume delegate's sequencer lookup 0xE04260 (research)");
    }
}

}  // namespace

void story_gate_host_stack(const uint8_t* packet, size_t size, uint64_t rsp, uint64_t rip, const uint64_t nv[8]) noexcept {
    static std::atomic<int> said{0};
    if (!story_gate_on() || size < kHeader + 8 || said.load() >= 8) return;
    const uint16_t op = static_cast<uint16_t>(packet[0] | packet[1] << 8);
    const uint8_t* d = packet + kHeader;
    const Slot* s = find(key_of(false, rd64(d)));
    if (!s || !sgl::is_story_name(s->name)) return;
    const bool dlc = std::strstr(s->name, "_dlc") != nullptr;
    if (!sgl::is_gate_name(s->name) && !dlc) return;
    if (op == kOpTriggerVolume) {
        sgl::Volume v;
        if (!sgl::parse_volume(d, size - kHeader, v) || !v.in || !is_own(sgl::kHost, v.actor)) return;
    }
    if (said.fetch_add(1) >= 8) return;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (op == kOpTriggerVolume && t_vol.valid) {
        const VolCall v = t_vol;
        t_vol.valid = false;
        const uint64_t id = rd64(d);
        thread_local uint64_t obj[0x800 / 8];
        std::string at;
        if (sg_read(obj, v.seq, sizeof obj)) {
            for (size_t i = 0; i < 0x800 / 8; ++i) if (obj[i] == id) at += std::format(" +0x{:X}", i * 8);
        }
        uint32_t key = 0;
        sg_read(&key, v.chr + 0x60, 4);
        log::info("StoryGate [client] the volume call before this 2808: sequencer object 0x{:X} (the id 0x{:X} at{}), in {}, "
                  "kind 0x{:08X}, character 0x{:X} (key +0x60 0x{:08X}), facing {} | volume calls {}, frames {}",
                  v.seq, id, at.empty() ? " (not found in 0x800 bytes)" : at, v.in, v.kind, v.chr, key, v.facing,
                  g_vol_calls.load(), g_frames.load());
    }
    uint64_t frames[24] = {};
    const int nf = sg_unwind(rip, rsp, nv, frames, 24);
    std::string chain;
    for (int i = 0; i < nf; ++i) {
        chain += frames[i] >= base && frames[i] < base + 0x144E9000 ? std::format(" exe+0x{:X}", frames[i] - base)
                                                                    : std::format(" 0x{:X}", frames[i]);
    }
    log::info("StoryGate [stack] the host's {} for '{}' | unwound callers:{}",
              op == kOpTriggerVolume ? "SequencerTriggerVolumeInOutReq 2808 (own entry)" : "StageBranchStateReq 2644", s->name, chain);
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
