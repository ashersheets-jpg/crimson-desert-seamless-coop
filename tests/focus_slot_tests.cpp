// Co-op+ 2026-11-24: FocusSlots (session2/focus_slot_logic.h), the per-player focus slot of the host's stage manager.
#include <cstdint>
#include <cstdio>

#include "mod/session2/focus_slot_logic.h"

using namespace cdmp::session2;
using pfs::Owner;
using pfs::Triple;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

// The manager's three fields; reads / writes can be made to fail.
struct FakeMgr {
    Triple live;
    bool fail_read = false, fail_write = false;
    int writes = 0;
    bool read(Triple& t) {
        if (fail_read) return false;
        t = live;
        return true;
    }
    bool write(const Triple& t) {
        if (fail_write) return false;
        live = t;
        ++writes;
        return true;
    }
};

const Triple kHostFocus{0x0, 0x0, 0x1DD};    // the host's stage 0x1DD has the focus
const Triple kUser2Focus{0x0, 0x0, 0x29D};   // the joined player's taming 0x29D

void test_swap_in_and_back() {
    FakeMgr m;
    m.live = kHostFocus;
    pfs::FocusSlots s;
    const uint32_t t = s.enter(m, Owner::User2);
    // User 2's own triple (empty at first) is in the manager; the host's is kept.
    CHECK(m.live == Triple{});
    CHECK(s.current() == Owner::User2 && s.depth() == 1);
    CHECK(s.kept(Owner::Host) == kHostFocus);
    m.live = kUser2Focus;  // its flagged branch took the focus (the game's setter, while user 2's triple is in)
    s.leave(m, t);
    CHECK(m.live == kHostFocus);  // the host's focus is untouched
    CHECK(s.current() == Owner::Host && s.depth() == 0);
    CHECK(s.kept(Owner::User2) == kUser2Focus);
    // The next piece of user 2's work sees its own focus again: the taming's last step is not blocked by the host's.
    const uint32_t t2 = s.enter(m, Owner::User2);
    CHECK(m.live == kUser2Focus);
    s.leave(m, t2);
    CHECK(m.live == kHostFocus);
    CHECK(s.swaps() == 4);
}

void test_host_work_never_swaps() {
    FakeMgr m;
    m.live = kHostFocus;
    pfs::FocusSlots s;
    const uint32_t t = s.enter(m, Owner::Host);
    CHECK(m.writes == 0 && s.depth() == 1);
    m.live = Triple{0, 0, 0x2A0};  // the host's own work moves its focus
    s.leave(m, t);
    CHECK(m.writes == 0 && m.live == (Triple{0, 0, 0x2A0}) && s.swaps() == 0);
}

void test_nested() {
    FakeMgr m;
    m.live = kHostFocus;
    pfs::FocusSlots s;
    const uint32_t a = s.enter(m, Owner::User2);  // user 2's request
    m.live = kUser2Focus;
    const uint32_t b = s.enter(m, Owner::Host);  // inside it, a host stage's end
    CHECK(m.live == kHostFocus);
    m.live = Triple{0x1DD, 0, 0};  // the host's focus ends (undo: +0xA0 = 0, +0x90 = +0x98 ...)
    const uint32_t c = s.enter(m, Owner::Host);  // same owner: no swap
    s.leave(m, c);
    s.leave(m, b);
    CHECK(m.live == kUser2Focus);  // back in user 2's request
    s.leave(m, a);
    CHECK(m.live == (Triple{0x1DD, 0, 0}));
    CHECK(s.kept(Owner::User2) == kUser2Focus && s.depth() == 0 && s.unbalanced() == 0);
}

void test_failures_change_nothing() {
    FakeMgr m;
    m.live = kHostFocus;
    pfs::FocusSlots s;
    m.fail_write = true;
    const uint32_t t = s.enter(m, Owner::User2);
    CHECK(s.failed() == 1 && s.current() == Owner::Host && m.live == kHostFocus);
    s.leave(m, t);  // nothing to swap back
    CHECK(s.depth() == 0 && m.live == kHostFocus);
    m.fail_write = false;
    m.fail_read = true;
    const uint32_t t2 = s.enter(m, Owner::User2);
    CHECK(s.failed() == 2 && s.current() == Owner::Host);
    s.leave(m, t2);
    CHECK(s.swaps() == 0);
}

