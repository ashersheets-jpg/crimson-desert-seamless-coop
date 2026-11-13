// Co-op+ (2026-11-08, build 1.0.0.3576): shop_guest.h / shop_guest_logic.h.
#include "mod/session2/shop_guest.h"

#include <windows.h>

#include <safetyhook.hpp>

#include <atomic>
#include <cstring>
#include <format>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/server.h"
#include "mod/session2/shop_guest_logic.h"

namespace cdmp::session2 {
namespace {

namespace off = game::build_2976;

constexpr uintptr_t kCheck = 0x2438BE0;
constexpr uint8_t kCheckHead[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
constexpr uintptr_t kA = 0x2438D53;  // test r12d,0x1FFFFFFF ; jne
constexpr uint8_t kABytes[] = {0x41, 0xF7, 0xC4, 0xFF, 0xFF, 0xFF, 0x1F, 0x75};
constexpr uintptr_t kB = 0x2439013;  // mov rcx,[rbp+0x67] ; mov rax,[rcx] ; mov dl,1 ; call [rax+0x20] ; cmp [rbp-0x31],rbx
constexpr uint8_t kBBytes[] = {0x48, 0x8B, 0x4D, 0x67, 0x48, 0x8B, 0x01, 0xB2, 0x01, 0xFF, 0x50, 0x20,
                               0x48, 0x39, 0x5D, 0xCF, 0x0F, 0x84};
constexpr intptr_t kRbpCurrent = -0x31;  // [rbp-0x31]: the manager's current stage, compared at 0x23C7787

bool g_guest = false, g_on = false;
uintptr_t g_base = 0;
SafetyHookMid g_a, g_b;
thread_local bool t_a_bypassed = false;
std::atomic<uint64_t> c_a{0}, c_a_bypass{0}, c_b{0}, c_b_linked{0}, c_b_none{0}, c_b_same{0};
std::atomic<uint32_t> g_last_key{0};

template <class T>
bool rd(uintptr_t a, T& v) {
    return a && game::safe_read(a, &v, sizeof v);
}

// storeComp (r13) -> its actor [+8] -> the guest's character?
bool guest_subject(uintptr_t store_comp, uint32_t& key) {
    uintptr_t actor = 0;
    key = 0;
    if (!rd(store_comp + 8, actor) || !actor || !rd(actor + off::kActor_Key, key)) return false;
    if ((key >> 28) != 0xA) return false;
    if (g_guest) return true;
    const uint32_t u2 = server_user2_player_key();
    return u2 && key == u2;
}

void a_mid(safetyhook::Context& ctx) noexcept {
    c_a.fetch_add(1, std::memory_order_relaxed);
    uint32_t key = 0;
    const bool g = guest_subject(ctx.r13, key);
    const auto v = sgl::on_empty(g, ctx.r12);
    t_a_bypassed = v.bypassed;
    if (v.bypassed) {
        ctx.r12 = v.r12;
        g_last_key.store(key, std::memory_order_relaxed);
        c_a_bypass.fetch_add(1, std::memory_order_relaxed);
    }
}

void b_mid(safetyhook::Context& ctx) noexcept {
    const uint64_t n = c_b.fetch_add(1, std::memory_order_relaxed) + 1;
    uint32_t key = 0;
    const bool g = guest_subject(ctx.r13, key);
    const bool a = t_a_bypassed;
    t_a_bypassed = false;
    if (!g) return;
    uint64_t current = 0;
    const uintptr_t slot = static_cast<uintptr_t>(static_cast<intptr_t>(ctx.rbp) + kRbpCurrent);
    if (!rd(slot, current)) return;
    const uint64_t linked = ctx.rbx;
    const uint64_t use = sgl::current_for_compare(true, a, current, linked);
    if (use == current) {
        c_b_same.fetch_add(1, std::memory_order_relaxed);
    } else {
        *reinterpret_cast<uint64_t*>(slot) = use;  // this call's own stack slot
        (linked ? c_b_linked : c_b_none).fetch_add(1, std::memory_order_relaxed);
    }
    g_last_key.store(key, std::memory_order_relaxed);
    const uint64_t k = c_b_linked.load() + c_b_none.load() + c_b_same.load();
    if (k <= 30 || k % 100 == 0)
        log::info("ShopGuest [check] {} 0x{:08X}: current stage 0x{:X}, the merchant's 0x{:X}{} -> {} (call {})",
                  g_guest ? "guest client" : "server, user 2", key, current, linked, a ? " (no current: A)" : "",
                  use == current ? "as the game" : (linked ? "the merchant's stage" : "refused as the game"), n);
}

bool code_ok(uintptr_t rva, const uint8_t* want, size_t n, const char* what) {
    uint8_t got[32] = {};
    if (n > sizeof got || !game::safe_read(g_base + rva, got, n) || std::memcmp(got, want, n) != 0) {
        log::warn("ShopGuest NOT installed: {} 0x{:X} has other bytes (another game build?)", what, rva);
        return false;
    }
    return true;
}

}  // namespace

void shop_guest_install(bool on, bool guest) {
    g_guest = guest;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!on) {
        log::info("ShopGuest off (guest_shop_check=0): the guest's store operations are judged against the host's "
                  "open shop (refused unless the host has that same shop open)");
        return;
    }
    if (!code_ok(kCheck, kCheckHead, sizeof kCheckHead, "the store check") ||
        !code_ok(kA, kABytes, sizeof kABytes, "its empty-manager test") ||
        !code_ok(kB, kBBytes, sizeof kBBytes, "its stage compare"))
        return;
    g_a = safetyhook::create_mid(reinterpret_cast<void*>(g_base + kA), &a_mid);
    g_b = safetyhook::create_mid(reinterpret_cast<void*>(g_base + kB), &b_mid);
    g_on = static_cast<bool>(g_a) && static_cast<bool>(g_b);
    if (!g_on) {
        g_a = {};
        g_b = {};
    }
    log::info("ShopGuest {} (guest_shop_check=1, {}): the store check 0x{:X} takes the merchant's own shop stage as "
              "the current one for {} (money, stock, inventory, distance: the guest's own, as the game)",
              g_on ? "ON" : "NOT installed (hook failed)", guest ? "guest" : "host", kCheck,
              guest ? "this client's player" : "user 2's character");
}

std::string shop_guest_describe() {
    return std::format("ShopGuest ({}): checks A {} (empty manager let through {}) B {}: the merchant's stage {}, "
                       "refused (no linked stage) {}, as the game {} | last 0x{:08X}",
                       g_on ? "on" : "off", c_a.load(), c_a_bypass.load(), c_b.load(), c_b_linked.load(),
                       c_b_none.load(), c_b_same.load(), g_last_key.load());
}

}  // namespace cdmp::session2
