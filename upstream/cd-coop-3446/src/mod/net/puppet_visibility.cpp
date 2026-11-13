#include "mod/net/puppet_visibility.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <mutex>
#include <unordered_map>

#include <safetyhook.hpp>

#include "mod/game/actor_lookup.h"
#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/net/enemy_relay.h"
#include "mod/net/remote_puppet.h"
#include "mod/net/lag_probe.h"

namespace cdmp::net {
namespace {
namespace off = game::build_3663;

constexpr uint32_t kPuppetPlayer = 0xA010F001;
constexpr uint32_t kPuppetHorse = 0xB010F002;
constexpr uint16_t kLoaded = 0x10;

using FlagsChangedFn = void(__fastcall*)(uintptr_t status, uint64_t now, uint64_t before, float fade_s);

uintptr_t g_base = 0;
SafetyHookMid g_hook;
std::atomic<uint64_t> g_shown{0};
// Copies being watched, by actor object (a reloaded copy is a new object and is watched again):
// frames seen since it loaded. A copy is watched for kWatchFrames after loading, in case the
// bit arrives a little after the load, then left alone.
constexpr uint32_t kWatchFrames = 600;  // ~10 s
struct Watch {
    uint32_t key;
    uint32_t frames;
};
std::mutex g_mutex;
std::unordered_map<uintptr_t, Watch> g_watch;

// Co-op+ (F9): one actor of our world drawn hidden (the NPC opponent at a wrestling table while the other player's
// copy sits in its place): the hide bits the game itself uses, set through its own flag-change handler, cleared again
// when released. Bit 20 alone (a loading copy's) did not hide the seated NPC; with bit 2 (the "alpha 0" of hidden
// party members, enemy_relay.cpp) it is gone (live, 2026-11-03).
constexpr uint64_t kHideBit = 0x100004;
std::atomic<uint32_t> g_hide_key{0};
std::atomic<uintptr_t> g_hidden_actor{0};  // the actor we set the bit on (game thread)
std::atomic<uint64_t> g_hide_logs{0};

bool is_copy(uint32_t key) { return key == kPuppetPlayer || key == kPuppetHorse || is_enemy_copy_key(key); }

uintptr_t component(uintptr_t actor, uintptr_t vtable_rva) {
    uintptr_t list = 0;
    uint32_t n = 0;
    if (!game::safe_read(actor + 0x78, &list, 8) || !game::safe_read(actor + 0x80, &n, 4) || n > 128) return 0;
    for (uint32_t i = 0; i < n; ++i) {
        uintptr_t c = 0, vt = 0;
        if (game::safe_read(list + i * 16, &c, 8) && c && game::safe_read(c, &vt, 8) && vt == g_base + vtable_rva) {
            return c;
        }
    }
    return 0;
}

// Clears the bits and lets the game's handler recompute and apply the alpha.
bool show(uintptr_t status, uint64_t flags[2]) {
    const uint64_t before = flags[0] | flags[1];
    const uint64_t a = flags[0] & ~off::kHideFlagsOfCopies, b = flags[1] & ~off::kHideFlagsOfCopies;
    __try {
        *reinterpret_cast<volatile uint64_t*>(status + off::kStatus_HideFlags) = a;
        *reinterpret_cast<volatile uint64_t*>(status + off::kStatus_HideFlags + 8) = b;
        reinterpret_cast<FlagsChangedFn>(g_base + off::kStatusFlagsChanged)(status, a | b, before, 0.0f);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Sets (hide) or clears the bit through the game's own flag-change handler, as show() does.
bool set_hidden(uintptr_t actor, bool hide) {
    const uintptr_t status = component(actor, off::kVtStatusComponent);
    uint64_t flags[2] = {};
    if (!status || !game::safe_read(status + off::kStatus_HideFlags, flags, sizeof flags)) return false;
    const uint64_t before = flags[0] | flags[1];
    const uint64_t a = hide ? flags[0] | kHideBit : flags[0] & ~kHideBit;
    if (a == flags[0]) return true;
    __try {
        *reinterpret_cast<volatile uint64_t*>(status + off::kStatus_HideFlags) = a;
        reinterpret_cast<FlagsChangedFn>(g_base + off::kStatusFlagsChanged)(status, a | flags[1], before, 0.0f);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The hidden actor's own step: hide it, or show it again once released.
void hide_step(uintptr_t actor, uint32_t key) {
    const uint32_t want = g_hide_key.load();
    if (want && key == want) {
        if (g_hidden_actor.load() == actor) return;
        const bool ok = set_hidden(actor, true);
        if (ok) g_hidden_actor = actor;
        if (g_hide_logs.fetch_add(1) < 20) log::info("Puppet: 0x{:08X} hidden{}", key, ok ? "" : " -- FAILED");
    } else if (g_hidden_actor.load() == actor) {
        const bool ok = set_hidden(actor, false);
        g_hidden_actor = 0;
        if (g_hide_logs.fetch_add(1) < 20) log::info("Puppet: 0x{:08X} shown again{}", key, ok ? "" : " -- FAILED");
    }
}

// [dev] lag probe (research, 2026-11-06): where the copy's position buffer lives in its movement controller on this
// build (2976: mc+0x3D0, samples of 0xA0 bytes, ts at +0x98; movimiento-remoto-cliente.md §2). Once a second, every
// spot of the controller that looks like {u8 done | ptr samples | u32 count <= 30 | u32 capacity} is logged with its
// newest sample's stamp next to the newest one we handed the game.
void scan_copy_buffer(uintptr_t actor) {
    static std::atomic<uint32_t> frames{0};
    static std::atomic<int> logged{0};
    if (!puppet_last_injected_move_ts() || ++frames % 60 != 0 || logged >= 20) return;
    uintptr_t comps = 0, cc = 0, mc = 0, vt = 0;
    if (!game::safe_read(actor + off::kActor_ComponentTable, &comps, 8) || !comps ||
        !game::safe_read(comps + off::kComponents_CharacterControl, &cc, 8) || !cc ||
        !game::safe_read(cc, &vt, 8) || !game::safe_read(cc + off::kControl_MoveController, &mc, 8) || !mc) {
        return;
    }
    const uint64_t told = puppet_last_injected_move_ts();
    std::string found;
    for (uint32_t o = 0x100; o < 0x900; o += 8) {
        uintptr_t samples = 0;
        uint32_t cnt[2] = {};
        if (!game::safe_read(mc + o + 8, &samples, 8) || !game::safe_read(mc + o + 0x10, cnt, 8)) continue;
        if (samples < 0x10000 || cnt[0] == 0 || cnt[0] > 64 || cnt[1] < cnt[0] || cnt[1] > 256) continue;
        for (uint32_t size : {0xA0u, 0xA8u, 0xB0u, 0x90u, 0x98u, 0xC0u}) {
            for (uint32_t ts_at : {size - 8, size - 16, 0x98u}) {
                uint64_t ts = 0;
                if (ts_at + 8 > size || !game::safe_read(samples + (cnt[0] - 1) * size + ts_at, &ts, 8)) continue;
                if (told && ts > told - 5000 && ts < told + 5000) {
                    found += std::format(" | mc+0x{:X}: {} samples (cap {}), size 0x{:X}, ts at +0x{:X} = {} ({:+} vs ours)",
                                         o, cnt[0], cnt[1], size, ts_at, ts, static_cast<int64_t>(ts - told));
                }
            }
        }
    }
    ++logged;
    log::info("Dev buffer scan: copy controller 0x{:X} (CC vtable exe+0x{:X}), our newest stamp {}{}", mc, vt - g_base,
              told, found.empty() ? " | nothing like a position buffer" : found);
}

// ---- Co-op+ low latency: the other player's positions straight into their copy's buffers -----------------------
// The game draws a remote actor from two buffers in its movement controller (movimiento-remoto-cliente.md §2; 3576:
// position at mc+0x430, rotation at mc+0x4E8, both +0x60 from 2976; found with scan_copy_buffer): {u8 done | samples*
// | u32 count | u32 capacity}, samples of 0xA0 bytes (vec3 or quat at +0, ts at +0x98), at most 30 kept. Positions
// otherwise reach it only as MoveActorAcks, which we can hand over only inside the game's next OnRecvPacket (10 ms
// later at the median, 20-35 at the 90th percentile, then the main thread's next frame). Here, in the copy's own
// movement step, right before it reads them, every position fed since the last frame is appended by the game's own
// rules: only newer than the newest sample, not over a 1 s gap (the game's reset is left to its own insert), at most 30
// (the oldest shifted out), done cleared. The same positions still arrive as Acks, which the game then drops (not
// newer) after updating everything else they carry. Checked on first use (the newest sample must be one of ours);
// a mismatch turns this off for the session.
constexpr uint32_t kPosBuffer = 0x430, kRotBuffer = 0x4E8, kSampleSize = 0xA0, kSampleTs = 0x98, kMaxSamples = 30;
std::atomic<int> g_direct_state{0};  // 0 checking, 1 on, -1 off
std::atomic<uint64_t> g_direct_inserted{0}, g_direct_skipped{0};

bool guarded_write(uintptr_t dst, const void* src, size_t n) {
    __try {
        std::memcpy(reinterpret_cast<void*>(dst), src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool guarded_move(uintptr_t dst, uintptr_t src, size_t n) {
    __try {
        std::memmove(reinterpret_cast<void*>(dst), reinterpret_cast<const void*>(src), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// One sample (`n` value bytes at +0, ts) into the buffer at `buf`. False if it was not taken.
bool append_sample(uintptr_t buf, const void* value, size_t n, uint64_t ts, float min_change) {
    uintptr_t samples = 0;
    uint32_t cnt[2] = {};
    if (!game::safe_read(buf + 8, &samples, 8) || !game::safe_read(buf + 0x10, cnt, 8) || !samples) return false;
    const uint32_t count = cnt[0], cap = cnt[1];
    if (count == 0 || count >= cap || cap > 256) return false;
    uint8_t last[kSampleSize];
    if (!game::safe_read(samples + (count - 1) * kSampleSize, last, sizeof last)) return false;
    uint64_t last_ts = 0;
    std::memcpy(&last_ts, last + kSampleTs, 8);
    if (ts <= last_ts || ts - last_ts > 1000) return false;
    float d = 0;
    for (size_t i = 0; i < n / 4; ++i) {
        float a, b;
        std::memcpy(&a, last + i * 4, 4);
        std::memcpy(&b, static_cast<const uint8_t*>(value) + i * 4, 4);
        d += (a - b) * (a - b);
    }
    if (d <= min_change * min_change) return false;  // the game would not take it either
    uint8_t s[kSampleSize];
    std::memcpy(s, last, sizeof s);  // its Path (unused for drawing) and padding as the newest one has them
    std::memcpy(s, value, n);
    std::memcpy(s + kSampleTs, &ts, 8);
    if (!guarded_write(samples + count * kSampleSize, s, sizeof s)) return false;
    uint32_t c = count + 1;
    if (c > kMaxSamples) {
        if (!guarded_move(samples, samples + kSampleSize, static_cast<size_t>(c - 1) * kSampleSize)) return false;
        --c;
    }
    const uint8_t done = 0;
    return guarded_write(buf + 0x10, &c, 4) && guarded_write(buf, &done, 1);
}

void insert_direct(uintptr_t actor) {
    static thread_local std::vector<PuppetPose> poses;
    puppet_take_direct(poses);
    if (poses.empty() || g_direct_state.load() < 0) return;
    uintptr_t comps = 0, cc = 0, mc = 0, vt = 0;
    if (!game::safe_read(actor + off::kActor_ComponentTable, &comps, 8) || !comps ||
        !game::safe_read(comps + off::kComponents_CharacterControl, &cc, 8) || !cc || !game::safe_read(cc, &vt, 8) ||
        vt != g_base + off::kVtCharacterControl || !game::safe_read(cc + off::kControl_MoveController, &mc, 8) || !mc) {
        return;
    }
    if (g_direct_state.load() == 0) {
        // The newest position sample must be one we fed (an Ack's), within 2 s and 20 m of what we feed now.
        uintptr_t samples = 0;
        uint32_t cnt[2] = {};
        float pos[3] = {};
        uint64_t ts = 0;
        const PuppetPose& p = poses.front();
        const bool read = game::safe_read(mc + kPosBuffer + 8, &samples, 8) && game::safe_read(mc + kPosBuffer + 0x10, cnt, 8) &&
                          samples && cnt[0] > 0 && cnt[0] <= kMaxSamples && cnt[1] >= cnt[0] &&
                          game::safe_read(samples + (cnt[0] - 1) * kSampleSize, pos, sizeof pos) &&
                          game::safe_read(samples + (cnt[0] - 1) * kSampleSize + kSampleTs, &ts, 8);
        const float dx = pos[0] - p.pos[0], dz = pos[2] - p.pos[2];
        const bool ok = read && ts + 2000 > p.ts && ts < p.ts + 2000 && dx * dx + dz * dz < 20.0f * 20.0f;
        static std::atomic<int> tries{0};
        if (!ok) {
            if (++tries >= 50) {
                g_direct_state = -1;
                log::warn("Puppet: direct positions OFF: the copy's buffer is not where this build should have it "
                          "(read {}, {} samples, newest ts {} vs ours {})", read, cnt[0], ts, p.ts);
            }
            return;
        }
        g_direct_state = 1;
        log::info("Puppet: direct positions on (the copy's buffer at mc+0x{:X}: {} samples, newest {} ms before ours)",
                  kPosBuffer, cnt[0], static_cast<int64_t>(p.ts - ts));
    }
    for (const PuppetPose& p : poses) {
        const bool a = append_sample(mc + kPosBuffer, p.pos, sizeof p.pos, p.ts, 1e-4f);
        append_sample(mc + kRotBuffer, p.quat, sizeof p.quat, p.ts, 1e-5f);
        (a ? g_direct_inserted : g_direct_skipped).fetch_add(1, std::memory_order_relaxed);
    }
    static std::atomic<uint64_t> next_log{100};
    if (g_direct_inserted.load() >= next_log.load()) {
        next_log = next_log * 4;
        log::info("Puppet: direct positions: {} put in the copy's buffer, {} left to the Acks", g_direct_inserted.load(),
                  g_direct_skipped.load());
    }
}

void on_step(safetyhook::Context& ctx) {
    uintptr_t actor = 0;
    uint32_t key = 0;
    if (!game::safe_read(ctx.rcx + 0x20, &actor, 8) || !actor || !game::safe_read(actor + 0x60, &key, 4)) return;
    if (key == kPuppetPlayer) {
        if (lag_probe_on()) scan_copy_buffer(actor);
        if (puppet_direct_enabled()) insert_direct(actor);
    }
    if (g_hide_key.load() || g_hidden_actor.load()) hide_step(actor, key);
    if (!is_copy(key)) return;
    uint16_t state = 0;
    if (!game::safe_read(actor + 0x5E, &state, 2) || state != kLoaded) return;  // not loaded yet: next frame
    {
        std::lock_guard lock(g_mutex);
        if (g_watch.size() > 4096) g_watch.clear();
        auto [it, fresh] = g_watch.try_emplace(actor, Watch{key, 0});
        if (!fresh && it->second.key != key) it->second = {key, 0};  // the object was reused
        if (it->second.frames > kWatchFrames) return;
        ++it->second.frames;
    }
    const uintptr_t status = component(actor, off::kVtStatusComponent);
    uint64_t flags[2] = {};
    if (!status || !game::safe_read(status + off::kStatus_HideFlags, flags, sizeof flags)) return;
    if (!((flags[0] | flags[1]) & off::kHideFlagsOfCopies)) return;  // nothing hides it (yet)
    const bool ok = show(status, flags);
    if (g_shown.fetch_add(1) < 50) {
        log::info("Puppet: copy 0x{:08X} made visible (hide flags {:X} cleared){}", key, flags[0] | flags[1],
                  ok ? "" : " -- FAULTED");
    }
}

bool head_ok(uintptr_t rva, const uint8_t* head, size_t n) {
    uint8_t got[16];
    return n <= sizeof got && game::safe_read(g_base + rva, got, n) && std::memcmp(got, head, n) == 0;
}

}  // namespace

void puppet_hide_tick() {
    const uint32_t key = g_hide_key.load();
    if (!key) return;
    const auto actor = game::find_actor(key);
    if (!actor) return;
    const uintptr_t status = component(*actor, off::kVtStatusComponent);
    uint64_t flags[2] = {};
    if (!status || !game::safe_read(status + off::kStatus_HideFlags, flags, sizeof flags)) return;
    // The game clears bit 20 again within ~2 s (live, 2026-11-03), and the actor may not have existed yet when
    // puppet_hide_actor ran: set the bits whenever one is missing.
    if (((flags[0] | flags[1]) & kHideBit) != kHideBit && set_hidden(*actor, true)) g_hidden_actor = *actor;
}

uint32_t puppet_hidden_key() { return g_hidden_actor.load() ? g_hide_key.load() : 0; }

void puppet_hide_actor(uint32_t key) {
    const uint32_t before = g_hide_key.exchange(key);
    if (before == key) return;
    log::info("Puppet: {} 0x{:08X}", key ? "hiding" : "no longer hiding", key ? key : before);
    // A seated minigame NPC runs no movement step (2026-11-03): applied from here as well, through the same handler.
    if (key) {
        if (const auto actor = game::find_actor(key); actor && g_hidden_actor.load() != *actor) {
            const bool ok = set_hidden(*actor, true);
            if (ok) g_hidden_actor = *actor;
            log::info("Puppet: 0x{:08X} hidden{}", key, ok ? "" : " -- FAILED");
        }
    } else if (const uintptr_t actor = g_hidden_actor.exchange(0)) {
        // Only while it still exists (the game unspawns the opponent at the end of the match).
        const auto now = game::find_actor(before);
        const bool ok = now && *now == actor && set_hidden(actor, false);
        log::info("Puppet: 0x{:08X} {}", before, ok ? "shown again" : "gone already");
    }
}

bool puppet_direct_active() { return g_direct_state.load(std::memory_order_relaxed) == 1; }

bool puppet_visibility_install() {
    if (g_hook) return true;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uintptr_t slot2 = 0;
    if (g_base != off::kImageBase || !game::safe_read(g_base + off::kVtNormalMovementMethod + 2 * 8, &slot2, 8) ||
        slot2 != g_base + off::kMovementMethodStep ||
        !head_ok(off::kMovementMethodStep, off::kMovementMethodStepHead, sizeof off::kMovementMethodStepHead) ||
        !head_ok(off::kStatusFlagsChanged, off::kStatusFlagsChangedHead, sizeof off::kStatusFlagsChangedHead)) {
        log::warn("Puppet: visibility fix not recognised on this build; copies may stay invisible");
        return false;
    }
    g_hook = safetyhook::create_mid(reinterpret_cast<void*>(slot2), on_step);
    log::info("Puppet: visibility fix {}", g_hook ? "installed" : "FAILED to install");
    return static_cast<bool>(g_hook);
}

}  // namespace cdmp::net
