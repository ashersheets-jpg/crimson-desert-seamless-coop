#pragma once
// Co-op+ (2026-11-08, build 1.0.0.3576): the guest buys and sells with ITS OWN money, stock and inventory at the
// host's merchants. Pure part (no game): tests/optd_shop_guest_tests.cpp.
//
// What the game checks (3576 decomp, 0x23C7350 = ByJunter's 2976 0x21F2780, nativo.md §1.3): every store operation
// (BuyStockFromStoreReq, SellItemToItemStoreReq and five more on the server; the store window's own checks on the
// client) asks "is the stage the merchant is linked to the CURRENT stage of the field's sequencer manager?":
//   r12 = [mgr+0xA0] if its low 29 bits are set, else [mgr+0x90]; 0 -> eErrNoInvalidStageInfo (0x23C74BB)
//   rbx = [[0x2148340(merchant's sequencer comp)+0x10]+0x30]; [rbp-0x31] (= that current stage) != rbx ->
//         eErrNoInvalidStageInfo (0x23C7787)
// The field's manager is the FIELD OWNER's (the host's): its current stage is the host's open shop or nothing. So a
// guest is refused unless the host has that same shop open. Nothing else in the check is about the host: the store
// component, money, stock left and inventory are the asking player's own, the distance is the guest's own.
//
// The rule: for the guest's character (the guest's client: its own store window; the host's server: user 2's
// requests) the "current stage" is taken to be the merchant's own linked stage when the merchant HAS one (a merchant
// a shop scene placed). A merchant without a linked stage stays refused exactly as the game says. The host is never
// touched. Store info, stock conditions, distance and the wanted level are still the game's.

#include <cstdint>

namespace cdmp::session2::sgl {

inline constexpr uint32_t kStageIdMask = 0x1FFFFFFF;

// Hook A (0x23C74BB, `test r12d, 0x1FFFFFFF`): the manager has no current stage at all. Returns the r12 to use:
// the native one, or a non-zero placeholder for the guest (r12 is overwritten right after the jump; hook B decides).
struct EmptyVerdict {
    uint64_t r12 = 0;
    bool bypassed = false;
};
inline EmptyVerdict on_empty(bool guest_subject, uint64_t r12) {
    if (!guest_subject || (r12 & kStageIdMask) != 0) return {r12, false};
    return {kStageIdMask, true};
}

// Hook B (0x23C777B, after rbx = the merchant's linked stage): the value [rbp-0x31] (the manager's current stage)
// is compared with rbx next. For the guest: the linked stage when there is one; when there is none and hook A
// bypassed the empty check, a value that cannot match (the game's refusal, as if A had not run).
inline uint64_t current_for_compare(bool guest_subject, bool a_bypassed, uint64_t current, uint64_t linked) {
    if (!guest_subject) return current;
    if (linked != 0) return linked;
    return a_bypassed ? ~0ull : current;
}

}  // namespace cdmp::session2::sgl
