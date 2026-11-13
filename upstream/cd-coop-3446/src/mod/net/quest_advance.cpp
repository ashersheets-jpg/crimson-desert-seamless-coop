#include "mod/net/quest_advance.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <safetyhook.hpp>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/net/coop_worlds.h"
#include "mod/net/local_capture.h"
#include "mod/net/quest_live.h"
#include "mod/net/troc_sniffer.h"

namespace cdmp::net {
namespace {
namespace off = game::build_3663;

// exe+0x1C3F0F0(manager, u32* mission id, ctx, u8 state, changes*): the server queues a mission state change.
// Called for every mission while a save loads, so the manager is known before anyone needs it.
constexpr uintptr_t kSetMissionState = 0x1CA3DE0;  // 3549: 0x1C3F0F0
constexpr uint8_t kSetMissionStateHead[16] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x44, 0x88, 0x4C,
                                              0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55};
// exe+0x2B95BE0: the quest advancement routine (quest_advance.h).
constexpr uintptr_t kAdvance = 0x2BFC3D0;  // 3549: 0x2B95BE0
constexpr uint8_t kAdvanceHead[16] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57,
                                      0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
// MissionTriggerInOutReq's handler (runs on the quest server's thread).
constexpr uintptr_t kTriggerHandler = 0x2F576B0;  // 3549: 0x2EEE800
constexpr uint8_t kTriggerHandlerHead[16] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
                                             0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x4C};
// The mission static data table (exe+0x4DBA40 reads it): [+0x08] u32 count, [+0x58] info* per index (null until
// loaded); info +0x08 = the mission key (as in UpdateMissionListAck).
constexpr uintptr_t kMissionTable = 0x7248520;  // 3549: 0x71C1940
constexpr uint64_t kRequestMs = 10000;
// Co-op+ shared XP (2026-11-07, Ghidra on 3576): the server's own "add sub-level experience" timer handler,
// ProcessSubLevelIncreaseExperienceOnceTimer (2069; its TrocTr vtable 0x5EFDF30, slot 2). From its message it reads
// u32 actor at +3 (0 = the user itself), u32 sub-level key at +7 (521 = Kliff's skill points; the game turns the key
// into its index), u64 amount at +0x0B, u8 flag at +0x13 (only echoed in AddSubLevelExperienceAck), takes the game's
// lock and calls the experience routine exe+0x2EB4180 -> AddSubLevelExperienceAck to our client. Its context: [0] the
// user, [3] the message. Called here on the quest server's thread with MissionTriggerInOutReq's user.
constexpr uintptr_t kSubLevelTimerHandler = 0x2F41420;
constexpr uint8_t kSubLevelTimerHead[16] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57,
                                            0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
using SubLevelTimerFn = uint32_t*(__fastcall*)(void* handler, uint32_t* out, int64_t* ctx);
// The server's actor lookup, as MissionTriggerInOutReq's handler uses it (Ghidra, 3576): exe+0x2D11AA0(manager,
// handle*, u32 key) with manager = [[exe+0x71C0610] + 0x50]; the handle: +0x08 the actor, +0x10 u8 found; released by
// setting +0x00 to exe+0x5AC58E0 and calling exe+0x1618660(handle). The timer handler's "user" is that actor (its key
// at +0x60, its components at +0x68).
constexpr uintptr_t kActorLookup = 0x2D78A00, kActorManagerGlobal = 0x72456D8, kHandleRelease = 0x1659380,
                    kHandleReleaseTag = 0x5B40000;
using ActorLookupFn = void*(__fastcall*)(void* manager, void* handle, uint32_t key);
using HandleReleaseFn = void(__fastcall*)(void* handle);
struct XpGrant {
    uint32_t key;
    uint64_t amount;
    uint64_t at;
};

using AdvanceFn = void*(__fastcall*)(void* manager, uint32_t* result, uint16_t* index, uint64_t* time, uint8_t flag);

struct Request {
    uint32_t key;
    uint64_t at;
    bool checked;
    uint32_t world_epoch;  // Plitos: a request from before a co-op world switch is never applied in the new one
};

uintptr_t g_base = 0;
SafetyHookMid g_state_hook, g_trigger_hook;
std::atomic<uintptr_t> g_manager{0};
std::mutex g_m;
std::deque<Request> g_queue;
std::deque<XpGrant> g_xp;           // under g_m
bool g_xp_site_ok = false;          // the timer handler's code checked at install
std::atomic<uint64_t> g_xp_granted{0}, g_xp_failed{0};

