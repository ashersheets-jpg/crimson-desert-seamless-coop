// Co-op+ (2026-11-08): the pure part of npc_category (src/mod/session2/npc_category_logic.h): the state-16 frame
// byte for byte, its refusals, the fingerprint and the decision rule (own entry kept, the host's category otherwise).

#include <cstdio>
#include <vector>

#include "mod/session2/npc_category_logic.h"

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

void test_frame() {
    const std::vector<ncl::Entry> e{{0xB01005BBu, 1}, {0xB0100400u, 2}};
    const auto p = ncl::make_payload(e);
    const std::vector<uint8_t> want{16, 1, 2, 0, 0xBB, 0x05, 0x10, 0xB0, 1, 0x00, 0x04, 0x10, 0xB0, 2};
    CHECK(p == want);
    const auto back = ncl::parse_payload(p);
    CHECK(back && back->size() == 2 && (*back)[0].key == 0xB01005BBu && (*back)[0].cat == 1 &&
          (*back)[1].key == 0xB0100400u && (*back)[1].cat == 2);
    // empty table
    const auto p0 = ncl::make_payload({});
    CHECK(p0.size() == 4 && ncl::parse_payload(p0) && ncl::parse_payload(p0)->empty());
    // refusals: other state, other version, short, trailing byte, a count that does not match
    auto bad = p;
    bad[0] = 12;
    CHECK(!ncl::parse_payload(bad));
    bad = p;
    bad[1] = 2;
    CHECK(!ncl::parse_payload(bad));
    CHECK(!ncl::parse_payload(std::vector<uint8_t>{16, 1, 1}));
    bad = p;
    bad.push_back(0);
    CHECK(!ncl::parse_payload(bad));
    bad = p;
    bad[2] = 3;
    CHECK(!ncl::parse_payload(bad));
    // the cap
    std::vector<ncl::Entry> big(ncl::kMaxEntries + 10, ncl::Entry{0xB0100001u, 1});
    const auto pb = ncl::make_payload(big);
    CHECK(pb.size() == 4 + 5 * ncl::kMaxEntries && ncl::parse_payload(pb) &&
          ncl::parse_payload(pb)->size() == ncl::kMaxEntries);
}

void test_effective_and_fingerprint() {
    CHECK(ncl::effective(1, 6) == 1);
    CHECK(ncl::effective(1, 0) == 0);
    CHECK(ncl::effective(0, 4) == 4);
    CHECK(ncl::effective(2, 200) == 2);
    const std::vector<ncl::Entry> a{{1, 1}, {2, 2}}, b{{2, 2}, {1, 1}}, c{{1, 1}, {2, 1}};
    CHECK(ncl::fingerprint(a) == ncl::fingerprint(b));
    CHECK(ncl::fingerprint(a) != ncl::fingerprint(c));
    CHECK(ncl::fingerprint({}) != ncl::fingerprint(a));
}

void test_decide() {
    using S = ncl::Source;
    // the game could not tell (2): kept
    auto d = ncl::decide(2, false, 0, true, 1, 0);
    CHECK(d.result == 2 && d.source == S::Native);
    // the guest's own non-zero category: kept, whatever the host has
    d = ncl::decide(1, true, 2, true, 0, 0);
    CHECK(d.result == 1 && d.source == S::Own);
    // own missing, host 1, the condition asks "== 0" -> 1 != 0 -> 1 (the Greet passes this leaf)
    d = ncl::decide(0, false, 0, true, 1, 0);
    CHECK(d.result == 1 && d.source == S::Host);
    // own 0, host 1, asks 4 -> 1
    d = ncl::decide(1, true, 0, true, 1, 4);
    CHECK(d.result == 1 && d.source == S::Host);
    // own 0, host 0 too -> 0 (the host is refused the same: no change)
    d = ncl::decide(0, true, 0, true, 0, 0);
    CHECK(d.result == 0 && d.source == S::Host);
    // the host has the category asked for -> 0
    d = ncl::decide(1, false, 0, true, 4, 4);
    CHECK(d.result == 0 && d.source == S::Host);
    // the host has no entry: as the game says
    d = ncl::decide(0, false, 0, false, 0, 0);
    CHECK(d.result == 0 && d.source == S::Native);
}

}  // namespace

int main() {
    test_frame();
    test_effective_and_fingerprint();
    test_decide();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("npc_category: all tests passed\n");
    return 0;
}
