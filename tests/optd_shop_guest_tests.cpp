// Co-op+ (2026-11-08): src/mod/session2/shop_guest_logic.h (the guest's own trade at the host's merchants).
#include <cstdio>

#include "mod/session2/shop_guest_logic.h"

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

void test_empty() {
    // the host (or anyone not the guest): untouched
    auto v = sgl::on_empty(false, 0);
    CHECK(v.r12 == 0 && !v.bypassed);
    v = sgl::on_empty(false, 0x20000196ull);
    CHECK(v.r12 == 0x20000196ull && !v.bypassed);
    // the guest with a current stage: untouched (hook B decides)
    v = sgl::on_empty(true, 0x196);
    CHECK(v.r12 == 0x196 && !v.bypassed);
    // the guest, the manager has none (only flag bits): placeholder
    v = sgl::on_empty(true, 0xE0000000ull);
    CHECK(v.bypassed && (v.r12 & sgl::kStageIdMask) != 0);
    v = sgl::on_empty(true, 0);
    CHECK(v.bypassed && (v.r12 & sgl::kStageIdMask) != 0);
}

void test_compare() {
    // the host: the game's own compare
    CHECK(sgl::current_for_compare(false, false, 0x196, 0x1FF) == 0x196);
    CHECK(sgl::current_for_compare(false, false, 0, 0) == 0);
    // the guest at a scene merchant: the merchant's own stage (passes)
    CHECK(sgl::current_for_compare(true, false, 0x196, 0x1FF) == 0x1FF);
    CHECK(sgl::current_for_compare(true, true, 0, 0x1FF) == 0x1FF);
    // the guest at a merchant with no linked stage: refused as before
    CHECK(sgl::current_for_compare(true, false, 0x196, 0) == 0x196);
    // ... and when hook A let the empty manager through: never equal to 0 (the native refusal)
    CHECK(sgl::current_for_compare(true, true, 0, 0) != 0);
}
}  // namespace

int main() {
    test_empty();
    test_compare();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("shop_guest: all tests passed\n");
    return 0;
}
