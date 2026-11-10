// Co-op+ (2026-11-10): lootr.h.
#include "mod/session2/lootr.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include <safetyhook.hpp>

#include "mod/game/safe_memory.h"
#include "mod/log.h"

namespace cdmp::session2 {
namespace {

safetyhook::InlineHook g_pickup;
std::atomic<uint64_t> g_watch_until{0};
std::atomic<int> g_logged{0};

struct Seen {
    uint32_t picker = 0, gimmick = 0;
    uint16_t idx = 0xFFFF;
    int64_t count = 0;
    uint8_t key16[16] = {};
};

void read_pickup(Seen& s, uintptr_t comp, uintptr_t picker) noexcept {
    __try {
        if (picker) s.picker = *reinterpret_cast<uint32_t*>(picker + 0x60);
        const uintptr_t actor = *reinterpret_cast<uintptr_t*>(comp + 8);
        if (actor) s.gimmick = *reinterpret_cast<uint32_t*>(actor + 0x60);
        const uintptr_t rec = *reinterpret_cast<uintptr_t*>(comp + 0xC0);
        if (rec) {
            s.idx = *reinterpret_cast<uint16_t*>(rec + 8);
            s.count = *reinterpret_cast<int64_t*>(rec + 0x10);
        }
        std::memcpy(s.key16, reinterpret_cast<void*>(comp + 0x30), 16);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

int* __fastcall pickup_detour(uintptr_t comp, int* err, uintptr_t picker, uint64_t kind) noexcept {
    Seen s;
    read_pickup(s, comp, picker);
    int* r = g_pickup.call<int*>(comp, err, picker, kind);
    int e = -1;
    if (err) game::fast_read(reinterpret_cast<uintptr_t>(err), &e, 4);
    if (g_logged.fetch_add(1) < 40) {
        log::info("Lootr probe: pickup by 0x{:08X} kind {} gimmick 0x{:08X} item idx 0x{:X} x{} key {:02X}{:02X}{:02X}{:02X}"
                  "{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X} -> err {}",
                  s.picker, kind & 0xFF, s.gimmick, s.idx, s.count, s.key16[0], s.key16[1], s.key16[2], s.key16[3],
                  s.key16[4], s.key16[5], s.key16[6], s.key16[7], s.key16[8], s.key16[9], s.key16[10], s.key16[11],
                  s.key16[12], s.key16[13], s.key16[14], s.key16[15], e);
    }
    g_watch_until = GetTickCount64() + 3000;
    return r;
}

}  // namespace

void lootr_start(bool probe, bool guest) {
    if (!probe || guest) return;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint8_t head[2]{};
    if (!game::safe_read(base + 0x2D37B10, head, sizeof head)) return;
    if (auto h = safetyhook::InlineHook::create(reinterpret_cast<void*>(base + 0x2D37B10), pickup_detour)) {
        g_pickup = std::move(*h);
    }
    log::info("Lootr probe: the pickup worker {}", g_pickup ? "hooked (log only)" : "NOT hooked");
}

void lootr_host_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept {
    if (!g_pickup || size < 5) return;
    const uint16_t op = static_cast<uint16_t>(packet[0] | (packet[1] << 8));
    // Always: the interaction / gimmick / item opcodes (notes_lootr.md); after a pickup: everything for 3 s.
    static const uint16_t kWatch[] = {3008, 3073, 2120, 2794, 2165, 2155, 2787, 2304, 2694, 2760, 3067, 3066, 3110,
                                      2414, 2491, 2605, 2066};
    bool watched = false;
    for (const uint16_t w : kWatch) watched = watched || w == op;
    const bool after = GetTickCount64() <= g_watch_until.load();
    if (!watched && !after) return;
    static std::atomic<int> n{0};
    if (n.fetch_add(1) > 600) return;
    if (op == 2002 || op == 1001) return;
    char hex[3 * 40 + 1] = {};
    const size_t m = size < 40 ? size : 40;
    for (size_t i = 0; i < m; ++i) std::snprintf(hex + i * 3, 4, "%02X ", packet[i]);
    log::info("Lootr probe: {} {} ({} bytes): {}", server_to_client ? "S>C" : "C>S", op, size, hex);
}

}  // namespace cdmp::session2
