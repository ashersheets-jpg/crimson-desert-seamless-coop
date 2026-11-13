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

// tcB347 / tcB348 (the host, right after a loot): exe+0x402C0C0 looks for "(" and ")" in a text; it takes the
// length - 1 as a 32-bit number (exe+0x402C23E: lea ebx, [rax - 1]; add rbx, rdx), so an EMPTY text makes it scan
// from 4 GB past the text (crash at exe+0x402C250 reading text + 0xFFFFFFFF). Mid hook at exe+0x402C23E: with a
// length of 0 it goes straight to the function's own "no ')' found" path (exe+0x402C261).
safetyhook::MidHook g_text_hook;
std::atomic<uint64_t> c_text_empty{0};
uintptr_t g_text_out = 0;
void on_text_len(safetyhook::Context& ctx) {  // rax = the text's length
    if (static_cast<uint32_t>(ctx.rax) != 0) return;
    c_text_empty.fetch_add(1, std::memory_order_relaxed);
    ctx.rip = g_text_out;
}

}  // namespace

void handle_guard_start() {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    static const uint8_t kBytes[8] = {0x90, 0x48, 0x8B, 0x00, 0x48, 0x8B, 0x48, 0x08};
    uint8_t got[8] = {};
    if (!game::safe_read(base + 0x375BE7A, got, sizeof got) || std::memcmp(got, kBytes, sizeof got) != 0) {
        log::warn("HandleGuard: exe+0x375BE7A is not this build's code; no guard");
        return;
    }
    g_out = base + 0x375BFFA;
    if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(base + 0x375BE7A), on_handle)) g_hook = std::move(*h);
    log::info("HandleGuard {}: an empty handle in exe+0x375BE20 leaves the function instead of crashing (tcB342)",
              g_hook ? "ON" : "NOT installed");
    static const uint8_t kLen[6] = {0x8D, 0x58, 0xFF, 0x48, 0x03, 0xDA};
    uint8_t at[6] = {};
    if (game::safe_read(base + 0x40AB49E, at, sizeof at) && std::memcmp(at, kLen, sizeof at) == 0) {
        g_text_out = base + 0x40AB4C1;
        if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(base + 0x40AB49E), on_text_len)) {
            g_text_hook = std::move(*h);
        }
    }
    log::info("TextGuard {}: an empty text in exe+0x40AB320 takes its \"no bracket\" path instead of crashing (tcB347)",
              g_text_hook ? "ON" : "NOT installed");
}

std::string handle_guard_describe() {
    return std::format("HandleGuard: calls {} | empty handles skipped {} | TextGuard: empty texts {}", c_calls.load(),
                       c_skipped.load(), c_text_empty.load());
}

}  // namespace cdmp::session2
