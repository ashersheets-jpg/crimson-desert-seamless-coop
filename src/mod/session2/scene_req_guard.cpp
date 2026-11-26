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

// Each guarded handler (1.0.0.3732) asks the subject's field (vf48) for its owner's sequencer stage manager and uses
// it with no null test. The hook sits on the instruction after `call [rax+0x48]`; a 0 answer goes to the handler's own
// "not attached" block, which (with the attached flag set) releases the attacher as the handler's normal end does,
// releases its scope and leaves with the result 0. The registers that block relies on are set at the handler's entry
// only (checked: build/port3732/guardcheck73.py), and no branch lands inside the patched bytes.
//
// 2808 SequencerTriggerVolumeInOutReq (Borque's host crash 18 Nov 13:46:51; 3663 exe+0x2C53BB0):
//   site: mov rsi,rax ; movzx r15d,byte [rbp+0x90] ... lea rdx,[rax+8] ; test rax,rax ; cmove ; mov rdx,[rdx] (crash)
// 2678 DeleteSequencerReq (handler 0x2D54840; 2026-11-19, the notes' 4.3 list traced: [rax+0x18] at 0x2D54A1A with no
//   test): site 0x2D549DA mov r13,rax ; movzx r15d,byte [rbp+0x90]; exit 0x2D5498D; the result r12d (0) at 0x2D54AC1.
// 3141 UpdateStageChartDelayedEventReq (handler 0x2D56490): the manager goes to 0x2B571B0, which reads [rcx+0x18] at
//   0x2B571FB with no test: site 0x2D5663F lea rcx,[rbp+0x88] ; mov [rsp+0x20],rcx; exit 0x2D56606; result r15d (0).
// The other seven of the notes' list (2644 2654 2672 2563 2313 2546 2924, 2125) branch away before their callee's
// first use of it on the straight path; not guarded (build/port3732/nulltrace73.py).
struct Site {
    uint16_t op;
    const char* name;
    uintptr_t site, exit;
    const uint8_t* site_bytes;
    size_t site_n;
    const uint8_t* exit_bytes;
    size_t exit_n;
};
constexpr uint8_t kCallBytes[] = {0x48, 0x8B, 0x01, 0xFF, 0x50, 0x48};  // mov rax,[rcx] ; call [rax+0x48], before each site
constexpr uint8_t k2808Site[] = {0x48, 0x8B, 0xF0, 0x44, 0x0F, 0xB6, 0xBD, 0x90, 0x00, 0x00, 0x00,
                                 0x8B, 0x5D, 0xB8, 0x44, 0x0F, 0xB6, 0xA5, 0x98, 0x00, 0x00, 0x00,
                                 0x44, 0x8B, 0x6D, 0xC8, 0x48, 0x8D, 0x50, 0x08, 0x48, 0x85, 0xC0,
                                 0xB8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x0F, 0x44, 0xD0, 0x48, 0x8B, 0x12};
constexpr uint8_t k2808Exit[] = {0x48, 0x8D, 0x05, 0xDB, 0x25, 0x0F, 0x03, 0x48, 0x89, 0x45, 0xD0};
constexpr uint8_t k2678Site[] = {0x4C, 0x8B, 0xE8, 0x44, 0x0F, 0xB6, 0xBD, 0x90, 0x00, 0x00, 0x00, 0x48, 0x8B,
                                 0x8D, 0x98, 0x00, 0x00, 0x00, 0x48, 0x89, 0x8D, 0x98, 0x00, 0x00, 0x00};
constexpr uint8_t k2678Exit[] = {0x48, 0x8D, 0x3D, 0x5C, 0x64, 0xFF, 0x02, 0x48, 0x89, 0x7D, 0xE0, 0x80};
constexpr uint8_t k3141Site[] = {0x48, 0x8D, 0x8D, 0x88, 0x00, 0x00, 0x00, 0x48, 0x89, 0x4C, 0x24, 0x20, 0x4C, 0x8D, 0x8D,
                                 0x80, 0x00, 0x00, 0x00, 0x4C, 0x8D, 0x45, 0xB8, 0x48, 0x8D, 0x55, 0x80, 0x48, 0x8B, 0xC8};
