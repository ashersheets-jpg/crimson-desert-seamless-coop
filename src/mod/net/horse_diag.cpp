#include "mod/net/horse_diag.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <format>

#include <safetyhook.hpp>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/net/enemy_relay.h"
#include "mod/session2/guest_actor_diag_logic.h"

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

void anim_probe_start(bool on) {
    if (!on || g_ap_on.exchange(true)) return;
    if (HANDLE t = CreateThread(nullptr, 0, anim_probe_thread, nullptr, 0, nullptr)) CloseHandle(t);
    log::info("AnimProbe ON (anim_probe=1, read only): NPC update / action / playback-change counts every 5 s");
}

}  // namespace cdmp::net
