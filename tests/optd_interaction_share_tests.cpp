// Far shops (tcB239): the pure logic of interaction_share (interaction_share_logic.h).
// SCOPE: the ledger only (who holds an NPC's dynamic interaction, when the game's remove-by-key is skipped).
// They do NOT test the hooks nor the game.

#include <cstdint>
#include <cstdio>

#include "mod/session2/interaction_share_logic.h"

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

constexpr uintptr_t kIc = 0x58ED2000000, kIc2 = 0x58ED2000100;
constexpr uintptr_t kHostMgr = 0x5900A1D8C00, kUser2Mgr = 0x58F61190700;

// tcB239's order: user 2's scene adds, the host's scene adds, user 2's scene closes.
void test_user2_closes_host_holds() {
    ishare::Ledger l;
    l.on_add(kIc, 7, 0x1C5, kUser2Mgr, true);
    l.on_add(kIc, 6, 0x1C5, kUser2Mgr, false);
    l.on_add(kIc, 7, 0x22E, kHostMgr, false);
    l.on_add(kIc, 6, 0x22E, kHostMgr, false);
    uint64_t keep = 0;
    CHECK(l.on_remove(kIc, 7, 0x1C5, kUser2Mgr, keep) && keep == 0x22E);
    keep = 0;
    CHECK(l.on_remove(kIc, 6, 0x1C5, kUser2Mgr, keep) && keep == 0x22E);
    // The host's scene closes last: the game's own remove runs, nothing is left.
    CHECK(!l.on_remove(kIc, 7, 0x22E, kHostMgr, keep));
    CHECK(!l.on_remove(kIc, 6, 0x22E, kHostMgr, keep));
    CHECK(l.size() == 0);
}

// The other way round: the host leaves, user 2 stays.
void test_host_closes_user2_holds() {
    ishare::Ledger l;
    l.on_add(kIc, 7, 0x22E, kHostMgr, true);
    l.on_add(kIc, 7, 0x1C5, kUser2Mgr, false);
    uint64_t keep = 0;
    CHECK(l.on_remove(kIc, 7, 0x22E, kHostMgr, keep) && keep == 0x1C5);
    CHECK(!l.on_remove(kIc, 7, 0x1C5, kUser2Mgr, keep));
}

// One player alone, or two scenes of the same manager: the game's behaviour (never skipped).
void test_same_manager_not_skipped() {
    ishare::Ledger l;
    uint64_t keep = 0;
    l.on_add(kIc, 7, 0x22E, kHostMgr, true);
    CHECK(!l.on_remove(kIc, 7, 0x22E, kHostMgr, keep));
    l.on_add(kIc, 7, 0x22E, kHostMgr, true);
    l.on_add(kIc, 7, 0x22F, kHostMgr, false);
    CHECK(!l.on_remove(kIc, 7, 0x22E, kHostMgr, keep));
    CHECK(l.size() == 0);  // the game removed every entry of the key
    CHECK(!l.on_remove(kIc, 7, 0x22F, kHostMgr, keep));
    // A remove nobody added.
    CHECK(!l.on_remove(kIc2, 7, 0x1C5, kUser2Mgr, keep));
}

// An empty component at an add = a new actor at an old address: what the ledger held is dropped.
void test_stale_component() {
    ishare::Ledger l;
    uint64_t keep = 0;
    l.on_add(kIc, 7, 0x22E, kHostMgr, true);
    l.on_add(kIc, 9, 0x22E, kHostMgr, false);
    l.on_add(kIc2, 7, 0x230, kHostMgr, true);
    l.on_add(kIc, 7, 0x1C5, kUser2Mgr, true);  // empty again: the host's records were stale
    CHECK(!l.on_remove(kIc, 7, 0x1C5, kUser2Mgr, keep));
    CHECK(!l.on_remove(kIc, 9, 0x1C5, kUser2Mgr, keep));
    CHECK(l.size() == 1);  // the other component is untouched
    // The same scene adding twice is one holder.
    l.on_add(kIc, 7, 0x1C5, kUser2Mgr, true);
    l.on_add(kIc, 7, 0x1C5, kUser2Mgr, false);
    l.on_add(kIc, 7, 0x22E, kHostMgr, false);
    CHECK(l.on_remove(kIc, 7, 0x22E, kHostMgr, keep) && keep == 0x1C5);
    CHECK(!l.on_remove(kIc, 7, 0x1C5, kUser2Mgr, keep));
}

}  // namespace

int main() {
    test_user2_closes_host_holds();
    test_host_closes_user2_holds();
    test_same_manager_not_skipped();
    test_stale_component();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_interaction_share: all passed\n");
    return 0;
}
