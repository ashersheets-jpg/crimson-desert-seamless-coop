// Co-op+ (2026-11-09): src/mod/session2/merchant_unhide_logic.h.
#include <cstdio>

#include "mod/session2/merchant_unhide_logic.h"

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
}  // namespace

int main() {
    // the scenes seen at the Hernand market
    CHECK(mul::is_shop_scene(0x978345CAu));   // butcher
    CHECK(mul::is_shop_scene(0xA7132561u));   // smithy
    CHECK(mul::is_shop_scene(0x8FA1605Eu));   // grocery
    CHECK(mul::is_shop_scene(0x2CFAD71Au));   // general
    CHECK(mul::is_shop_scene(0xAA015527u));   // bank
    CHECK(mul::is_shop_scene(0xEA60A5DFu));   // pub
    CHECK(mul::is_shop_scene(0x0591B85Bu));   // clothes
    CHECK(mul::is_shop_scene(0x6EA4BC4Eu));   // leather
    CHECK(mul::is_shop_scene(0xE2EB7F12u));   // smuggler
    CHECK(mul::is_shop_scene(0x5FCC0360u));   // dye (2026-11-18)
    CHECK(mul::is_shop_scene(0x25C5EC9Bu));   // goblin broker (2026-11-18)
    CHECK(!mul::is_shop_scene(u2trade::dict_hash("cd_seq_funcnpc_craftsmith")));  // unhide only, no twin
    CHECK(mul::is_unhide_scene(u2trade::dict_hash("cd_seq_funcnpc_craftsmith")));
    CHECK(mul::is_unhide_scene(0x5FCC0360u));
    CHECK(!mul::is_unhide_scene(0x4F70088Bu));
    CHECK(!mul::is_shop_scene(0x4F70088Bu));  // Yann's scene (not a shop)
    CHECK(!mul::is_shop_scene(0x94FEA6AEu));  // the tavern crowd
    CHECK(!mul::is_shop_scene(0));
    // the decision
    CHECK(!mul::should_unhide(0x200, 1000, 4999));  // not stuck long enough (solo clears it in ~1 s)
    CHECK(mul::should_unhide(0x200, 1000, 5000));
    CHECK(mul::should_unhide(0x202, 1000, 9000));    // other bits do not matter
    CHECK(!mul::should_unhide(0x002, 1000, 9000));   // not hidden by bit 9
    CHECK(!mul::should_unhide(0, 1000, 9000));
    CHECK(!mul::should_unhide(0x200, 9000, 1000));   // a clock that went back: never
    // the hide mirror's payload (state 18)
    {
        const std::vector<uint32_t> keys = {0xB010017Au, 0xB010017Bu, 0x12345678u};
        const auto p = mul::make_visible_payload(keys);
        CHECK(p.size() == 2 + 4 * 3 && p[0] == 18 && p[1] == 3 && p[2] == 0x7A && p[5] == 0xB0);
        const auto back = mul::parse_visible_payload(p);
        CHECK(back && *back == keys);
        CHECK(mul::parse_visible_payload(mul::make_visible_payload({})) && mul::parse_visible_payload(mul::make_visible_payload({}))->empty());
        auto bad = p;
        bad.pop_back();
        CHECK(!mul::parse_visible_payload(bad));        // short
        bad = p;
        bad[0] = 17;
        CHECK(!mul::parse_visible_payload(bad));        // another state
        std::vector<uint32_t> many(200, 0xB0000001u);
        CHECK(mul::make_visible_payload(many).size() == 2 + 4 * mul::kVisibleMax);  // capped
        CHECK(!mul::parse_visible_payload(std::vector<uint8_t>{18}));
    }
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("merchant_unhide: all tests passed\n");
    return 0;
}
