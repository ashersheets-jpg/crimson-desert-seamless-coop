#pragma once
// Co-op+ (2026-11-09, build 1.0.0.3576), ROADMAP B19 anti-grief: the pure part of anti_grief.cpp (no game access).
// The guest's melee and projectile hits reach the host's server as DoFrameEventReq 3030 (payload: 0x04 ..., a fixed
// part of at least 0x207 bytes, then one 677-byte entry per target, the target's actor key first; the entries are at
// the END, read backwards until a word is no actor key -- the Co-op+ layer's guest_hits.cpp reading, verified on 3576).
// Protected NPCs: every NPC the host's client was given a quest-dialog list for (SetQuestDialogAck 3086, NPC key at
// +5): townsfolk, merchants, quest givers -- the people you talk to; enemies have none.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cdmp::session2::agl {

inline constexpr uint16_t kOpDoFrameEventReq = 3030;
inline constexpr uint16_t kOpQuestDialogAck = 3086;
inline constexpr size_t kFixed = 0x207, kEntry = 677;

inline uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }
// World actors (NPCs, gimmicks) 0xB..., characters 0xA....
inline bool key_shape(uint32_t k) { return (k >> 28) == 0xB || (k >> 28) == 0xA; }

// The targets of a DoFrameEventReq payload (at most 30).
inline std::vector<uint32_t> targets(std::span<const uint8_t> pl) {
    std::vector<uint32_t> out;
    if (pl.empty() || pl[0] != 0x04) return out;
    const size_t n = pl.size();
    for (size_t k = 1; k <= 30 && n >= k * kEntry + kFixed; ++k) {
        const uint32_t key = rd32(pl.data() + n - k * kEntry);
        if (!key_shape(key)) break;
        out.push_back(key);
    }
    return out;
}

// The payload without the entries whose target `is_protected` says yes (the order of the others kept: the server
// wants them strictly ascending, which a removal keeps). The server reads the count from the size
// (decomp/notes_anti_grief.md). Empty when nothing is removed. `removed` counts what went.
template <class Pred>
std::vector<uint8_t> without_targets(std::span<const uint8_t> pl, Pred is_protected, size_t* removed = nullptr) {
    std::vector<uint8_t> out;
    if (removed) *removed = 0;
    const size_t n_targets = targets(pl).size();
    if (!n_targets) return out;
    const size_t start = pl.size() - n_targets * kEntry;  // the first (lowest) entry
    size_t gone = 0;
    for (size_t i = 0; i < n_targets; ++i) gone += is_protected(rd32(pl.data() + start + i * kEntry)) ? 1 : 0;
    if (!gone) return out;
    out.assign(pl.begin(), pl.begin() + static_cast<std::ptrdiff_t>(start));
    for (size_t i = 0; i < n_targets; ++i) {
        const uint8_t* e = pl.data() + start + i * kEntry;
        if (!is_protected(rd32(e))) out.insert(out.end(), e, e + kEntry);
    }
    if (removed) *removed = gone;
    return out;
}

// The NPC of a SetQuestDialogAck payload (u8 4 | u32 actor | u32 NPC | entries), 0 when not one.
inline uint32_t dialog_npc(std::span<const uint8_t> pl) {
    if (pl.size() < 9 || pl[0] != 4) return 0;
    const uint32_t npc = rd32(pl.data() + 5);
    return (npc >> 28) == 0xB ? npc : 0;
}

}  // namespace cdmp::session2::agl
