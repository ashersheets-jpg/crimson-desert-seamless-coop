// Co-op+ 2026-11-16 (build 1.0.0.3663): knowledge_grant.h.
#include "mod/session2/knowledge_grant.h"

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cstring>
#include <format>

#include <safetyhook.hpp>

#include "mod/game/game_fault.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/server.h"
#include "mod/session2/user2_stages.h"

namespace cdmp::session2 {
namespace {

// 3663 (the 3576 decomp + byte patterns, checked by their first bytes below):
//   kLearnCore  FUN 0x2E7C210(comp, int* err, const u16* key, u32 level, u8 fromType, i64, u32 a5, i64* a7, u8)
//               (3576 FUN_142e14c20): the learn itself, on the learner actor's server thread.
//   kLearnEntry the thunk 0x2E7E1F0 -> .xtls (3576 0x2E16BC0): int* (comp, int* err, const u16* key, i32 level, u32 a5,
//               u8 fromType, u64 a7); posts TrocTrPushKnowledgeOnceTimer 0x7FB to the comp's actor (0x2BADCF0 calls it
//               (comp, &err, &key, 1, 0, 2, 0)).
//   The knowledge comp: [[character + 0x68] + 0x150]; its +8 is the actor; +0x18 an array of 0x18-byte entries by key,
//   the level at +0 (horse_diag.cpp's user2_knowledge_share read it on the server).
//   SAM::FindActor / the scope release: as user2_stages.cpp (kFindActorFn ...).
constexpr uintptr_t kLearnCore = 0x2E7C210, kLearnEntry = 0x2E7E1F0;
constexpr uint8_t kLearnCoreHead[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x44, 0x89, 0x4C, 0x24, 0x20};
constexpr uint8_t kLearnEntryHead[] = {0xE9};
constexpr uintptr_t kFindActorFn = 0x2D78E40, kScopeDtorFn = 0x1659380, kLookupSlot = 0x72456D0, kVtScopeBase = 0x5B40000;
constexpr uint8_t kFindActorHead[] = {0x44, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10};
constexpr uint32_t kActorKey = 0x60;
constexpr uint32_t kActorComps = 0x68, kCompsKnowledge = 0x150, kKnowledgeArray = 0x18, kEntrySize = 0x18;

using LearnEntryFn = int*(__fastcall*)(uintptr_t comp, int* err, const uint16_t* key, int32_t level, uint32_t a5,
                                       uint8_t from, uint64_t a7);
using FindActorFn = void*(__fastcall*)(uintptr_t sam, void* scope, uint32_t key);
using ScopeDtorFn = void(__fastcall*)(void* scope);
struct alignas(16) ActorScope {
    uintptr_t q[4];
};

uintptr_t g_base = 0;
SafetyHookInline g_hook;
std::atomic<uint64_t> c_learns{0}, c_players{0}, c_granted{0}, c_had{0}, c_no_other{0}, c_faults{0}, c_from[8]{};
std::atomic<int> g_lines{0};

bool head_is(uintptr_t rva, const uint8_t* bytes, size_t n) {
    uint8_t b[16] = {};
    return n <= sizeof b && game::safe_read(g_base + rva, b, n) && std::memcmp(b, bytes, n) == 0;
}
bool server_thread() {
    __try {
        const auto tls = reinterpret_cast<uintptr_t*>(__readgsqword(0x58));
        return tls && tls[0] && *reinterpret_cast<const uint8_t*>(tls[0] + 0x1EB) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
int32_t level_of(uintptr_t comp, uint16_t key) {
    uintptr_t arr = 0;
    int32_t lvl = -1;
    if (!comp || !game::safe_read(comp + kKnowledgeArray, &arr, 8) || !arr) return -1;
    if (!game::safe_read(arr + static_cast<uintptr_t>(kEntrySize) * key, &lvl, 4)) return -1;
    return lvl;
}
uint32_t comp_actor_key(uintptr_t comp) {
    uintptr_t actor = 0;
    uint32_t k = 0;
    if (!game::safe_read(comp + 8, &actor, 8) || !actor || !game::safe_read(actor + kActorKey, &k, 4)) return 0;
    return k;
}
int grant_fault(EXCEPTION_POINTERS* ep) noexcept {
    c_faults.fetch_add(1, std::memory_order_relaxed);
    return game::game_fault_continue(ep, "KnowledgeGrant", "SAM::FindActor / the learn entry for the other player");
}
// The other player's server character: its knowledge comp, its level of `key`, and (when lower than `level`) the learn.
// 0 granted, 1 it had it already, 2 not found, 3 a fault.
int grant_other(uint32_t other, uint16_t key, int32_t level, uint32_t a5, uint8_t from, int32_t* had, int* err_out) {
    const uintptr_t lookup = [] {
        uintptr_t v = 0;
        game::safe_read(g_base + kLookupSlot, &v, 8);
        return v;
    }();
    uintptr_t sam = 0;
    if (!lookup || !game::safe_read(lookup, &sam, 8) || !sam) return 2;
    ActorScope sc{};
    int r = 2;
    __try {
        reinterpret_cast<FindActorFn>(g_base + kFindActorFn)(sam, &sc, other);
        const uintptr_t actor = sc.q[1];
        if ((sc.q[2] & 0xFF) && actor) {
            uint32_t k = 0;
            uintptr_t comps = 0, comp = 0;
            if (game::safe_read(actor + kActorKey, &k, 4) && k == other && game::safe_read(actor + kActorComps, &comps, 8) &&
                comps && game::safe_read(comps + kCompsKnowledge, &comp, 8) && comp) {
                *had = level_of(comp, key);
                if (*had >= level) {
                    r = 1;
                } else {
                    int err = 0;
                    const uint16_t k16 = key;
                    reinterpret_cast<LearnEntryFn>(g_base + kLearnEntry)(comp, &err, &k16, level, a5, from, 0);
                    *err_out = err;
                    r = 0;
                }
            }
        }
        sc.q[0] = g_base + kVtScopeBase;
        reinterpret_cast<ScopeDtorFn>(g_base + kScopeDtorFn)(&sc);
    } __except (grant_fault(GetExceptionInformation())) {
        r = 3;
    }
    return r;
}

int* __fastcall learn_detour(uintptr_t comp, int* err, const uint16_t* key, uint32_t level, uint8_t from, int64_t p6,
                             uint32_t a5, int64_t* a7, uint8_t p9) {
    uint16_t k = 0xFFFF;
    if (key) game::safe_read(reinterpret_cast<uintptr_t>(key), &k, 2);
    const int32_t before = k != 0xFFFF ? level_of(comp, k) : -1;
    int* const out = g_hook.unsafe_call<int*>(comp, err, key, level, from, p6, a5, a7, p9);
    c_learns.fetch_add(1, std::memory_order_relaxed);
    if (k == 0xFFFF || !server_thread()) return out;
    const uint32_t me = comp_actor_key(comp);
    if ((me >> 28) != 0xA) return out;
    const uint32_t host = user2_stages_host_key(), u2 = server_user2_player_key();
    if (!host || !u2 || (me != host && me != u2)) return out;
    c_players.fetch_add(1, std::memory_order_relaxed);
    c_from[from & 7].fetch_add(1, std::memory_order_relaxed);
    const int32_t after = level_of(comp, k);
    const int32_t lvl = after > 0 ? after : static_cast<int32_t>(level);
    if (after <= before && after < static_cast<int32_t>(level)) {
        // Not learned (refused, or already known at that level): nothing to share.
        if (g_lines.fetch_add(1) < 40)
            log::info("KnowledgeGrant: {} 0x{:08X} learn of 0x{:04X} level {} (from type {}) did not take (level {} -> {}, "
                      "err {})", me == host ? "the host" : "user 2", me, k, level, from, before, after, err ? *err : -1);
        return out;
    }
    const uint32_t other = me == host ? u2 : host;
    int32_t had = -1;
    int gerr = -1;
    const int r = grant_other(other, k, lvl, a5, from, &had, &gerr);
    if (r == 0) c_granted.fetch_add(1, std::memory_order_relaxed);
    if (r == 1) c_had.fetch_add(1, std::memory_order_relaxed);
    if (r == 2) c_no_other.fetch_add(1, std::memory_order_relaxed);
    if (g_lines.fetch_add(1) < 200)
        log::info("KnowledgeGrant: {} 0x{:08X} learned 0x{:04X} level {} (from type {}, level {} -> {}) -> {} 0x{:08X}: {} "
                  "(its level {}{})", me == host ? "the host" : "user 2", me, k, level, from, before, after,
                  me == host ? "user 2" : "the host", other,
                  r == 0 ? "GRANTED" : r == 1 ? "already had it" : r == 2 ? "not found on the server" : "FAULT", had,
                  r == 0 ? std::format(", learn entry err {}", gerr) : std::string());
    return out;
}

}  // namespace

void knowledge_grant_start(bool on, bool guest) {
    if (!on || guest || g_hook) return;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const bool core = head_is(kLearnCore, kLearnCoreHead, sizeof kLearnCoreHead);
    const bool entry = head_is(kLearnEntry, kLearnEntryHead, sizeof kLearnEntryHead);
    const bool find = head_is(kFindActorFn, kFindActorHead, sizeof kFindActorHead);
    if (!core || !entry || !find) {
        log::warn("KnowledgeGrant NOT installed (knowledge_grant=1, host): the code is not the expected one (learn 0x{:X} {}, "
                  "entry 0x{:X} {}, FindActor 0x{:X} {}): another game build?", kLearnCore, core ? "ok" : "DIFFERENT",
                  kLearnEntry, entry ? "ok" : "DIFFERENT", kFindActorFn, find ? "ok" : "DIFFERENT");
        return;
    }
    g_hook = safetyhook::create_inline(reinterpret_cast<void*>(g_base + kLearnCore), reinterpret_cast<void*>(&learn_detour));
    log::warn("KnowledgeGrant {} (knowledge_grant=1, host): a knowledge one player's character learns on the server is "
              "learned by the other player's character too (the game's own learn entry; never twice)",
              g_hook ? "ON" : "NOT installed (the hook failed)");
}

std::string knowledge_grant_describe() {
    if (!g_hook) return {};
    return std::format("KnowledgeGrant: learns {} (the players' {}: from types 0-7 {} {} {} {} {} {} {} {}) | granted to the "
                       "other {}, it had them {}, other not found {} | faults {}",
                       c_learns.load(), c_players.load(), c_from[0].load(), c_from[1].load(), c_from[2].load(),
                       c_from[3].load(), c_from[4].load(), c_from[5].load(), c_from[6].load(), c_from[7].load(),
                       c_granted.load(), c_had.load(), c_no_other.load(), c_faults.load());
}

}  // namespace cdmp::session2
