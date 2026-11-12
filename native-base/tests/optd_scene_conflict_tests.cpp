// Co-op+ (2026-11-09, tcB225): hso::SceneHold's conflict hold, on the 1.0.0.3576 layouts (2380 PrepareSequencerAck
// with the 92-byte prefix, 2292 DeleteSequencerAck with the actor @0 and the id @4).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "mod/session2/hold_scene_order_logic.h"
#include "mod/session2/observer_seq_copy_logic.h"

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

constexpr uint32_t kOwn = 0xA0100066, kHost = 0xA0100001, kRenee = 0xB010030B, kOther = 0xB0100310;
constexpr uint32_t kButcher = 0x978345CA, kLeather = 0x6EA4BC4E;

void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) { std::memcpy(&v[at], &x, 4); }
void put64(std::vector<uint8_t>& v, size_t at, uint64_t x) { std::memcpy(&v[at], &x, 8); }

std::vector<uint8_t> prepare(uint32_t owner, uint64_t id, uint32_t hash, std::vector<uint32_t> bound) {
    std::vector<uint8_t> p(hso::kPreparePrefix + hso::kBindingWire * bound.size(), 0);
    p[0] = 4;
    put32(p, 1, owner);
    put32(p, 5, owner);
    put64(p, 9, id);
    put32(p, 27, hash);
    for (size_t i = 0; i < bound.size(); ++i) {
        const size_t at = hso::kPreparePrefix + hso::kBindingWire * i;
        put32(p, at, static_cast<uint32_t>(i + 1));
        put32(p, at + 4, bound[i]);
        p[at + 8] = 4;
    }
    return p;
}
std::vector<uint8_t> del(uint32_t actor, uint64_t id) {
    std::vector<uint8_t> p(17, 0);
    put32(p, 0, actor);
    put64(p, 4, id);
    return p;
}
std::vector<uint8_t> branch(uint64_t id) {  // a 2920 StageBranchStateAck (layout as hso::scene_id reads it)
    std::vector<uint8_t> p(21, 0);
    p[0] = 4;
    put32(p, 1, kOwn);
    put64(p, 5, id);
    return p;
}

struct Route {
    hso::SceneHold t2;
    std::vector<hso::Event> ev;
    std::vector<std::pair<uint16_t, uint64_t>> queue;  // (opcode, id) as they reached user 2
    uint64_t seq = 0, now = 1'000'000;
    hso::Take send(uint16_t op, const std::vector<uint8_t>& payload, uint64_t id) {
        std::vector<uint8_t> packet = payload;
        const hso::Take tk = t2.offer(op, payload, packet, now, kOwn, [](uint32_t) { return false; },
                                      [&] { return ++seq; }, ev);
        if (tk == hso::Take::Pass) {
            queue.push_back({op, id});
            std::vector<hso::OutPacket> after;
            t2.take_after(after);
            for (const hso::OutPacket& o : after) queue.push_back({o.op, 0xFFFF});
        }
        return tk;
    }
};

void test_off_by_default() {
    Route r;
    CHECK(r.send(hso::kOpPrepare, prepare(kOwn, 0x1CD, kButcher, {kRenee}), 0x1CD) == hso::Take::Pass);
    CHECK(r.send(hso::kOpPrepare, prepare(kHost, 0x241, kButcher, {kRenee}), 0x241) == hso::Take::Pass);
    CHECK(r.t2.scenes() == 0 && r.t2.own_scenes() == 0 && r.queue.size() == 2);
}

