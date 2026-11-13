// Co-op+ (2026-11-09, build 1.0.0.3576): stage_candidate_guard.h.
#include "mod/session2/stage_candidate_guard.h"

#include <windows.h>

#include <safetyhook.hpp>

#include <atomic>
#include <cstring>
#include <format>

#include "mod/game/safe_memory.h"
#include "mod/log.h"

namespace cdmp::session2 {
namespace {

constexpr uintptr_t kCall = 0x2D53B48;   // call [rax+0x48]   (the scene's candidate holder)
constexpr uintptr_t kPoint = 0x2D53B4B;  // vmovsd xmm0,[rbp-0x50]; vmovsd [rax+0x668],xmm0 (the crash)
constexpr uintptr_t kExit = 0x2D53BDF;   // the handler's exit path after the copy loop
// call [rax+0x48] | vmovsd xmm0,[rbp-0x50] | vmovsd [rax+0x668],xmm0 | mov ecx,[rbp-0x48]
constexpr uint8_t kBytes[] = {0xFF, 0x50, 0x48, 0xC5, 0xFB, 0x10, 0x45, 0xB0, 0xC5, 0xFB, 0x11,
                              0x80, 0x70, 0x06, 0x00, 0x00, 0x8B, 0x4D, 0xB8};
// lea rax,[rip+..] at the exit path (the scope's vtable put back), checked too.
constexpr uint8_t kExitBytes[] = {0x48, 0x8D, 0x05};

uintptr_t g_base = 0;
SafetyHookMid g_hook;
std::atomic<uint64_t> c_skipped{0};

void on_point(safetyhook::Context& ctx) {
    if (ctx.rax != 0) return;
    ctx.r15 = 0;
    ctx.rip = g_base + kExit;
    const uint64_t n = c_skipped.fetch_add(1) + 1;
    if (n <= 10 || n % 100 == 0)
        log::warn("StageCandidateGuard: a 3117 for a scene with no candidate holder (deleted?) skipped instead of "
                  "crashing ({} so far)", n);
}

}  // namespace

void stage_candidate_guard_start(bool on) {
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!on) {
        log::info("StageCandidateGuard off (stage_candidate_guard=0)");
        return;
    }
    uint8_t b[sizeof kBytes]{}, e[sizeof kExitBytes]{};
    if (!game::safe_read(g_base + kCall, b, sizeof b) || std::memcmp(b, kBytes, sizeof b) != 0 ||
        !game::safe_read(g_base + kExit, e, sizeof e) || std::memcmp(e, kExitBytes, sizeof e) != 0) {
        log::warn("StageCandidateGuard: the 3117 handler's bytes differ (another build?): not installed");
        return;
    }
    g_hook = safetyhook::create_mid(reinterpret_cast<void*>(g_base + kPoint), &on_point);
    log::info("StageCandidateGuard {}: a 3117 whose scene has no candidate holder is skipped (0x{:X} -> 0x{:X})",
              g_hook ? "ON" : "FAILED", kPoint, kExit);
}

std::string stage_candidate_guard_describe() {
    return std::format("StageCandidateGuard: {} | skipped {}", g_hook ? "on" : "off", c_skipped.load());
}

}  // namespace cdmp::session2
