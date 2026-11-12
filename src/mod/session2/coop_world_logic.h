#pragma once
// Co-op+ 2026-11-12, the friend's character of a co-op world: the pure part (no Windows, no game, no files).
//
// The owner's decision: when the host starts a NEW co-op save the friend (user 2) gets a new save too, so both start
// at the same point; when the two CONTINUE that co-op save the friend comes back as that run's character. Worlds made
// by "Start From My Solo Progress" and worlds older than this keep the behaviour before it (the friend brings its own
// character: its upload is staged).
//
//   marker    <world>\cdmp_coop_world.txt, written "user2_character=pending" by save_hooks.cpp when a world is made
//             with seed "new". No marker = the behaviour before this. Once the host seeded the friend's character it
//             reads "user2_character=world", the guest it was made for and the line's root (the SHA-256 of the
//             save.save the character was seeded from: the mailbox generations of that line are the character).
//   upload    in a marker world the friend's upload is checked and acknowledged but never staged.
//   gate      at user 2's LoginUserToFieldServerReq 2535 (server.cpp) the host stages what user 2 loads, in the
//             order of decide_stage below. guest_save_tunnel.cpp does it (network thread); the files are read and
//             written by guest_save_host.cpp.
//   switch    [session2] coop_world_character=1 (default) | 0 (no marker written or read: the behaviour before this).
//
// Tests: tests/optd_guest_save_b_tests.cpp (test_coop_world).

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "common/sha256.h"
#include "mod/session2/guest_save_logic.h"
#include "mod/session2/guest_save_wire.h"

