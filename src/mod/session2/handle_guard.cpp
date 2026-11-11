// Co-op+ (2026-11-10): handle_guard.h.
#include "mod/session2/handle_guard.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <format>

#include <safetyhook.hpp>

#include "mod/game/safe_memory.h"
#include "mod/log.h"

namespace cdmp::session2 {
namespace {

safetyhook::MidHook g_hook;
std::atomic<uint64_t> c_calls{0}, c_skipped{0};
uintptr_t g_out = 0;

void on_handle(safetyhook::Context& ctx) {  // exe+0x36E601A: rax = the handle the virtual +0x460 returned
    c_calls.fetch_add(1, std::memory_order_relaxed);
    uintptr_t target = 0;
    if (ctx.rax && game::fast_read(ctx.rax, &target, sizeof target) && target) return;
    c_skipped.fetch_add(1, std::memory_order_relaxed);
    ctx.rip = g_out;  // the function's own early-out
}

}  // namespace

void handle_guard_start() {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    static const uint8_t kBytes[8] = {0x90, 0x48, 0x8B, 0x00, 0x48, 0x8B, 0x48, 0x08};
    uint8_t got[8] = {};
    if (!game::safe_read(base + 0x36E601A, got, sizeof got) || std::memcmp(got, kBytes, sizeof got) != 0) {
        log::warn("HandleGuard: exe+0x36E601A is not this build's code; no guard");
        return;
    }
    g_out = base + 0x36E619A;
    if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(base + 0x36E601A), on_handle)) g_hook = std::move(*h);
    log::info("HandleGuard {}: an empty handle in exe+0x36E5FC0 leaves the function instead of crashing (tcB342)",
              g_hook ? "ON" : "NOT installed");
}

std::string handle_guard_describe() {
    return std::format("HandleGuard: calls {} | empty handles skipped {}", c_calls.load(), c_skipped.load());
}

}  // namespace cdmp::session2