void test_held_until_own_delete() {
    Route r;
    r.t2.set_conflict(true);
    // tcB225: user 2's own butcher (U-1, far) binds Renee; then, near the host, the host's butcher binds her too.
    CHECK(r.send(hso::kOpPrepare, prepare(kOwn, 0x1CD, kButcher, {kRenee}), 0x1CD) == hso::Take::Pass);
    CHECK(r.t2.own_scenes() == 1 && r.t2.live() == 1);
    CHECK(r.send(hso::kOpPrepare, prepare(kHost, 0x241, kButcher, {kRenee}), 0x241) == hso::Take::Held);
    CHECK(r.send(hso::kOpBranch, branch(0x241), 0x241) == hso::Take::Held);  // its later packets wait too
    CHECK(r.t2.kept(0x241) && r.ev.back().kind == hso::Kind::WouldHold && r.ev.back().conflict);
    // A host scene binding nobody of user 2's passes; another owner's delete of the same id changes nothing.
    CHECK(r.send(hso::kOpPrepare, prepare(kHost, 0x242, kLeather, {kOther}), 0x242) == hso::Take::Pass);
    CHECK(r.send(hso::kOpDelete, del(kHost, 0x1CD), 0x1CD) == hso::Take::Pass);
    CHECK(r.t2.kept(0x241));
    // The drain: user 2's own 0x1CD is deleted -> Renee is refreshed on user 2's client (3067 + a fresh 2005): the
    // host's scene now waits for her 2005, then its 2380 and 2920 follow right behind it, in order.
    const size_t before = r.queue.size();
    CHECK(r.send(hso::kOpDelete, del(kOwn, 0x1CD), 0x1CD) == hso::Take::Pass);
    CHECK(r.queue.size() == before + 1 && r.t2.kept(0x241) && r.t2.own_scenes() == 0);
    std::vector<hso::OutPacket> rel;
    const uint32_t keys[] = {kRenee};
    r.t2.announced(keys, r.now, rel, r.ev);
    CHECK(rel.size() == 2 && rel[0].op == hso::kOpPrepare && rel[1].op == hso::kOpBranch);
    CHECK(!r.t2.kept(0x241) && r.t2.live() == 0);
    CHECK(r.ev.back().kind == hso::Kind::Released);
    // Afterwards the host's packets of that id pass at once.
    CHECK(r.send(hso::kOpBranch, branch(0x241), 0x241) == hso::Take::Pass);
}

void test_two_own_scenes() {
    Route r;
    r.t2.set_conflict(true);
    CHECK(r.send(hso::kOpPrepare, prepare(kOwn, 0x1CD, kButcher, {kRenee}), 0x1CD) == hso::Take::Pass);
    CHECK(r.send(hso::kOpPrepare, prepare(kOwn, 0x1D2, kLeather, {kOther}), 0x1D2) == hso::Take::Pass);
    CHECK(r.send(hso::kOpPrepare, prepare(kHost, 0x300, kButcher, {kRenee, kOther}), 0x300) == hso::Take::Held);
    CHECK(r.send(hso::kOpDelete, del(kOwn, 0x1CD), 0x1CD) == hso::Take::Pass);
    CHECK(r.t2.kept(0x300));  // still waits for 0x1D2
    CHECK(r.send(hso::kOpDelete, del(kOwn, 0x1D2), 0x1D2) == hso::Take::Pass);
    CHECK(r.t2.kept(0x300));  // now for both NPCs' fresh 2005
    std::vector<hso::OutPacket> rel;
    const uint32_t one[] = {kRenee};
    r.t2.announced(one, r.now, rel, r.ev);
    CHECK(rel.empty() && r.t2.kept(0x300));
    const uint32_t two[] = {kOther};
    r.t2.announced(two, r.now, rel, r.ev);
    CHECK(rel.size() == 1 && !r.t2.kept(0x300));
}

void test_bound_and_clear() {
    Route r;
    r.t2.set_conflict(true);
    CHECK(r.send(hso::kOpPrepare, prepare(kOwn, 0x1CD, kButcher, {kRenee}), 0x1CD) == hso::Take::Pass);
    CHECK(r.send(hso::kOpPrepare, prepare(kHost, 0x241, kButcher, {kRenee}), 0x241) == hso::Take::Held);
    // The 100 s bound discards it (native: the client would have refused it anyway, its NPC busy).
    r.now += 101'000'000;
    std::vector<hso::Event> out;
    r.t2.tick(r.now, out);
    CHECK(!r.t2.kept(0x241) && !out.empty() && out.back().kind == hso::Kind::Expired);
    std::vector<hso::Event> gone;
    r.t2.clear(r.now, gone);
    CHECK(r.t2.own_scenes() == 0 && r.t2.live() == 0);
}

void test_seq_copy_own_blocks() {
    osq::Copier c;
    c.on_user2_own_binds(0x1CA, {kRenee});
    CHECK(!c.own_blocks({kRenee}) && c.own_binds() == 0);  // off: nothing noted
    c.set_own_conflict(true);
    c.on_user2_own_binds(0x1CA, {kRenee});
    c.on_user2_own_binds(0x1CF, {kOther});
    CHECK(c.own_blocks({kRenee}) && c.own_blocks({0x12345678u, kOther}) && !c.own_blocks({0x12345678u}));
    CHECK(c.on_user2_own_deleted(0x1CA) && !c.own_blocks({kRenee}) && c.own_blocks({kOther}));
    CHECK(!c.on_user2_own_deleted(0x1CA));
    c.set_own_conflict(false);
    CHECK(!c.own_blocks({kOther}) && c.own_binds() == 0);
}

}  // namespace

int main() {
    test_seq_copy_own_blocks();
    test_off_by_default();
    test_held_until_own_delete();
    test_two_own_scenes();
    test_bound_and_clear();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_scene_conflict: all checks passed\n");
    return 0;
}
