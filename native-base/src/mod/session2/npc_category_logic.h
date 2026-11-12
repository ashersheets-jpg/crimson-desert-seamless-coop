#pragma once
// Co-op+ (2026-11-08, build 1.0.0.3576): the NPCs' quest-dialog categories, shared from the host. Pure part (no game,
// no sockets): tests/optd_npc_category_tests.cpp runs all of it.
//
// What it is (PROGRESS.md 2026-11-08, tcB75-tcB78): ConditionData_CheckQuestDialogCategory (vtable 0x5CFB9F0, its
// evaluate 0x24752E0) reads a per-player table (the player's comps [actor+0x68]+0xF8, looked up by 0x1C358E0 with the
// NPC's key) and compares the NPC's category byte (+0x19 when < 6, else +0x18) with the one the condition names
// (obj+0x18). The NPCs' own stage / AI functions set that byte (SetQuestDialogAICategory, protected code); they run
// only where the NPC is controlled -- the host's client -- and only for its local player. So the guest's table, on
// its client and on the host's server, holds 0 or nothing for nearly every NPC (the host's: 1 or 2), and the Greet's
// condition ("refused when the category is 0") refuses the guest everywhere, whatever its own story progress.
//
// The rule: the category is the NPC's state, not the player's progress. The host sends its own table (key ->
// category) to the guest (tunnel control state 16, ToGuestClient); for the guest's character (on the guest's client
// and on the host's server) an NPC whose own entry is missing or 0 is judged with the host's category. A non-zero own
// entry is kept (the game set it for the guest itself). Quests, knowledge, friendship: untouched (the guest's own).
//
// Frame payload (after the Troc control opcode 1, slot -1), LE:
//   u8 16 | u8 version 1 | u16 n | n x {u32 NPC key | u8 category}        (4 + 5n bytes, n <= kMaxEntries)

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cdmp::session2::ncl {

inline constexpr uint8_t kState = 16;
inline constexpr uint8_t kVersion = 1;
inline constexpr size_t kMaxEntries = 4096;  // 20 KB at most, inside one Troc packet (0x8000 - 0x20)
inline constexpr uint8_t kNoOverride = 6;    // 0x1C358E0: +0x19 below 6 wins over +0x18

struct Entry {
    uint32_t key = 0;
    uint8_t cat = 0;
};

// The category 0x1C358E0 returns for an entry.
inline uint8_t effective(uint8_t c18, uint8_t c19) { return c19 < kNoOverride ? c19 : c18; }

inline std::vector<uint8_t> make_payload(std::span<const Entry> entries) {
    const size_t n = entries.size() < kMaxEntries ? entries.size() : kMaxEntries;
    std::vector<uint8_t> p;
    p.reserve(4 + 5 * n);
    p.push_back(kState);
    p.push_back(kVersion);
    p.push_back(static_cast<uint8_t>(n & 0xFF));
    p.push_back(static_cast<uint8_t>(n >> 8));
    for (size_t i = 0; i < n; ++i) {
        for (int b = 0; b < 4; ++b) p.push_back(static_cast<uint8_t>(entries[i].key >> (8 * b)));
        p.push_back(entries[i].cat);
    }
    return p;
}

// Null unless the payload is exactly one state-16 version-1 frame.
inline std::optional<std::vector<Entry>> parse_payload(std::span<const uint8_t> p) {
    if (p.size() < 4 || p[0] != kState || p[1] != kVersion) return std::nullopt;
    const size_t n = static_cast<size_t>(p[2]) | (static_cast<size_t>(p[3]) << 8);
    if (n > kMaxEntries || p.size() != 4 + 5 * n) return std::nullopt;
    std::vector<Entry> out(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* q = p.data() + 4 + 5 * i;
        out[i].key = static_cast<uint32_t>(q[0]) | (static_cast<uint32_t>(q[1]) << 8) |
                     (static_cast<uint32_t>(q[2]) << 16) | (static_cast<uint32_t>(q[3]) << 24);
        out[i].cat = q[4];
    }
    return out;
}

// Order-independent fingerprint of a table (the host resends only on a change, or every few seconds).
inline uint64_t fingerprint(std::span<const Entry> entries) {
    uint64_t sum = 0, x = 0;
    for (const Entry& e : entries) {
        uint64_t h = (static_cast<uint64_t>(e.key) << 8 | e.cat) * 0x9E3779B97F4A7C15ull;
        h ^= h >> 29;
        sum += h;
        x ^= h * 0xBF58476D1CE4E5B9ull;
    }
    return sum ^ (x << 1) ^ entries.size();
}

// The guest's character asks: what does CheckQuestDialogCategory return? native: the game's own answer (0/1/2).
// own_present / own_cat: the guest's own entry for that NPC. host_present / host_cat: the host's.
// want: the category the condition compares with (obj+0x18). The game returns (category != want), 2 when it cannot
// tell; an absent entry reads as category 0.
enum class Source : uint8_t { Native, Own, Host };
struct Decision {
    uint8_t result = 0;
    Source source = Source::Native;
};
inline Decision decide(uint8_t native, bool own_present, uint8_t own_cat, bool host_present, uint8_t host_cat,
                       uint8_t want) {
    if (native > 1) return {native, Source::Native};          // no subject / no table: as the game says
    if (own_present && own_cat != 0) return {native, Source::Own};  // the game set it for the guest itself
    if (!host_present) return {native, Source::Native};
    return {static_cast<uint8_t>(host_cat != want ? 1 : 0), Source::Host};
}

}  // namespace cdmp::session2::ncl