namespace cdmp::session2::cwl {

// ---------------------------------------------------------------- the marker

inline constexpr wchar_t kMarkerFile[] = L"cdmp_coop_world.txt";
// <world>_guest_prev\<account>\slot204: last session's guest slot, set aside at the host's start (save_hooks.cpp).
inline constexpr wchar_t kGuestPrevSuffix[] = L"_guest_prev";

enum class Character : uint8_t {
    None,     // no marker: the friend brings its own character (the behaviour before this)
    Pending,  // a new co-op save whose friend has no character yet: seeded from the host's own save
    World,    // the friend's character of this world exists: the line `lineage_root`
};
inline const char* character_name(Character c) {
    switch (c) {
        case Character::None: return "none";
        case Character::Pending: return "pending";
        case Character::World: return "world";
    }
    return "?";
}

struct Marker {
    Character character = Character::None;
    std::string guest;         // World: the guest the character was made for (its mailbox folder's id); may be empty
    std::string lineage_root;  // World: 64 lower-case hex digits
    bool operator==(const Marker&) const = default;
};

// The file's text: one title line (no '='), then "key=value" lines.
inline std::string marker_text(const Marker& m) {
    std::string s = "cdmp co-op world: the friend's character of this co-op save (written by the mod; do not edit)\n";
    s += "version=1\n";
    s += std::string("user2_character=") + (m.character == Character::World ? "world" : "pending") + "\n";
    if (m.character == Character::World) {
        std::string guest;
        for (const char c : m.guest) {
            if (c != '\r' && c != '\n') guest += c;
        }
        s += "guest=" + guest + "\n";
        s += "lineage_root=" + m.lineage_root + "\n";
    }
    return s;
}

// nullopt = not a marker this build understands (the caller logs it and behaves as with no marker). "world" needs a
// line root of 64 hex digits that is not all zero; the guest may be empty.
inline std::optional<Marker> parse_marker(std::string_view text) {
    const gsw::KeyValues kv = gsw::parse_key_values(text);
    const auto character = gsw::kv_get(kv, "user2_character");
    if (!character) return std::nullopt;
    Marker m;
    if (*character == "pending") {
        m.character = Character::Pending;
        return m;
    }
    if (*character != "world") return std::nullopt;
    const auto root = gsw::kv_root(kv);
    if (!root || gsw::digest_zero(*root)) return std::nullopt;
    m.character = Character::World;
    m.lineage_root = sha256::hex(*root);
    m.guest = gsw::kv_get(kv, "guest").value_or("");
    return m;
}

// ---------------------------------------------------------------- which slot of the host's

// The host's slot the friend's character is seeded from: the most recent save.save among the slots a "Continue" loads
// (0-2, 100-108: never 3, CrossSave, nor the 200s, the guest's own 204 among them) that have both files and no
// slot<N>_new / slot<N>_backup folder beside them (a save of that slot is being written or was interrupted: skipped,
// never read). Ties: the lower number. nullopt = none yet.
inline std::optional<uint32_t> pick_host_slot(std::span<const gsw::SlotSeen> seen,
                                              uint32_t guest_slot = gsv::kGuestSlot) {
    const gsw::SlotSeen* best = nullptr;
    for (const gsw::SlotSeen& s : seen) {
        if (!gsw::offer_slot_ok(s.slot) || s.slot == guest_slot || !s.has_lobby || !s.has_save || s.has_new ||
            s.has_backup) {
            continue;
        }
        if (!best || s.save_time > best->save_time || (s.save_time == best->save_time && s.slot < best->slot)) best = &s;
    }
    if (!best) return std::nullopt;
    return best->slot;
}

// A slot of the host's is copied only when two reads a short time apart gave the same files (the game may be in the
// middle of writing it).
struct SlotSample {
    uint32_t slot = 0;
    uint64_t lobby_size = 0, save_size = 0;
    sha256::Digest lobby{}, save{};
    bool operator==(const SlotSample&) const = default;
};
inline constexpr uint64_t kStableMs = 1500;  // between the two reads, at least
enum class Stable : uint8_t { First, Changed, TooSoon, Yes };
inline Stable judge_stable(const std::optional<SlotSample>& before, uint64_t before_ms, const SlotSample& now,
                           uint64_t now_ms) {
    if (!before) return Stable::First;
    if (!(*before == now)) return Stable::Changed;
    return now_ms >= before_ms && now_ms - before_ms >= kStableMs ? Stable::Yes : Stable::TooSoon;
}

// ---------------------------------------------------------------- what user 2 loads

inline constexpr uint64_t kGateLookMs = 500;        // the disk is looked at this often while user 2's login waits
// A complete, stable, REAL save of the host's: waited for this long. Five minutes, not two: the first real save of a
// new game came 63 s after the host stood in the world (fs1a), and a fall-back here means the friend's solo character
// in a new co-op save, which the owner ruled out.
inline constexpr uint64_t kHostSlotWaitMs = 300000;
// A new game's first save is a stub of about 5 KB with no quest in it; real saves are hundreds of KB.
inline constexpr size_t kMinSeedSaveBytes = 64 * 1024;
inline constexpr uint64_t kGaveUpMs = 60000;        // after a hard failure the gate stays open this long, unasked

enum class Stage : uint8_t {
    Today,           // the switch is off, own-save mode is not 1, or the world has no marker: nothing to do
    KeepStaged,      // a pair of the world's line is staged already (this login's decision was made)
    KeepLive,        // the world's line is live in the guest slot in this run (a second login): it stays
    SeedFromHost,    // marker pending: a copy of the host's slot, a new line; the marker becomes "world"
    Mailbox,         // marker world: the newest mailbox generation of its line
    Prev,            // marker world, nothing in the mailbox: last session's guest slot, set aside at the start
    ReseedFromHost,  // marker world, neither: a copy of the host's slot again (a warning: the character is new)
    Wait,            // not yet: the host has no complete slot read the same twice
    Fallback,        // waited too long: the gate opens with the behaviour before this (the upload), an error line
};
inline const char* stage_name(Stage s) {
    switch (s) {
        case Stage::Today: return "as before (no co-op world character)";
        case Stage::KeepStaged: return "the staged pair stays";
        case Stage::KeepLive: return "the live guest slot stays";
        case Stage::SeedFromHost: return "seeded from the host's slot";
        case Stage::Mailbox: return "the mailbox generation of the world's line";
        case Stage::Prev: return "last session's guest slot";
        case Stage::ReseedFromHost: return "seeded AGAIN from the host's slot";
        case Stage::Wait: return "wait";
        case Stage::Fallback: return "fall back to the upload";
    }
    return "?";
}

struct StageInputs {
    bool enabled = false;       // coop_world_character=1 and guest_own_save=1 installed
    Character marker = Character::None;
    bool staged_world = false;  // marker world: a pair labelled with its line is staged
    bool live_world = false;    // marker world: the live guest slot is labelled with its line
    bool mailbox = false;       // marker world: the mailbox has a generation of its line
    bool prev = false;          // marker world: <world>_guest_prev\<account>\slot204 has both files
    bool host_stable = false;   // a complete slot of the host's was read the same twice
    bool timed_out = false;     // kHostSlotWaitMs went by without one
};

// The order of the design: pending -> the host's slot; world -> what is already there, the mailbox, last session's
// slot, the host's slot again. With nothing usable the login waits, but never for ever.
inline Stage decide_stage(const StageInputs& in) {
    if (!in.enabled || in.marker == Character::None) return Stage::Today;
    if (in.marker == Character::Pending) {
        if (in.host_stable) return Stage::SeedFromHost;
        return in.timed_out ? Stage::Fallback : Stage::Wait;
    }
    if (in.staged_world) return Stage::KeepStaged;
    if (in.live_world) return Stage::KeepLive;
    if (in.mailbox) return Stage::Mailbox;
    if (in.prev) return Stage::Prev;
    if (in.host_stable) return Stage::ReseedFromHost;
    return in.timed_out ? Stage::Fallback : Stage::Wait;
}

}  // namespace cdmp::session2::cwl
