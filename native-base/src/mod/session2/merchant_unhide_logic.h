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
#include <cstdint>

#include "mod/session2/funcnpc_trade_policy.h"

namespace cdmp::session2::mul {

inline constexpr uint64_t kHideBit = 0x200;  // the hide reason the shop NPCs keep in co-op
inline constexpr uint32_t kStuckMs = 4000;   // solo: cleared ~0.1-1 s after it is set (2-3 s after the binding)

// The shop scenes, by the dictionary hash of their base name (lookup3, seed 0xC5EDE; the names were matched to the
// hashes seen at the Hernand market, tcB100-tcB134).
inline constexpr std::array<uint32_t, 9> kShopScenes = {
    u2trade::dict_hash("cd_seq_funcnpc_butcher"),  u2trade::dict_hash("cd_seq_funcnpc_smithy"),
    u2trade::dict_hash("cd_seq_funcnpc_grocery"),  u2trade::dict_hash("cd_seq_funcnpc_general"),
    u2trade::dict_hash("cd_seq_funcnpc_bank"),     u2trade::dict_hash("cd_seq_funcnpc_pub"),
    u2trade::dict_hash("cd_seq_funcnpc_clothes"),  u2trade::dict_hash("cd_seq_funcnpc_leather"),
    u2trade::dict_hash("cd_seq_funcnpc_smuggler"),
};
static_assert(u2trade::dict_hash("cd_seq_funcnpc_grocery") == 0x8FA1605Eu, "grocery (tcB100: the stall)");
static_assert(u2trade::dict_hash("cd_seq_funcnpc_bank") == 0xAA015527u, "bank");

inline constexpr bool is_shop_scene(uint32_t hash) noexcept {
    for (uint32_t h : kShopScenes)
        if (h == hash) return true;
    return false;
}

// Unhide now? hide = the NPC's mask +0x3B8; bound_ms = when its shop scene bound it; now_ms.
inline constexpr bool should_unhide(uint64_t hide, uint64_t bound_ms, uint64_t now_ms) noexcept {
    return (hide & kHideBit) != 0 && now_ms >= bound_ms && now_ms - bound_ms >= kStuckMs;
}

}  // namespace cdmp::session2::mul
