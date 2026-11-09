// Co-op+ (2026-11-09, build 1.0.0.3576): quest_route.h.
#include "mod/session2/quest_route.h"

#include <windows.h>

#include <safetyhook.hpp>

#include <atomic>
#include <cstring>
#include <format>

#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

// The quest component's game-event callback, as registered (ServerQuestActorComponent vfB0 0x2B744B0 stores it).
constexpr uintptr_t kThunk = 0x2B9C660;
constexpr uint8_t kThunkBytes[] = {0xE9, 0x3B, 0xE1, 0xFD, 0xFF};  // jmp 0x2B7A7A0
// In 0x2B7A7A0: mov rcx,[rsp+0x30] | mov rax,[rcx] | call [rax+0x40] | mov rsi,rax | test rax,rax | je +0x4D
constexpr uintptr_t kCtx = 0x2B7A7D0, kPoint = 0x2B7A7DB;
constexpr uint8_t kCtxBytes[] = {0x48, 0x8B, 0x4C, 0x24, 0x30, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x40,
                                 0x48, 0x8B, 0xF0, 0x48, 0x85, 0xC0, 0x74, 0x4D};
// The callback's next instructions read the component's actor (mov rbx,[rbx+8]): rbx = the component here.
constexpr uint32_t kCompActor = 0x08;
constexpr uint32_t kActorComponents = 0x68, kComponentsQuest = 0x138, kQuestManager = 0x18;
constexpr uint32_t kActorUser = 0xA0, kUserMain = 0xD0;
constexpr uintptr_t kVtQuestComponent = 0x5EE8C80, kVtQuestManager = 0x5EE8DD0;  // 3576 RTTI

uintptr_t g_base = 0;
bool g_own = false, g_shared = false;
SafetyHookMid g_mid;
SafetyHookInline g_thunk;
thread_local int t_pass = 0;  // 0: not inside our detour, 1: the event's own pass, 2: the other player's pass
std::atomic<uint64_t> c_events{0}, c_own_u2{0}, c_own_host{0}, c_unreadable{0}, c_shared{0}, c_shared_skipped{0};

uintptr_t rdp(uintptr_t a) {
    uintptr_t v = 0;
    return a && game::fast_read(a, &v, sizeof v) ? v : 0;
}

uintptr_t manager_of(uintptr_t actor) {
    const uintptr_t comps = rdp(actor + kActorComponents);
    const uintptr_t comp = rdp(comps + kComponentsQuest);
    if (!comp || rdp(comp) != g_base + kVtQuestComponent) return 0;
    const uintptr_t mgr = rdp(comp + kQuestManager);
    return mgr && rdp(mgr) == g_base + kVtQuestManager ? mgr : 0;
}

// The other player's main character (0: none in this session).
uintptr_t other_character(uintptr_t actor, bool* actor_is_u2) {
    uintptr_t host = 0, u2 = 0;
    if (!server_users(host, u2) || !host || !u2) return 0;
    const uintptr_t user = rdp(actor + kActorUser);
    if (actor_is_u2) *actor_is_u2 = user == u2;
    if (user == u2) return rdp(host + kUserMain);
    if (user == host) return rdp(u2 + kUserMain);
    return 0;
}

void on_point(safetyhook::Context& ctx) {
    const uintptr_t actor = rdp(ctx.rbx + kCompActor);
    if (!actor) return;
    if (t_pass == 2) {  // the other player's pass: its manager, or none (rax 0: the callback skips)
        const uintptr_t other = other_character(actor, nullptr);
        const uintptr_t mgr = other ? manager_of(other) : 0;
        if (!mgr) {
            c_shared_skipped.fetch_add(1, std::memory_order_relaxed);
        } else if (c_shared.fetch_add(1, std::memory_order_relaxed) < 5) {
            log::info("QuestRoute [shared] a game event of character 0x{:08X} also offered to the other player's quest "
                      "manager 0x{:X}", static_cast<uint32_t>(rdp(actor + 0x60)), mgr);
        }
        ctx.rax = mgr;
        return;
    }
    if (!g_own) return;
    if ((c_events.fetch_add(1, std::memory_order_relaxed) + 1) % 200 == 0) log::info("{}", quest_route_describe());
    const uintptr_t mine = manager_of(actor);
    if (!mine) {
        c_unreadable.fetch_add(1, std::memory_order_relaxed);
        return;  // the native manager (the field owner's)
    }
    if (mine != ctx.rax) {
        const uint64_t n = c_own_u2.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 10)
            log::info("QuestRoute: a game event of character 0x{:08X} applied to its OWN quest manager 0x{:X} (the "
                      "field's was 0x{:X})", static_cast<uint32_t>(rdp(actor + 0x60)), mine, ctx.rax);
        ctx.rax = mine;
    } else {
        c_own_host.fetch_add(1, std::memory_order_relaxed);
    }
}

uintptr_t thunk_detour(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
    const int outer = t_pass;
    t_pass = 1;
    const uintptr_t r = g_thunk.unsafe_call<uintptr_t>(a, b, c, d);
    if (g_shared && outer == 0) {
        t_pass = 2;
        g_thunk.unsafe_call<uintptr_t>(a, b, c, d);
    }
    t_pass = outer;
    return r;
}

}  // namespace

void quest_route_start(bool own_progress, bool shared_credit, bool guest) {
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (guest || (!own_progress && !shared_credit)) {
        log::info("QuestRoute off (quest_own_progress={}, quest_shared_credit={}{})", own_progress ? 1 : 0,
                  shared_credit ? 1 : 0, guest ? ", guest" : "");
        return;
    }
    uint8_t t[sizeof kThunkBytes]{}, c[sizeof kCtxBytes]{};
    if (!game::safe_read(g_base + kThunk, t, sizeof t) || std::memcmp(t, kThunkBytes, sizeof t) != 0 ||
        !game::safe_read(g_base + kCtx, c, sizeof c) || std::memcmp(c, kCtxBytes, sizeof c) != 0) {
        log::warn("QuestRoute: the quest event callback's bytes differ (another build?): not installed");
        return;
    }
    g_own = own_progress;
    g_mid = safetyhook::create_mid(reinterpret_cast<void*>(g_base + kPoint), &on_point);
    if (shared_credit && g_mid) {
        auto h = safetyhook::InlineHook::create(reinterpret_cast<void*>(g_base + kThunk),
                                                reinterpret_cast<void*>(&thunk_detour));
        if (h) {
            g_thunk = std::move(*h);
            g_shared = true;
        }
    }
    log::info("QuestRoute {}: own progress {} (each player's game events go to its own quest manager, 0x{:X}) | "
              "shared credit {} (the same event also offered to the other player's manager, 0x{:X})",
              g_mid ? "ON" : "FAILED", g_own ? "on" : "off", kPoint, g_shared ? "on" : (shared_credit ? "FAILED" : "off"),
              kThunk);
}

std::string quest_route_describe() {
    return std::format("QuestRoute: {} | events {} | own manager given (not the field's) {} | same as the field's {} | "
                       "unreadable {} | shared passes {} (no other manager {})",
                       g_mid ? "on" : "off", c_events.load(), c_own_u2.load(), c_own_host.load(), c_unreadable.load(),
                       c_shared.load(), c_shared_skipped.load());
}

}  // namespace cdmp::session2
