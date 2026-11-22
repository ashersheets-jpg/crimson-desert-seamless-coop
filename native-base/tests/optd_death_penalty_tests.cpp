// Co-op+ T26 (2026-11-22): src/mod/session2/death_penalty_logic.h, the death penalty's rules.
#include <cstdio>
#include <cstring>

#include "mod/session2/death_penalty_logic.h"

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
    namespace dp = death_penalty;
    // The settings.
    CHECK(dp::valid_percent(0) == 0);
    CHECK(dp::valid_percent(10) == 10);
    CHECK(dp::valid_percent(25) == 25);
    CHECK(dp::valid_percent(50) == 50);
    CHECK(dp::valid_percent(100) == 100);
    CHECK(dp::valid_percent(5) == 0);
    CHECK(dp::valid_percent(-10) == 0);
    CHECK(dp::valid_percent(101) == 0);

    // The share: rounded down, at least 1, never more than the pouch holds.
    CHECK(dp::share(1000, 10) == 100);
    CHECK(dp::share(1234, 10) == 123);
    CHECK(dp::share(1234, 25) == 308);
    CHECK(dp::share(1234, 50) == 617);
    CHECK(dp::share(1234, 100) == 1234);
    CHECK(dp::share(5, 10) == 1);  // 0.5 -> at least 1
    CHECK(dp::share(1, 10) == 1);
    CHECK(dp::share(1, 100) == 1);
    CHECK(dp::share(0, 50) == 0);
    CHECK(dp::share(-3, 50) == 0);
    CHECK(dp::share(1000, 0) == 0);
    CHECK(dp::share(1000, 33) == 0);  // not a setting: off
    CHECK(dp::share(int64_t{9000000000000000000}, 50) == dp::kMaxCount);  // no overflow, capped
    CHECK(dp::share(int64_t{9000000000000000000}, 100) == dp::kMaxCount);
    CHECK(dp::share(199, 50) == 99);

    // Once per death.
    CHECK(!dp::too_soon(0, 50000));
    CHECK(dp::too_soon(40000, 50000));
    CHECK(!dp::too_soon(40000, 60000));
    CHECK(!dp::too_soon(40000, 70000));
    CHECK(!dp::too_soon(80000, 70000));  // a clock that went back: not blocked for ever

    // Only a player asking for itself.
    CHECK(dp::own_request(0xA0100066, 0xA0100066, 0xA0100066));
    CHECK(dp::own_request(0xA0100001, 0xA0100001, 0xA0100001));
    CHECK(!dp::own_request(0xA0100066, 0xA0100001, 0xA0100066));  // charging the other player
    CHECK(!dp::own_request(0xA0100066, 0xA0100066, 0xA0100001));
    CHECK(!dp::own_request(0xB0100066, 0xB0100066, 0xB0100066));  // not a player character
    CHECK(!dp::own_request(0, 0, 0));

    // The request's body: the fields where the worker reads them (trade_probe's 3104 layout, Lootr's).
    {
        uint8_t b[30];
        std::memset(b, 0xCC, sizeof b);
        dp::request_body(b, 0xA0100066, 7);
        uint32_t item = 0, giver = 0, receiver = 0, entry = 0;
        uint16_t f4 = 0, inv = 0, slot = 0;
        uint64_t count = 0;
        std::memcpy(&item, b + 0, 4);
        std::memcpy(&f4, b + 4, 2);
        std::memcpy(&count, b + 6, 8);
        std::memcpy(&giver, b + 14, 4);
        std::memcpy(&receiver, b + 18, 4);
        std::memcpy(&inv, b + 22, 2);
        std::memcpy(&slot, b + 24, 2);
        std::memcpy(&entry, b + 26, 4);
        CHECK(item == 7);
        CHECK(f4 == 0xFFFF);
        CHECK(count == 1);
        CHECK(giver == 0xA0100066);
        CHECK(receiver == 0xA0100066);
        CHECK(inv == 0xFFFF);
        CHECK(slot == 0xFFFF);
        CHECK(entry == 0xC0DE0009);
    }

    if (g_failures == 0) std::printf("All tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
