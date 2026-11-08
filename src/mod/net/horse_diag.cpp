#include "mod/net/horse_diag.h"

#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>
#include <chrono>
#include <cstring>
#include <format>

#include <safetyhook.hpp>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/net/enemy_relay.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/guest_lifecycle.h"
#include "mod/session2/offsets_s2.h"
#include "mod/session2/guest.h"
#include "mod/game/actor_lookup.h"

namespace cdmp::net {
namespace {
namespace off = game::build_2976;
namespace gad = session2::gad;

constexpr uint32_t kCopyHorse = 0xB010F002;

struct Site {
    uintptr_t rva;
    uint8_t head[16];
    const char* name;
};
// Co-op+ port 1.0.0.3549: the three client functions found together by their heads and spacing (update, post +0x11F0,
// restore +0x4CD0; 2976: +0x1190 / +0x4B30), the only such triple; pending near the restore byte's writer (log only).
constexpr Site kCcUpdate = {0x9451E0, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
                                       0x41, 0x56, 0x41, 0x57}, "CC update"};
constexpr Site kCcPost = {0x9463D0, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57,
                                     0x41, 0x56, 0x48, 0x83}, "CC post-update"};
constexpr Site kRestore = {0x949EB0, {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x20, 0x48, 0x89, 0x50, 0x10, 0x55,
                                      0x56, 0x57, 0x41, 0x54}, "restore actions"};
constexpr Site kPending = {0x251B420, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89,
                                       0x74, 0x24, 0x20, 0x57}, "pending actions"};

SafetyHookMid g_cc_hook, g_post_hook, g_restore_hook, g_pending_hook;
uint8_t* g_ret = nullptr;
bool g_contain = false;
std::atomic<uint32_t> g_watch_real{0};
std::atomic<int> g_lines{0};
constexpr int kMaxLines = 400;
std::atomic<uint64_t> g_skipped{0}, g_skipped_post{0};
std::atomic<uint32_t> g_extra_copy{0};  // option D: user 2's character
std::atomic<uint32_t> g_owner{0};       // option D: everything user 2 owns (pet, horse...)
uint64_t g_t0 = 0;

// P8 guest, guest_remote_update_guard (horse_diag_install_remote). Set before
// its hooks go in, never changed after: in this mode only the remote check runs.
bool g_remote_mode = false;
std::atomic<uint32_t> g_remote_local{0};  // the guest's user key; 0 = not known (nothing contained)
std::atomic<uint64_t> g_remote_skipped{0}, g_remote_skipped_post{0};
std::atomic<uint32_t> g_remote_last_key{0};
gad::KeySet<1024> g_remote_keys;       // distinct contained keys (only touched on a skip)
gad::FirstPairs<32> g_remote_first;    // the first 32: key, controller (logged by the reporter)
std::atomic<uint64_t> g_remote_first_dropped{0};  // keys beyond the 1024 of the set

uint64_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

struct Layer {
    uintptr_t a0, a8, a10, a18, chart;
};
struct State {
    uint32_t key = 0;
    Layer layer[2]{};
    uint8_t pending = 0;     // CC+0xA8
    uintptr_t pending_list = 0;  // CC+0x80
    uint8_t restore = 0xFF;  // [[CC+0x40]+0x30]
};

