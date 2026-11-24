// DRAFT tests for gimmick_handoff_logic.h (to tests/ with the header; traced by hand 2026-11-24, not compiled yet).
#include <cstdio>
#include <map>

#include "mod/session2/gimmick_handoff_logic.h"

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

constexpr uint32_t kHost = 0x90100000, kU2 = 0x90100065, kSpot = 0xB0100500, kTree = 0xB0100501;

struct World {
    std::map<uint32_t, ghl::Facts> f;
    ghl::Facts operator()(uint32_t k) const {
        const auto it = f.find(k);
        return it == f.end() ? ghl::Facts{} : it->second;
    }
};

ghl::Facts spot(float d, uint32_t ctl = kHost) {
    ghl::Facts f;
    f.exists = true;
    f.type = 7;
    f.controller = ctl;
    f.user2_m = d;
    return f;
}

void test_hand_and_back() {
    ghl::Book b;
    World w;
    w.f[kSpot] = spot(4.0f);
    // No intent: nothing.
    CHECK(b.tick(1'000, kHost, kU2, false, w).empty());
    // Intent, near, host idle: hand over.
    b.on_user2_intent(kSpot, 10'000);
    auto d = b.tick(10'500, kHost, kU2, false, w);
    CHECK(d.size() == 1 && d[0].key == kSpot && d[0].act == ghl::Act::Hand);
    b.handed(kSpot, 10'500);
    w.f[kSpot].controller = kU2;
    CHECK(b.handed_now() == 1);
    // User 2 keeps using it: nothing.
    b.on_user2_use(kSpot, 15'000);
    CHECK(b.tick(20'000, kHost, kU2, false, w).empty());
    // Idle 10 s but still near: kept.
    CHECK(b.tick(26'000, kHost, kU2, false, w).empty());
    // Idle and far: back.
    w.f[kSpot].user2_m = 30.0f;
    d = b.tick(26'000, kHost, kU2, false, w);
    CHECK(d.size() == 1 && d[0].act == ghl::Act::HandBack && d[0].why == ghl::Why::User2Idle);
    b.handed_back(kSpot);
    CHECK(b.tracked() == 0 && b.hand_count() == 1 && b.back_count() == 1);
}

void test_no_hand() {
    ghl::Book b;
    World w;
    // Far, docked, not a gimmick, someone else's, the host busy with it, stale intent: never handed.
    w.f[kSpot] = spot(40.0f);
    b.on_user2_intent(kSpot, 1'000);
    CHECK(b.tick(1'500, kHost, kU2, false, w).empty());
    w.f[kSpot] = spot(4.0f);
    w.f[kSpot].docked = true;
    b.on_user2_intent(kSpot, 2'000);
    CHECK(b.tick(2'500, kHost, kU2, false, w).empty());
    w.f[kSpot] = spot(4.0f);
    w.f[kSpot].type = 3;
    b.on_user2_intent(kSpot, 3'000);
    CHECK(b.tick(3'500, kHost, kU2, false, w).empty());
    w.f[kSpot] = spot(4.0f, 0x90100099);
    b.on_user2_intent(kSpot, 4'000);
    CHECK(b.tick(4'500, kHost, kU2, false, w).empty());
    w.f[kSpot] = spot(4.0f);
    b.on_host_use(kSpot, 5'000);
    b.on_user2_intent(kSpot, 6'000);
    CHECK(b.tick(6'500, kHost, kU2, false, w).empty());  // the host used it 1.5 s ago
    b.on_user2_intent(kSpot, 9'000);
    CHECK(b.tick(12'000, kHost, kU2, false, w).empty());  // the intent is 3 s old
    // Old intents are forgotten.
    CHECK(b.tick(80'000, kHost, kU2, false, w).empty() && b.tracked() == 0);
}

void test_host_asks_gone_removed() {
    ghl::Book b;
    World w;
    w.f[kSpot] = spot(4.0f);
    w.f[kTree] = spot(6.0f);
    b.on_user2_intent(kSpot, 1'000);
    b.on_user2_intent(kTree, 1'000);
    CHECK(b.tick(1'200, kHost, kU2, false, w).size() == 2);
    b.handed(kSpot, 1'200);
    b.handed(kTree, 1'200);
    w.f[kSpot].controller = kU2;
    w.f[kTree].controller = kU2;
    // The host asks for the spot right after: user 2 not idle 5 s yet, kept.
    b.on_host_use(kSpot, 3'000);
    CHECK(b.tick(3'100, kHost, kU2, false, w).empty());
    // The host asks again with user 2 idle 5 s: back.
    b.on_host_use(kSpot, 7'000);
    auto d = b.tick(7'100, kHost, kU2, false, w);
    CHECK(d.size() == 1 && d[0].key == kSpot && d[0].why == ghl::Why::HostAsks);
    b.handed_back(kSpot);
    // The tree is removed (its level unloaded): back, forgotten.
    w.f.erase(kTree);
    d = b.tick(8'000, kHost, kU2, false, w);
    CHECK(d.size() == 1 && d[0].key == kTree && d[0].why == ghl::Why::Removed && b.tracked() == 0);
    // User 2 gone: every handed one at once.
    w.f[kSpot] = spot(4.0f);
    b.on_user2_intent(kSpot, 9'000);
    CHECK(b.tick(9'100, kHost, kU2, false, w).size() == 1);
    b.handed(kSpot, 9'100);
    d = b.on_user2_gone();
    CHECK(d.size() == 1 && d[0].why == ghl::Why::User2Gone);
}

void test_probe_and_bounds() {
    ghl::Book b;
    World w;
    w.f[kSpot] = spot(4.0f);  // probe: the controller stays the host's
    b.on_user2_intent(kSpot, 1'000);
    CHECK(b.tick(1'100, kHost, kU2, true, w).size() == 1);
    b.handed(kSpot, 1'100);
    CHECK(b.tick(2'000, kHost, kU2, true, w).empty() && b.handed_now() == 1);  // not forgotten in probe
    CHECK(b.tick(2'000, kHost, kU2, false, w).empty() && b.tracked() == 0);    // in mode 1 it is (not ours)
    for (uint32_t k = 1; k <= ghl::kMaxTracked + 5; ++k) b.on_user2_intent(0xB0200000 + k, 5'000);
    CHECK(b.tracked() == ghl::kMaxTracked && b.full() == 5);
    b.on_user2_intent(0, 5'000);
    CHECK(b.tracked() == ghl::kMaxTracked);
}
}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_hand_and_back();
    test_no_hand();
    test_host_asks_gone_removed();
    test_probe_and_bounds();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("gimmick_handoff: all tests passed\n");
    return 0;
}
