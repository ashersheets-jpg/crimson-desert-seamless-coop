// Co-op+ (2026-11-09): src/mod/session2/shop_twin_logic.h, with the payloads of the 3576 captures (tcB112-tcB158).
#include <cstdio>
#include <vector>

#include "mod/session2/shop_twin_logic.h"

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

bool shop(uint32_t h) { return h == 0x978345CAu; }

std::vector<uint8_t> hexs(const char* s) {
    std::vector<uint8_t> v;
    unsigned x;
    while (std::sscanf(s, "%2x", &x) == 1) {
        v.push_back(static_cast<uint8_t>(x));
        s += 2;
        while (*s == ' ') ++s;
    }
    return v;
}

// The butcher's 2380 seen on the guest (tcB158): 92-byte prefix + 2 bindings.
const char* kPrep =
    "04010010a0010010a092010000000000"
    "0001005e470f0000000000ca4583975e"
    "470f00d4d426c68fe21844e90674c587"
    "5e2cc000000000000000000000000000"
    "0000000000ffffff7f01000000000000"
    "000000ffffffffffffffff0d02000020"
    "010010a005000004000020e80210b005"
    "0000";
}  // namespace

int main() {
    const auto pl = hexs(kPrep);
    CHECK(pl.size() == 114);
    const uint32_t guest = 0xA0100204u;
    const auto t = stl::twin_prepare(pl, guest, shop);
    CHECK(t.has_value());
    if (t) {
        CHECK(stl::rd32(t->data() + 1) == guest);
        CHECK(stl::rd32(t->data() + 5) == guest);
        CHECK(stl::rd64(t->data() + 9) == 0x192 + stl::kTwinIdBase);
        CHECK(stl::rd32(t->data() + 92 + 4) == guest);        // the player binding (node 0x20000002)
        CHECK(stl::rd32(t->data() + 103 + 4) == 0xB01002E8u);  // the shopkeeper stays
        CHECK(stl::rd32(t->data() + 27) == 0x978345CAu);       // same resource
    }
    // not a shop, not kind 4, already a twin, the guest's own scene: none
    auto other = pl;
    other[27] = 0;
    CHECK(!stl::twin_prepare(other, guest, shop));
    other = pl;
    other[0] = 1;
    CHECK(!stl::twin_prepare(other, guest, shop));
    if (t) CHECK(!stl::twin_prepare(*t, guest, shop));
    CHECK(!stl::twin_prepare(pl, 0xA0100001u, shop));
    CHECK(!stl::twin_prepare(pl, 0, shop));
    // 2920 / 2292 rewrites
    const auto st = hexs("04010010a0920100000000000000000000ffffffff9d909c9700000000");
    const auto st2 = stl::twin_branch_state(st, guest);
    CHECK(stl::rd32(st2.data() + 1) == guest && stl::id_of_2920(st2) == 0x192 + stl::kTwinIdBase && st2.size() == st.size());
    const auto del = hexs("010010a0920100000000000000000000" "07");
    CHECK(stl::id_of_2292(del) == 0x192);
    const auto del2 = stl::twin_delete(del, guest);
    CHECK(stl::rd32(del2.data()) == guest && stl::id_of_2292(del2) == 0x192 + stl::kTwinIdBase && del2[16] == 7);
    // local answers
    const uint64_t tid = 0x192 + stl::kTwinIdBase;
    std::vector<uint8_t> br(14, 0);
    stl::wr64(br.data(), tid);
    stl::wr32(br.data() + 8, 6);
    CHECK(stl::root_of_initial_2920(st) == 0x979C909Du);
    auto a = stl::local_answers(stl::kOpBranchReq, br, guest, 0x979C909Du);
    CHECK(a.size() == 3);
    if (a.size() == 3) {
        uint16_t op = 0;
        std::span<const uint8_t> p;
        CHECK(stl::split(a[0], op, p) && op == 2708 && p.size() == 18 && p[13] == 1 && stl::rd32(p.data() + 14) == guest);
        CHECK(stl::split(a[1], op, p) && op == 2436 && p.size() == 18 && p[0] == 4 && stl::rd32(p.data() + 1) == guest &&
              stl::rd64(p.data() + 5) == tid && stl::rd32(p.data() + 13) == 6 && p[17] == 0);
        CHECK(stl::split(a[2], op, p) && op == 2920 && p.size() == 21 && stl::rd32(p.data() + 13) == 1 &&
              stl::rd32(p.data() + 17) == 6);
    }
    stl::wr32(br.data() + 8, 0);  // back to the start: the host's own answer to its branch 0 (tcB116)
    a = stl::local_answers(stl::kOpBranchReq, br, guest, 0x979C909Du);
    CHECK(a.size() == 3);
    if (a.size() == 3) {
        uint16_t op = 0;
        std::span<const uint8_t> p;
        CHECK(stl::split(a[0], op, p) && op == 2436 && stl::rd32(p.data() + 13) == 0);
        CHECK(stl::split(a[1], op, p) && op == 2708 && p[13] == 0);
        CHECK(stl::split(a[2], op, p) && op == 2920 && p.size() == 29 && stl::rd32(p.data() + 13) == 2 &&
              stl::rd32(p.data() + 17) == 0 && stl::rd32(p.data() + 21) == 0x979C909Du && stl::rd32(p.data() + 25) == 0);
    }
    std::vector<uint8_t> ce(17, 0);
    stl::wr64(ce.data(), tid);
    ce[12] = 1;
    ce[16] = 1;
    a = stl::local_answers(stl::kOpChartEventReq, ce, guest);
    CHECK(a.size() == 1);
    if (a.size() == 1) {
        uint16_t op = 0;
        std::span<const uint8_t> p;
        CHECK(stl::split(a[0], op, p) && op == 2187 && p.size() == 22 && p[17] == 1 && p[21] == 1);
    }
    CHECK(stl::local_answers(2752, ce, guest).empty());
    // shop dialog choices: the host's 3086 for Renee (tcB116) and the guest's (tcB161)
    const auto hostd = hexs("04010010a0f30110b040440f000a00000040440f000b00000001000000e4110000");
    uint32_t npc = 0;
    std::vector<stl::Dialog> d;
    CHECK(stl::shop_dialogs_of(hostd, npc, d) && npc == 0xB01001F3u && d.size() == 2 && d[0].index == 0x0A &&
          d[1].index == 0x0B);
    const auto guestd = hexs("04e50110a0ad0210b001000000e6110000");
    CHECK(stl::shop_dialogs_of(guestd, npc, d) && d.empty());
    const auto merged = stl::with_shop_dialogs(guestd, {{0x000F4440u, 0x0A}, {0x000F4440u, 0x0B}});
    CHECK(merged && merged->size() == guestd.size() + 16 && stl::rd32(merged->data() + 9) == 0x000F4440u &&
          stl::rd32(merged->data() + 13) == 0x0A && stl::rd32(merged->data() + 25) == 1 &&
          stl::rd32(merged->data() + 29) == 0x11E6);
    if (merged) CHECK(!stl::with_shop_dialogs(*merged, {{0x000F4440u, 0x0A}}));  // already there
    const std::vector<stl::NpcDialogs> nd{{0xB01001F3u, {{0x000F4440u, 0x0A}, {0x000F4440u, 0x0B}}}, {0xB01001F5u, {}}};
    const auto fp = stl::make_dialog_payload(nd);
    const auto back = stl::parse_dialog_payload(fp);
    CHECK(back && back->size() == 2 && (*back)[0].dialogs.size() == 2 && (*back)[0].dialogs[1].index == 0x0B &&
          (*back)[1].dialogs.empty());
    auto bad = fp;
    bad.push_back(0);
    CHECK(!stl::parse_dialog_payload(bad));
    CHECK(stl::is_twin_id(tid) && !stl::is_twin_id(0x192));
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("shop_twin: all tests passed\n");
    return 0;
}
