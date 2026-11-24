#pragma once
// Co-op+ (2026-11-09, build 1.0.0.3576): merchants are hidden in co-op. Pure part (no game):
// tests/optd_merchant_unhide_tests.cpp.
//
// What happens (PROGRESS.md 2026-11-09, tcB117-tcB134, HideWatch): a shop scene (cd_seq_funcnpc_*) binds its NPC
// (2380 PrepareSequencerAck); ~2-3 s later the game hides the NPC (hide reason bit 9 = 0x200 of
// [[npc+0x68]+0x20]+0x3B8, set through 0x18CD090) and the NPC's own AI clears it again a moment later
// (0x221B680 -> 0x18CD090(info, 0x200, clear, fade, mask 0)). With a second player in the world the NPC's AI stops
// after its first action and the clear never comes: the merchant stays invisible (eErrNoInteractionTargetIsHidden)
// for both players, even after the guest leaves.
//
// The rule: an NPC bound to a SHOP scene (the list below, by the scene resource's dictionary hash) whose bit 9 is
// still set kStuckMs after its binding is unhidden with the game's own call, as its AI would. Solo the AI clears it
// within about a second, so nothing is ever touched there.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "mod/session2/funcnpc_trade_policy.h"

namespace cdmp::session2::mul {

inline constexpr uint64_t kHideBit = 0x200;  // the hide reason the shop NPCs keep in co-op
inline constexpr uint32_t kStuckMs = 4000;   // solo: cleared ~0.1-1 s after it is set (2-3 s after the binding)

// The shop scenes, by the dictionary hash of their base name (lookup3, seed 0xC5EDE; the names were matched to the
// hashes seen at the Hernand market, tcB100-tcB134).
// 2026-11-18 (Borque on 9e85b891: the dye vendor and the goblin trader outside Hernand never showed for the joined
// player): the dye vendor and the goblin broker, both prepared by the host in his session (cd_seq_funcnpc_dye
// 0x5FCC0360, cd_seq_funcnpc_goblinbroker 0x25C5EC9B), are shops too (unhide and the shop twin).
inline constexpr std::array<uint32_t, 11> kShopScenes = {
    u2trade::dict_hash("cd_seq_funcnpc_butcher"),  u2trade::dict_hash("cd_seq_funcnpc_smithy"),
    u2trade::dict_hash("cd_seq_funcnpc_grocery"),  u2trade::dict_hash("cd_seq_funcnpc_general"),
    u2trade::dict_hash("cd_seq_funcnpc_bank"),     u2trade::dict_hash("cd_seq_funcnpc_pub"),
    u2trade::dict_hash("cd_seq_funcnpc_clothes"),  u2trade::dict_hash("cd_seq_funcnpc_leather"),
    u2trade::dict_hash("cd_seq_funcnpc_smuggler"), u2trade::dict_hash("cd_seq_funcnpc_dye"),
    u2trade::dict_hash("cd_seq_funcnpc_goblinbroker"),
};
static_assert(u2trade::dict_hash("cd_seq_funcnpc_dye") == 0x5FCC0360u, "dye (Borque 1118b, L2000)");
static_assert(u2trade::dict_hash("cd_seq_funcnpc_goblinbroker") == 0x25C5EC9Bu, "goblin broker (Borque 1118b)");

// The other function NPCs' scenes the host prepares (seen in our hosts' logs, 15-18 Nov): no shop twin for them (their
// menus are not shops), only the unhide, which acts on an NPC whose hide bit 9 is still set kStuckMs after its binding
// (solo the AI clears it within about a second, so nothing changes there).
inline constexpr std::array<uint32_t, 7> kUnhideOnlyScenes = {
    u2trade::dict_hash("cd_seq_funcnpc_craftsmith"),   u2trade::dict_hash("cd_seq_funcnpc_seatdealer"),
    u2trade::dict_hash("cd_seq_funcnpc_wantedfence"),  u2trade::dict_hash("cd_seq_funcnpc_charlatanstore_demeniss_west"),
    u2trade::dict_hash("cd_seq_funcnpc_bond"),         u2trade::dict_hash("cd_seq_funcnpc_church"),
    u2trade::dict_hash("cd_seq_funcnpc_contribution"),
};
static_assert(u2trade::dict_hash("cd_seq_funcnpc_grocery") == 0x8FA1605Eu, "grocery (tcB100: the stall)");
static_assert(u2trade::dict_hash("cd_seq_funcnpc_bank") == 0xAA015527u, "bank");

inline constexpr bool is_shop_scene(uint32_t hash) noexcept {
    for (uint32_t h : kShopScenes)
        if (h == hash) return true;
    return false;
}

// The merchant unhide's scenes: the shops and the other function NPCs.
inline constexpr bool is_unhide_scene(uint32_t hash) noexcept {
    if (is_shop_scene(hash)) return true;
    for (uint32_t h : kUnhideOnlyScenes)
        if (h == hash) return true;
    return false;
}

// Unhide now? hide = the NPC's mask +0x3B8; bound_ms = when its shop scene bound it; now_ms.
inline constexpr bool should_unhide(uint64_t hide, uint64_t bound_ms, uint64_t now_ms) noexcept {
    return (hide & kHideBit) != 0 && now_ms >= bound_ms && now_ms - bound_ms >= kStuckMs;
}

// 2026-11-23 (pp49-fresh: a host scene 0x2BC74C06 binds three critters; on the host their bit 9 is clear within
// 4 s, on the guest they keep it for good; Brycecco's invisible boss is most likely the same): the HIDE MIRROR. The
// host sends the keys of the NPCs of unlisted scenes that are VISIBLE on its game (bit 9 clear) as tunnel control
// state 18; the guest clears bit 9 (the game's setter, as for the shops) on such an NPC that still has it kStuckMs
// after its binding and is in the host's recent list. Nothing hidden on the host is ever shown on the guest.
// Payload: u8 18 | u8 n | n x u32 key (little endian), n <= kVisibleMax.
inline constexpr uint8_t kVisibleState = 18;
inline constexpr size_t kVisibleMax = 96;
inline std::vector<uint8_t> make_visible_payload(const std::vector<uint32_t>& keys) {
    const size_t n = keys.size() < kVisibleMax ? keys.size() : kVisibleMax;
    std::vector<uint8_t> p;
    p.reserve(2 + 4 * n);
    p.push_back(kVisibleState);
    p.push_back(static_cast<uint8_t>(n));
    for (size_t i = 0; i < n; ++i)
        for (int b = 0; b < 4; ++b) p.push_back(static_cast<uint8_t>(keys[i] >> (8 * b)));
    return p;
}
inline std::optional<std::vector<uint32_t>> parse_visible_payload(std::span<const uint8_t> p) {
    if (p.size() < 2 || p[0] != kVisibleState) return std::nullopt;
    const size_t n = p[1];
    if (n > kVisibleMax || p.size() != 2 + 4 * n) return std::nullopt;
    std::vector<uint32_t> keys(n);
    for (size_t i = 0; i < n; ++i)
        keys[i] = static_cast<uint32_t>(p[2 + 4 * i]) | static_cast<uint32_t>(p[3 + 4 * i]) << 8 |
                  static_cast<uint32_t>(p[4 + 4 * i]) << 16 | static_cast<uint32_t>(p[5 + 4 * i]) << 24;
    return keys;
}

}  // namespace cdmp::session2::mul
