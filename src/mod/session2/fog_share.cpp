// Co-op+ (2026-11-09, build 1.0.0.3576): fog_share.h.
#include "mod/session2/fog_share.h"

#include <atomic>
#include <format>
#include <mutex>

#include "mod/log.h"
#include "mod/session2/fog_share_logic.h"

namespace cdmp::session2 {
namespace {

constexpr size_t kHeader = 5;  // u16 opcode | u8 slot | u16 length
bool g_on = false;
std::mutex g_mu;
fsl::Chunks g_host;     // guarded by g_mu
size_t g_guest_index = 0;  // guarded by g_mu: the next guest chunk's index
std::atomic<uint64_t> c_host_chunks{0}, c_guest_chunks{0}, c_merged_bytes{0}, c_unmatched{0};

bool fog_payload(std::span<const uint8_t> packet, std::span<const uint8_t>& payload) {
    if (packet.size() <= kHeader + fsl::kFogHead) return false;
    if (static_cast<uint16_t>(packet[0] | (packet[1] << 8)) != fsl::kOpFogDataAck) return false;
    const size_t len = static_cast<size_t>(packet[3] | (packet[4] << 8));
    if (len != packet.size() - kHeader) return false;
    payload = packet.subspan(kHeader);
    return true;
}

}  // namespace

void fog_share_start(bool on, bool guest) {
    g_on = on && !guest;
    log::info("FogShare {} (fog_share={}{}): {}", g_on ? "ON" : "off", on ? 1 : 0, guest ? ", guest" : "",
              g_on ? "the guest's map fog at its login is OR-ed with the host's (the union of both explored maps)"
                   : "each player sees its own map");
}

void fog_share_host_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept {
    if (!g_on || !server_to_client) return;
    try {
        std::span<const uint8_t> pl;
        if (!fog_payload(std::span<const uint8_t>(packet, size), pl)) return;
        std::lock_guard lk(g_mu);
        if (fsl::record(g_host, pl)) c_host_chunks.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {
    }
}

void fog_share_on_user2_out(std::span<uint8_t> packet) noexcept {
    if (!g_on) return;
    try {
        std::span<const uint8_t> cpl;
        if (!fog_payload(packet, cpl)) return;
        std::span<uint8_t> pl = packet.subspan(kHeader);
        std::lock_guard lk(g_mu);
        if (pl[0] == 1) g_guest_index = 0;
        const size_t changed = fsl::merge(pl, g_host, g_guest_index);
        if (!changed && (g_guest_index >= g_host.data.size() || g_host.data[g_guest_index].size() != pl.size() - fsl::kFogHead))
            c_unmatched.fetch_add(1, std::memory_order_relaxed);
        ++g_guest_index;
        c_guest_chunks.fetch_add(1, std::memory_order_relaxed);
        c_merged_bytes.fetch_add(changed, std::memory_order_relaxed);
        if (g_guest_index == g_host.data.size() || pl[0] == 1)
            log::info("FogShare: the guest's map chunk {} sent with the host's explored cells added ({} bytes changed so "
                      "far, host chunks {}, unmatched {})",
                      g_guest_index, c_merged_bytes.load(), g_host.data.size(), c_unmatched.load());
    } catch (...) {
    }
}

std::string fog_share_describe() {
    return std::format("FogShare: {} | host chunks {} | guest chunks {} (bytes changed {}, unmatched {})",
                       g_on ? "on" : "off", c_host_chunks.load(), c_guest_chunks.load(), c_merged_bytes.load(),
                       c_unmatched.load());
}

}  // namespace cdmp::session2
