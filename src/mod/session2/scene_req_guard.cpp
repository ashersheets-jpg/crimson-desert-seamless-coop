#include "mod/session2/scene_req_guard.h"

#include <windows.h>

#include <safetyhook.hpp>

#include <atomic>
#include <cstring>
#include <format>

#include "mod/game/safe_memory.h"
#include "mod/log.h"

namespace cdmp::session2 {
namespace {

// 1.0.0.3663, the 2808 handler 0x2C53BB0..0x2C5404E:
//   2C53DBB  call [rax+0x48]          ; field->vf48: the owner's ServerSequencerStageManager (0 = none)
//   2C53DBE  mov rsi,rax              ; <- the hook (11 bytes: this and the next movzx)
//   2C53DC1  movzx r15d,byte [rbp+0x90]
//   ...
//   2C53DD8  lea rdx,[rax+8] ; test rax,rax ; mov eax,0 ; cmove rdx,rax
//   2C53DE8  mov rdx,[rdx]            ; the crash: reading 0x0
// 2C53D7E is where the handler goes when the subject is not attached to a field: it releases the attacher and leaves
// with r15 = 0 (the result 0), the state it has at the hook (the notes, [C]).
constexpr uintptr_t kSite = 0x2C5884E, kNotAttached = 0x2C5880E;
constexpr uint8_t kSiteBytes[] = {0x48, 0x8B, 0xF0, 0x44, 0x0F, 0xB6, 0xBD, 0x90, 0x00, 0x00, 0x00,
                                  0x8B, 0x5D, 0xB8, 0x44, 0x0F, 0xB6, 0xA5, 0x98, 0x00, 0x00, 0x00,
                                  0x44, 0x8B, 0x6D, 0xC8, 0x48, 0x8D, 0x50, 0x08, 0x48, 0x85, 0xC0,
                                  0xB8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x0F, 0x44, 0xD0, 0x48, 0x8B, 0x12};
constexpr uint8_t kCallBytes[] = {0x48, 0x8B, 0x4D, 0xD8, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x48};  // at 0x2C53DB4
constexpr uint8_t kExitBytes[] = {0x48, 0x8D, 0x05, 0xE3, 0x1D, 0x0F, 0x03, 0x48, 0x89, 0x45, 0xD0};

uintptr_t g_base = 0;
safetyhook::MidHook g_hook;
std::atomic<bool> g_active{false};
std::atomic<uint64_t> g_seen{0}, g_dropped{0};

bool code_ok(uintptr_t rva, const uint8_t* bytes, size_t n, const char* what) {
    uint8_t actual[64] = {};
    if (n <= sizeof actual && game::safe_read(g_base + rva, actual, n) && std::memcmp(actual, bytes, n) == 0) return true;
    log::warn("SceneReqGuard: {} (exe+0x{:X}) is not the expected code (another game build?); the guard stays off", what,
              rva);
    return false;
}

void site_mid(safetyhook::Context& ctx) noexcept {
    g_seen.fetch_add(1, std::memory_order_relaxed);
    if (ctx.rax != 0) return;
    ctx.rip = g_base + kNotAttached;
    const uint64_t n = g_dropped.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 20 || n % 100 == 0) {
        log::warn("SceneReqGuard: a trigger volume enter / leave (2808) found no stage manager for its field's owner "
                  "(the host loading?): dropped instead of the game's null read at exe+0x{:X} ({} so far)",
                  kSite + 0x2A, n);
    }
}

}  // namespace

bool scene_req_guard_install() {
    if (g_active.load()) return true;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!code_ok(kSite - 10, kCallBytes, sizeof kCallBytes, "the 2808 handler's vf48 call") ||
        !code_ok(kSite, kSiteBytes, sizeof kSiteBytes, "its unchecked manager read") ||
        !code_ok(kNotAttached, kExitBytes, sizeof kExitBytes, "its not-attached exit")) {
        return false;
    }
    g_hook = safetyhook::create_mid(reinterpret_cast<void*>(g_base + kSite), &site_mid);
    if (!g_hook) {
        log::error("SceneReqGuard: cannot hook exe+0x{:X}; the 2808 null read stays possible", kSite);
        return false;
    }
    g_active.store(true);
    log::info("SceneReqGuard ON: a 2808 (trigger volume in / out) whose field owner has no stage manager leaves by the "
              "handler's not-attached exit (hook exe+0x{:X} -> exe+0x{:X})", kSite, kNotAttached);
    return true;
}

std::string scene_req_guard_describe() {
    if (!g_active.load()) return "SceneReqGuard off";
    return std::format("SceneReqGuard: 2808 handled {} | no stage manager, dropped {}", g_seen.load(), g_dropped.load());
}

}  // namespace cdmp::session2
