#include "mod/game/game_clock.h"

#include <windows.h>

#include <cstring>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"

namespace cdmp::game {
namespace off = build_3663;

std::optional<uint64_t> client_game_clock_ms() {
    static const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    // Co-op+ (3576): the clock found with the dev clockscan. local_capture still checks it against our Reqs' stamps
    // before trusting it (within 250 ms of 50 of them).
    if constexpr (off::kClientClockMs != 0) {
        if (off::kClientClockMs + 8 > off::kImageSize) return std::nullopt;
        const auto ms = read<uint64_t>(base + off::kClientClockMs);
        if (!ms || *ms == 0 || *ms > 365ull * 24 * 60 * 60 * 1000) return std::nullopt;
        return *ms;
    }
    // The clock getter's own address is still the 2976 one and its head cannot match
    // on this build, so comparing the head -- the old check -- would disable the clock
    // outright. What is resolved instead is the GLOBAL that getter consults: on 3446
    // four independent `cmp byte [rip+d],0` sites agree on 0x70CDCAD.
    //
    // So check the data instead of the code: the flag must be a byte in the image
    // (it is in zero-filled memory, so only the range can be checked offline, but a
    // live game fills it), and the two clock fields next to it must look like a
    // clock -- non-zero and monotonically plausible. A wrong slot fails those and
    // the feature turns itself off, exactly as before, but a RIGHT slot now works.
    //
    // The flag and both fields were confirmed on a live 3446 process: the flag held
    // 1, and flag+0x3C advanced steadily (134323 -> 141902 over 1.947 s).
    static const bool known = [] {
        // Cheap sanity: the whole clock family must be inside the image. The bound
        // is kImageSize (per build) rather than a literal, because this one really
        // does move: it was 0x145B9000 on 3424 and 0x14268000 on 3446, and a stale
        // bound silently accepts addresses past the end of the module.
        // Co-op+: these are RVAs; the old `> kImageBase` made this false on every build, so the
        // read never ran. (On 3479 the ms field does not hold a clock either, live: the range
        // checks below refuse it, and local_capture estimates the clock instead.)
        return off::kClientClockGlobal != 0 && off::kClientFrameClockUs + 8 < off::kImageSize;
    }();
    if (!known) return std::nullopt;
    const auto ms = read<uint64_t>(base + off::kClientFrameClockMs);
    const auto us = read<uint64_t>(base + off::kClientFrameClockUs);
    const auto bias = read<int64_t>(base + off::kClientClockBias);
    if (!ms || *ms == 0 || !us || !bias) return std::nullopt;
    // Guard against a wrong slot that happens to hold non-zero junk. The 2976
    // comment describes the layout as "[ms] + [us] / 1000 while [ms] != 0", so the
    // microsecond field is a sub-second fraction, not a raw nanosecond count.
    // Accept both readings: either it is < 1e6 (a microsecond fraction) or it is
    // under 2^32 (a wider counter that still has to be sane). Bounds are loose on
    // purpose -- a wrong slot that also survives them only costs the feature, and
    // time_sync re-checks by requiring two reads to agree.
    if (*ms > 365ull * 24 * 60 * 60 * 1000) return std::nullopt;  // > a year of ms
    if (*us >= 1000000000ull) return std::nullopt;                // >= 1e9 is not a fraction
    return static_cast<uint64_t>(static_cast<int64_t>(*ms + *us / 1000) + *bias);
}

}  // namespace cdmp::game