bool head_is(uintptr_t rva, const uint8_t* want, size_t n) {
    uint8_t head[16] = {};
    return n <= sizeof head && game::safe_read(g_base + rva, head, n) && std::memcmp(head, want, n) == 0;
}

void on_set_state(safetyhook::Context& ctx) { g_manager.store(ctx.rcx, std::memory_order_relaxed); }

// The index of the loaded mission with this key, or -1.
int mission_index(uint32_t key) {
    uintptr_t table = 0, arr = 0;
    uint32_t count = 0;
    if (!game::safe_read(g_base + kMissionTable, &table, 8) || !game::safe_read(table + 8, &count, 4) ||
        !game::safe_read(table + 0x58, &arr, 8) || count > 0x10000) {
        return -1;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t info = 0;
        uint32_t k = 0;
        if (game::safe_read(arr + i * 8ull, &info, 8) && info && game::safe_read(info + 8, &k, 4) && k == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool call_lookup_guarded(ActorLookupFn fn, void* manager, void* handle, uint32_t key) {
    __try {
        fn(manager, handle, key);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void call_release_guarded(HandleReleaseFn fn, void* handle) {
    __try {
        fn(handle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// The game's handler, called with SEH around it: no C++ objects in here.
bool call_timer_guarded(SubLevelTimerFn fn, void* handler, uint32_t* out, int64_t* ctx) {
    __try {
        fn(handler, out, ctx);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Quest server thread: the queued experience grants, through the game's own timer handler.
void run_xp_grants(safetyhook::Context& ctx) {
    std::vector<XpGrant> grants;
    {
        std::lock_guard lock(g_m);
        if (g_xp.empty()) return;
        const uint64_t now = GetTickCount64();
        for (const auto& g : g_xp) {
            if (now - g.at <= kRequestMs) grants.push_back(g);
        }
        g_xp.clear();
    }
    // Our own player's actor on this server (looked up as the game does), its handle released afterwards.
    alignas(16) uint8_t handle[0x40] = {};
    uintptr_t global = 0, manager = 0;
    const uint32_t own = capture_own_player_key();
    int64_t user = 0;
    if (g_xp_site_ok && own && game::safe_read(g_base + kActorManagerGlobal, &global, 8) && global &&
        game::safe_read(global + 0x50, &manager, 8) && manager &&
        call_lookup_guarded(reinterpret_cast<ActorLookupFn>(g_base + kActorLookup), reinterpret_cast<void*>(manager),
                            handle, own) &&
        handle[0x10]) {
        std::memcpy(&user, handle + 8, 8);
    }
    (void)ctx;
    if (!user) {
        g_xp_failed += grants.size();
        log::warn("XP: {} grant(s) not made (the server's experience timer or its user is not available)", grants.size());
        return;
    }
    const auto fn = reinterpret_cast<SubLevelTimerFn>(g_base + kSubLevelTimerHandler);
    for (const auto& g : grants) {
        alignas(8) uint8_t msg[0x20] = {};
        const uint16_t op = 2069;
        std::memcpy(msg, &op, 2);
        std::memcpy(msg + 7, &g.key, 4);
        std::memcpy(msg + 0x0B, &g.amount, 8);
        int64_t fake[8] = {user, 0, 0, reinterpret_cast<int64_t>(msg), 0, 0, 0, 0};
        uint32_t out = 0xFFFFFFFF;
        const bool ran = call_timer_guarded(fn, reinterpret_cast<void*>(ctx.rcx), &out, fake);
        (ran && out == 0 ? g_xp_granted : g_xp_failed) += 1;
        log::info("XP: the server's experience timer for sub-level {} +{}: {}", g.key, g.amount,
                  !ran ? "FAULTED (caught)" : out == 0 ? "done" : "refused (code " + std::to_string(out) + ")");
    }
    const uintptr_t tag = g_base + kHandleReleaseTag;
    std::memcpy(handle, &tag, 8);
    call_release_guarded(reinterpret_cast<HandleReleaseFn>(g_base + kHandleRelease), handle);
}

void on_trigger(safetyhook::Context& ctx) {
    run_xp_grants(ctx);
    std::vector<std::pair<uint32_t, bool>> keys;
    {
        std::lock_guard lock(g_m);
        if (g_queue.empty()) return;
        const uint64_t now = GetTickCount64();
        const uint32_t epoch = worlds_switch_epoch();
        for (const auto& r : g_queue) {
            if (r.world_epoch == epoch && now - r.at <= kRequestMs) keys.emplace_back(r.key, r.checked);
        }
        g_queue.clear();
    }
    const uintptr_t mgr = g_manager.load();
    for (const auto& [key, checked] : keys) {
        if (checked && !quest_live_can_advance(key)) continue;  // re-checked just before: still under way here, in the world
        const int idx = mission_index(key);
        if (!mgr || idx < 0) {
            log::warn("Live quests: step {:08X} not advanced (quest data not found here)", key);
            continue;
        }
        uint32_t result = 0;
        uint16_t index = static_cast<uint16_t>(idx);
        uint64_t when = 0;  // the game's clock time; 0 was accepted for go-to, defeat and talk steps
        reinterpret_cast<AdvanceFn>(g_base + kAdvance)(reinterpret_cast<void*>(mgr), &result, &index, &when, 0);
        log::info("Live quests: the quest server advanced step {:08X} (index {}, result {:X})", key, idx, result);
    }
}

}  // namespace

bool server_trigger_hook_install() {
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] {
        g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (g_base != off::kImageBase || !head_is(kTriggerHandler, kTriggerHandlerHead, sizeof kTriggerHandlerHead)) {
            log::warn("XP: the quest server's request handler is not as expected; no experience grants");
            return;
        }
        g_xp_site_ok = head_is(kSubLevelTimerHandler, kSubLevelTimerHead, sizeof kSubLevelTimerHead);
        g_trigger_hook = safetyhook::create_mid(reinterpret_cast<void*>(g_base + kTriggerHandler), on_trigger);
        ok = static_cast<bool>(g_trigger_hook);
        log::info("XP: server-thread hook {}; the experience timer {}", ok ? "installed" : "FAILED",
                  g_xp_site_ok ? "found" : "NOT found");
    });
    return ok;
}

bool quest_advance_install() {
    if (g_state_hook) return true;
    if (!server_trigger_hook_install()) return false;
    if (g_base != off::kImageBase || !head_is(kSetMissionState, kSetMissionStateHead, sizeof kSetMissionStateHead) ||
        !head_is(kAdvance, kAdvanceHead, sizeof kAdvanceHead)) {
        log::warn("Live quests: the quest server's code is not as expected; steps other than go-to will not follow");
        return false;
    }
    g_state_hook = safetyhook::create_mid(reinterpret_cast<void*>(g_base + kSetMissionState), on_set_state);
    const bool ok = g_state_hook && g_trigger_hook;
    log::info("Live quests: quest server hooks {}", ok ? "installed" : "FAILED");
    return ok;
}

bool quest_advance_ready() { return g_trigger_hook && g_manager.load() != 0; }

namespace {
void wake_server() {
    // A harmless trigger (mission 0) so the server's handler runs soon.
    std::vector<uint8_t> pkt(5 + 9, 0);
    const uint16_t op = 3106, len = 9;
    std::memcpy(pkt.data(), &op, 2);
    std::memcpy(pkt.data() + 3, &len, 2);
    sniffer_inject_c2s(std::move(pkt));
}
}  // namespace

bool quest_advance_grant_xp(uint32_t sublevel_key, uint64_t amount) {
    if (!server_trigger_hook_install() || !g_xp_site_ok || amount == 0) return false;
    {
        std::lock_guard lock(g_m);
        if (g_xp.size() >= 64) return false;
        g_xp.push_back(XpGrant{sublevel_key, amount, GetTickCount64()});
    }
    wake_server();
    return true;
}

uint64_t quest_advance_xp_granted() { return g_xp_granted.load(); }

void quest_advance_request(uint32_t mission_key, bool checked) {
    {
        std::lock_guard lock(g_m);
        if (g_queue.size() >= 32) return;
        g_queue.push_back(Request{mission_key, GetTickCount64(), checked, worlds_switch_epoch()});
    }
    // A harmless trigger (mission 0) so the server's handler runs soon.
    std::vector<uint8_t> pkt(5 + 9, 0);
    const uint16_t op = 3106, len = 9;
    std::memcpy(pkt.data(), &op, 2);
    std::memcpy(pkt.data() + 3, &len, 2);
    sniffer_inject_c2s(std::move(pkt));
}

}  // namespace cdmp::net
