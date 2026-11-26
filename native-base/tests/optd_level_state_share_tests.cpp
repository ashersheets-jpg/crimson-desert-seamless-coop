// Co-op+ (2026-11-26): src/mod/session2/level_state_share_logic.h.
#include <cstdio>
#include <vector>

#include "mod/session2/level_state_share_logic.h"

using namespace cdmp::session2;

namespace {
int g_failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

// A wire packet: u16 opcode | u8 slot | u16 length | the payload (u8 4 | u32 key | u32 1 | u8 1 | entries).
std::vector<uint8_t> packet(uint16_t op, uint32_t key, const std::vector<std::pair<uint32_t, uint32_t>>& entries,
                            int len_delta = 0) {
    std::vector<uint8_t> pl{0x04};
    for (int i = 0; i < 4; ++i) pl.push_back(static_cast<uint8_t>(key >> (8 * i)));
    pl.insert(pl.end(), {0x01, 0x00, 0x00, 0x00, 0x01});
    for (const auto& [level, state] : entries) {
        for (int i = 0; i < 4; ++i) pl.push_back(static_cast<uint8_t>(level >> (8 * i)));
        for (int i = 0; i < 4; ++i) pl.push_back(static_cast<uint8_t>(state >> (8 * i)));
        pl.push_back(0x00);
    }
    const size_t len = pl.size() + len_delta;
    std::vector<uint8_t> p{static_cast<uint8_t>(op), static_cast<uint8_t>(op >> 8), 0xFF, static_cast<uint8_t>(len),
                           static_cast<uint8_t>(len >> 8)};
    p.insert(p.end(), pl.begin(), pl.end());
    return p;
}
}  // namespace

int main() {
    // The first bytes of the captures' lists (cs103: 04 01 00 10 A0 01 00 00 00 01 AB 42 0F 00 A8 15 BE 66 00 ...).
    const auto host = packet(lss::kOpChangeGameLevelStateAck, 0xA0100001,
                             {{0x0F42AB, 0x66BE15A8}, {0x0F4284, 0xA19E44B1}, {0x0F42AF, 0x748ACF5A}});
    CHECK(host.size() == 5 + 10 + 3 * 9);
    CHECK(lss::character_of(host) == 0xA0100001);
    CHECK(lss::entries_of(host) == 3);
    uint32_t level = 0, state = 0;
    lss::entry_at(host, 1, level, state);
    CHECK(level == 0x0F4284 && state == 0xA19E44B1);

    // The copy names user 2's character and nothing else changes.
    const auto copy = lss::copy_for(host, 0xA0100066);
    CHECK(copy.size() == host.size());
    CHECK(lss::character_of(copy) == 0xA0100066);
    CHECK(copy[5] == 0x04 && copy[10] == 0x01 && copy[14] == 0x01);
    bool same_tail = true;
    for (size_t i = 10; i < host.size(); ++i) same_tail = same_tail && copy[i] == host[i];
    CHECK(same_tail);
    CHECK(copy[0] == host[0] && copy[1] == host[1] && copy[3] == host[3] && copy[4] == host[4]);

    // Not ours: another opcode, a wrong length, a broken entry size, a key that is no player character's.
    CHECK(lss::character_of(packet(2668, 0xA0100001, {{1, 2}})) == 0);
    CHECK(lss::character_of(packet(lss::kOpChangeGameLevelStateAck, 0xA0100001, {{1, 2}}, 1)) == 0);
    auto odd = host;
    odd.push_back(0);
    odd[3] = static_cast<uint8_t>(odd.size() - 5);
    CHECK(lss::character_of(odd) == 0);
    CHECK(lss::character_of(packet(lss::kOpChangeGameLevelStateAck, 0xB0100001, {{1, 2}})) == 0);
    CHECK(lss::character_of(std::vector<uint8_t>{0x6D, 0x0A, 0xFF, 0x00}) == 0);
    // A list with no entries is still a list (10 bytes of head).
    CHECK(lss::character_of(packet(lss::kOpChangeGameLevelStateAck, 0xA0100001, {})) == 0xA0100001);

    // A full list (333 entries) has a length above 255: both length bytes are read.
    std::vector<std::pair<uint32_t, uint32_t>> many;
    for (uint32_t i = 0; i < 333; ++i) many.push_back({0x0F4200 + i, 0x1F51E0E2});
    const auto full = packet(lss::kOpChangeGameLevelStateAck, 0xA0100001, many);
    CHECK(full.size() == 5 + 3007);
    CHECK(lss::character_of(full) == 0xA0100001 && lss::entries_of(full) == 333);

    // The decisions.
    CHECK(lss::decide(0, true, 0xA0100066) == lss::Verdict::NotOurs);
    CHECK(lss::decide(0xA0100001, true, 0) == lss::Verdict::NoUser2);
    CHECK(lss::decide(0xA0100001, true, 0xA0100001) == lss::Verdict::NoUser2);  // never back to its own character
    CHECK(lss::decide(0xA0100001, false, 0xA0100066) == lss::Verdict::NotInWorld);
    CHECK(lss::decide(0xA0100001, true, 0xA0100066) == lss::Verdict::Copy);

    CHECK(lss::parse_mode("1") == lss::Mode::On && lss::parse_mode("probe") == lss::Mode::Probe);
    CHECK(lss::parse_mode("0") == lss::Mode::Off && lss::parse_mode("") == lss::Mode::Off &&
          lss::parse_mode("yes") == lss::Mode::Off && lss::parse_mode(nullptr) == lss::Mode::Off);

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_level_state_share: all passed\n");
    return 0;
}
