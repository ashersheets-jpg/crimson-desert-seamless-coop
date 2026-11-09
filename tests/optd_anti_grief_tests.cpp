// Co-op+ (2026-11-09): src/mod/session2/anti_grief_logic.h.
#include <cstdio>
#include <vector>

#include "mod/session2/anti_grief_logic.h"

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
void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
    for (int i = 0; i < 4; ++i) v[at + i] = static_cast<uint8_t>(x >> (8 * i));
}
}  // namespace

int main() {
    // A frame event with two targets: the fixed part, then two 677-byte entries.
    std::vector<uint8_t> pl(agl::kFixed + 2 * agl::kEntry, 0);
    pl[0] = 0x04;
    put32(pl, pl.size() - agl::kEntry, 0xB0100421u);      // the last entry
    put32(pl, pl.size() - 2 * agl::kEntry, 0xB01007BAu);  // the one before it
    auto t = agl::targets(pl);
    CHECK(t.size() == 2 && t[0] == 0xB0100421u && t[1] == 0xB01007BAu);
    std::vector<uint8_t> one(agl::kFixed + agl::kEntry, 0);
    one[0] = 0x04;
    put32(one, one.size() - agl::kEntry, 0xA01001EEu);
    CHECK(agl::targets(one).size() == 1);
    one[0] = 0x01;
    CHECK(agl::targets(one).empty());
    std::vector<uint8_t> small(10, 0);
    CHECK(agl::targets(small).empty());
    // A dialog list: u8 4 | actor | NPC | entries.
    std::vector<uint8_t> d = {4, 0x01, 0x00, 0x10, 0xA0, 0x21, 0x04, 0x10, 0xB0, 1, 0, 0, 0, 0x18, 0x11, 0, 0};
    CHECK(agl::dialog_npc(d) == 0xB0100421u);
    d[8] = 0xA0;
    CHECK(agl::dialog_npc(d) == 0);
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("anti_grief: all tests passed\n");
    return 0;
}
