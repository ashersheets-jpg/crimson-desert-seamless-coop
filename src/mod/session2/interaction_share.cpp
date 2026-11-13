#include "mod/session2/interaction_share.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

#include <safetyhook.hpp>

#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/interaction_share_logic.h"

// 3576: the StageChart interaction node 0x255E100(node, ctx, bool add). Frame: rbp+0x850 = node, rbp+0x858 = ctx
// ([ctx] = scene: +0x28 stage manager, +0x30 id). ADD = the call at 0x255E45C (rcx = the actor's interaction
// component, r8w = interaction index, [rsp+0x20] = scene id) -> 0x22EBB40; REMOVE = the call at 0x255E639
// (rcx = component, edx = key) -> 0x22EBF00 -> 0xCADEB10, which deletes EVERY dynamic entry of that key.
namespace cdmp::session2 {
namespace {
constexpr uintptr_t kAddCall = 0x25CFCFC, kRemoveCall = 0x25CFED8, kRemoveNext = 0x25CFEDD;
SafetyHookMid g_add, g_remove;
std::mutex g_mu;
ishare::Ledger g_ledger;
std::atomic<int> g_say{200};
uintptr_t g_base = 0;

template <typename T>
T rd(uintptr_t a) noexcept {
    T v{};
    if (a < 0x10000 || !game::fast_read(a, &v, sizeof v)) return T{};
    return v;
}
bool wr64(uintptr_t a, uint64_t v) noexcept {
    __try {
        *reinterpret_cast<volatile uint64_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
struct Scene {
    uint64_t id = 0;
    uintptr_t manager = 0;
    uint16_t idx = 0xFFFF;
};
Scene scene_of(const SafetyHookContext& c) noexcept {
    Scene s;
    const uintptr_t scene = rd<uintptr_t>(rd<uintptr_t>(c.rbp + 0x858));
    s.id = rd<uint64_t>(scene + 0x30);
    s.manager = rd<uintptr_t>(scene + 0x28);
    s.idx = rd<uint16_t>(rd<uintptr_t>(c.rbp + 0x850) + 0x38);
    return s;
}
void on_add(SafetyHookContext& c) {
    const uintptr_t ic = c.rcx;
    const Scene s = scene_of(c);
    if (!ic || !s.id || !s.manager) return;
    const bool empty = rd<uint32_t>(ic + 0x98) == 0;
    std::lock_guard lock(g_mu);
    g_ledger.on_add(ic, static_cast<uint16_t>(c.r8), s.id, s.manager, empty);
}
void on_remove(SafetyHookContext& c) {
    const uintptr_t ic = c.rcx;
    const Scene s = scene_of(c);
    if (!ic || !s.id || !s.manager) return;
    uint64_t keep = 0;
    {
        std::lock_guard lock(g_mu);
        if (!g_ledger.on_remove(ic, s.idx, s.id, s.manager, keep)) return;
    }
    // The entries of this index now belong to the scene that still holds them.
    const uintptr_t arr = rd<uintptr_t>(ic + 0x90);
    const uint32_t n = rd<uint32_t>(ic + 0x98);
    int handed = 0;
    for (uint32_t i = 0; arr && i < n && i < 64; ++i) {
        const uintptr_t e = arr + i * 0x38ull;
        if (rd<uint16_t>(e + 0x10) == s.idx && rd<uint64_t>(e + 0x20) == s.id && wr64(e + 0x20, keep)) ++handed;
    }
    c.rip = g_base + kRemoveNext;  // skip the game's remove-by-key
    if (g_say.fetch_sub(1) > 0) {
        log::info("InteractionShare [kept] component 0x{:X} index 0x{:X}: scene 0x{:X} (manager 0x{:X}) closed, scene "
                  "0x{:X} of the other player still holds it ({} handed over)", ic, s.idx, s.id, s.manager, keep,
                  handed);
    }
}
}  // namespace

void interaction_share_start() {
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    // Byte guards: the two call sites as built for 3576.
    uint8_t a[5]{}, r[5]{};
    const uint8_t want_a[5] = {0xE8, 0xDF, 0xD6, 0xD8, 0xFF}, want_r[5] = {0xE8, 0xC2, 0xD8, 0xD8, 0xFF};
    if (!game::safe_read(g_base + kAddCall, a, 5) || !game::safe_read(g_base + kRemoveCall, r, 5) ||
        std::memcmp(a, want_a, 5) != 0 || std::memcmp(r, want_r, 5) != 0) {
        log::warn("InteractionShare: the node's call sites do not match this build; off");
        return;
    }
    if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(g_base + kAddCall), on_add)) g_add = std::move(*h);
    if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(g_base + kRemoveCall), on_remove)) {
        g_remove = std::move(*h);
    }
    if (!g_add || !g_remove) {
        g_add = {};
        g_remove = {};
        log::warn("InteractionShare: a hook failed; off");
        return;
    }
    log::info("InteractionShare: on (an NPC's interactions stay while the other player's scene holds them)");
}
}  // namespace cdmp::session2
