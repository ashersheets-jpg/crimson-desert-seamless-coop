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
    uint32_t state = 0;  // the twin's initial 2920
    auto a = stl::local_answers(stl::kOpBranchReq, br, guest, 0x979C909Du, state);
    CHECK(a.size() == 3 && state == 1);
    if (a.size() == 3) {
        uint16_t op = 0;
        std::span<const uint8_t> p;
        CHECK(stl::split(a[0], op, p) && op == 2307 && p.size() == 18 && p[13] == 1 && stl::rd32(p.data() + 14) == guest);
        CHECK(stl::split(a[1], op, p) && op == 2239 && p.size() == 18 && p[0] == 4 && stl::rd32(p.data() + 1) == guest &&
              stl::rd64(p.data() + 5) == tid && stl::rd32(p.data() + 13) == 6 && p[17] == 0);
        CHECK(stl::split(a[2], op, p) && op == 2455 && p.size() == 21 && stl::rd32(p.data() + 13) == 1 &&
              stl::rd32(p.data() + 17) == 6);
    }
    stl::wr32(br.data() + 8, 0);  // the greeting ends: the menu (the host's own answer, tcB116)
    a = stl::local_answers(stl::kOpBranchReq, br, guest, 0x979C909Du, state);
    CHECK(a.size() == 3 && state == 2);
    if (a.size() == 3) {
        uint16_t op = 0;
        std::span<const uint8_t> p;
        CHECK(stl::split(a[0], op, p) && op == 2239 && stl::rd32(p.data() + 13) == 0);
        CHECK(stl::split(a[1], op, p) && op == 2307 && p[13] == 0);
        CHECK(stl::split(a[2], op, p) && op == 2455 && p.size() == 29 && stl::rd32(p.data() + 13) == 2 &&
              stl::rd32(p.data() + 17) == 0 && stl::rd32(p.data() + 21) == 0x979C909Du && stl::rd32(p.data() + 25) == 0);
    }
    // Buy from the menu: the store opens (type 3, no root, no 2708); closing it: the menu again (host, 566.4 / 573.6)
    auto expect = [&](uint32_t branch, uint32_t type, size_t len, size_t n_msgs) {
        stl::wr32(br.data() + 8, branch);
        const auto r = stl::local_answers(stl::kOpBranchReq, br, guest, 0x979C909Du, state);
        CHECK(r.size() == n_msgs && state == type);
        if (r.empty()) return;
        uint16_t op = 0;
        std::span<const uint8_t> p;
        CHECK(stl::split(r.back(), op, p) && op == 2455 && p.size() == len && stl::rd32(p.data() + 13) == type &&
              stl::rd32(p.data() + 17) == branch);
        if (len == 29) CHECK(stl::rd32(p.data() + 21) == 0x979C909Du);
        bool ev = false;
        for (const auto& m : r) ev = ev || (stl::split(m, op, p) && op == 2239 && stl::rd32(p.data() + 13) == branch);
        CHECK(ev);
    };
    expect(0, 3, 21, 2);  // Buy
    expect(0, 2, 29, 2);  // the store closes
    expect(1, 4, 21, 2);  // Sell (host alone, tcB169)
    expect(0, 2, 29, 2);  // the sell window closes
    expect(3, 7, 21, 2);  // Leave
    expect(1, 0, 29, 2);  // the scene is over
    expect(6, 1, 21, 3);  // and starts again
    std::vector<uint8_t> ce(17, 0);
    stl::wr64(ce.data(), tid);
    ce[12] = 1;
    ce[16] = 1;
    a = stl::local_answers(stl::kOpChartEventReq, ce, guest);
    CHECK(a.size() == 1);
    if (a.size() == 1) {
        uint16_t op = 0;
        std::span<const uint8_t> p;
        CHECK(stl::split(a[0], op, p) && op == 2308 && p.size() == 22 && p[17] == 1 && p[21] == 1);
    }
    CHECK(stl::local_answers(2432, ce, guest).empty());
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
    // dialog_share_all: every group but the greeting (tcB186's 0xb0100434: shop 02/03 + 0xF435B 0x92, greeting 1)
    const auto mixed = hexs("04010010a0340410b0" "40440f0002000000" "40440f0003000000" "5b430f0092000000" "0100000018110000");
    CHECK(stl::dialogs_of(mixed, npc, d, false) && d.size() == 2);
    CHECK(stl::dialogs_of(mixed, npc, d, true) && npc == 0xB0100434u && d.size() == 3 && d[2].group == 0xF435Bu &&
          d[2].index == 0x92);
    const auto g1 = hexs("04e50110a0340410b0" "0100000018110000");
    const auto m1 = stl::with_shop_dialogs(g1, d);
    CHECK(m1 && m1->size() == g1.size() + 24 && stl::rd32(m1->data() + 25) == 0xF435Bu);
    // twin_extra (B14): the arm-wrestling table's answers (cs83w3, the host's won match)
    {
        const uint64_t aid = 0x31F + stl::kTwinIdBase;
        const uint32_t root = 0x22B6EC91u;
        uint32_t ast = 0;
        auto req = [&](uint32_t branch, uint16_t flag) {
            std::vector<uint8_t> r(14, 0);
            stl::wr64(r.data(), aid);
            stl::wr32(r.data() + 8, branch);
            r[12] = static_cast<uint8_t>(flag);
            r[13] = static_cast<uint8_t>(flag >> 8);
            return r;
        };
        const auto chart = stl::chart_of(stl::kResArmwrest);
        CHECK(!chart.empty() && stl::chart_of(0x978345CAu).empty());
        auto r = stl::table_answers(chart, req(5, 0), guest, root, ast);  // the talk: the menu
        CHECK(r.known && ast == 1 && r.now.size() == 2 && r.later.empty());
        uint16_t op = 0;
        std::span<const uint8_t> p;
        if (r.now.size() == 2) {
            CHECK(stl::split(r.now[0], op, p) && op == 2239 && stl::rd32(p.data() + 13) == 5 && p[17] == 0 &&
                  stl::rd64(p.data() + 5) == aid);
            CHECK(stl::split(r.now[1], op, p) && op == 2455 && p.size() == 29 && stl::rd32(p.data() + 13) == 1 &&
                  stl::rd32(p.data() + 17) == 5 && stl::rd32(p.data() + 21) == root && stl::rd32(p.data() + 1) == guest);
        }
        r = stl::table_answers(chart, req(0, 1), guest, root, ast);  // Participate: the state held for the 2654
        CHECK(r.known && ast == 3 && r.now.size() == 2 && r.later.size() == 2);
        if (r.now.size() == 2) {
            CHECK(stl::split(r.now[0], op, p) && op == 2307 && p[13] == 1);
            CHECK(stl::split(r.now[1], op, p) && op == 2239 && stl::rd32(p.data() + 13) == 0 && p[17] == 1);
        }
        if (r.later.size() == 2) {
            CHECK(stl::split(r.later[0], op, p) && op == 2455 && stl::rd32(p.data() + 13) == 3 &&
                  stl::rd32(p.data() + 17) == 0);
            CHECK(stl::split(r.later[1], op, p) && op == 2307 && p[13] == 0);  // its AvoidOff after it
        }
        r = stl::table_answers(chart, req(0, 0), guest, root, ast);  // 3 -> 4: on, event, off, state
        CHECK(r.known && ast == 4 && r.now.size() == 4 && r.later.empty());
        if (r.now.size() == 4) {
            CHECK(stl::split(r.now[0], op, p) && op == 2307 && p[13] == 1);
            CHECK(stl::split(r.now[2], op, p) && op == 2307 && p[13] == 0);
            CHECK(stl::split(r.now[3], op, p) && op == 2455 && stl::rd32(p.data() + 13) == 4);
        }
        for (const auto& [b, want] : {std::pair<uint32_t, uint32_t>{1, 5}, {0, 6}, {0, 7}, {1, 9}, {0, 10}}) {
            r = stl::table_answers(chart, req(b, 0), guest, root, ast);  // a lost match
            CHECK(r.known && ast == want);
        }
        r = stl::table_answers(chart, req(6, 1), guest, root, ast);  // Play Again: held again
        CHECK(r.known && ast == 3 && r.now.size() == 2 && r.later.size() == 2);
        for (const auto& [b, want] : {std::pair<uint32_t, uint32_t>{0, 4}, {1, 5}, {0, 6}, {0, 7}, {0, 8}, {0, 10}}) {
            r = stl::table_answers(chart, req(b, 0), guest, root, ast);  // a won match
            CHECK(r.known && ast == want);
        }
        r = stl::table_answers(chart, req(7, 1), guest, root, ast);  // Back after the win: held, then 11 -> 0
        CHECK(r.known && ast == 11 && r.now.size() == 2 && r.later.size() == 2);
        r = stl::table_answers(chart, req(0, 0), guest, root, ast);
        CHECK(r.known && ast == 0);
        r = stl::table_answers(chart, req(3, 0), guest, root, ast);  // not in the table: back to the start
        CHECK(!r.known && ast == 0 && r.now.size() == 2);
        if (r.now.size() == 2) CHECK(stl::split(r.now[1], op, p) && op == 2455 && stl::rd32(p.data() + 13) == 0);
        r = stl::table_answers(chart, req(5, 0), guest, root, ast);  // the menu again, then Back
        r = stl::table_answers(chart, req(1, 0), guest, root, ast);
        CHECK(r.known && ast == 2 && r.now.size() == 4);
        r = stl::table_answers(chart, req(0, 0), guest, root, ast);
        CHECK(r.known && ast == 0);
        // the minigame requests: the twin's id -> the host's, and the client's own Ack back
        auto spawn = hexs("041f030010000000004042" "51d50000" "06000020");
        CHECK(stl::minigame_req_to_host(2863, spawn) == 0x31F && stl::rd64(spawn.data() + 1) == 0x31F);
        CHECK(stl::minigame_req_to_host(2863, spawn) == 0);  // the host's id already: unchanged
        auto unsp = hexs("041f03001000000000" "4042" "5c0a10b0");
        CHECK(stl::minigame_req_to_host(2553, unsp) == 0x31F);
        CHECK(stl::minigame_req_to_host(2423, unsp) == 0);
        auto ack = hexs("04" "040210a0" "1f03000000000000" "06000020" "5c0a10b0" "050000");
        const uint32_t own = 0xA0100204u;
        CHECK(stl::minigame_ack_to_twin(2616, ack, own, [](uint64_t h) { return h == 0x31F; }) == aid &&
              stl::rd64(ack.data() + 5) == aid);
        auto ack2 = hexs("04" "010010a0" "1f03000000000000" "06000020" "5c0a10b0" "050000");  // the host's own: no
        CHECK(stl::minigame_ack_to_twin(2616, ack2, own, [](uint64_t) { return true; }) == 0);
        // twin_extra_chart (cs92): the twin's state-7 chart event to the host's scene, the server's 2105 back to the twin
        auto creq = hexs("1f03001000000000" "070000000000000001");
        CHECK(stl::chart_req_to_host(2811, creq) == 0x31F && stl::rd64(creq.data()) == 0x31F);
        CHECK(stl::chart_req_to_host(2811, creq) == 0);  // the host's id already: unchanged
        auto chart_short = hexs("1f03001000000000" "0700000000000000");
        CHECK(stl::chart_req_to_host(2811, chart_short) == 0);
        CHECK(stl::chart_req_to_host(2726, creq) == 0);
        std::vector<stl::ForwardedChart> pending(1);
        pending[0].host = 0x31F;
        std::memcpy(pending[0].ev, creq.data() + 8, 9);
        pending[0].at_ms = 1000;
        auto cack = hexs("04" "040210a0" "1f03000000000000" "070000000000000001");
        auto other = hexs("04" "040210a0" "1f03000000000000" "070000000100000001");  // event 7/1: not forwarded
        CHECK(stl::chart_ack_to_twin(2308, other, pending, 1500, 3000) == 0 && pending.size() == 1);
        CHECK(stl::chart_ack_to_twin(2308, cack, pending, 5000, 3000) == 0 && pending.size() == 1);  // too old
        CHECK(stl::chart_ack_to_twin(2308, cack, pending, 1500, 3000) == aid && stl::rd64(cack.data() + 5) == aid &&
              pending.empty());
        auto cack2 = hexs("04" "040210a0" "1f03000000000000" "070000000000000001");
        CHECK(stl::chart_ack_to_twin(2308, cack2, pending, 1500, 3000) == 0);  // used up: the next one is the host's
        // twin_extra_chart=3: the grip made here (cs92's 55 bytes, catcher and target replaced) and the 2810's opponent
        const auto grip = stl::catch_result(0xA0100066u, 0xB0100AF8u);
        CHECK(grip.size() == 5 + 55 && grip[0] == (2560 & 0xFF) && grip[1] == (2560 >> 8));
        CHECK(stl::rd32(grip.data() + 5) == 0xA0100066u && stl::rd32(grip.data() + 9) == 0xB0100AF8u);
        CHECK(stl::rd32(grip.data() + 5 + 31) == 0x18F128E3u && stl::rd32(grip.data() + 5 + 50) == 0x63F9EFFDu &&
              grip[5 + 54] == 0x17);
        const auto spawn_ack = hexs("04" "660010a0" "2103000000000000" "06000020" "f70a10b0" "050000");
        CHECK(stl::minigame_opponent(spawn_ack) == 0xB0100AF7u);
        CHECK(stl::minigame_opponent(hexs("04660010a0")) == 0);
    }
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("shop_twin: all tests passed\n");
    return 0;
}
