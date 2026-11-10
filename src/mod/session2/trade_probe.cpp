#include "mod/session2/trade_probe.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <format>
#include <string>

#include <safetyhook.hpp>

#include "mod/game/safe_memory.h"
#include "mod/log.h"

// 3576: handler 0x2C948C0 (MoveItemInventoryToOtherInventoryReq 3111) -> worker 0x2E0B2B0(char, int* err, u32 item,
// u16 b, u64 count, u32* giver, u32* receiver, u16 inventory key, u16 slot, u32 entry) -> the distance check
// 0x2301D20 -> the move 0x2DF04A0 (or 0x2DF2560 for an entry of kind 2).
namespace cdmp::session2 {
namespace {
SafetyHookInline g_worker;
SafetyHookMid g_handler, g_move, g_move2;
std::atomic<int> g_left{64};

int* __fastcall worker_detour(uintptr_t self, int* err, uint32_t item, uint16_t b, uint64_t count, uint32_t* giver,
                              uint32_t* receiver, uint16_t inv, uint16_t slot, uint32_t entry) noexcept {
    uint32_t g = 0, r = 0;
    game::fast_read(reinterpret_cast<uintptr_t>(giver), &g, sizeof g);
    game::fast_read(reinterpret_cast<uintptr_t>(receiver), &r, sizeof r);
    const bool say = g_left.fetch_sub(1) > 0;
    if (say) {
        log::info("TradeProbe [worker in] self 0x{:X} item {} b 0x{:X} count {} giver 0x{:08X} receiver 0x{:08X} "
                  "inventory 0x{:X} slot 0x{:X} entry {}", self, item, b, count, g, r, inv, slot, entry);
    }
    int* ret = g_worker.unsafe_call<int*>(self, err, item, b, count, giver, receiver, inv, slot, entry);
    int e = -1;
    game::fast_read(reinterpret_cast<uintptr_t>(err), &e, sizeof e);
    if (say) {
        // The worker's two own errors: "no such item / inventory / entry" (0x712DFEC) and "an actor is missing"
        // (0x712E058); anything else comes from the distance check or the move itself.
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        int bad = 0, actor = 0;
        game::fast_read(base + 0x712DFEC, &bad, sizeof bad);
        game::fast_read(base + 0x712E058, &actor, sizeof actor);
        log::info("TradeProbe [worker out] err {} (0x{:X}) = {}", e, static_cast<uint32_t>(e),
                  e == 0 ? "none" : e == bad ? "bad item / inventory / entry" : e == actor ? "actor missing"
                                                                                           : "distance or move");
    }
    return ret;
}
void on_handler(SafetyHookContext&) {
    if (g_left.load() > 0) log::info("TradeProbe [handler] 3111 reached the server's handler");
}
void on_move(SafetyHookContext&) {
    if (g_left.load() > 0) log::info("TradeProbe [move] 0x2DF04A0 entered (kind 1)");
}
void on_move2(SafetyHookContext&) {
    if (g_left.load() > 0) log::info("TradeProbe [move] 0x2DF2560 entered (kind 2)");
}
SafetyHookMid g_node, g_lookup;
std::atomic<int> g_node_left{600}, g_lookup_left{80};

template <typename T>
T rd(uintptr_t a) noexcept {
    T v{};
    if (a < 0x10000 || !game::fast_read(a, &v, sizeof v)) return T{};
    return v;
}
std::string dyn_list(uintptr_t actor) {
    const uintptr_t ic = rd<uintptr_t>(rd<uintptr_t>(actor + 0x68) + 0x98);
    const uintptr_t arr = rd<uintptr_t>(ic + 0x90);
    const uint32_t n = rd<uint32_t>(ic + 0x98);
    std::string t = std::format("IC 0x{:X} dynamic {}:", ic, n);
    for (uint32_t i = 0; i < n && i < 12; ++i) {
        const uintptr_t e = arr + i * 0x38ull;
        t += std::format(" [idx 0x{:X} q20 0x{:X} d28 0x{:X} b2c {} w2e 0x{:X} b30 {}]", rd<uint16_t>(e + 0x10),
                         rd<uint64_t>(e + 0x20), rd<uint32_t>(e + 0x28), rd<uint8_t>(e + 0x2C), rd<uint16_t>(e + 0x2E),
                         rd<uint8_t>(e + 0x30));
    }
    return t;
}
void on_node(SafetyHookContext& c) {
    if (g_node_left.fetch_sub(1) <= 0) return;
    const uintptr_t node = c.rcx, ctx = c.rdx;
    const uintptr_t scene = rd<uintptr_t>(ctx);
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::string t;
    for (uint32_t o = 0x20; o < 0x70; o += 8) t += std::format(" +0x{:X}={:X}", o, rd<uint64_t>(scene + o));
    log::info("InterProbe [node {}] node 0x{:X} idx 0x{:X} b3b {} b3c {} | scene 0x{:X} vt exe+0x{:X}:{} | ctx+0x48 "
              "0x{:X}", (c.r8 & 0xFF) ? "ADD" : "REMOVE", node, rd<uint16_t>(node + 0x38), rd<uint8_t>(node + 0x3B),
              rd<uint8_t>(node + 0x3C), scene, rd<uintptr_t>(scene) - base, t, rd<uint64_t>(ctx + 0x48));
}
void on_lookup(SafetyHookContext& c) {
    const uintptr_t actor = c.rcx;
    // Only the shop interaction or a target with dynamic entries (NPC AI lookups flood otherwise).
    if (rd<uint32_t>(c.r8) != 0x000F4321 && rd<uint32_t>(rd<uintptr_t>(rd<uintptr_t>(actor + 0x68) + 0x98) + 0x98) == 0) {
        return;
    }
    if (g_lookup_left.fetch_sub(1) <= 0) return;
    log::info("InterProbe [lookup] target 0x{:08X} (actor 0x{:X}) flag {} key 0x{:08X} | {}", rd<uint32_t>(actor + 0x60),
              actor, static_cast<int>(c.rdx & 0xFF), rd<uint32_t>(c.r8), dyn_list(actor));
}
}  // namespace

void inter_probe_start() {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint8_t head[2]{};
    if (!game::safe_read(base + 0x255E100, head, sizeof head)) {
        log::warn("InterProbe: the node is unreadable; off");
        return;
    }
    if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(base + 0x255E100), on_node)) g_node = std::move(*h);
    if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(base + 0x1940AD0), on_lookup)) {
        g_lookup = std::move(*h);
    }
    log::info("InterProbe: node {} lookup {} (read only)", static_cast<bool>(g_node), static_cast<bool>(g_lookup));
}

void trade_probe_start() {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint8_t head[2]{};
    if (!game::safe_read(base + 0x2E0B2B0, head, sizeof head)) {
        log::warn("TradeProbe: the worker is unreadable; off");
        return;
    }
    if (auto h = safetyhook::InlineHook::create(reinterpret_cast<void*>(base + 0x2E0B2B0), worker_detour)) {
        g_worker = std::move(*h);
    }
    if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(base + 0x2C948C0), on_handler)) {
        g_handler = std::move(*h);
    }
    if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(base + 0x2DF04A0), on_move)) g_move = std::move(*h);
    if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(base + 0x2DF2560), on_move2)) g_move2 = std::move(*h);
    log::info("TradeProbe: worker {} handler {} move {} move2 {} (read only)", static_cast<bool>(g_worker),
              static_cast<bool>(g_handler), static_cast<bool>(g_move), static_cast<bool>(g_move2));
}
}  // namespace cdmp::session2
