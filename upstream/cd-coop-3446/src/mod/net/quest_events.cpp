#include "mod/net/quest_events.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include <safetyhook.hpp>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/net/quest_advance.h"

namespace cdmp::net {
namespace {
namespace off = game::build_3663;

constexpr uintptr_t kPostEvent = 0x19A9B80;  // 3549: 0x19434D0; exe+0x1944110(handler, GameEventHandlerParameter*)
constexpr uint8_t kPostHead[16] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
                                   0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55};
// The quest advancement routine (quest_advance.h): every call is logged with its mission index.
constexpr uintptr_t kAdvance = 0x2BFC3D0;  // 3549: 0x2B95BE0
constexpr uint8_t kAdvanceHead[16] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57,
                                      0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
constexpr int kPerSite = 6;      // lines per (type, caller)
constexpr int kMaxLines = 3000;  // lines per run

bool g_enabled = false;
uintptr_t g_base = 0;
SafetyHookMid g_hook, g_advance_hook;
std::atomic<int> g_lines{0}, g_advance_lines{0};
std::mutex g_mu;
std::unordered_map<uint64_t, int> g_seen;  // (type << 32 | caller rva) -> times logged

void on_post(safetyhook::Context& ctx) {
    if (g_lines.load(std::memory_order_relaxed) >= kMaxLines) return;
    uint8_t p[0x70] = {};
    uintptr_t ret = 0, handler_vt = 0;
    if (!game::safe_read(ctx.rdx, p, sizeof p)) return;
    game::safe_read(ctx.rsp, &ret, sizeof ret);
    game::safe_read(ctx.rcx, &handler_vt, sizeof handler_vt);
    const uint8_t type = p[8];
    const uint32_t caller = static_cast<uint32_t>(ret - g_base);
    {
        std::lock_guard lk(g_mu);
        int& n = g_seen[(static_cast<uint64_t>(type) << 32) | caller];
        if (++n > kPerSite) return;
    }
    g_lines.fetch_add(1, std::memory_order_relaxed);
    uint32_t u[6];
    std::memcpy(u, p + 0x10, sizeof u);
    char hex[0x70 * 2 + 1];
    for (size_t i = 0; i < sizeof p; ++i) std::snprintf(hex + i * 2, 3, "%02x", p[i]);
    log::info("QuestEvents: type 0x{:02X} from exe+0x{:X} (handler vt exe+0x{:X}) +10: {:08X} {:08X} {:08X} {:08X} "
              "{:08X} {:08X} | {}",
              type, caller, handler_vt - g_base, u[0], u[1], u[2], u[3], u[4], u[5], hex);
}

void on_advance(safetyhook::Context& ctx) {
    if (g_advance_lines.fetch_add(1, std::memory_order_relaxed) >= 400) return;
    uint16_t idx = 0;
    uint64_t when = 0;
    uintptr_t ret = 0;
    game::safe_read(ctx.r8, &idx, sizeof idx);
    game::safe_read(ctx.r9, &when, sizeof when);
    game::safe_read(ctx.rsp, &ret, sizeof ret);
    log::info("QuestEvents: ADVANCE mission index {} (manager 0x{:X}, time {}) from 0x{:X} (exe+0x{:X}) [thread {}]", idx,
              ctx.rcx, when, ret, ret - g_base, GetCurrentThreadId());
}

bool head_is(uintptr_t rva, const uint8_t* want, size_t n) {
    uint8_t head[16] = {};
    return n <= sizeof head && game::safe_read(g_base + rva, head, n) && std::memcmp(head, want, n) == 0;
}

}  // namespace

void quest_events_enable(bool on) { g_enabled = on; }

void quest_events_install() {
    if (!g_enabled) return;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (g_base != off::kImageBase) return;
    if (!head_is(kPostEvent, kPostHead, sizeof kPostHead)) {
        log::warn("QuestEvents: event post not recognised at exe+0x{:X}", kPostEvent);
        return;
    }
    g_hook = safetyhook::create_mid(reinterpret_cast<void*>(g_base + kPostEvent), on_post);
    if (head_is(kAdvance, kAdvanceHead, sizeof kAdvanceHead)) {
        g_advance_hook = safetyhook::create_mid(reinterpret_cast<void*>(g_base + kAdvance), on_advance);
    }
    log::info("QuestEvents: event post hook {}, advance hook {}", g_hook ? "installed" : "FAILED",
              g_advance_hook ? "installed" : "FAILED");
}

void quest_events_request_advance(uint32_t mission_key) {
    log::info("QuestEvents: advance {:08X} requested (no check that it is under way: dev only)", mission_key);
    quest_advance_request(mission_key, false);
}

}  // namespace cdmp::net
