// Co-op+ (2026-11-09): src/mod/session2/fog_share_logic.h.
#include <cstdio>
#include <vector>

#include "mod/session2/fog_share_logic.h"

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
std::vector<uint8_t> chunk(uint8_t seq, std::vector<uint8_t> bits) {
    std::vector<uint8_t> p{seq, 0x01, 0x00, 0x10, 0xA0};
    p.insert(p.end(), bits.begin(), bits.end());
    return p;
}
}  // namespace

int main() {
    fsl::Chunks host;
    CHECK(fsl::record(host, chunk(1, {0x01, 0x80, 0x00})));
    CHECK(fsl::record(host, chunk(2, {0x00, 0xC0})));
    CHECK(host.data.size() == 2);
    auto g0 = chunk(1, {0x02, 0x80, 0x00});
    CHECK(fsl::merge(g0, host, 0) == 1 && g0[5] == 0x03 && g0[6] == 0x80 && g0[7] == 0x00);
    CHECK(g0[0] == 1 && g0[1] == 0x01);  // the header untouched
    auto g1 = chunk(2, {0x01, 0x00});
    CHECK(fsl::merge(g1, host, 1) == 1 && g1[5] == 0x01 && g1[6] == 0xC0);
    auto bad = chunk(2, {0x01});  // another size: untouched
    CHECK(fsl::merge(bad, host, 1) == 0 && bad[5] == 0x01);
    CHECK(fsl::merge(g1, host, 5) == 0);  // no such host chunk
    CHECK(fsl::record(host, chunk(1, {0xFF})) && host.data.size() == 1);  // a new map starts over
    CHECK(!fsl::record(host, std::vector<uint8_t>{1, 2, 3}));
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("fog_share: all tests passed\n");
    return 0;
}
