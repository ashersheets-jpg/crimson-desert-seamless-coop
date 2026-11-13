#include "mod/net/slowmo.h"

#include <windows.h>

#include <atomic>
#include <cstring>

#include "mod/log.h"

namespace cdmp::net {
namespace {

// EnableSpecialModeAck: u32 user | u32 mode | f32 duration (FLT_MAX = until disabled) | u32 | u32. Focus is
// mode 101 (0x65): the server switches it on when the player starts Focus and off (DisableSpecialModeAck) at the end;
// the client never asks for it (2026-11-04 recordings).
constexpr uint16_t kOpEnableSpecialModeAck = 2551;
constexpr uint32_t kModeFocus = 101;

std::atomic<bool> g_block{true}, g_always{false}, g_connected{false};
std::atomic<uint64_t> g_dropped{0};

}  // namespace

void slowmo_configure(bool block, bool always) {
    g_block = block;
    g_always = always;
    log::info("Slow motion: {}{}", block ? "Focus's slow motion is skipped in co-op sessions" : "as in the game",
              always ? " | [dev] skipped without a partner too" : "");
}

bool slowmo_filter_s2c(const uint8_t* packet, uint16_t size) {
    // The game's own receive buffer of `size` bytes, read directly like the other filters in the chain. Co-op+ (3549
    // port): this used game::safe_read, a VirtualQuery per packet received; while a load streams thousands of
    // messages that call waited on the loader threads' allocations, and LoadingCompleteAck came ~50 s late.
    if (!packet || size < 14) return false;
    uint16_t op;
    uint32_t mode;
    std::memcpy(&op, packet, 2);
    if (op != kOpEnableSpecialModeAck) return false;
    std::memcpy(&mode, packet + 9, 4);
    if (mode != kModeFocus) return false;
    if (!(g_always.load() || (g_block.load() && g_connected.load()))) {
        static std::atomic<int> said{0};
        if (said.fetch_add(1) < 5) {
            log::info("Slow motion: Focus started; passed on (block {}, connected {}, always {})", g_block.load(),
                      g_connected.load(), g_always.load());
        }
        return false;
    }
    const uint64_t n = g_dropped.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 20) log::info("Slow motion: Focus's special mode not applied here (co-op), #{}", n);
    return true;
}

void slowmo_set_connected(bool connected) { g_connected = connected; }
void slowmo_set_block(bool block) {
    g_block = block;
    log::info("Slow motion in co-op: {}", block ? "skipped (normal speed)" : "as in the game");
}
bool slowmo_blocking() { return g_block.load(); }

}  // namespace cdmp::net