constexpr uint8_t k3141Exit[] = {0x48, 0x8D, 0x05, 0xE3, 0x47, 0xFF, 0x02, 0x48, 0x89, 0x45, 0xC0, 0x80};
constexpr Site kSites[] = {
    {3004, "trigger volume enter / leave", 0x2D90DDE, 0x2D90D9E, k2808Site, sizeof k2808Site, k2808Exit, sizeof k2808Exit},
    {2981, "sequencer delete", 0x2E91F1A, 0x2E91ECD, k2678Site, sizeof k2678Site, k2678Exit, sizeof k2678Exit},
    {3149, "stage chart delayed event", 0x2E93B7F, 0x2E93B46, k3141Site, sizeof k3141Site, k3141Exit, sizeof k3141Exit},
};
constexpr size_t kN = std::size(kSites);

uintptr_t g_base = 0;
safetyhook::MidHook g_hooks[kN];
std::atomic<bool> g_active{false}, g_on[kN]{};
std::atomic<uint64_t> g_seen[kN]{}, g_dropped[kN]{};

bool code_ok(uintptr_t rva, const uint8_t* bytes, size_t n, const char* what, uint16_t op) {
    uint8_t actual[64] = {};
    if (n <= sizeof actual && game::safe_read(g_base + rva, actual, n) && std::memcmp(actual, bytes, n) == 0) return true;
    log::warn("SceneReqGuard: {} of the {} handler (exe+0x{:X}) is not the expected code (another game build?); that "
              "guard stays off", what, op, rva);
    return false;
}

template <size_t I>
void site_mid(safetyhook::Context& ctx) noexcept {
    g_seen[I].fetch_add(1, std::memory_order_relaxed);
    if (ctx.rax != 0) return;
    ctx.rip = g_base + kSites[I].exit;
    const uint64_t n = g_dropped[I].fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 20 || n % 100 == 0) {
        log::warn("SceneReqGuard: a {} ({}) found no stage manager for its field's owner (the host loading?): dropped "
                  "instead of the game's null read ({} so far)",
                  kSites[I].name, kSites[I].op, n);
    }
}
using MidFn = void (*)(safetyhook::Context&) noexcept;
constexpr MidFn kFns[kN] = {&site_mid<0>, &site_mid<1>, &site_mid<2>};

}  // namespace

bool scene_req_guard_install() {
    if (g_active.load()) return true;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    int on = 0;
    for (size_t i = 0; i < kN; ++i) {
        const Site& s = kSites[i];
        if (!code_ok(s.site - sizeof kCallBytes, kCallBytes, sizeof kCallBytes, "the vf48 call", s.op) ||
            !code_ok(s.site, s.site_bytes, s.site_n, "the unchecked manager use", s.op) ||
            !code_ok(s.exit, s.exit_bytes, s.exit_n, "the not-attached exit", s.op))
            continue;
        g_hooks[i] = safetyhook::create_mid(reinterpret_cast<void*>(g_base + s.site), kFns[i]);
        if (!g_hooks[i]) {
            log::error("SceneReqGuard: cannot hook exe+0x{:X}; the {} null read stays possible", s.site, s.op);
            continue;
        }
        g_on[i].store(true);
        ++on;
        log::info("SceneReqGuard ON for {} ({}): a field owner without a stage manager leaves by the handler's "
                  "not-attached exit (hook exe+0x{:X} -> exe+0x{:X})", s.op, s.name, s.site, s.exit);
    }
    if (!on) return false;
    g_active.store(true);
    return true;
}

std::string scene_req_guard_describe() {
    if (!g_active.load()) return "SceneReqGuard off";
    std::string out = "SceneReqGuard:";
    for (size_t i = 0; i < kN; ++i) {
        if (!g_on[i].load()) {
            out += std::format(" {} off |", kSites[i].op);
            continue;
        }
        out += std::format(" {} handled {} | no stage manager, dropped {} |", kSites[i].op, g_seen[i].load(),
                           g_dropped[i].load());
    }
    out.pop_back();
    return out;
}

}  // namespace cdmp::session2
