#pragma once
// Co-op+ (2026-11-26, build 1.0.0.3732), ROADMAP B10 "the host's story places": the pure logic of
// level_state_share.cpp (no game access), so the tests can run it.
//
// ChangeGameLevelStateAck (3732 opcode 2669, the sniffer's runtime RTTI names; 2976's 2344) lists the world's game
// levels and their states. The server writes it with a ToTarget finder (owner_fanout_writers.h), to ONE character:
//   payload: u8 4 | u32 the character's key | u32 1 | u8 1 | n x {u32 game level key (0x0F42xx...), u32 state hash, u8}
//   (captures cs83w3 / cs92 / cs103 / cs85c-cs93: the full list is 3007 bytes = 10 + 333 x 9; a short one 82 = 10 + 8 x 9)
// The joined player gets the whole list once, at its own entry into the field; the host gets it again later in a
// session (its own trips, reloads, story steps). ByJunter's R1 (pendientes/p8-s9-cinematica.md 136-140, 318-319):
// after the owner's reload at the prologue camp's scene "the camp changes level state" for the owner only and the
// guest keeps the old camp. The prologue's Abyss island and the castle's shining gate (testers 25 Nov) are levels of
// the host's story that never load in the joined player's game: the likely same cause (to be proven in a test).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace cdmp::session2::lss {

constexpr uint16_t kOpChangeGameLevelStateAck = 2189;  // 1.0.0.3732
constexpr size_t kHeader = 5;                          // u16 opcode | u8 slot | u16 length (fog_share.cpp)
constexpr size_t kHead = 10;                           // u8 4 | u32 character key | u32 1 | u8 1
constexpr size_t kEntry = 9;                           // u32 game level key | u32 state hash | u8
constexpr size_t kKeyAt = kHeader + 1;                 // the character's key in the wire packet

enum class Mode : uint8_t { Off, Probe, On };

inline Mode parse_mode(const char* raw) {
    if (!raw) return Mode::Off;
    if (std::strcmp(raw, "1") == 0) return Mode::On;
    if (std::strcmp(raw, "probe") == 0) return Mode::Probe;
    return Mode::Off;
}
inline const char* mode_name(Mode m) { return m == Mode::On ? "1" : m == Mode::Probe ? "probe" : "0"; }

inline uint32_t le32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

// The character key a well formed ChangeGameLevelStateAck is for; 0 = another packet, a malformed one, or a key
// that is no player character's (0xA...).
inline uint32_t character_of(std::span<const uint8_t> packet) {
    if (packet.size() < kHeader + kHead) return 0;
    if (static_cast<uint16_t>(packet[0] | (packet[1] << 8)) != kOpChangeGameLevelStateAck) return 0;
    const size_t len = static_cast<size_t>(packet[3] | (packet[4] << 8));
    if (len != packet.size() - kHeader) return 0;
    if ((len - kHead) % kEntry != 0) return 0;
    const uint32_t key = le32(packet.data() + kKeyAt);
    return (key >> 28) == 0xA ? key : 0;
}

inline size_t entries_of(std::span<const uint8_t> packet) {
    return packet.size() < kHeader + kHead ? 0 : (packet.size() - kHeader - kHead) / kEntry;
}

// Entry i (0-based) of a well formed packet: its game level key and state hash.
inline void entry_at(std::span<const uint8_t> packet, size_t i, uint32_t& level, uint32_t& state) {
    const uint8_t* e = packet.data() + kHeader + kHead + i * kEntry;
    level = le32(e);
    state = le32(e + 4);
}

// The copy for another character: the same bytes with the character's key replaced.
inline std::vector<uint8_t> copy_for(std::span<const uint8_t> packet, uint32_t to) {
    std::vector<uint8_t> out(packet.begin(), packet.end());
    if (out.size() >= kKeyAt + 4) std::memcpy(out.data() + kKeyAt, &to, 4);
    return out;
}

// What to do with one of the host client's packets.
enum class Verdict : uint8_t { NotOurs, NotInWorld, NoUser2, Copy };
inline Verdict decide(uint32_t key, bool user2_in_world, uint32_t user2_key) {
    if (!key) return Verdict::NotOurs;
    if (!user2_key || user2_key == key) return Verdict::NoUser2;
    if (!user2_in_world) return Verdict::NotInWorld;
    return Verdict::Copy;
}

}  // namespace cdmp::session2::lss