bool read_key(uintptr_t cc, uint32_t& key) {
    __try {
        const auto actor = *reinterpret_cast<const uintptr_t*>(cc + 0x08);
        if (!actor) return false;
        key = *reinterpret_cast<const uint32_t*>(actor + off::kActor_Key);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool read_state(uintptr_t cc, State& s) {
    __try {
        for (int i = 0; i < 2; ++i) {
            const auto l = *reinterpret_cast<const uintptr_t*>(cc + 0x88 + 8 * i);
            if (!l) continue;
            s.layer[i] = {*reinterpret_cast<const uintptr_t*>(l + 0x00), *reinterpret_cast<const uintptr_t*>(l + 0x08),
                          *reinterpret_cast<const uintptr_t*>(l + 0x10), *reinterpret_cast<const uintptr_t*>(l + 0x18),
                          *reinterpret_cast<const uintptr_t*>(l + 0x110)};
        }
        s.pending = *reinterpret_cast<const uint8_t*>(cc + 0xA8);
        s.pending_list = *reinterpret_cast<const uintptr_t*>(cc + 0x80);
        const auto st = *reinterpret_cast<const uintptr_t*>(cc + 0x40);
        s.restore = st ? *reinterpret_cast<const uint8_t*>(st + 0x30) : 0xFE;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void read_restore_data(uintptr_t cc, uint8_t (&raw)[0x10], uint64_t& f8) {
    __try {
        const auto st = *reinterpret_cast<const uintptr_t*>(cc + 0x40);
        if (st) {
            std::memcpy(raw, reinterpret_cast<const void*>(st + 0x30), sizeof raw);
            f8 = *reinterpret_cast<const uint64_t*>(st + 0xF8);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// An actor of the CC owned by the option D user (its character and children).
bool owned_by_guest(uintptr_t cc) {
    const uint32_t owner = g_owner.load(std::memory_order_relaxed);
    if (!owner) return false;
    __try {
        const auto actor = *reinterpret_cast<const uintptr_t*>(cc + 0x08);
        return actor && *reinterpret_cast<const uint32_t*>(actor + off::kActor_OwnerUser) == owner;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool is_copy(uint32_t key) {
    const uint32_t extra = g_extra_copy.load(std::memory_order_relaxed);
    return key == kCopyHorse || is_enemy_copy_key(key) || (extra && key == extra);
}

bool watched(uint32_t key) {
    return is_copy(key) || (key && key == g_watch_real.load(std::memory_order_relaxed));
}

const char* who(uint32_t key) {
    if (const uint32_t extra = g_extra_copy.load(std::memory_order_relaxed); extra && key == extra) return "USER 2";
    return key == kCopyHorse ? "COPY" : is_enemy_copy_key(key) ? "ENEMY COPY" : "real";
}

// Same "shape" = same nullness of every field (pointers themselves change).
uint32_t shape(const State& s) {
    uint32_t v = 0;
    for (int i = 0; i < 2; ++i) {
        const Layer& l = s.layer[i];
        v = v << 5 | (l.a0 != 0) | (l.a8 != 0) << 1 | (l.a10 != 0) << 2 | (l.a18 != 0) << 3 | (l.chart != 0) << 4;
    }
    return v << 10 | (s.pending != 0) << 9 | (s.pending_list != 0) << 8 | s.restore;
}

bool take_line() { return g_lines.fetch_add(1, std::memory_order_relaxed) < kMaxLines; }

void log_state(const char* where, const State& s) {
    if (!take_line()) return;
    log::info("HorseDiag +{} ms {} {} 0x{:08X}: L0 a0={} a8={} cur(+10)={} play(+18)={} chart={} | "
              "L1 cur={} play={} chart={} | pending(+A8)={} list(+80)={} restoreByte={}",
              now_ms() - g_t0, where, who(s.key), s.key, s.layer[0].a0 != 0, s.layer[0].a8 != 0,
              s.layer[0].a10 != 0, s.layer[0].a18 != 0, s.layer[0].chart != 0, s.layer[1].a10 != 0,
              s.layer[1].a18 != 0, s.layer[1].chart != 0, s.pending, s.pending_list != 0, s.restore);
}

// A layer with its own chart but nothing playing: the update crashes on it.
// Both layers: an enemy copy ("Wolf Tracker") got its layer-0 action and
// crashed on layer 1 (live test 2026-10-04).
bool layer_without_action(const State& s) {
    for (const Layer& l : s.layer) {
        if (l.chart && (!l.a10 || !l.a18)) return true;
    }
    return false;
}

// Last shape seen per watched actor (2 slots: copy, real).
std::atomic<uint32_t> g_last_shape[3] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};

// P8 guest (S34): the CC's actor is controlled by another user and a layer has
// a chart and no action. Runs for every character every frame: the local key
// first, then the controller (most actors stop there), the layers last.
// Guarded reads of scalars only (no struct copy: no CRT memcpy in a mid hook).
bool remote_without_action(uintptr_t cc, uint32_t local, uint32_t& key, uint32_t& ctrl) {
    __try {
        const auto actor = *reinterpret_cast<const uintptr_t*>(cc + 0x08);
        if (!actor) return false;
        ctrl = *reinterpret_cast<const uint32_t*>(actor + off::kActor_OwnerUser);
        if (!gad::remote_user_controlled(ctrl, local)) return false;
        // Co-op+ (2026-11-08, 3576: the guest's NPCs stood frozen in the bind pose, "actors held 623"): the game's own
        // rule from its per-frame layer tick 0xA12C00 (decomp; Co-op+'s copy_tick_would_fault): a layer with flag 2 is
        // skipped; one without flag 1 whose entry (the CC's +0x80 array, 0x78 bytes each: int n, ..., int16 a, b, c at
        // +0x20) is empty or unset is skipped; only a layer the tick really reads with its current action (+0x10) null
        // would fault. The old test (a chart and no action, 2976) also held idle NPCs whose second layer is unused, and
        // a held character is never updated: it stood in the bind pose.
        bool lacks0 = false, lacks1 = false;
        const auto entries = *reinterpret_cast<const uintptr_t*>(cc + 0x80);
        for (int i = 0; i < 2; ++i) {
            const auto layer = *reinterpret_cast<const uintptr_t*>(cc + off::kControl_Layer0 + 8ull * i);
            bool lacks = false;
            if (!layer) {
                lacks = true;
            } else {
                const uint8_t flags = *reinterpret_cast<const uint8_t*>(layer + 0x108);
                bool skip = (flags & 2) != 0;
                if (!skip && !(flags & 1)) {
                    if (!entries) {
                        skip = true;
                    } else {
                        const auto e = entries + 0x78ull * i;
                        const int32_t n = *reinterpret_cast<const int32_t*>(e);
                        const int16_t a = *reinterpret_cast<const int16_t*>(e + 0x20);
                        const int16_t b = *reinterpret_cast<const int16_t*>(e + 0x22);
                        const int16_t c = *reinterpret_cast<const int16_t*>(e + 0x24);
                        skip = n == 0 || a == -1 || b == -1 || c == -1;
                    }
                }
                if (!skip) lacks = *reinterpret_cast<const uintptr_t*>(layer + 0x10) == 0;
            }
            (i == 0 ? lacks0 : lacks1) = lacks;
            if (lacks) break;
        }
        // Co-op+ (2026-11-08, tcB36: the guest crashed at exe+0x251AF7D reading 0x28, an NPC with a chart and no
        // action on both layers): every CharacterControl::Update (0x9451E0) calls 0x9496C0, which first calls
        // 0x251AF40: it reads the SELECTED layer ([[CC+0x30]+0x1B], 0 or 1) -> its playback +0x18 -> +0x28 -> +0x7E
        // with no null check. A remote actor without that chain is held too (it has no animation to show anyway).
        if (!lacks0 && !lacks1) {
            const auto info = *reinterpret_cast<const uintptr_t*>(cc + 0x30);
            const uint8_t sel = info ? *reinterpret_cast<const uint8_t*>(info + 0x1B) : 0;
            const auto layer = sel < 2 ? *reinterpret_cast<const uintptr_t*>(cc + off::kControl_Layer0 + 8ull * sel) : 0;
            const auto play = layer ? *reinterpret_cast<const uintptr_t*>(layer + 0x18) : 0;
            const auto deep = play ? *reinterpret_cast<const uintptr_t*>(play + 0x28) : 0;
            if (!info || !deep) (sel == 1 ? lacks1 : lacks0) = true;
        }
        if (!gad::contain_remote_update(ctrl, local, lacks0, lacks1)) return false;
        key = *reinterpret_cast<const uint32_t*>(actor + off::kActor_Key);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The remote mode's whole hook body (update and post-update): true = return at once.
bool contain_remote(uintptr_t cc, std::atomic<uint64_t>& skipped) {
    const uint32_t local = g_remote_local.load(std::memory_order_relaxed);
    if (!local) return false;
    uint32_t key = 0, ctrl = 0;
    if (!remote_without_action(cc, local, key, ctrl)) return false;
    skipped.fetch_add(1, std::memory_order_relaxed);
    g_remote_last_key.store(key, std::memory_order_relaxed);
    const auto ins = g_remote_keys.insert(key);
    if (ins == gad::KeySet<1024>::Insert::Added) {
        g_remote_first.add(key, ctrl);
    } else if (ins == gad::KeySet<1024>::Insert::Full) {
        g_remote_first_dropped.fetch_add(1, std::memory_order_relaxed);
    }
    return true;
}

// Co-op+ AnimProbe (2026-11-08, [session2] anim_probe=1, host and guest, read only): the guest's NPCs stand in the
// bind pose although their actions restore (tcB37-40: 999 of 999 "restored"). Per actor seen by
// CharacterControl::Update (0x9451E0): its calls; every 5 s, over the NPC keys (0xB...): how many were updated,
// how many have a current action on layer 0, and how many whose layer-0 playback bytes (+0x18 -> 64 bytes) CHANGED
// since the last report (an advancing animation). The host's numbers are the reference.
struct ApSlot {
    std::atomic<uint32_t> key{0};
    std::atomic<uintptr_t> cc{0};
    std::atomic<uint32_t> calls{0};
    uint32_t last_calls = 0;
    uint64_t last_digest = 0;
};
constexpr size_t kApSlots = 4096;
ApSlot g_ap[kApSlots];
std::atomic<bool> g_ap_on{false};
std::atomic<uint64_t> g_ap_full{0};

void anim_probe_note(uintptr_t cc) {
    uint32_t key = 0;
    uintptr_t actor = 0;
    if (!game::safe_read(cc + 0x08, &actor, 8) || !actor || !game::safe_read(actor + off::kActor_Key, &key, 4) || !key) return;
    size_t i = (static_cast<size_t>(key) * 2654435761u >> 12) & (kApSlots - 1);
    for (int n = 0; n < 32; ++n, i = (i + 1) & (kApSlots - 1)) {
        ApSlot& sl = g_ap[i];
        uint32_t k = sl.key.load(std::memory_order_relaxed);
        if (k == 0) {
            if (!sl.key.compare_exchange_strong(k, key)) {
                if (k != key) continue;
            }
            k = key;
        }
        if (k != key) continue;
        sl.cc.store(cc, std::memory_order_relaxed);
        sl.calls.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_ap_full.fetch_add(1, std::memory_order_relaxed);
}

DWORD WINAPI anim_probe_thread(LPVOID) {
    for (;;) {
        Sleep(5000);
        uint32_t npcs = 0, updated = 0, with_action = 0, with_play = 0, advancing = 0, other = 0, with_default = 0;
        std::string samples;
        int shown = 0;
        for (auto& sl : g_ap) {
            const uint32_t key = sl.key.load(std::memory_order_relaxed);
            if (!key) continue;
            if ((key >> 28) != 0xB) {
                ++other;
                continue;
            }
            ++npcs;
            const uint32_t calls = sl.calls.load(std::memory_order_relaxed);
            const uint32_t d = calls - sl.last_calls;
            sl.last_calls = calls;
            if (!d) continue;
            ++updated;
            const uintptr_t cc = sl.cc.load(std::memory_order_relaxed);
            uintptr_t layer = 0, cur = 0, play = 0;
            if (!game::safe_read(cc + 0x88, &layer, 8) || !layer) continue;
            game::safe_read(layer + 0x10, &cur, 8);
            game::safe_read(layer + 0x18, &play, 8);
            if (cur) ++with_action;
            uintptr_t def0 = 0, def1 = 0;
            uint8_t b23a = 0;
            game::safe_read(cc + 0x70, &def0, 8);
            game::safe_read(cc + 0x78, &def1, 8);
            game::safe_read(cc + 0x23A, &b23a, 1);
            if (def0 || def1) ++with_default;
            if (!play) continue;
            ++with_play;
            uint8_t b[64] = {};
            if (!game::safe_read(play, b, sizeof b)) continue;
            uint64_t h = 1469598103934665603ull;
            for (uint8_t x : b) h = (h ^ x) * 1099511628211ull;
            if (sl.last_digest && h != sl.last_digest) ++advancing;
            sl.last_digest = h;
            if (shown < 3) {
                ++shown;
                const uint32_t* w = reinterpret_cast<const uint32_t*>(b);
                samples += std::format(" | 0x{:08X} calls {} def 0x{:X}/0x{:X} +23A {} cur 0x{:X} play", key, d, def0, def1,
                                       b23a, cur);
                for (int j = 0; j < 16; ++j) samples += std::format(" {:08X}", w[j]);
            }
        }
        log::info("AnimProbe: NPC keys {} | updated {} | layer0 action {} playback {} | playback CHANGED {} | default chart {}"
                  " | other keys {} | table full {}{}", npcs, updated, with_action, with_play, advancing, with_default, other,
                  g_ap_full.load(), samples);
    }
}

// Co-op+ InteractProbe (2026-11-08, [session2] interact_probe=1, host and guest, read only): the guest gets no
// "Greet" prompt on NPCs. The client's candidate collector 0xAC0150(player side, target actor, out list, flag) (3576
// decomp): gates (0x9857C0 state, vf188, 0xABF090, the field 0x215B790), then 0xABF3E0 (result 0 = go on, at
// 0xAC027B), then the target's entries from its component [[target+0x68]+0x98] via 0x22EAE00 (count [rbp-0x78] at
// 0xAC02E4), each tested by 0xABF5A0 (kept when byte [rsp+0x30] != 0 at 0xAC0335). Per target key: calls, the
// 0xABF3E0 refusals (last code), entries seen, entries kept. Every 5 s the 8 most asked targets.
struct IpSlot {
    std::atomic<uint32_t> key{0};
    std::atomic<uint32_t> calls{0}, gate_fail{0}, gate_code{0}, entries{0}, kept{0}, reached{0}, why{0};
    uint32_t last_calls = 0;
};
constexpr size_t kIpSlots = 1024;
IpSlot g_ip[kIpSlots];
std::atomic<bool> g_ip_on{false};
std::atomic<uint64_t> g_ks_calls{0}, g_ks_shared{0}, g_ks_still_no{0};  // KnowledgeShare (below)
SafetyHookMid g_ip_entry, g_ip_gate, g_ip_count, g_ip_keep, g_ip_eval, g_oc_entry, g_oc_fail;
// The guest's refusal is eErrNoInteractionOwnerConditionCheckFail (0x714AF5C = 0x6467532C, tcB51), stored by
// 0x25FA200(condition, player actor, target, mode, out) -- called by 0x22E7A90 for each owner condition of the entry
// (the subject is the PLAYER: [interaction side + 8]). Its failure exit 0x25FA3DA: the condition that failed.
thread_local uintptr_t t_oc_cond = 0, t_oc_player = 0, t_oc_target = 0;
thread_local uint16_t t_entry_idx = 0xFFFF;  // the collector's current entry (0xAC0330, r8 = the entry, +0x10)
std::atomic<bool> g_oc_override{false};
std::atomic<uint64_t> g_oc_overridden{0}, g_tc_overridden{0};
bool remote_npc(uintptr_t actor);
SafetyHookMid g_ip_call, g_tc_fail;
std::atomic<uint32_t> g_oc_lines{0};
uint64_t g_oc_seen[32] = {};
// 0x1940AD0(target, filter = the player's vf178(), &entry key, out) (the condition of 0x22E6FC0): it re-gathers the
// target's entries with that filter through 0x22EAE00 -- filter 0 (or an empty server list at comp+0x98) takes them
// from the NPC's AI: [[actor+0x68]+0x58] (AI component) +0x90 -> {+8 graph, +0x10 u16 state index pair}.
std::atomic<uint32_t> g_ev_lines{0};

uint32_t ip_key_of(uintptr_t actor) {
    uint32_t key = 0;
    if (!actor || !game::safe_read(actor + off::kActor_Key, &key, 4)) return 0;
    return key;
}
IpSlot* ip_slot(uint32_t key) {
    if (!key) return nullptr;
    size_t i = (static_cast<size_t>(key) * 2654435761u >> 10) & (kIpSlots - 1);
    for (int n = 0; n < 32; ++n, i = (i + 1) & (kIpSlots - 1)) {
        IpSlot& sl = g_ip[i];
        uint32_t k = sl.key.load(std::memory_order_relaxed);
        if (k == 0) {
            if (!sl.key.compare_exchange_strong(k, key) && k != key) continue;
            return &sl;
        }
        if (k == key) return &sl;
    }
    return nullptr;
}
std::atomic<uint32_t> g_local_lines{0};
std::atomic<uint32_t> g_ip_player_key{0};  // the local player's character (the collector's rcx+8)
void ip_on_entry(safetyhook::Context& ctx) {
    if (IpSlot* sl = ip_slot(ip_key_of(ctx.rdx))) sl->calls.fetch_add(1, std::memory_order_relaxed);
    {
        uintptr_t pl = 0;
        if (game::safe_read(ctx.rcx + 8, &pl, 8) && pl) g_ip_player_key.store(ip_key_of(pl), std::memory_order_relaxed);
    }
    // The player's "controlled by this client" test (vf178 0x5B3AA0, and inline in 0xABF5A0): [player+0xA0] ==
    // [[exe+0x71C0100]+0x30]+0x58 and byte [player+0x94] != 0. Logged a few times (rcx = the player's interaction
    // side, +8 its actor).
    if (g_local_lines.load(std::memory_order_relaxed) < 6) {
        static uint64_t s_last = 0;
        const uint64_t now = GetTickCount64();
        if (now - s_last < 10000) return;
        s_last = now;
        g_local_lines.fetch_add(1, std::memory_order_relaxed);
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        uintptr_t player = 0, owner = 0, g = 0, g30 = 0, local = 0, local_key_obj = 0;
        uint8_t b94 = 0;
        game::safe_read(ctx.rcx + 8, &player, 8);
        if (player) {
            game::safe_read(player + 0xA0, &owner, 8);
            game::safe_read(player + 0x94, &b94, 1);
        }
        game::safe_read(base + 0x71C0100, &g, 8);
        if (g) game::safe_read(g + 0x30, &g30, 8);
        if (g30) game::safe_read(g30 + 0x58, &local, 8);
        uint32_t owner_key = 0, local_key = 0;
        if (owner) game::safe_read(owner + 0x60, &owner_key, 4);
        if (local) game::safe_read(local + 0x60, &local_key, 4);
        (void)local_key_obj;
        log::info("InteractProbe [is local] player 0x{:08X} +0xA0 0x{:X} (key 0x{:08X}) +0x94 {} | client local "
                  "[[0x71C0100]+0x30]+0x58 0x{:X} (key 0x{:08X}) -> {}", ip_key_of(player), owner, owner_key, b94, local,
                  local_key, owner == local && b94 ? "LOCAL" : "NOT LOCAL");
    }
}
void ip_on_gate(safetyhook::Context& ctx) {  // 0xAC027B cmp [rax],r12d (r15 = target)
    IpSlot* sl = ip_slot(ip_key_of(ctx.r15));
    if (!sl) return;
    sl->reached.fetch_add(1, std::memory_order_relaxed);
    uint32_t code = 0;
    if (game::safe_read(ctx.rax, &code, 4) && code) {
        sl->gate_fail.fetch_add(1, std::memory_order_relaxed);
        sl->gate_code.store(code, std::memory_order_relaxed);
    }
}
std::atomic<uint32_t> g_st_lines{0};
uint32_t g_st_seen[48] = {};
void ip_on_count(safetyhook::Context& ctx) {  // 0xAC02E4 (after 0x22EAE00): count [rbp-0x78]
    IpSlot* sl = ip_slot(ip_key_of(ctx.r15));
    uint32_t n = 0;
    if (sl && game::safe_read(ctx.rbp - 0x78, &n, 4)) sl->entries.fetch_add(n, std::memory_order_relaxed);
    // The NPC's control state, which picks its owner-condition list (0x22E7A90: [[target+0x68]+0x30], u16 index at
    // +0x48 -> 0x3DE240 -> 0x194DEA0): once per talkable NPC (entries > 0), to compare host and guest.
    const uint32_t key = ip_key_of(ctx.r15);
    if (!n || (key >> 28) != 0xB || g_st_lines.load(std::memory_order_relaxed) >= 48) return;
    for (auto& k : g_st_seen) {
        if (k == key) return;
        if (!k) { k = key; break; }
    }
    uintptr_t comps = 0, ctrl = 0;
    uint16_t st = 0xFFFF;
    uint32_t owner = 0;
    game::safe_read(ctx.r15 + 0x68, &comps, 8);
    if (comps) game::safe_read(comps + 0x30, &ctrl, 8);
    if (ctrl) game::safe_read(ctrl + 0x48, &st, 2);
    game::safe_read(ctx.r15 + off::kActor_OwnerUser, &owner, 4);
    g_st_lines.fetch_add(1, std::memory_order_relaxed);
    log::info("InteractProbe [npc state] 0x{:08X} control state index 0x{:04X} (entries {}, controller 0x{:08X})", key, st,
              n, owner);
}
void ip_on_eval(safetyhook::Context& ctx) {  // 0x1940AD0 entry: rcx target, dl filter, r8 &key
    const uint32_t key = ip_key_of(ctx.rcx);
    if (!key || (key >> 28) != 0xB) return;  // NPCs only (not the players nor their horses' keys 0xA...)
    IpSlot* sl = ip_slot(key);
    if (!sl || sl->entries.load(std::memory_order_relaxed) == 0) return;  // a target with interaction entries
    static std::atomic<uint32_t> s_seen[64] = {};
    bool known = false;
    uint32_t free_i = 64;
    for (uint32_t i = 0; i < 64; ++i) {
        const uint32_t k = s_seen[i].load(std::memory_order_relaxed);
        if (k == key) { known = true; break; }
        if (!k && free_i == 64) free_i = i;
    }
    if (known || free_i == 64 || g_ev_lines.fetch_add(1, std::memory_order_relaxed) >= 64) return;
    s_seen[free_i].store(key, std::memory_order_relaxed);
    uintptr_t comps = 0, icomp = 0, ai = 0, state = 0, graph = 0;
    uint32_t count = 0, want = 0;
    uint16_t idx = 0;
    game::safe_read(ctx.rcx + 0x68, &comps, 8);
    if (comps) {
        game::safe_read(comps + 0x98, &icomp, 8);
        game::safe_read(comps + 0x58, &ai, 8);
    }
    if (icomp) game::safe_read(icomp + 0x98, &count, 4);
    if (ai) game::safe_read(ai + 0x90, &state, 8);
    if (state) {
        game::safe_read(state + 8, &graph, 8);
        uintptr_t ip = 0;
        if (game::safe_read(state + 0x10, &ip, 8) && ip) game::safe_read(ip, &idx, 2);
    }
    game::safe_read(ctx.r8, &want, 4);
    log::info("InteractProbe [eval 0x1940AD0] target 0x{:08X} filter {} want 0x{:X} | server list {} | AI 0x{:X} state 0x{:X} "
              "graph 0x{:X} index 0x{:04X}", key, static_cast<uint32_t>(ctx.rdx & 0xFF), want, count, ai, state, graph, idx);
}
void ip_on_call(safetyhook::Context& ctx) {  // 0xAC0330 call 0xABF5A0 (r8 = the entry)
    uint16_t idx = 0xFFFF;
    game::safe_read(ctx.r8 + 0x10, &idx, 2);
    t_entry_idx = idx;
}
std::string cond_dump(uintptr_t c, uintptr_t base);
std::atomic<uint32_t> g_tc_lines{0};
uint64_t g_tc_seen[64] = {};
void tc_on_fail(safetyhook::Context& ctx) {  // 0x25FA549: the store of eErrNoInteractionTargetConditionCheckFail
    if (g_oc_override.load(std::memory_order_relaxed) && t_entry_idx == 0x0055 && remote_npc(t_oc_target)) {
        g_tc_overridden.fetch_add(1, std::memory_order_relaxed);
        ctx.rip = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + 0x25FA55F;
        return;
    }
    // Server side (the host's process): a target condition failing for ANOTHER player's character (user 2's request
    // evaluated by the host's server) is always logged; on a client only the Greet entry 0x0055.
    bool server_user2 = false;
    {
        const auto base1 = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        uintptr_t g = 0, g30 = 0, local = 0, ch = 0;
        game::safe_read(base1 + 0x71C0100, &g, 8);
        if (g) game::safe_read(g + 0x30, &g30, 8);
        if (g30) game::safe_read(g30 + 0x58, &local, 8);
        if (local) game::safe_read(local + 0x50, &ch, 8);  // the local user's character (0x99EA20)
        const uint32_t pk = ip_key_of(t_oc_player);
        (void)ch;
        // The host's character is 0xA0100001 in every two-copy run; any other 0xA... player here is user 2's.
        server_user2 = (pk >> 28) == 0xA && pk != 0xA0100001u;
    }
    if (!server_user2 && t_entry_idx != 0x0055) return;  // the Greet (tcB60: the entry the host keeps)
    const uintptr_t c = t_oc_cond;
    const uint32_t target = ip_key_of(t_oc_target);
    const uint64_t sig = (static_cast<uint64_t>(c & 0xFFFFFFF) << 32) ^ target;
    for (auto& v : g_tc_seen) {
        if (v == sig) return;
        if (!v) { v = sig; break; }
    }
    if (g_tc_lines.fetch_add(1) >= 24) return;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    log::info("InteractProbe [{} target condition FAILED 0x25FA549] player 0x{:08X} target 0x{:08X} | condition ...{:07X}{}",
              server_user2 ? "SERVER for another player" : "GREET", ip_key_of(t_oc_player), target, c & 0xFFFFFFF,
              cond_dump(c, base));
}
// Leaf results (2026-11-08, read only, with interact_probe=1): while 0x25FA200 judges the Greet on a client (entry
// 0x0055) or any condition of a player's request on a SERVER thread, every leaf ConditionData_* evaluated through the
// generic 0x2417610 (obj, compare param, invert flag) is noted as "name=result" (tri-state al: 0/1/2; '~' = the invert
// flag set), and the exit 0x25FA561 (al = 1 pass / 0 refused) logs the list: the same NPC judged for the host's
// character (its client) and for user 2's (the host's server) shows which leaf differs.
std::string rtti_name(uintptr_t obj, uintptr_t base);
thread_local bool t_gt_on = false;
thread_local int t_gt_n = 0;
thread_local std::string t_gt_buf;
SafetyHookInline g_leaf_hook;
SafetyHookMid g_oc_exit;
std::atomic<uint32_t> g_gt_lines_client{0}, g_gt_lines_server{0};
uint64_t g_gt_seen[128] = {};
std::mutex g_gt_mu;
uintptr_t __fastcall leaf_detour(uintptr_t obj, uintptr_t param, uintptr_t flag, uintptr_t r9) {
    const uintptr_t r = g_leaf_hook.unsafe_call<uintptr_t>(obj, param, flag, r9);
    if (t_gt_on && t_gt_n < 48) {
        ++t_gt_n;
        std::string n = rtti_name(obj, reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
        if (n.rfind("ConditionData_", 0) == 0) n = n.substr(14);
        t_gt_buf += std::format(" {}{}={}", (flag & 0xFF) ? "~" : "", n, static_cast<unsigned>(r & 0xFF));
        if (n == "CheckQuestDialogCategory") {
            // 0x24752E0: the player's user's main character ([[actor+0xA0]+0xD0]), its comps [+0x68], the quest
            // dialog table [comps+0xF8] (0x1C358E0: buckets +0x68, count +0x6C, by the target's u32 +0x60), the
            // category byte the leaf compares at obj+0x18.
            uintptr_t user = 0, main = 0, comps = 0, tab = 0, tgt = 0;
            uint32_t buckets = 0, count = 0, tkey = 0;
            uint8_t want = 0;
            uintptr_t subj = 0;
            game::safe_read(param + 0x830, &subj, 8);
            if (subj) game::safe_read(subj + 0xA0, &user, 8);
            if (user) game::safe_read(user + 0xD0, &main, 8);
            if (main) game::safe_read(main + 0x68, &comps, 8);
            if (comps) game::safe_read(comps + 0xF8, &tab, 8);
            if (tab) {
                game::safe_read(tab + 0x68, &buckets, 4);
                game::safe_read(tab + 0x6C, &count, 4);
            }
            game::safe_read(param + 0x828, &tgt, 8);
            if (tgt) game::safe_read(tgt + 0x60, &tkey, 4);
            game::safe_read(obj + 0x18, &want, 1);
            // The lookup as 0x1C358E0 does it: bucket (key % buckets) * 0x100 + [tab+0x78]: u32 n, then n pairs
            // {u32 key, u32 index} from +8; the entry [[tab+0x80] + index*8]: +4 key, category +0x19 (< 6) or +0x18.
            std::string found = "absent";
            uintptr_t bk = 0, ents = 0;
            if (tab && buckets && count && game::safe_read(tab + 0x78, &bk, 8) && bk &&
                game::safe_read(tab + 0x80, &ents, 8) && ents) {
                const uintptr_t b = bk + (tkey % buckets) * 0x100ull;
                uint32_t n = 0;
                game::safe_read(b, &n, 4);
                for (uint32_t i = 0; i < n && i < 30; ++i) {
                    uint32_t k = 0, ix = 0;
                    game::safe_read(b + 8 + 8ull * i, &k, 4);
                    game::safe_read(b + 12 + 8ull * i, &ix, 4);
                    if (k != tkey) continue;
                    uintptr_t e = 0;
                    uint8_t c18 = 0, c19 = 0;
                    game::safe_read(ents + 8ull * ix, &e, 8);
                    if (e) {
                        game::safe_read(e + 0x18, &c18, 1);
                        game::safe_read(e + 0x19, &c19, 1);
                    }
                    found = std::format("entry {} cat18 {} cat19 {}", ix, c18, c19);
                    break;
                }
            }
            t_gt_buf += "[" + found + "]";
            t_gt_buf += std::format("[want {} | npc +60 0x{:X} | subject 0x{:08X} main 0x{:08X} table {} buckets {} count {}]", want, tkey,
                                    subj ? ip_key_of(subj) : 0, main ? ip_key_of(main) : 0, tab ? "yes" : "NONE", buckets, count);
        }
    }
    return r;
}
bool ks_server_thread();
void oc_on_exit(safetyhook::Context& ctx) {  // 0x25FA561: al = the result
    if (!t_gt_on) return;
    t_gt_on = false;
    const bool server = ks_server_thread();
    const uint32_t pk = ip_key_of(t_oc_player), tk = ip_key_of(t_oc_target);
    uint64_t h = 1469598103934665603ull ^ (static_cast<uint64_t>(tk) << 1) ^ (server ? 1 : 0) ^ ((ctx.rax & 0xFF) << 40);
    for (unsigned char ch : t_gt_buf) h = (h ^ ch) * 1099511628211ull;
    h ^= static_cast<uint64_t>(pk) << 20;
    {
        std::lock_guard lk(g_gt_mu);
        for (auto& v : g_gt_seen) {
            if (v == h) return;
            if (!v) { v = h; break; }
        }
    }
    auto& lines = server ? g_gt_lines_server : g_gt_lines_client;
    if (lines.fetch_add(1) >= 120) return;
    log::info("InteractProbe [LEAVES {}] player 0x{:08X} target 0x{:08X} entry 0x{:04X} -> {} |{}",
              server ? "SERVER" : "client Greet", pk, tk, t_entry_idx, (ctx.rax & 0xFF) ? "PASS" : "REFUSED", t_gt_buf);
}
// TEST user2_condition_subject_host=1 (HOST): on a SERVER thread, an interaction condition whose subject is another
// player's character is evaluated with the HOST's server character instead ("the guest follows the host's progress",
// ByJunter's rule for quests): any NPC interaction the host can do, the guest can. The host's character is looked up
// by key (0xA0100001, the first user's in every run) in the ServerActorManager each time (never kept).
std::atomic<bool> g_subject_swap{false};
std::atomic<bool> g_subject_swap_client{false};  // TEST guest_condition_subject_host=1 (GUEST): the same on the client
std::atomic<uint64_t> g_subject_swapped{0};
bool ks_server_thread();
void oc_on_entry(safetyhook::Context& ctx) {  // 0x25FA200
    if (g_subject_swap_client.load(std::memory_order_relaxed) && !ks_server_thread()) {
        // The guest's client: its own character as the subject -> the host's character's copy on this client.
        uint32_t pkey = 0;
        game::safe_read(ctx.rdx + off::kActor_Key, &pkey, 4);
        if ((pkey >> 28) == 0xA && pkey != 0xA0100001u) {
            if (const auto host = game::find_actor(0xA0100001u); host && *host) {
                ctx.rdx = *host;
                g_subject_swapped.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    if (g_subject_swap.load(std::memory_order_relaxed) && ks_server_thread()) {
        uint32_t pkey = 0, powner = 0;
        game::safe_read(ctx.rdx + off::kActor_Key, &pkey, 4);
        game::safe_read(ctx.rdx + off::kActor_OwnerUser, &powner, 4);
        if ((pkey >> 28) == 0xA && pkey != 0xA0100001u && powner != 0x90100000u) {
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            uintptr_t sam = 0;
            game::safe_read(base + session2::off::kSamSlot, &sam, 8);
            if (const auto host = session2::sam_find_actor(sam, 0xA0100001u); host && *host) {
                ctx.rdx = *host;
                g_subject_swapped.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    t_oc_cond = ctx.rcx;
    t_oc_player = ctx.rdx;
    t_oc_target = ctx.r8;
    if (g_leaf_hook) {
        const bool server = ks_server_thread();
        t_gt_on = server ? (ip_key_of(ctx.rdx) >> 28) == 0xA && (ip_key_of(ctx.r8) >> 28) == 0xB : t_entry_idx == 0x0055 && (ip_key_of(ctx.r8) >> 28) == 0xB;
        t_gt_n = 0;
        t_gt_buf.clear();
    }
}
// The class name of a game object from its RTTI (vtable[-1] -> COL; COL+12 = the type descriptor's RVA; name at +0x10).
std::string rtti_name(uintptr_t obj, uintptr_t base) {
    uintptr_t vt = 0, col = 0;
    uint32_t td = 0;
    char nm[72] = {};
    if (!game::safe_read(obj, &vt, 8) || !game::safe_read(vt - 8, &col, 8) || !game::safe_read(col + 12, &td, 4) ||
        !game::safe_read(base + td + 0x10, nm, sizeof nm - 1))
        return "?";
    std::string n(nm);
    if (n.rfind(".?AV", 0) == 0) n = n.substr(4);
    if (auto at = n.find("@pa@@"); at != std::string::npos) n = n.substr(0, at);
    return n;
}
// A condition tree (3576): And/Or/Not ContentsLogicFunction (children at +8 / +0x10, objects), ConditionData_Macro
// (u16 macro index at +0x10 -> [[0x71C2150]+0x58][index] -> info, its condition object at info+0x10, as 0x24178F0),
// leaves ConditionData_* (u16 function type at +8: its name from the table 0x5D02C70; fields +0x10..+0x48).
void cond_tree(uintptr_t obj, uintptr_t base, int depth, int& budget, std::string& out) {
    if (!obj || budget-- <= 0 || depth > 8) return;
    const std::string n = rtti_name(obj, base);
    out += std::format(" {}{}", depth ? "(" : "", n);
    if (n == "AndContentsLogicFunction" || n == "OrContentsLogicFunction") {
        uintptr_t l = 0, r = 0;
        game::safe_read(obj + 8, &l, 8);
        game::safe_read(obj + 0x10, &r, 8);
        cond_tree(l, base, depth + 1, budget, out);
        cond_tree(r, base, depth + 1, budget, out);
    } else if (n == "NotContentsLogicFunction") {
        uintptr_t ch = 0;
        game::safe_read(obj + 8, &ch, 8);
        cond_tree(ch, base, depth + 1, budget, out);
    } else if (n == "ConditionData_Macro") {
        uint16_t idx = 0;
        uintptr_t table = 0, arr = 0, info = 0, inner = 0;
        game::safe_read(obj + 0x10, &idx, 2);
        game::safe_read(base + 0x71C2150, &table, 8);
        if (table) game::safe_read(table + 0x58, &arr, 8);
        if (arr) game::safe_read(arr + 8ull * idx, &info, 8);
        if (info) game::safe_read(info + 0x10, &inner, 8);
        out += std::format(" #{}", idx);
        cond_tree(inner, base, depth + 1, budget, out);
    } else {
        uint16_t type = 0;
        uintptr_t np = 0;
        char fn[40] = {};
        game::safe_read(obj + 8, &type, 2);
        if (game::safe_read(base + 0x5D02C70 + 8ull * type, &np, 8) && np) game::safe_read(np, fn, sizeof fn - 1);
        out += std::format(" fn 0x{:X} '{}' f", type, fn);
        for (uint32_t o = 0x10; o < 0x48; o += 8) {
            uint64_t q = 0;
            game::safe_read(obj + o, &q, 8);
            out += std::format(" {:X}", q);
        }
    }
    if (depth) out += ")";
}
std::string cond_dump(uintptr_t c, uintptr_t base) {
    std::string out;
    for (int k = 0; k < 2; ++k) {
        uintptr_t p1 = 0, obj = 0;
        if (!game::safe_read(c + 8ull * k, &p1, 8) || !p1 || !game::safe_read(p1, &obj, 8) || !obj) {
            out += std::format(" | {} none", k ? "TARGET" : "OWNER");
            continue;
        }
        out += std::format(" | {}:", k ? "TARGET" : "OWNER");
        int budget = 40;
        cond_tree(obj, base, 0, budget, out);
    }
    return out;
}
bool remote_npc(uintptr_t actor) {
    if (!actor || (ip_key_of(actor) >> 28) != 0xB) return false;
    uintptr_t info = 0, comps = 0, ai = 0;
    uint8_t type = 0;
    if (!game::safe_read(actor + 0x88, &info, 8) || !info || !game::safe_read(info + 1, &type, 1) || type == 7)
        return false;  // not a player character
    if (!game::safe_read(actor + 0x68, &comps, 8) || !comps || !game::safe_read(comps + 0x58, &ai, 8) || !ai)
        return false;  // NPCs have an AI component; gimmicks (lamps...) do not
    const auto base0 = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uintptr_t g = 0, g30 = 0, local = 0;
    uint32_t local_key = 0, ctrl = 0;
    game::safe_read(base0 + 0x71C0100, &g, 8);
    if (g) game::safe_read(g + 0x30, &g30, 8);
    if (g30) game::safe_read(g30 + 0x58, &local, 8);
    if (local) game::safe_read(local + 0x60, &local_key, 4);
    game::safe_read(actor + off::kActor_OwnerUser, &ctrl, 4);
    return local_key && ctrl && ctrl != local_key;
}
void oc_on_fail(safetyhook::Context& ctx) {  // 0x25FA3DA: the store of eErrNoInteractionOwnerConditionCheckFail
    // TEST (interact_owner_override=1, guest): for a target another user controls (its [actor+0x90] is not this
    // client's user key), the owner condition passes: resume at 0x25FA55F (mov al,1 ; the epilogue).
    if (g_oc_override.load(std::memory_order_relaxed) && t_entry_idx == 0x0055 && remote_npc(t_oc_target)) {
        g_oc_overridden.fetch_add(1, std::memory_order_relaxed);
        ctx.rip = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + 0x25FA55F;
        return;
    }
    const uintptr_t c = t_oc_cond;
    const uint32_t target = ip_key_of(t_oc_target);
    IpSlot* sl = ip_slot(target);
    if (!sl || sl->entries.load(std::memory_order_relaxed) == 0) return;  // NPCs with interaction entries only
    // Both sub-conditions: c+0 and c+8 point to a pointer to the condition object, whose [0] is its exe vtable.
    auto cls = [](uintptr_t slot) -> uintptr_t {
        uintptr_t p1 = 0, obj = 0, vt = 0;
        if (!game::safe_read(slot, &p1, 8) || !p1 || !game::safe_read(p1, &obj, 8) || !obj ||
            !game::safe_read(obj, &vt, 8))
            return 0;
        return vt;
    };
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto rva = [&](uintptr_t a) { return a >= base && a < base + 0x14000000 ? a - base : a; };
    const uintptr_t v0 = rva(cls(c)), v1 = rva(cls(c + 8));
    const uint64_t sig = (static_cast<uint64_t>(c & 0xFFFFFFF) << 32) ^ target;
    for (auto& v : g_oc_seen) {
        if (v == sig) return;
        if (!v) { v = sig; break; }
    }
    if (g_oc_lines.fetch_add(1, std::memory_order_relaxed) >= 32) return;
    // A ConditionData_Macro (vtable 0x5D09E08): its macro (u16 index at obj+8) from [0x71C2150]+0x58, the macro's
    // inner condition [info+0x10] (its class), the info's first 0x60 bytes and the strings it points to.
    std::string macro;
    for (uintptr_t slot : {c, c + 8}) {
        uintptr_t p1 = 0, obj = 0, vt = 0;
        if (!game::safe_read(slot, &p1, 8) || !p1 || !game::safe_read(p1, &obj, 8) || !obj ||
            !game::safe_read(obj, &vt, 8) || rva(vt) != 0x5D09E08)
            continue;
        uint16_t idx = 0;
        uintptr_t table = 0, arr = 0, info = 0, inner = 0, inner_vt = 0;
        game::safe_read(obj + 8, &idx, 2);
        game::safe_read(base + 0x71C2150, &table, 8);
        if (table) game::safe_read(table + 0x58, &arr, 8);
        if (arr) game::safe_read(arr + 8ull * idx, &info, 8);
        if (info) game::safe_read(info + 0x10, &inner, 8);
        if (inner) game::safe_read(inner, &inner_vt, 8);
        macro += std::format(" | MACRO index {} info 0x{:X} inner class vtable 0x{:X} | info:", idx, info, rva(inner_vt));
        for (uint32_t o = 0; info && o < 0x60; o += 8) {
            uintptr_t q = 0;
            game::safe_read(info + o, &q, 8);
            macro += std::format(" +{:X}={:X}", o, q);
            char str[48] = {};
            if (q > 0x10000 && game::safe_read(q, str, sizeof str - 1)) {
                bool text = str[0] >= 0x20 && str[0] < 0x7F;
                for (int i = 0; text && str[i]; ++i) text = str[i] >= 0x20 && str[i] < 0x7F;
                if (text && std::strlen(str) >= 3) macro += std::format("('{}')", str);
            }
        }
        if (inner) {
            macro += " | inner:";
            for (uint32_t o = 0; o < 0x40; o += 8) {
                uintptr_t q = 0;
                game::safe_read(inner + o, &q, 8);
                macro += std::format(" +{:X}={:X}", o, rva(q));
            }
        }
    }
    log::info("InteractProbe [owner condition FAILED] target 0x{:08X} | condition ...{:07X} | sub +0 class vtable 0x{:X} | "
              "sub +8 class vtable 0x{:X}{}", target, c & 0xFFFFFFF, v0, v1, cond_dump(c, base));
}
void ip_on_keep(safetyhook::Context& ctx) {  // 0xAC0335 cmp byte [rsp+0x30],0
    IpSlot* sl = ip_slot(ip_key_of(ctx.r15));
    uint8_t ok = 0;
    if (sl && game::safe_read(ctx.rsp + 0x30, &ok, 1) && ok) sl->kept.fetch_add(1, std::memory_order_relaxed);
    uint32_t why = 0;  // a refused entry: 0xABF5A0's reason code (result + 4)
    if (sl && !ok && game::safe_read(ctx.rsp + 0x34, &why, 4)) sl->why.store(why, std::memory_order_relaxed);
    // Per (NPC, entry index): kept or the refusal code, once each (rbx = the entry, its u16 index at +0x10).
    static std::atomic<uint32_t> s_lines{0};
    static uint64_t s_seen[700] = {};
    const uint32_t key = ip_key_of(ctx.r15);
    uint16_t idx = 0xFFFF;
    uint32_t n_entries = 0;  // talkable townsfolk: 24-25 entries per query (the totals / the queries)
    const uint32_t calls = sl ? sl->reached.load() : 0;
    if (sl && calls) n_entries = sl->entries.load() / calls;
    if (!sl || (key >> 28) != 0xB || n_entries < 20 || !game::safe_read(ctx.rbx + 0x10, &idx, 2) ||
        s_lines.load() >= 700)
        return;
    const uint64_t sig = (static_cast<uint64_t>(key) << 16) | idx;
    for (auto& v : s_seen) {
        if (v == sig) return;
        if (!v) { v = sig; break; }
    }
    s_lines.fetch_add(1);
    log::info("InteractProbe [entry] 0x{:08X} index 0x{:04X} {}", key, idx,
              ok ? std::string("KEPT") : std::format("refused 0x{:08X}", why));
}

DWORD WINAPI interact_probe_thread(LPVOID) {
    for (;;) {
        Sleep(5000);
        struct Row { uint32_t key, d, reached, fail, code, entries, kept, why; };
        std::vector<Row> rows;
        for (auto& sl : g_ip) {
            const uint32_t key = sl.key.load(std::memory_order_relaxed);
            if (!key) continue;
            const uint32_t calls = sl.calls.load(std::memory_order_relaxed);
            const uint32_t d = calls - sl.last_calls;
            sl.last_calls = calls;
            if (!d) continue;
            rows.push_back({key, d, sl.reached.load(), sl.gate_fail.load(), sl.gate_code.load(), sl.entries.load(),
                            sl.kept.load(), sl.why.load()});
        }
        if (rows.empty()) continue;
        static bool s_codes = false;  // the refusal codes are runtime globals (filled after start): once, when set
        if (!s_codes) {
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            std::string codes;
            bool any = false;
            for (uintptr_t g : {0x7135728ull, 0x7137FC8ull, 0x7145948ull, 0x7145998ull, 0x714ABB8ull, 0x714AEECull,
                                0x714AF08ull, 0x714AF0Cull, 0x714AF14ull, 0x714AF18ull, 0x714AF2Cull, 0x714AF30ull,
                                0x714AF80ull, 0x714BE54ull, 0x714C018ull, 0x714C07Cull}) {
                uint32_t v = 0;
                game::safe_read(base + g, &v, 4);
                any = any || v;
                codes += std::format(" [0x{:X}]=0x{:X}", g, v);
            }
            if (any) {
                s_codes = true;
                log::info("InteractProbe: the refusal codes of 0x22E6FC0 / 0xABF5A0 (globals):{}", codes);
                // Where the guest's refusal 0x6467532C lives: every dword of .sdata/.rodata (0x6CD2000..0x7824000)
                // holding it (the code globals are filled at run time).
                std::string at;
                int hits = 0;
                std::vector<uint32_t> chunk(0x4000);
                for (uintptr_t r = 0x6CD2000; r < 0x7824000 && hits < 16; r += chunk.size() * 4) {
                    if (!game::safe_read(base + r, chunk.data(), chunk.size() * 4)) continue;
                    for (size_t i = 0; i < chunk.size() && hits < 16; ++i) {
                        if (chunk[i] == 0x6467532Cu) {
                            at += std::format(" 0x{:X}", r + i * 4);
                            ++hits;
                        }
                    }
                }
                log::info("InteractProbe: 0x6467532C is held at:{}", at.empty() ? " nowhere in 0x6CD2000..0x7824000" : at);
            }
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.d > b.d; });
        std::string s;
        for (size_t i = 0; i < rows.size() && i < 8; ++i) {
            const Row& r = rows[i];
            s += std::format(" | 0x{:08X} asked {} (total: past gates {}, 0xABF3E0 refused {} last 0x{:X}, entries {}, "
                             "kept {}, last refusal 0x{:X})", r.key, r.d, r.reached, r.fail, r.code, r.entries, r.kept,
                             r.why);
        }
        log::info("InteractProbe: {} targets asked in 5 s (overridden so far: owner {}, target {}) | KnowledgeShare: "
                  "user 2 no {} -> shared {}, still no {} | subject swapped to the host {}{}", rows.size(),
                  g_oc_overridden.load(), g_tc_overridden.load(), g_ks_calls.load(), g_ks_shared.load(),
                  g_ks_still_no.load(), g_subject_swapped.load(), s);
    }
}

bool ip_hook(uintptr_t base, uintptr_t rva, std::initializer_list<uint8_t> head, SafetyHookMid& h,
             safetyhook::MidHookFn fn) {
    std::vector<uint8_t> b(head.size());
    if (!game::safe_read(base + rva, b.data(), b.size()) || !std::equal(b.begin(), b.end(), head.begin())) {
        log::warn("InteractProbe: exe+0x{:X} is not the expected code; not hooked", rva);
        return false;
    }
    auto m = safetyhook::MidHook::create(reinterpret_cast<void*>(base + rva), fn);
    if (!m) return false;
    h = std::move(*m);
    return true;
}

// Co-op+ user2_knowledge_share (2026-11-08, TEST, HOST): the host's server refuses user 2's NPC interactions on
// ConditionData_CheckKnowledge (tcB65): its evaluate 0x242A270 asks 0x2308FC0(knowledge comp, &key, level) of the
// SUBJECT'S USER'S MAIN CHARACTER ([[actor+0xA0]+0xD0], comps +0x150); the comp's +8 is its actor, +0x18 an array of
// 0x18-byte entries indexed by the knowledge key, the level learned at +0. With this on, a "no" for another player's
// character on a SERVER thread (the exe's TLS byte +0x1EB) is asked again of the host's character's knowledge
// (B04: "knowledge learned by one counts for both"). Read only: nothing is learned or written.
SafetyHookInline g_ks_hook;
std::atomic<uintptr_t> g_ks_host_comp{0};
bool ks_server_thread() {
    __try {
        const auto tls = reinterpret_cast<uintptr_t*>(__readgsqword(0x58));
        return tls && tls[0] && *reinterpret_cast<const uint8_t*>(tls[0] + 0x1EB) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
int32_t ks_level(uintptr_t comp, uint16_t key) {
    uintptr_t arr = 0;
    int32_t lvl = -1;
    if (!comp || !game::safe_read(comp + 0x18, &arr, 8) || !arr) return -1;
    game::safe_read(arr + 0x18ull * key, &lvl, 4);
    return lvl;
}
bool __fastcall ks_detour(uintptr_t comp, const uint16_t* key, int32_t level) {
    const bool has = g_ks_hook.unsafe_call<bool>(comp, key, level);
    uintptr_t actor = 0;
    uint32_t akey = 0, owner = 0;
    game::safe_read(comp + 8, &actor, 8);
    if (actor) {
        game::safe_read(actor + off::kActor_Key, &akey, 4);
        game::safe_read(actor + off::kActor_OwnerUser, &owner, 4);
    }
    if ((akey >> 28) != 0xA) return has;
    if (owner == 0x90100000u) {  // the host's user (the first user in every run): its character's knowledge, from
        g_ks_host_comp.store(comp, std::memory_order_relaxed);  // either realm (the host's client mirrors it and
        return has;                                             // checks it constantly; the server seldom)
    }
    if (!ks_server_thread()) return has;
    if (has) return has;
    g_ks_calls.fetch_add(1, std::memory_order_relaxed);
    uint16_t k = 0;
    if (!key || !game::safe_read(reinterpret_cast<uintptr_t>(key), &k, 2)) return has;
    const uintptr_t host = g_ks_host_comp.load(std::memory_order_relaxed);
    static std::atomic<uint32_t> s_lines{0};
    if (s_lines.fetch_add(1) < 12) {
        log::info("KnowledgeShare [no] user 2 0x{:08X} asks key 0x{:04X} level {}: its level {} | the host's comp {} -> "
                  "level {}", akey, k, level, ks_level(comp, k), host ? "known" : "NOT known",
                  host ? ks_level(host, k) : -2);
    }
    if (host && ks_level(host, k) >= level) {
        g_ks_shared.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    g_ks_still_no.fetch_add(1, std::memory_order_relaxed);
    return has;
}

void on_cc_update(safetyhook::Context& ctx) {
    if (g_ap_on.load(std::memory_order_relaxed)) anim_probe_note(ctx.rcx);
    if (g_remote_mode) {
        if (contain_remote(ctx.rcx, g_remote_skipped)) ctx.rip = reinterpret_cast<uintptr_t>(g_ret);
        return;
    }
    uint32_t key = 0;
    // User 2's children too (pet, horse...): before this they returned here,
    // above the containment (live test 2026-10-09, crash 0x232D5FF via 0x85FC94).
    if (!read_key(ctx.rcx, key) || !(watched(key) || owned_by_guest(ctx.rcx))) return;
    State s;
    s.key = key;
    if (!read_state(ctx.rcx, s)) return;
    const uint32_t sh = shape(s);
    auto& last = g_last_shape[key == kCopyHorse ? 0 : is_enemy_copy_key(key) ? 2 : 1];
    if (last.exchange(sh, std::memory_order_relaxed) != sh) log_state("update", s);
    // Containment: a copy whose main layer has a chart but no current action
    // crashed three different functions of this update (INFORME §5).
    if (g_contain && (is_copy(key) || owned_by_guest(ctx.rcx)) && layer_without_action(s)) {
        g_skipped.fetch_add(1, std::memory_order_relaxed);
        ctx.rip = reinterpret_cast<uintptr_t>(g_ret);
    }
}

void on_cc_post(safetyhook::Context& ctx) {
    if (g_remote_mode) {
        if (contain_remote(ctx.rcx, g_remote_skipped_post)) ctx.rip = reinterpret_cast<uintptr_t>(g_ret);
        return;
    }
    if (!g_contain) return;
    uint32_t key = 0;
    if (!read_key(ctx.rcx, key) || !(is_copy(key) || owned_by_guest(ctx.rcx))) return;
    State s;
    if (!read_state(ctx.rcx, s)) return;
    if (layer_without_action(s)) {
        g_skipped_post.fetch_add(1, std::memory_order_relaxed);
        ctx.rip = reinterpret_cast<uintptr_t>(g_ret);
    }
}

void on_restore(safetyhook::Context& ctx) {
    uint32_t key = 0;
    if (!read_key(ctx.rcx, key) || !watched(key)) return;
    State s;
    s.key = key;
    if (!read_state(ctx.rcx, s)) return;
    log_state("RESTORE(0x8646D0)", s);
    // The restore data: bytes +0x30..+0x40 and the u64 at +0xF8 of [CC+0x40].
    uint8_t raw[0x10] = {};
    uint64_t f8 = 0;
    read_restore_data(ctx.rcx, raw, f8);
    if (take_line()) {
        log::info("HorseDiag   restore data [CC+0x40]+0x30: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} "
                  "{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} | +0xF8 0x{:X}",
                  raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7], raw[8], raw[9], raw[10],
                  raw[11], raw[12], raw[13], raw[14], raw[15], f8);
    }
}

void on_pending(safetyhook::Context& ctx) {
    uint32_t key = 0;
    if (!read_key(ctx.rcx, key) || !watched(key)) return;
    State s;
    s.key = key;
    if (!read_state(ctx.rcx, s)) return;
    if (s.pending) log_state("PENDING(0x2329D70)", s);
}

bool hook(const Site& site, SafetyHookMid& out, safetyhook::MidHookFn fn) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint8_t head[16];
    if (!game::safe_read(base + site.rva, head, sizeof head) || std::memcmp(head, site.head, sizeof head) != 0) {
        log::warn("HorseDiag: {} (exe+0x{:X}) not recognised", site.name, site.rva);
        return false;
    }
    auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(base + site.rva), fn);
    if (!h) {
        log::warn("HorseDiag: {} hook failed", site.name);
        return false;
    }
    out = std::move(*h);
    return true;
}

// The lone `ret` the containment resumes at (once per process).
bool make_ret() {
    if (g_ret) return true;
    g_ret = static_cast<uint8_t*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!g_ret) return false;
    g_ret[0] = 0xC3;
    DWORD old = 0;
    VirtualProtect(g_ret, 16, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), g_ret, 16);
    return true;
}

}  // namespace

bool horse_diag_install(bool contain) {
    if (g_cc_hook) return true;
    g_t0 = now_ms();
    if (!make_ret()) return false;
    const bool ok = hook(kCcUpdate, g_cc_hook, on_cc_update) && hook(kRestore, g_restore_hook, on_restore) &&
                    hook(kPending, g_pending_hook, on_pending);
    if (!ok) {
        g_cc_hook = {};
        g_restore_hook = {};
        g_pending_hook = {};
        return false;
    }
    g_contain = contain && hook(kCcPost, g_post_hook, on_cc_post);
    log::info("HorseDiag: installed (containment {})", g_contain ? "on" : "off");
    return true;
}

void horse_diag_watch(uint32_t key) {
    g_watch_real = key;
    log::info("HorseDiag: watching the copy 0x{:08X} and the real horse 0x{:08X}", kCopyHorse, key);
}

void horse_diag_also_key(uint32_t key) { g_extra_copy.store(key, std::memory_order_relaxed); }

void horse_diag_owner(uint32_t user_key) { g_owner.store(user_key, std::memory_order_relaxed); }

std::string horse_diag_describe() {
    return std::format("character update skipped {} (post-update {}) while without action",
                       g_skipped.load(std::memory_order_relaxed), g_skipped_post.load(std::memory_order_relaxed));
}

bool horse_diag_install_remote() {
    if (g_cc_hook) return false;  // the co-op's / host's install owns the two hooks
    g_t0 = now_ms();
    if (!make_ret()) return false;
    g_remote_mode = true;  // before the hooks: they read it
    if (!hook(kCcUpdate, g_cc_hook, on_cc_update)) {
        g_remote_mode = false;
        return false;
    }
    if (!hook(kCcPost, g_post_hook, on_cc_post)) {
        g_cc_hook = {};
        g_remote_mode = false;
        return false;
    }
    g_contain = true;
    return true;
}

void horse_diag_contain_remote(uint32_t local_user_key) {
    g_remote_local.store(local_user_key, std::memory_order_relaxed);
}

void horse_diag_remote_report() {
    if (!g_remote_mode) return;
    const uint32_t local = g_remote_local.load(std::memory_order_relaxed);
    g_remote_first.drain([local](uint32_t key, uint32_t ctrl, size_t i) {
        log::info("RemoteUpdateGuard: holding actor 0x{:08X} (control 0x{:08X}, not the local user 0x{:08X}): a "
                  "layer has a chart and no action; CharacterControl::Update 0x85FBA0 and PostUpdate 0x860D30 "
                  "return at once until it has one (key {} of the first 32 logged)",
                  key, ctrl, local, i + 1);
    });
}

std::string horse_diag_remote_describe() {
    if (!g_remote_mode) return {};
    const uint64_t uncounted = g_remote_first_dropped.load(std::memory_order_relaxed);
    return std::format(" | remote update guard: update skipped {} post-update {} | actors held {}{} (last 0x{:08X})",
                       g_remote_skipped.load(std::memory_order_relaxed),
                       g_remote_skipped_post.load(std::memory_order_relaxed), g_remote_keys.size(),
                       uncounted ? std::format(" (key set full: {} skips of uncounted keys)", uncounted) : std::string(),
                       g_remote_last_key.load(std::memory_order_relaxed));
}

void horse_diag_tick() {
    static uint64_t logged = 0, logged_post = 0, last = 0;
    const uint64_t a = g_skipped.load(std::memory_order_relaxed), b = g_skipped_post.load(std::memory_order_relaxed);
    const uint64_t t = now_ms();
    if ((a != logged || b != logged_post) && t - last >= 2000) {
        log::info("HorseDiag: copy's update skipped {} times (post-update {}) while it has no action", a, b);
        logged = a;
        logged_post = b;
        last = t;
    }
}

void interact_probe_start(bool on, bool owner_override) {
    g_oc_override.store(owner_override);
    if (owner_override) log::warn("InteractProbe: interact_owner_override=1 (TEST): an owner condition that fails for an "
                                  "NPC another user controls passes on this client (0x25FA3DA -> 0x25FA55F)");
    if (!on && owner_override) on = true;  // the override needs the hooks
    if (!on || g_ip_on.exchange(true)) return;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    int n = 0;
    n += ip_hook(base, 0xAC0150, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18}, g_ip_entry, ip_on_entry);
    n += ip_hook(base, 0xAC027B, {0x44, 0x39, 0x20, 0x0F, 0x85, 0xC0, 0x01, 0x00, 0x00}, g_ip_gate, ip_on_gate);
    n += ip_hook(base, 0xAC02E4, {0x48, 0x8B, 0x5D, 0x80, 0x8B, 0x45, 0x88}, g_ip_count, ip_on_count);
    n += ip_hook(base, 0xAC0335, {0x80, 0x7C, 0x24, 0x30, 0x00, 0x74, 0x32}, g_ip_keep, ip_on_keep);
    n += ip_hook(base, 0x25FA200, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18}, g_oc_entry, oc_on_entry);
    n += ip_hook(base, 0xAC0330, {0xE8, 0x6B, 0xF2, 0xFF, 0xFF}, g_ip_call, ip_on_call);
    n += ip_hook(base, 0x25FA549, {0x48, 0x8B, 0x8D, 0xB0, 0x07, 0x00, 0x00, 0x88, 0x01}, g_tc_fail, tc_on_fail);
    n += ip_hook(base, 0x25FA3DA, {0x48, 0x8B, 0x8D, 0xB0, 0x07, 0x00, 0x00, 0x88, 0x01}, g_oc_fail, oc_on_fail);
    n += ip_hook(base, 0x25FA561, {0x4C, 0x8D, 0x9C, 0x24, 0x60, 0x08, 0x00, 0x00}, g_oc_exit, oc_on_exit);
    {
        uint8_t h[9] = {};
        game::safe_read(base + 0x2417610, h, 9);
        static constexpr uint8_t want[9] = {0x48, 0x83, 0x79, 0x10, 0x00, 0x4C, 0x8B, 0xCA, 0x74};
        if (std::memcmp(h, want, 9) == 0)
            g_leaf_hook = safetyhook::create_inline(reinterpret_cast<void*>(base + 0x2417610),
                                                    reinterpret_cast<void*>(&leaf_detour));
        log::info("InteractProbe: leaf results 0x2417610 {}", g_leaf_hook ? "hooked" : "NOT hooked (bytes differ)");
    }
    {
        uint8_t h[5] = {};
        game::safe_read(base + 0x1940AD0, h, 5);
        log::info("InteractProbe: 0x1940AD0 head {:02X} {:02X} {:02X} {:02X} {:02X}", h[0], h[1], h[2], h[3], h[4]);
        if (auto m = safetyhook::MidHook::create(reinterpret_cast<void*>(base + 0x1940AD0), ip_on_eval)) {
            g_ip_eval = std::move(*m);
            ++n;
        }
    }
    if (HANDLE t = CreateThread(nullptr, 0, interact_probe_thread, nullptr, 0, nullptr)) CloseHandle(t);
    log::info("InteractProbe ON (interact_probe=1, read only): {}/5 points in the candidate collector 0xAC0150 (+ 0x1940AD0)", n);
    // 0xABF5A0's refusal codes are loaded from globals: their values, to name a refusal by its path.
    std::string codes;
    for (uintptr_t g : {0x714C018ull, 0x714AF0Cull, 0x7137FC8ull, 0x714BE54ull, 0x714AF14ull}) {
        uint32_t v = 0;
        game::safe_read(base + g, &v, 4);
        codes += std::format(" [0x{:X}]=0x{:X}", g, v);
    }
    log::info("InteractProbe: 0xABF5A0 refusal codes:{} (0x714C018 the 0xABCD30 path, 0x714AF0C 0x8D0FE70, 0x7137FC8 the "
              "target's +0x120 flag, 0x714BE54 the user's current interaction, 0x714AF14 the target's action flag +0x116 & 4)",
              codes);
}

// TEST test_greet=1 (GUEST): 70 s after the probe starts, the talkable NPC this client asked most (20+ entries per
// query) gets the host's greet request (interaction 0xAD, the bytes the host's client sent in tcB34): 3008, then 3073.
DWORD WINAPI test_greet_thread(LPVOID) {
    uint32_t best = 0, best_n = 0;
    for (int tries = 0; tries < 120 && best_n < 50; ++tries) {  // until a talkable NPC was asked 50 times (10 min)
        Sleep(5000);
        best = 0;
        best_n = 0;
        for (auto& sl : g_ip) {
            const uint32_t key = sl.key.load();
            const uint32_t reached = sl.reached.load();
            if (!key || (key >> 28) != 0xB || !reached || sl.entries.load() / reached < 20) continue;
            if (sl.calls.load() > best_n) {
                best_n = sl.calls.load();
                best = key;
            }
        }
    }
    const uint32_t me = g_ip_player_key.load();
    if (!best || !me) {
        log::warn("TestGreet: no talkable NPC asked ({:08X}) or no own character ({:08X}); nothing sent", best, me);
        return 0;
    }
    auto put = [](std::vector<uint8_t>& v, uint32_t x) {
        for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
    };
    std::vector<uint8_t> pre{0xC0, 0x0B, 0x00, 0x19, 0x00};
    put(pre, me);
    put(pre, best);
    for (uint8_t b : {0xAD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0, 0, 0, 0}) pre.push_back(b);
    std::vector<uint8_t> step{0x01, 0x0C, 0x00, 0x10, 0x00};
    put(step, me);
    put(step, best);
    for (uint8_t b : {0xAD, 0, 0, 0, 0, 0, 0, 0}) step.push_back(b);
    const bool a1 = session2::guest_test_push_raw(pre);
    Sleep(400);
    const bool a2 = session2::guest_test_push_raw(step);
    log::warn("TestGreet: sent the greet (interaction 0xAD) as 0x{:08X} to the NPC 0x{:08X} (asked {} times): 3008 {}, "
              "3073 {}", me, best, best_n, a1 ? "queued" : "REFUSED", a2 ? "queued" : "REFUSED");
    return 0;
}

void test_greet_start(bool on) {
    if (!on) return;
    if (HANDLE t = CreateThread(nullptr, 0, test_greet_thread, nullptr, 0, nullptr)) CloseHandle(t);
    log::warn("TestGreet ON (test_greet=1, TEST, guest): a greet request goes to the host 70 s after start");
}

void subject_swap_client_start(bool on) {
    g_subject_swap_client.store(on);
    if (on) log::warn("SubjectSwap (client) ON (guest_condition_subject_host=1, TEST, guest): interaction conditions are "
                      "evaluated with the host's character's copy on this client");
}

void subject_swap_start(bool on) {
    g_subject_swap.store(on);
    if (on) log::warn("SubjectSwap ON (user2_condition_subject_host=1, TEST, host): user 2's interaction conditions are "
                      "evaluated with the host's character on the server");
}

void knowledge_share_start(bool on) {
    if (!on || g_ks_hook) return;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint8_t h[8] = {};
    game::safe_read(base + 0x2308FC0, h, sizeof h);
    // 0x2308FC0: mov [rsp+8],rbx ; push rdi ; sub rsp,20 ... (checked by its first bytes)
    log::info("KnowledgeShare: 0x2308FC0 head {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}", h[0], h[1], h[2],
              h[3], h[4], h[5], h[6], h[7]);
    g_ks_hook = safetyhook::create_inline(reinterpret_cast<void*>(base + 0x2308FC0), reinterpret_cast<void*>(&ks_detour));
    log::warn("KnowledgeShare {} (user2_knowledge_share=1, TEST, host): a server-side knowledge check that fails for "
              "another player's character asks the host's character's knowledge too (read only)",
              g_ks_hook ? "ON" : "NOT installed");
}

std::string knowledge_share_describe() {
    return std::format("KnowledgeShare: user 2's failed knowledge checks {} -> shared from the host's {}, still no {} "
                       "(host knowledge comp {})", g_ks_calls.load(), g_ks_shared.load(), g_ks_still_no.load(),
                       g_ks_host_comp.load() ? "known" : "NOT seen yet");
}

void anim_probe_start(bool on) {
    if (!on || g_ap_on.exchange(true)) return;
    if (HANDLE t = CreateThread(nullptr, 0, anim_probe_thread, nullptr, 0, nullptr)) CloseHandle(t);
    log::info("AnimProbe ON (anim_probe=1, read only): NPC update / action / playback-change counts every 5 s");
}

}  // namespace cdmp::net
