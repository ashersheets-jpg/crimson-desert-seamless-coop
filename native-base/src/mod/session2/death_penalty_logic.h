#pragma once
// Co-op+ T26 (2026-11-22): the death penalty's rules (docs/research/t26-death-penalty.md, route 2). Pure; unit-tested in
// tests/optd_death_penalty_tests.cpp. The dying player's own game asks once it gets ITSELF up (the revive prompt's
// instant or checkpoint choice, never a friend's revive): a 3104 on its own connection with trade_probe's worker entry
// kEntry, giver = receiver = its own character. The HOST's server (trade_probe.cpp's worker, the server thread)
// applies the HOST's setting to that player's own money: the share of one money item (the silver) in its Money pouch.
#include <cstdint>

namespace cdmp::session2::death_penalty {

inline constexpr uint32_t kEntry = 0xC0DE0009;   // trade_probe's worker: "I died and got up myself"
inline constexpr uint64_t kMinGapMs = 20000;     // one charge per death: the same player again sooner is refused
inline constexpr int64_t kMaxCount = 100000000;  // a sanity cap on what one death removes
inline constexpr int kMaxSlots = 100;            // the Money pouch's slots read (100 in every test save)

// The settings that exist (Co-op Settings: Off, 10 %, 25 %, 50 %, All); anything else is off.
inline int valid_percent(int p) noexcept { return p == 10 || p == 25 || p == 50 || p == 100 ? p : 0; }

// The share of `have` at `percent`: rounded down, at least 1 while there is any, never more than `have`.
inline int64_t share(int64_t have, int percent) noexcept {
    percent = valid_percent(percent);
    if (have <= 0 || percent == 0) return 0;
    int64_t n = percent == 100 ? have : have / 100 * percent + (have % 100) * percent / 100;
    if (n < 1) n = 1;
    if (n > have) n = have;
    return n < kMaxCount ? n : kMaxCount;
}

// The same player's earlier charge at `last_ms` (0 = none) is too recent at `now_ms`.
inline bool too_soon(uint64_t last_ms, uint64_t now_ms) noexcept {
    return last_ms != 0 && now_ms >= last_ms && now_ms - last_ms < kMinGapMs;
}

// A request the worker takes: a player character (key class 0xA) asking for itself, on its own session.
inline bool own_request(uint32_t requester, uint32_t giver, uint32_t receiver) noexcept {
    return requester != 0 && requester == giver && requester == receiver && (requester >> 28) == 0xA;
}

// The 3104 body the dying player's game sends (30 bytes, MoveItemInventoryToOtherInventoryReq on 3732):
// u32 item (the death's number, for the log) | u16 0xFFFF | u64 count 1 | u32 giver | u32 receiver | u16 0xFFFF |
// u16 0xFFFF | u32 entry.
inline void request_body(uint8_t out[30], uint32_t own_key, uint32_t death_no) noexcept {
    const uint16_t none = 0xFFFF;
    const uint64_t one = 1;
    const uint32_t entry = kEntry;
    auto put = [&](int at, const void* v, int n) {
        const auto* b = static_cast<const uint8_t*>(v);
        for (int i = 0; i < n; ++i) out[at + i] = b[i];
    };
    put(0, &death_no, 4);
    put(4, &none, 2);
    put(6, &one, 8);
    put(14, &own_key, 4);
    put(18, &own_key, 4);
    put(22, &none, 2);
    put(24, &none, 2);
    put(26, &entry, 4);
}

}  // namespace cdmp::session2::death_penalty
