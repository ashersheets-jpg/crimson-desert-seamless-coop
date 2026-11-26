// Co-op+ (2026-11-26, build 1.0.0.3732): level_state_share.h.
#include "mod/session2/level_state_share.h"

#include <atomic>
#include <format>
#include <span>
#include <vector>

#include "mod/log.h"
#include "mod/session2/level_state_share_logic.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

std::atomic<lss::Mode> g_mode{lss::Mode::Off};
std::atomic<uint64_t> c_seen{0}, c_copied{0}, c_refused{0}, c_not_in_world{0}, c_no_user2{0}, c_entries{0};
std::atomic<uint32_t> g_lines{0};
constexpr uint32_t kLines = 40;

// The first entries of a packet, for the log (level key -> state hash).
std::string first_entries(std::span<const uint8_t> pkt, size_t n) {
    std::string s;
    const size_t total = lss::entries_of(pkt);
    for (size_t i = 0; i < total && i < n; ++i) {
        uint32_t level = 0, state = 0;
        lss::entry_at(pkt, i, level, state);
        s += std::format(" 0x{:X}=0x{:08X}", level, state);
    }
    if (total > n) s += " ...";
    return s;
}

}  // namespace

void level_state_share_start(const std::string& mode, bool guest) {
    const lss::Mode m = guest ? lss::Mode::Off : lss::parse_mode(mode.c_str());
    g_mode.store(m, std::memory_order_relaxed);
    log::info("LevelStateShare {} (level_state_share={}{}): {}", m == lss::Mode::Off ? "off" : "ON",
              mode.empty() ? std::string("0") : mode, guest ? ", guest: host only" : "",
              m == lss::Mode::On    ? "the host's ChangeGameLevelStateAck goes to user 2 too (its own character key), "
                                      "while user 2 is in the world"
              : m == lss::Mode::Probe ? "PROBE: the copies are decided and logged, none is sent"
                                      : "user 2 gets the game level states only at its own entry");
}

void level_state_share_host_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept {
    const lss::Mode m = g_mode.load(std::memory_order_relaxed);
    if (m == lss::Mode::Off || !server_to_client) return;
    try {
        const std::span<const uint8_t> pkt(packet, size);
        const uint32_t key = lss::character_of(pkt);
        if (!key) return;
        c_seen.fetch_add(1, std::memory_order_relaxed);
        const uint32_t u2 = server_user2_player_key();
        const lss::Verdict v = lss::decide(key, server_user2_in_world(), u2);
        const size_t n = lss::entries_of(pkt);
        bool sent = false;
        if (v == lss::Verdict::Copy) {
            if (m == lss::Mode::On) {
                const std::vector<uint8_t> copy = lss::copy_for(pkt, u2);
                sent = server_push_extra(copy);
                (sent ? c_copied : c_refused).fetch_add(1, std::memory_order_relaxed);
                if (sent) c_entries.fetch_add(n, std::memory_order_relaxed);
            }
        } else if (v == lss::Verdict::NotInWorld) {
            c_not_in_world.fetch_add(1, std::memory_order_relaxed);
        } else {
            c_no_user2.fetch_add(1, std::memory_order_relaxed);
        }
        if (g_lines.fetch_add(1, std::memory_order_relaxed) < kLines) {
            const char* what = v == lss::Verdict::Copy
                                   ? (m == lss::Mode::Probe ? "WOULD COPY (probe)"
                                      : sent             ? "COPIED to user 2"
                                                         : "NOT copied: the queue refused it")
                               : v == lss::Verdict::NotInWorld ? "not copied: user 2 is not in the world (loading)"
                                                               : "not copied: no user 2";
            log::info("LevelStateShare [2669] the host's ChangeGameLevelStateAck for 0x{:08X}: {} entries ({} bytes) "
                      "-> {}{} |{}",
                      key, n, size, what, v == lss::Verdict::Copy ? std::format(" as 0x{:08X}", u2) : std::string(),
                      first_entries(pkt, 4));
        }
    } catch (...) {
    }
}

std::string level_state_share_describe() {
    const lss::Mode m = g_mode.load(std::memory_order_relaxed);
    if (m == lss::Mode::Off) return {};
    return std::format("LevelStateShare: {} | the host's 2669 seen {} | copied to user 2 {} ({} entries), refused by "
                       "the queue {} | user 2 not in the world {} | no user 2 {}",
                       lss::mode_name(m), c_seen.load(), c_copied.load(), c_entries.load(), c_refused.load(),
                       c_not_in_world.load(), c_no_user2.load());
}

}  // namespace cdmp::session2
