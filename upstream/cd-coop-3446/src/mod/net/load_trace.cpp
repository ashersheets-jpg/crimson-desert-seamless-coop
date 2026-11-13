#include "mod/net/load_trace.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>

#include <safetyhook.hpp>

#include "mod/game/actor_lookup.h"
#include "mod/game/offsets.h"
#include "mod/game/rtti.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"

namespace cdmp::net {
namespace {
namespace off = game::build_3663;

// The state steps on 3460 (each: cmp word [rcx+0x5E], from / jnz / mov word [rcx+0x5E], to / ret).
struct Step {
    uintptr_t rva;
    uint16_t from, to;
};
constexpr Step kSteps[] = {{0x165B530, 2, 4}, {0x165B540, 4, 8}, {0x165B550, 8, 0x10}};  // 3576 (3549: -0x820)

// Finishes an actor tree: collects the root and its children, then runs the 8 -> 0x10 step
// (vf 0x78) and vf 0x140 on each (exe+0x5B1AA6).
constexpr uintptr_t kFinishTree = 0x5DDFC0;  // 3549: 0x5B2240
constexpr uint8_t kFinishTreeHead[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55};

// In the "prepare" step (exe+0x5B0CD0), right after `call [rax+0x80]` on each component of each
// actor of the tree: al != 0 means "this component is still loading", which keeps the actor
// out of the finish step. rdi = the component entry ([rdi] = component), r14 = tree entry
// ([r14+8] = actor).
constexpr uintptr_t kComponentBusy = 0x5DD55F;  // 3549: 0x5B17DF
constexpr uint8_t kComponentBusyHead[] = {0x84, 0xC0, 0x75, 0x43};  // test al,al / jnz

// ClientCharacterControlActorComponent vtable slots 18 (resource load complete: sets the hide
// bits on 2976) and 24 (end of login: clears them), entry; rcx = component, [rcx+8] = actor.
constexpr uintptr_t kCcSlot18 = 0x971460, kCcSlot24 = 0x9717B0;  // 3576, vtable slots (3549: 0x943910, 0x943C60)
// ICharacterMovementMethod vf2 (slot 2 of Normal/AnimationAll/...): the per-frame step that,
// for a remote actor, interpolates its network samples. rcx = method, [rcx+0x20] = actor.
constexpr uintptr_t kMethodStep = 0xBF51B0;  // 3576 (3549: 0xBBD5F0)
SafetyHookMid g_step_hook;
std::atomic<uint64_t> g_step_own{0}, g_step_copy{0};
SafetyHookMid g_cc18_hook, g_cc24_hook;

std::atomic<bool> g_enabled{false};
std::atomic<int> g_mode{0};
SafetyHookMid g_hooks[3];
SafetyHookMid g_finish_hook;
SafetyHookMid g_busy_hook;
// Co-op+ (3479): who runs a DriverMovementMethod, and which actor it resolves as the one it drives.
// Enemy copies crashed in it (exe+0x780ED0 -> 0x976590 -> 0x976921, a driven actor's null action).
constexpr uintptr_t kDriverStep = 0x7AE9D9;  // 3549: 0x7819A9  // mov r10,[rax+0xC8]; rax = the driven actor's CC
SafetyHookMid g_driver_hook;
std::atomic<int> g_driver_lines{0};
void on_driver_step(safetyhook::Context& ctx) {
    if (g_driver_lines.load(std::memory_order_relaxed) > 40) return;
    uintptr_t driven_actor = 0, method_owner = 0;
    uint32_t driven = 0, owner = 0;
    game::safe_read(ctx.rax + 8, &driven_actor, 8);
    if (driven_actor) game::safe_read(driven_actor + 0x60, &driven, 4);
    // rdi = the step's rdx argument (the frame context); the method itself is not kept in a register
    // here, so the owner is read from the frame's own actor slot when there is one.
    game::safe_read(ctx.rbx + 0x20, &method_owner, 8);
    if (method_owner) game::safe_read(method_owner + 0x60, &owner, 4);
    g_driver_lines.fetch_add(1);
    log::info("LoadTrace: DriverMovementMethod step: drives actor {:08X} (owner guess {:08X})", driven, owner);
}
std::atomic<int> g_lines{0};
uintptr_t g_base = 0;

bool head_ok(const Step& s) {
    const uint8_t want[] = {0x66, 0x83, 0x79, 0x5E, static_cast<uint8_t>(s.from), 0x75, 0x06,
                            0x66, 0xC7, 0x41, 0x5E, static_cast<uint8_t>(s.to), 0x00, 0xC3};
    uint8_t got[sizeof want];
    return game::safe_read(g_base + s.rva, got, sizeof got) && std::memcmp(got, want, sizeof want) == 0;
}

// Exe return addresses on the stack (a cheap backtrace), as rvas.
int exe_frames(uintptr_t rsp, uintptr_t* out, int max) {
    int n = 0;
    __try {
        const auto* sp = reinterpret_cast<const uintptr_t*>(rsp);
        for (int i = 0; i < 96 && n < max; ++i) {
            const uintptr_t v = sp[i];
            if (v > g_base + 0x1000 && v < g_base + 0x5612000) out[n++] = v - g_base;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return n;
}

// The copies (0x?010Fxxx) always; our own player and user a few times, for comparison.
std::atomic<int> g_own_lines{0};
bool wanted(uint32_t key) {
    if ((key & 0x0FFFF000) == 0x0010F000) return g_lines.fetch_add(1) < 2000;
    if (key == 0xA0100001 || key == 0x90100000) return g_own_lines.fetch_add(1) < 40;
    return false;
}

template <int I>
void on_step(safetyhook::Context& ctx) {
    uint32_t key = 0;
    uint16_t state = 0;
    if (!game::safe_read(ctx.rcx + 0x60, &key, 4) || !game::safe_read(ctx.rcx + 0x5E, &state, 2)) return;
    if (!wanted(key)) return;
    uintptr_t frames[6];
    const int n = exe_frames(ctx.rsp, frames, 6);
    std::string text = std::format("LoadTrace: {:08X} step {:X}->{:X} (state now {:X}) | ret", key, kSteps[I].from,
                                   kSteps[I].to, state);
    for (int i = 0; i < n; ++i) text += std::format(" {:X}", frames[i]);
    log::write(log::Level::Info, text);
}

void on_finish(safetyhook::Context& ctx) {
    uint32_t key = 0;
    uint16_t state = 0;
    if (!ctx.rcx || !game::safe_read(ctx.rcx + 0x60, &key, 4) || !game::safe_read(ctx.rcx + 0x5E, &state, 2)) return;
    if (!wanted(key)) return;
    uintptr_t frames[6];
    const int n = exe_frames(ctx.rsp, frames, 6);
    std::string text = std::format("LoadTrace: finish tree, root {:08X} (state {:X}) | ret", key, state);
    for (int i = 0; i < n; ++i) text += std::format(" {:X}", frames[i]);
    log::write(log::Level::Info, text);
}

template <int Slot>
void on_cc_slot(safetyhook::Context& ctx) {
    uintptr_t actor = 0;
    uint32_t key = 0;
    if (!game::safe_read(ctx.rcx + 8, &actor, 8) || !game::safe_read(actor + 0x60, &key, 4) || !wanted(key)) return;
    uintptr_t frames[6];
    const int n = exe_frames(ctx.rsp, frames, 6);
    std::string text = std::format("LoadTrace: {:08X} CC slot {} | ret", key, Slot);
    for (int i = 0; i < n; ++i) text += std::format(" {:X}", frames[i]);
    log::write(log::Level::Info, text);
}

void on_method_step(safetyhook::Context& ctx) {
    uintptr_t actor = 0;
    uint32_t key = 0;
    if (!game::safe_read(ctx.rcx + 0x20, &actor, 8) || !game::safe_read(actor + 0x60, &key, 4)) return;
    if (key == 0xA0100001) {
        g_step_own.fetch_add(1, std::memory_order_relaxed);
    } else if (key == 0xA010F001) {
        g_step_copy.fetch_add(1, std::memory_order_relaxed);
    }
}

std::atomic<int> g_busy_lines{0};
void on_component_busy(safetyhook::Context& ctx) {
    if ((ctx.rax & 0xFF) == 0) return;
    uintptr_t actor = 0, comp = 0, vt = 0;
    uint32_t key = 0;
    if (!game::safe_read(ctx.r14 + 8, &actor, 8) || !game::safe_read(actor + 0x60, &key, 4)) return;
    if ((key & 0x0FFFF000) != 0x0010F000) return;  // copies only
    if (!game::safe_read(ctx.rdi, &comp, 8) || !game::safe_read(comp, &vt, 8)) return;
    if (g_busy_lines.fetch_add(1) > 200) return;
    log::info("LoadTrace: copy {:08X} waits on component {} (vtable exe+0x{:X}, comp 0x{:X})", key,
              game::rtti_name_of_vtable(vt), vt - g_base, comp);
}

// Experiment ([dev] load_trace=2): clear the copy's status flags at ClientStatusActorComponent
// +0x260 (bit 2 there on the copy, 0 on our own character) once it has loaded, and log it.
uintptr_t status_component(uintptr_t actor) {
    uintptr_t list = 0;
    uint32_t n = 0;
    if (!game::safe_read(actor + 0x78, &list, 8) || !game::safe_read(actor + 0x80, &n, 4) || n > 128) return 0;
    for (uint32_t i = 0; i < n; ++i) {
        uintptr_t c = 0, vt = 0;
        if (game::safe_read(list + i * 16, &c, 8) && c && game::safe_read(c, &vt, 8) &&
            vt == g_base + off::kVtStatusComponent) {
            return c;
        }
    }
    return 0;
}
uintptr_t component_of(uintptr_t actor, uintptr_t vtable_rva) {
    uintptr_t list = 0;
    uint32_t n = 0;
    if (!game::safe_read(actor + 0x78, &list, 8) || !game::safe_read(actor + 0x80, &n, 4) || n > 128) return 0;
    for (uint32_t i = 0; i < n; ++i) {
        uintptr_t c = 0, vt = 0;
        if (game::safe_read(list + i * 16, &c, 8) && c && game::safe_read(c, &vt, 8) && vt == g_base + vtable_rva) return c;
    }
    return 0;
}
bool write_f32(uintptr_t at, float v) {
    __try {
        *reinterpret_cast<volatile float*>(at) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool write_u32(uintptr_t at, uint32_t v) {
    __try {
        *reinterpret_cast<volatile uint32_t*>(at) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
DWORD WINAPI unhide_loop(LPVOID) {
    uint64_t last[4][2] = {};
    bool seen[4] = {};
    const uint32_t keys[4] = {0xA0100001u, 0xA010F001u, 0xB010006Cu, 0xB010F001u};
    uint64_t last_report = GetTickCount64();
    for (;;) {
        Sleep(100);
        if (GetTickCount64() - last_report >= 5000) {
            last_report = GetTickCount64();
            log::info("LoadTrace: movement steps in 5 s: ours {}, copy {}", g_step_own.exchange(0), g_step_copy.exchange(0));
        }
        for (int k = 0; k < 4; ++k) {
            const auto actor = game::find_actor(keys[k]);
            if (!actor) continue;
            const uintptr_t st = status_component(*actor);
            uint64_t flags[2] = {};
            uint16_t state = 0;
            if (!st || !game::safe_read(st + 0x3B8, flags, sizeof flags)) continue;
            game::safe_read(*actor + 0x5E, &state, 2);
            if (!seen[k] || flags[0] != last[k][0] || flags[1] != last[k][1]) {
                log::info("LoadTrace: {:08X} status flags +0x3B8 {:X} +0x3C0 {:X} (load state {:X})", keys[k],
                          flags[0], flags[1], state);
                seen[k] = true;
                last[k][0] = flags[0];
                last[k][1] = flags[1];
            }
        }
    }
}

}  // namespace

void load_trace_enable(bool on) { g_enabled = on; }
int load_trace_mode() { return g_mode; }
void load_trace_set_mode(int mode) {
    g_mode = mode;
    g_enabled = mode != 0;
}

void load_trace_install() {
    if (!g_enabled) return;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (g_base != off::kImageBase) return;
    g_driver_hook = safetyhook::create_mid(reinterpret_cast<void*>(g_base + kDriverStep), on_driver_step);
    log::info("LoadTrace: driver-step hook {}", g_driver_hook ? "installed" : "FAILED");
    using Fn = void (*)(safetyhook::Context&);
    constexpr Fn kFns[] = {on_step<0>, on_step<1>, on_step<2>};
    for (int i = 0; i < 3; ++i) {
        if (!head_ok(kSteps[i])) {
            log::warn("LoadTrace: step {:X}->{:X} not recognised at exe+0x{:X}", kSteps[i].from, kSteps[i].to,
                      kSteps[i].rva);
            continue;
        }
        g_hooks[i] = safetyhook::create_mid(reinterpret_cast<void*>(g_base + kSteps[i].rva), kFns[i]);
        log::info("LoadTrace: step {:X}->{:X} hook {}", kSteps[i].from, kSteps[i].to, g_hooks[i] ? "installed" : "FAILED");
    }
    uint8_t head[sizeof kFinishTreeHead];
    if (game::safe_read(g_base + kFinishTree, head, sizeof head) && std::memcmp(head, kFinishTreeHead, sizeof head) == 0) {
        g_finish_hook = safetyhook::create_mid(reinterpret_cast<void*>(g_base + kFinishTree), on_finish);
        log::info("LoadTrace: finish-tree hook {}", g_finish_hook ? "installed" : "FAILED");
    }
    {
        const uintptr_t vt = g_base + off::kVtCharacterControl;
        uintptr_t s18 = 0, s24 = 0;
        if (game::safe_read(vt + 18 * 8, &s18, 8) && game::safe_read(vt + 24 * 8, &s24, 8) &&
            s18 == g_base + kCcSlot18 && s24 == g_base + kCcSlot24) {
            g_cc18_hook = safetyhook::create_mid(reinterpret_cast<void*>(s18), on_cc_slot<18>);
            g_cc24_hook = safetyhook::create_mid(reinterpret_cast<void*>(s24), on_cc_slot<24>);
            log::info("LoadTrace: CC slot 18/24 hooks {}/{}", g_cc18_hook ? "installed" : "FAILED",
                      g_cc24_hook ? "installed" : "FAILED");
        }
    }
    {
        uintptr_t s2 = 0;
        if (game::safe_read(g_base + off::kVtNormalMovementMethod + 2 * 8, &s2, 8) && s2 == g_base + kMethodStep) {
            g_step_hook = safetyhook::create_mid(reinterpret_cast<void*>(s2), on_method_step);
            log::info("LoadTrace: movement-step hook {}", g_step_hook ? "installed" : "FAILED");
        }
    }
    CloseHandle(CreateThread(nullptr, 0, unhide_loop, nullptr, 0, nullptr));  // watches; clears only in mode 2
    uint8_t busy[sizeof kComponentBusyHead];
    if (game::safe_read(g_base + kComponentBusy, busy, sizeof busy) &&
        std::memcmp(busy, kComponentBusyHead, sizeof busy) == 0) {
        g_busy_hook = safetyhook::create_mid(reinterpret_cast<void*>(g_base + kComponentBusy), on_component_busy);
        log::info("LoadTrace: component-busy hook {}", g_busy_hook ? "installed" : "FAILED");
    }
}

}  // namespace cdmp::net