void test_overflow_and_unbalanced() {
    FakeMgr m;
    pfs::FocusSlots s;
    uint32_t tokens[pfs::FocusSlots::kMaxDepth + 1];
    for (uint32_t i = 0; i <= pfs::FocusSlots::kMaxDepth; ++i) tokens[i] = s.enter(m, i % 2 ? Owner::User2 : Owner::Host);
    CHECK(tokens[pfs::FocusSlots::kMaxDepth] == pfs::FocusSlots::kRefused && s.overflow() == 1);
    s.leave(m, tokens[pfs::FocusSlots::kMaxDepth]);  // refused: nothing
    CHECK(s.depth() == pfs::FocusSlots::kMaxDepth);
    s.leave(m, tokens[0]);  // the outermost: unwinds all (the inner leaves were missed)
    CHECK(s.depth() == 0 && s.current() == Owner::Host && s.unbalanced() == 1);
    s.leave(m, tokens[0]);  // again: nothing open
    CHECK(s.unbalanced() == 2);
}

void test_failed_swap_back_repaired() {
    FakeMgr m;
    m.live = kHostFocus;
    pfs::FocusSlots s;
    const uint32_t t = s.enter(m, Owner::User2);
    m.live = kUser2Focus;
    m.fail_write = true;  // the swap back fails: user 2's triple stays in the manager at depth 0
    s.leave(m, t);
    CHECK(s.depth() == 0 && s.current() == Owner::User2 && s.failed() == 1 && m.live == kUser2Focus);
    m.fail_write = false;
    const uint32_t t2 = s.enter(m, Owner::Host);  // the next work first gives the manager the host's triple again
    CHECK(s.repairs() == 1 && s.current() == Owner::Host && m.live == kHostFocus);
    CHECK(s.kept(Owner::User2) == kUser2Focus);
    s.leave(m, t2);
    const uint32_t t3 = s.enter(m, Owner::Host);  // nothing to repair now
    CHECK(s.repairs() == 1);
    s.leave(m, t3);
}

void test_reset_user2() {
    FakeMgr m;
    m.live = kHostFocus;
    pfs::FocusSlots s;
    uint32_t t = s.enter(m, Owner::User2);
    m.live = kUser2Focus;
    s.leave(m, t);
    CHECK(s.kept(Owner::User2) == kUser2Focus);
    s.reset_user2();  // outside any work: at once
    CHECK(s.kept(Owner::User2) == Triple{});
    t = s.enter(m, Owner::User2);
    m.live = kUser2Focus;
    s.reset_user2();  // while its triple is in: when it is swapped out
    s.leave(m, t);
    CHECK(m.live == kHostFocus && s.kept(Owner::User2) == Triple{});
}

// The outermost point closes what missed leaves left open (review 2026-11-24 #1).
void test_unwind_missed_leaves() {
    FakeMgr m;
    m.live = kHostFocus;
    pfs::FocusSlots s;
    CHECK(s.unwind(m) == 0 && s.missed() == 0 && s.swaps() == 0);  // nothing open: nothing done
    (void)s.enter(m, Owner::User2);  // user 2's request
    m.live = kUser2Focus;
    (void)s.enter(m, Owner::Host);  // a host stage inside it; neither leave comes (an exception)
    CHECK(s.depth() == 2 && m.live == kHostFocus);
    m.live = Triple{0, 0, 0x2A0};  // the host's stage moved the host's focus
    CHECK(s.unwind(m) == 2);
    CHECK(s.depth() == 0 && s.current() == Owner::Host && s.missed() == 1 && s.unbalanced() == 1);
    CHECK(m.live == (Triple{0, 0, 0x2A0}));        // the host's triple, as the host's stage left it
    CHECK(s.kept(Owner::User2) == kUser2Focus);  // user 2's kept for its next request
    const uint32_t t = s.enter(m, Owner::User2);
    CHECK(m.live == kUser2Focus);
    s.leave(m, t);
    // user 2's triple in at depth 0 after a failed swap back: unwind gives the host's back
    (void)s.enter(m, Owner::User2);
    m.fail_write = true;
    s.leave(m, 0);
    CHECK(s.depth() == 0 && s.current() == Owner::User2);
    m.fail_write = false;
    CHECK(s.unwind(m) == 0 && s.current() == Owner::Host && m.live == (Triple{0, 0, 0x2A0}));
}

}  // namespace

int main() {
    test_swap_in_and_back();
    test_host_work_never_swaps();
    test_nested();
    test_failures_change_nothing();
    test_overflow_and_unbalanced();
    test_failed_swap_back_repaired();
    test_reset_user2();
    test_unwind_missed_leaves();
    if (g_failures) {
        std::printf("%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("focus_slot: all checks passed\n");
    return 0;
}
