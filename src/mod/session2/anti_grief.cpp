// Co-op+ (2026-11-09, build 1.0.0.3576): anti_grief.h.
#include "mod/session2/anti_grief.h"

#include <atomic>
#include <format>
#include <mutex>
#include <unordered_set>

#include "mod/log.h"
#include "mod/session2/anti_grief_logic.h"

namespace cdmp::session2 {
namespace {

bool g_on = false;
std::mutex g_mu;
std::unordered_set<uint32_t> g_protected;  // guarded by g_mu
std::atomic<uint64_t> c_dropped{0}, c_hits_seen{0}, c_stripped{0}, c_freed{0};

}  // namespace

void anti_grief_start(bool on, bool guest) {
    g_on = on && !guest;
    log::info("AntiGrief {} (anti_grief={}{}): {}", g_on ? "ON" : "off", on ? 1 : 0, guest ? ", guest" : "",
              g_on ? "the guest's hits on the NPCs the host talks to (townsfolk, merchants, quest givers) never reach "
                     "the host's server"
                   : "the guest can hit any NPC");
}

void anti_grief_host_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept {
    if (!g_on || size < 5 + 9) return;
    // Playtest 2026-11-10 (Borque: "enemies seem to not react to guest attacks"): some hostile NPCs have a dialog list
    // too (quest fights). An NPC the HOST attacks is fair game for both from then on.
    if (!server_to_client) {
        if (static_cast<uint16_t>(packet[0] | (packet[1] << 8)) != agl::kOpDoFrameEventReq) return;
        try {
            const auto t = agl::targets(std::span<const uint8_t>(packet + 5, size - 5));
            std::lock_guard lk(g_mu);
            for (const uint32_t k : t) {
                if (g_protected.erase(k)) {
                    const uint64_t n = c_freed.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (n <= 20) log::info("AntiGrief: NPC 0x{:08X} is no longer protected: the host fights it", k);
                }
            }
        } catch (...) {
        }
        return;
    }
    if (static_cast<uint16_t>(packet[0] | (packet[1] << 8)) != agl::kOpQuestDialogAck) return;
    const uint32_t npc = agl::dialog_npc(std::span<const uint8_t>(packet + 5, size - 5));
    if (!npc) return;
    try {
        std::lock_guard lk(g_mu);
        g_protected.insert(npc);
    } catch (...) {
    }
}

std::vector<uint8_t> anti_grief_strip_user2_req(uint16_t opcode, std::span<const uint8_t> payload) noexcept {
    if (!g_on || opcode != agl::kOpDoFrameEventReq) return {};
    try {
        std::lock_guard lk(g_mu);
        const auto t = agl::targets(payload);
        size_t prot = 0;
        for (const uint32_t k : t) prot += g_protected.count(k) ? 1 : 0;
        if (!prot || prot == t.size()) return {};  // nothing to remove, or all: dropped whole
        size_t gone = 0;
        auto out = agl::without_targets(payload, [](uint32_t k) { return g_protected.count(k) != 0; }, &gone);
        const uint64_t n = c_stripped.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 10 || n % 50 == 0)
            log::info("AntiGrief: the guest's hit kept {} target(s), {} person(s) of the host's world taken out ({} so far)",
                      t.size() - gone, gone, n);
        return out;
    } catch (...) {
    }
    return {};
}

bool anti_grief_drop_user2_req(uint16_t opcode, std::span<const uint8_t> payload) noexcept {
    if (!g_on || opcode != agl::kOpDoFrameEventReq) return false;
    try {
        c_hits_seen.fetch_add(1, std::memory_order_relaxed);
        const auto t = agl::targets(payload);
        std::lock_guard lk(g_mu);
        size_t prot = 0;
        for (const uint32_t k : t) prot += g_protected.count(k) ? 1 : 0;
        if (prot && prot < t.size()) return false;  // anti_grief_strip_user2_req keeps the others
        for (const uint32_t k : t) {
            if (g_protected.count(k)) {
                const uint64_t n = c_dropped.fetch_add(1, std::memory_order_relaxed) + 1;
                if (n <= 10 || n % 50 == 0)
                    log::info("AntiGrief: the guest's hit on NPC 0x{:08X} (a person of the host's world) dropped ({} so "
                              "far)", k, n);
                return true;
            }
        }
    } catch (...) {
    }
    return false;
}

std::string anti_grief_describe() {
    std::lock_guard lk(g_mu);
    return std::format("AntiGrief: {} | protected NPCs {} | the guest's hits seen {} dropped {} trimmed {} | freed by the host's hits {}",
                       g_on ? "on" : "off", g_protected.size(), c_hits_seen.load(), c_dropped.load(), c_stripped.load(),
                       c_freed.load());
}

}  // namespace cdmp::session2
