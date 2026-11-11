// Co-op+ (2026-11-10): lootr.h.
#include "mod/session2/lootr.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#include <safetyhook.hpp>

#include "mod/game/safe_memory.h"
#include "mod/net/remote_puppet.h"
#include "mod/net/troc_sniffer.h"
#include "mod/log.h"
#include "mod/session2/anti_grief_logic.h"
#include "mod/session2/lootr_logic.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

constexpr uint32_t kHostCharacter = 0xA0100001;
constexpr uint32_t kLootrGiveEntry = 0xC0DE0005;  // trade_probe's worker: add an item to the requester
constexpr uint64_t kGatherMs = 1700;              // the 3073 -> pickup delay the game shows (tcB324: ~1.76 s)
constexpr uint64_t kKeepMs = 30 * 60 * 1000;      // an entry the guest never takes is forgotten after 30 min

bool g_on = false, g_probe = false;
safetyhook::InlineHook g_pickup;
std::atomic<uint64_t> g_watch_until{0};
std::atomic<int> g_logged{0};
std::atomic<bool> g_kill_hits{false};  // [dev] lootr_kill_hits

// ---- the picker's bag (manager +0xA2): a gather pickup's item is made inside the pickup and added a few ms after
// the worker returns (tcB325), so the bag is compared again later (tick / the host's 3110).
struct BagSnap {
    uint16_t idx[200];
    int64_t n[200];
    int slots = 0;
};
bool snap_bag(uintptr_t picker, BagSnap& b) noexcept {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        const uintptr_t slot = *reinterpret_cast<uintptr_t*>(base + 0x71C0498);
        const uintptr_t root = *reinterpret_cast<uintptr_t*>(slot);
        const uintptr_t info = *reinterpret_cast<uintptr_t*>(root + 8);
        const uintptr_t mgr = *reinterpret_cast<uintptr_t*>(info + 0x240);
        const uint16_t bag = *reinterpret_cast<uint16_t*>(mgr + 0xA2);
        const uintptr_t holder = reinterpret_cast<uintptr_t (__fastcall*)(uintptr_t)>(base + 0x22F1DC0)(picker);
        if (!holder) return false;
        const uintptr_t list = *reinterpret_cast<uintptr_t*>(holder + 0x18);
        const uint32_t cnt = *reinterpret_cast<uint32_t*>(holder + 0x20);
        for (uint32_t k = 0; k < cnt && k < 64; ++k) {
            const uintptr_t inv = *reinterpret_cast<uintptr_t*>(list + k * 8);
            if (!inv || *reinterpret_cast<int16_t*>(inv + 0x10) != static_cast<int16_t>(bag)) continue;
            const uintptr_t first = *reinterpret_cast<uintptr_t*>(inv);
            const int16_t slots = *reinterpret_cast<int16_t*>(inv + 8);
            b.slots = slots < 0 ? 0 : (slots > 200 ? 200 : slots);
            for (int i = 0; i < b.slots; ++i) {
                const uintptr_t e = first + static_cast<uintptr_t>(i) * 200;
                b.idx[i] = *reinterpret_cast<uint16_t*>(e + 8);
                b.n[i] = *reinterpret_cast<int64_t*>(e + 0x10);
            }
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}
// The item row (exe+0x3DD8A0): its key at +8 (tcB326: idx 0x50D -> key 0x0B8D0E).
uint32_t item_key_of(uint16_t idx) noexcept {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        const uintptr_t row = reinterpret_cast<uintptr_t (__fastcall*)(uint64_t, uint32_t)>(base + 0x3DD8A0)(0, idx);
        return row ? *reinterpret_cast<uint32_t*>(row + 8) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// ---- the table: gimmicks the HOST took that the guest has not (phase 1: the host takes first).
struct Taken {
    uint32_t g = 0;
    uint8_t key16[16] = {};
    uintptr_t picker = 0;
    uint64_t at = 0;
    BagSnap before;
    bool item_known = false;
    uint16_t item_idx = 0xFFFF;
    uint32_t item_key = 0;
    int64_t count = 0;
    std::vector<std::vector<uint8_t>> held;  // what the guest was not shown, to replay
    int state = 0;                           // 0 waiting for the other player, 1 its gather runs, 2 done
    uint64_t step_at = 0;
    uint32_t requester = 0;
    bool host_waits = false;                 // phase 2: the GUEST took it first, the host takes its copy later
};
std::mutex g_mu;
std::vector<Taken> g_taken;  // guarded by g_mu
struct DevBody {
    uint32_t key;
    uint64_t at;
    int step;  // 0 killed, 1 the host asked to loot, 2 the guest asked too
};
std::vector<DevBody> g_dev_bodies;  // guarded by g_mu ([dev] lootr_kill_hits)

// ---- phase 2a: gimmicks the GUEST took first, kept for the host. Written on the server thread, read in the host
// client's OnRecvPacket (the game's thread: no lock, no allocation, no log there), so fixed slots and atomics.
struct KeepHost {
    std::atomic<uint32_t> g{0};
    uint8_t key16[16] = {};
    std::atomic<uint64_t> at{0};
};
KeepHost g_keep[32];
std::atomic<uint32_t> g_keep_next{0};
// The originals of what the host's client was not shown (for a replay when the host takes it).
struct HostHeld {
    std::atomic<uint32_t> g{0};
    uint16_t size = 0;
    uint8_t bytes[640] = {};
};
HostHeld g_host_held[64];
std::atomic<uint32_t> g_hh_next{0};
std::atomic<uint64_t> c_host_blanked{0}, c_guest_first{0};

void keep_for_host(uint32_t g, const uint8_t* key16) noexcept {
    KeepHost& k = g_keep[g_keep_next.fetch_add(1) % 32];
    k.g.store(0);
    std::memcpy(k.key16, key16, 16);
    k.at.store(GetTickCount64());
    k.g.store(g);
}
void host_held_copy(uint32_t g, const uint8_t* packet, uint16_t size) noexcept {
    if (size > sizeof(HostHeld::bytes)) return;
    HostHeld& h = g_host_held[g_hh_next.fetch_add(1) % 64];
    h.g.store(0);
    std::memcpy(h.bytes, packet, size);
    h.size = size;
    h.g.store(g);
}
std::atomic<uint64_t> c_taken{0}, c_held{0}, c_given{0};

struct Seen {
    uint32_t picker = 0, gimmick = 0;
    uint16_t idx = 0xFFFF;
    int64_t count = 0;
    uint8_t key16[16] = {};
};
void read_pickup(Seen& s, uintptr_t comp, uintptr_t picker) noexcept {
    __try {
        if (picker) s.picker = *reinterpret_cast<uint32_t*>(picker + 0x60);
        const uintptr_t actor = *reinterpret_cast<uintptr_t*>(comp + 8);
        if (actor) s.gimmick = *reinterpret_cast<uint32_t*>(actor + 0x60);
        const uintptr_t rec = *reinterpret_cast<uintptr_t*>(comp + 0xC0);
        if (rec) {
            s.idx = *reinterpret_cast<uint16_t*>(rec + 8);
            s.count = *reinterpret_cast<int64_t*>(rec + 0x10);
        }
        std::memcpy(s.key16, reinterpret_cast<void*>(comp + 0x30), 16);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

int* __fastcall pickup_detour(uintptr_t comp, int* err, uintptr_t picker, uint64_t kind) noexcept {
    Seen s;
    read_pickup(s, comp, picker);
    // Only things that MAKE their item when taken (plants, herbs, nodes: comp+0xC0 empty before the pickup). A
    // concrete item lying in the world (comp+0xC0 set: something a player dropped, a drop thrown on the ground) is
    // one object: per player it would be a duplication (drop it, the friend takes it, take "your copy").
    const bool concrete = s.idx != 0xFFFF;
    const bool guest_first = g_on && !concrete && s.picker != kHostCharacter && (s.picker >> 28) == 0xA && s.gimmick;
    if (guest_first) {
        keep_for_host(s.gimmick, s.key16);  // phase 2a: the guest took it first; the host still sees it
        c_guest_first.fetch_add(1);
    }
    if (g_on && !concrete && (s.picker == kHostCharacter || guest_first) && s.gimmick) {
        try {
            Taken t;
            t.g = s.gimmick;
            std::memcpy(t.key16, s.key16, 16);
            t.picker = picker;
            t.host_waits = guest_first;
            t.at = GetTickCount64();
            snap_bag(picker, t.before);
            std::lock_guard lk(g_mu);
            if (g_taken.size() < 512) g_taken.push_back(std::move(t));
            c_taken.fetch_add(1);
        } catch (...) {
        }
    }
    int* r = g_pickup.call<int*>(comp, err, picker, kind);
    int e = -1;
    if (err) game::fast_read(reinterpret_cast<uintptr_t>(err), &e, 4);
    if ((g_probe || g_on) && g_logged.fetch_add(1) < 40) {
        log::info("Lootr: pickup by 0x{:08X} kind {} gimmick 0x{:08X} item idx 0x{:X} x{} key {:02X}{:02X}{:02X}{:02X}"
                  "{:02X}{:02X}{:02X}{:02X} -> err {}{}",
                  s.picker, kind & 0xFF, s.gimmick, s.idx, s.count, s.key16[0], s.key16[1], s.key16[2], s.key16[3],
                  s.key16[4], s.key16[5], s.key16[6], s.key16[7], e,
                  !g_on ? "" : concrete ? " | a concrete item: one for whoever takes it"
                  : s.picker == kHostCharacter ? " | kept for the guest (Lootr)" : " | kept for the host (Lootr)");
    }
    g_watch_until = GetTickCount64() + 3000;
    return r;
}

// ---- bodies (probe first; build/decomp/notes_lootr_bodies_chests.md, local): the loot worker
// exe+0x2E224F0(lootComp, int* err, looter, u8 a, u8 b). comp+8 = the body, comp+0x6C = rolled, pool comp+0x58 /
// count comp+0x60, sub list count comp+0x50.
safetyhook::InlineHook g_body_loot;
struct BodySeen {
    uint32_t body = 0, looter = 0;
    uint8_t rolled = 0xFF;
    uint32_t pool = 0, sub = 0;
};
void read_body(BodySeen& s, uintptr_t comp, uintptr_t looter) noexcept {
    __try {
        const uintptr_t body = *reinterpret_cast<uintptr_t*>(comp + 8);
        if (body) s.body = *reinterpret_cast<uint32_t*>(body + 0x60);
        if (looter) s.looter = *reinterpret_cast<uint32_t*>(looter + 0x60);
        s.rolled = *reinterpret_cast<uint8_t*>(comp + 0x6C);
        s.pool = *reinterpret_cast<uint32_t*>(comp + 0x60);
        s.sub = *reinterpret_cast<uint32_t*>(comp + 0x50);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
// ---- bodies, per player ("re-arm the roll", notes_lootr_bodies_chests.md A.4; live facts tcB347: the pool is already
// rolled when the first player loots, the first take empties its lootable entries and the second player gets nothing).
// After the FIRST player's take the rolled flag comp+0x6C goes back to 0, so the second player's take rolls again
// (the game's own roll, for that player) and takes; the entries that second roll added for the other kinds are then
// removed again (swap-remove exe+0x230FDA0 keeps the old entries below the old count) and the list is sent again
// (exe+0x2E25FC0). Meanwhile the leftover list 2414 of the first take is kept from the other player, so the body
// still shows its loot for them. A player who looted a body is refused a second time.
bool g_bodies_on = false;
struct BodyEntry {
    uint32_t body = 0;
    uintptr_t comp = 0;
    uint32_t first = 0;  // the first looter's character key
    bool both = false;
    uint64_t at = 0;
};
std::vector<BodyEntry> g_bodies;  // guarded by g_mu
// Bodies whose 2414 the HOST's client must not take yet (the guest looted first): read on the game's thread.
std::atomic<uint32_t> g_body_keep_host[32];
std::atomic<uint32_t> g_body_keep_next{0};
std::atomic<uint64_t> c_body_first{0}, c_body_second{0}, c_body_refused{0}, c_body_2414{0};
thread_local bool t_keep_body = false;
safetyhook::MidHook g_keep_body_hook;
alignas(16) uint8_t g_zero_row[0x40] = {};
void on_keep_body(safetyhook::Context& ctx) {  // exe+0x2E22885: rax = the character row, dil = a
    if (!t_keep_body) return;
    ctx.rax = reinterpret_cast<uintptr_t>(g_zero_row);  // row+0x1A reads 0: the game's own "not removed" branch
    ctx.rdi &= ~static_cast<uintptr_t>(0xFF);
}
void body_keep_host(uint32_t body, bool on) noexcept {
    if (on) {
        g_body_keep_host[g_body_keep_next.fetch_add(1) % 32].store(body);
    } else {
        for (auto& k : g_body_keep_host) {
            if (k.load() == body) k.store(0);
        }
    }
}
// The duplicates a second roll appended (index >= n), from the last one down; the sub list count back to s; the
// list sent again. False on an exception.
bool body_trim(uintptr_t comp, uint32_t n, uint32_t s, uint32_t* removed) noexcept {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        uint32_t count = *reinterpret_cast<uint32_t*>(comp + 0x60);
        while (count > n && count < 4096) {
            reinterpret_cast<void (__fastcall*)(uintptr_t, uint32_t)>(base + 0x230FDA0)(comp + 0x58, count - 1);
            const uint32_t now = *reinterpret_cast<uint32_t*>(comp + 0x60);
            if (now >= count) break;  // nothing went: stop
            count = now;
            ++*removed;
        }
        if (*reinterpret_cast<uint32_t*>(comp + 0x50) > s) *reinterpret_cast<uint32_t*>(comp + 0x50) = s;
        reinterpret_cast<void (__fastcall*)(uintptr_t)>(base + 0x2E25FC0)(comp);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool write_rolled(uintptr_t comp, uint8_t v) noexcept {
    __try {
        *reinterpret_cast<volatile uint8_t*>(comp + 0x6C) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
int64_t bag_total(uintptr_t actor) noexcept {
    static BagSnap b;  // the server thread only
    if (!snap_bag(actor, b)) return -1;
    int64_t t = 0;
    for (int i = 0; i < b.slots; ++i) {
        if (b.idx[i] != 0xFFFF && b.n[i] > 0) t += b.n[i];
    }
    return t;
}

int* body_loot_per_player(uintptr_t comp, int* err, uintptr_t looter, uint8_t a, uint8_t b, bool* handled) noexcept {
    *handled = false;
    BodySeen s;
    read_body(s, comp, looter);
    const uint32_t guest = server_user2_player_key();
    if (!g_bodies_on || !err || !s.body || !guest || (s.looter != kHostCharacter && s.looter != guest)) return nullptr;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    int role = 0;  // 1 first looter, 2 second, 3 a repeat
    try {
        std::lock_guard lk(g_mu);
        const uint64_t now = GetTickCount64();
        std::erase_if(g_bodies, [&](const BodyEntry& e) { return now - e.at > kKeepMs; });
        BodyEntry* found = nullptr;
        for (BodyEntry& e : g_bodies) {
            if (e.body == s.body && e.comp == comp) found = &e;
        }
        if (!found) {
            role = 1;
        } else if (found->both || found->first == s.looter) {
            role = 3;
        } else {
            role = 2;
        }
    } catch (...) {
        return nullptr;
    }
    *handled = true;
    if (role == 3) {
        int none = 0;
        game::fast_read(base + 0x714BFA0, &none, sizeof none);  // "nothing to loot"
        *err = none ? none : 1;
        c_body_refused.fetch_add(1);
        return err;
    }
    const int64_t bag_before = bag_total(looter);
    if (role == 1) {
        // The other player keeps seeing the loot: its copy of the leftover list is kept back from now on.
        try {
            std::lock_guard lk(g_mu);
            if (g_bodies.size() < 256) g_bodies.push_back({s.body, comp, s.looter, false, GetTickCount64()});
        } catch (...) {
        }
        if (s.looter != kHostCharacter) body_keep_host(s.body, true);
        t_keep_body = true;
        int* r = g_body_loot.call<int*>(comp, err, looter, a, b);
        t_keep_body = false;
        BodySeen after;
        read_body(after, comp, 0);
        const bool took = *err == 0 && after.body == s.body && after.pool < s.pool;
        if (took) {
            write_rolled(comp, 0);  // the re-arm
            c_body_first.fetch_add(1);
        } else {  // nothing taken (or the body went): no entry, the next loot is a first one again
            try {
                std::lock_guard lk(g_mu);
                std::erase_if(g_bodies, [&](const BodyEntry& e) { return e.body == s.body && e.comp == comp; });
            } catch (...) {
            }
            body_keep_host(s.body, false);
        }
        static std::atomic<int> said{0};
        if (said.fetch_add(1) < 30)
            log::info("Lootr: body 0x{:08X} looted first by 0x{:08X}: pool {} -> {}, bag {:+d}, err {}{}", s.body, s.looter,
                      s.pool, after.pool, static_cast<int>(bag_total(looter) - bag_before), *err,
                      took ? " | kept for the other player (re-armed)" : " | nothing taken, not kept");
        return r;
    }
    // role 2: the game's own second roll and take, then the duplicates of the other kinds go again. Nothing is kept
    // back from this player any more, so the take's own list and the corrected one reach its client (tcB350: the
    // corrected list was still dropped for the guest).
    auto mark = [&](bool both) {
        try {
            std::lock_guard lk(g_mu);
            for (BodyEntry& e : g_bodies) {
                if (e.body == s.body && e.comp == comp) e.both = both;
            }
        } catch (...) {
        }
    };
    mark(true);
    body_keep_host(s.body, false);
    int* r = g_body_loot.call<int*>(comp, err, looter, a, b);
    BodySeen after;
    read_body(after, comp, 0);
    uint32_t removed = 0;
    bool trimmed = false;
    if (*err == 0 && after.body == s.body) trimmed = body_trim(comp, s.pool, s.sub, &removed);
    if (*err != 0) {  // e.g. a full bag: this player may try again
        mark(false);
        if (s.looter == kHostCharacter) body_keep_host(s.body, true);
    }
    c_body_second.fetch_add(1);
    static std::atomic<int> said2{0};
    if (said2.fetch_add(1) < 30)
        log::info("Lootr: body 0x{:08X} looted second by 0x{:08X}: rolled {} -> {}, pool {} -> {} (after the take), bag "
                  "{:+d}, err {} | {} duplicate(s) removed{}", s.body, s.looter, s.rolled, after.rolled, s.pool,
                  after.pool, static_cast<int>(bag_total(looter) - bag_before), *err, removed,
                  trimmed ? "" : " (list not trimmed)");
    return r;
}

int* __fastcall body_loot_detour(uintptr_t comp, int* err, uintptr_t looter, uint8_t a, uint8_t b) noexcept {
    BodySeen before, after;
    read_body(before, comp, looter);
    bool handled = false;
    int* r = body_loot_per_player(comp, err, looter, a, b, &handled);
    if (!handled) r = g_body_loot.call<int*>(comp, err, looter, a, b);
    int e = -1;
    if (err) game::fast_read(reinterpret_cast<uintptr_t>(err), &e, 4);
    read_body(after, comp, 0);  // the comp may be gone when the body was logged out: SEH-guarded
    static std::atomic<int> said{0};
    if (g_probe && said.fetch_add(1) < 40) {
        log::info("Lootr probe: body loot by 0x{:08X} of body 0x{:08X} a {} b {} | rolled {} -> {} | pool {} -> {} | "
                  "sub {} -> {} | body after 0x{:08X} | err {}", before.looter, before.body, a, b, before.rolled,
                  after.rolled, before.pool, after.pool, before.sub, after.sub, after.body, e);
    }
    g_watch_until = GetTickCount64() + 3000;
    return r;
}

}  // namespace

std::string lootr_describe();

void lootr_set_kill_hits(bool on) { g_kill_hits = on; }

void lootr_start(bool on, bool probe, bool guest, bool bodies) {
    g_on = on && !guest;
    g_probe = probe && !guest;
    g_bodies_on = bodies && g_on;
    if (!g_on && !g_probe) return;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint8_t head[2]{};
    if (!game::safe_read(base + 0x2D37B10, head, sizeof head)) return;
    if (auto h = safetyhook::InlineHook::create(reinterpret_cast<void*>(base + 0x2D37B10), pickup_detour)) {
        g_pickup = std::move(*h);
    }
    if (g_probe || g_bodies_on) {
        static const uint8_t kHead[10] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x44, 0x88, 0x4C, 0x24, 0x20};
        uint8_t got[10] = {};
        if (game::safe_read(base + 0x2E224F0, got, sizeof got) && std::memcmp(got, kHead, sizeof got) == 0) {
            if (auto h = safetyhook::InlineHook::create(reinterpret_cast<void*>(base + 0x2E224F0), body_loot_detour)) {
                g_body_loot = std::move(*h);
            }
        }
        if (g_bodies_on && g_body_loot) {
            static const uint8_t kLea[7] = {0x48, 0x8D, 0x35, 0xF4, 0xC1, 0x0C, 0x03};
            uint8_t at[7] = {};
            if (game::safe_read(base + 0x2E22885, at, sizeof at) && std::memcmp(at, kLea, sizeof at) == 0) {
                if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(base + 0x2E22885), on_keep_body)) {
                    g_keep_body_hook = std::move(*h);
                }
            }
        }
        log::info("Lootr: the body loot worker {}{}", g_body_loot ? "hooked" : "NOT hooked",
                  !g_bodies_on ? " (log only)"
                  : g_keep_body_hook ? ": enemy loot is per player (the second looter gets its own roll)"
                                     : ": per player, but the keep-the-body hook is NOT in");
    }
    log::info("Lootr {} (lootr={}, lootr_probe={}): {}", g_on ? "ON" : "probe only", on ? 1 : 0, probe ? 1 : 0,
              !g_pickup ? "the pickup worker NOT hooked"
              : g_on    ? "a world item the host takes stays for the guest, who can take it once too"
                        : "the pickup worker logged");
}

bool lootr_on_guest_out(std::vector<uint8_t>& p) noexcept {
    if (!g_on) return false;
    try {
        std::lock_guard lk(g_mu);
        if (lrl::opcode_of(p) == 2414) {  // LootDropCharacterRewardListAck: u8 4 | u32 body | ...
            if (p.size() < lrl::kHeader + 5) return false;
            const uint32_t body = lrl::rd32(p.data() + lrl::kHeader + 1);
            for (const BodyEntry& e : g_bodies) {
                if (e.body == body && !e.both && e.first == kHostCharacter) {
                    c_body_2414.fetch_add(1);
                    return true;  // the guest still sees the body's loot
                }
            }
            return false;
        }
        for (Taken& t : g_taken) {
            if (t.state == 2 || t.host_waits) continue;
            const int a = lrl::about(p, t.g, t.key16);
            if (a == 1) {
                t.held.push_back(p);
                c_held.fetch_add(1);
                return true;
            }
            if (a == 2) {
                t.held.push_back(lrl::only_key(p, t.g));
                p = lrl::strip_key(p, t.g);
                c_held.fetch_add(1);
                return false;
            }
        }
    } catch (...) {
    }
    return false;
}

int lootr_on_guest_req(uint16_t opcode, std::span<const uint8_t> payload) noexcept {
    if (!g_on || (opcode != lrl::kOpPreemptReq && opcode != lrl::kOpDoStepReq) || payload.size() < 8) return 0;
    try {
        const uint32_t requester = lrl::rd32(payload.data()), target = lrl::rd32(payload.data() + 4);
        std::lock_guard lk(g_mu);
        for (Taken& t : g_taken) {
            if (t.g != target || t.state != 0 || !t.item_known || t.host_waits) continue;
            if (opcode == lrl::kOpPreemptReq) {
                server_push_extra(lrl::answer(lrl::kOpPreemptAck, requester, payload));
            } else {
                server_push_extra(lrl::answer(lrl::kOpDoStepAck, requester, payload));
                t.state = 1;
                t.requester = requester;
                t.step_at = GetTickCount64() + kGatherMs;
            }
            return 1;
        }
    } catch (...) {
    }
    return 0;
}

void lootr_tick(std::vector<std::vector<uint8_t>>& to_server) noexcept {
    if (!g_on) return;
    try {
        const uint64_t now = GetTickCount64();
        std::lock_guard lk(g_mu);
        for (DevBody& b : g_dev_bodies) {  // [dev] LootingDropFromDeadBodyReq 2491: u32 body | u8 remove | u8 send list
            uint8_t pl[6] = {0, 0, 0, 0, 0, 1};
            std::memcpy(pl, &b.key, 4);
            if (b.step == 0 && now - b.at >= 3000) {
                b.step = 1;
                const bool ok = net::sniffer_inject_c2s(lrl::make(2491, pl, 0x00));
                log::info("Lootr probe: [dev] the host loots body 0x{:08X} (2491){}", b.key, ok ? "" : " NOT sent");
            } else if (b.step == 1 && now - b.at >= 6000) {
                b.step = 2;
                to_server.push_back(lrl::make(2491, pl, 0x00));
                log::info("Lootr probe: [dev] the guest loots body 0x{:08X} (2491, as its own request)", b.key);
            }
        }
        static uint64_t said = 0;
        if (now - said >= 30000) {
            said = now;
            log::info("{}", lootr_describe());
        }
        for (Taken& t : g_taken) {
            if (!t.item_known && now - t.at >= 150) {  // what the host's pickup added
                static BagSnap after;
                if (snap_bag(t.picker, after)) {
                    for (int i = 0; i < after.slots && !t.item_known; ++i) {
                        const bool same = i < t.before.slots && t.before.idx[i] == after.idx[i];
                        const int64_t was = same ? t.before.n[i] : 0;
                        if (after.idx[i] == 0xFFFF || after.n[i] <= was) continue;
                        t.item_idx = after.idx[i];
                        t.count = after.n[i] - was;
                        t.item_key = item_key_of(t.item_idx);
                        t.item_known = t.item_key != 0;
                    }
                }
                if (!t.item_known && now - t.at >= 3000) t.state = 2;  // nothing came: not an item pickup
                if (t.item_known)
                    log::info("Lootr: gimmick 0x{:08X} gave the {} item 0x{:X} (key {}) x{}; kept for the {} ({} "
                              "packet(s) held)", t.g, t.host_waits ? "guest" : "host", t.item_idx, t.item_key, t.count,
                              t.host_waits ? "host" : "guest", t.held.size());
            }
            if (t.state == 1 && now >= t.step_at && t.host_waits) {
                // Phase 2b: the host's copy (its own request, the server thread adds it), then what its client was
                // not shown is delivered to it (the plant goes for the host too).
                std::vector<uint8_t> pl(30, 0);
                const uint16_t none = 0xFFFF;
                const uint64_t n = static_cast<uint64_t>(t.count);
                std::memcpy(&pl[0], &t.item_key, 4);
                std::memcpy(&pl[4], &none, 2);
                std::memcpy(&pl[6], &n, 8);
                std::memcpy(&pl[14], &kHostCharacter, 4);
                std::memcpy(&pl[18], &kHostCharacter, 4);
                std::memcpy(&pl[22], &none, 2);
                std::memcpy(&pl[24], &none, 2);
                std::memcpy(&pl[26], &kLootrGiveEntry, 4);
                const bool sent = net::sniffer_inject_c2s(lrl::make(3111, pl, 0x00));
                size_t replayed = 0;
                for (HostHeld& h : g_host_held) {
                    if (h.g.load() != t.g || h.size < lrl::kHeader) continue;
                    const uint16_t op = static_cast<uint16_t>(h.bytes[0] | (h.bytes[1] << 8));
                    net::puppet_inject_s2c(op, std::vector<uint8_t>(h.bytes + lrl::kHeader, h.bytes + h.size), 0);
                    h.g.store(0);
                    ++replayed;
                }
                for (KeepHost& k : g_keep) {
                    if (k.g.load() == t.g) k.g.store(0);
                }
                c_given.fetch_add(1);
                log::info("Lootr: the host took gimmick 0x{:08X} too (the guest was first): item key {} x{}{}, {} "
                          "packet(s) replayed", t.g, t.item_key, t.count, sent ? "" : " (add NOT sent)", replayed);
                t.state = 2;
            }
            if (t.state == 1 && now >= t.step_at) {
                // The guest's own copy: the add on the server thread (trade_probe's worker, entry 0xC0DE0005),
                // then what it was not shown (the gimmick goes for it now).
                std::vector<uint8_t> pl(30, 0);
                const uint16_t none = 0xFFFF;
                const uint64_t n = static_cast<uint64_t>(t.count);
                std::memcpy(&pl[0], &t.item_key, 4);
                std::memcpy(&pl[4], &none, 2);
                std::memcpy(&pl[6], &n, 8);
                std::memcpy(&pl[14], &t.requester, 4);
                std::memcpy(&pl[18], &t.requester, 4);
                std::memcpy(&pl[22], &none, 2);
                std::memcpy(&pl[24], &none, 2);
                std::memcpy(&pl[26], &kLootrGiveEntry, 4);
                to_server.push_back(lrl::make(3111, pl, 0x00));  // a client Req
                for (const auto& h : t.held) server_push_extra(h);
                c_given.fetch_add(1);
                log::info("Lootr: the guest took gimmick 0x{:08X} too: item key {} x{} for 0x{:08X}, {} packet(s) "
                          "replayed", t.g, t.item_key, t.count, t.requester, t.held.size());
                t.state = 2;
            }
        }
        std::erase_if(g_taken, [&](const Taken& t) { return t.state == 2 || now - t.at > kKeepMs; });
    } catch (...) {
    }
}

std::string lootr_describe() {
    return std::format("Lootr: {} | host pickups kept for the guest {} | packets held {} | taken by the guest too {} | "
                       "guest pickups kept for the host {} | host packets blanked {}",
                       g_on ? "on" : "off", c_taken.load(), c_held.load(), c_given.load(), c_guest_first.load(),
                       c_host_blanked.load()) +
           std::format(" | bodies {}: first loots {} second loots {} repeats refused {} lists kept back {}",
                       g_bodies_on ? "per player" : "as the game", c_body_first.load(), c_body_second.load(),
                       c_body_refused.load(), c_body_2414.load());
}

void lootr_host_recv(uint8_t* packet, uint16_t size) noexcept {
    if (!g_on || size < lrl::kHeader + 4) return;
    const uint16_t op = static_cast<uint16_t>(packet[0] | (packet[1] << 8));
    if (op == 2414 && size >= lrl::kHeader + 5) {  // the leftover list of a body the guest looted first
        const uint32_t body = lrl::rd32(packet + lrl::kHeader + 1);
        if (!body) return;
        for (auto& k : g_body_keep_host) {
            if (k.load() == body) {
                std::memset(packet + lrl::kHeader + 1, 0, 4);
                c_body_2414.fetch_add(1);
                return;
            }
        }
        return;
    }
    if (op == 3100 && size >= lrl::kHeader + 8) {  // InteractionPreemptionStepFailAck: u32 actor | u32 target | ...
        uint8_t* fl = packet + lrl::kHeader;
        const uint32_t target = lrl::rd32(fl + 4);
        for (KeepHost& k : g_keep) {
            if (k.g.load() == target && target) {
                std::memset(fl + 4, 0, 4);  // not about this plant any more: the client waits for our 2120
                c_host_blanked.fetch_add(1);
                return;
            }
        }
        return;
    }
    if (op != lrl::kOpGimmickGone && op != lrl::kOpGimmickState && op != lrl::kOpRemoveActor && op != lrl::kOpCoolTime)
        return;
    const uint64_t now = GetTickCount64();
    uint8_t* pl = packet + lrl::kHeader;
    const size_t n = size - lrl::kHeader;
    for (KeepHost& k : g_keep) {
        const uint32_t g = k.g.load();
        if (!g || now - k.at.load() > kKeepMs) continue;
        const int a = lrl::about(std::span<const uint8_t>(packet, size), g, k.key16);
        if (!a) continue;
        host_held_copy(g, packet, size);
        // Blank the gimmick in place (the same size): an actor key 0 / a zero save key the client finds nothing for.
        if (op == lrl::kOpGimmickGone) {
            std::memset(pl, 0, n >= 8 ? 8 : 4);
        } else if (op == lrl::kOpGimmickState) {
            std::memset(pl, 0, 4);
        } else if (op == lrl::kOpCoolTime && n >= 24) {
            std::memset(pl + 8, 0, 16);
        } else if (op == lrl::kOpRemoveActor) {
            for (size_t i = 6; i + 4 <= n; i += 4) {
                if (lrl::rd32(pl + i) == g) std::memset(pl + i, 0, 4);
            }
        }
        c_host_blanked.fetch_add(1);
        return;
    }
}

void host_take(uint16_t op, const uint8_t* packet, size_t size) noexcept {
    if (size < lrl::kHeader + 8) return;
    const std::span<const uint8_t> pl(packet + lrl::kHeader, size - lrl::kHeader);
    const uint32_t requester = lrl::rd32(pl.data()), target = lrl::rd32(pl.data() + 4);
    if (requester != kHostCharacter) return;
    try {
        std::lock_guard lk(g_mu);
        for (Taken& t : g_taken) {
            if (!t.host_waits || t.g != target || t.state != 0 || !t.item_known) continue;
            if (op == lrl::kOpPreemptReq) {
                const auto a = lrl::answer(lrl::kOpPreemptAck, requester, pl);
                net::puppet_inject_s2c(lrl::kOpPreemptAck, std::vector<uint8_t>(a.begin() + lrl::kHeader, a.end()), 0);
            } else {
                const auto a = lrl::answer(lrl::kOpDoStepAck, requester, pl);
                net::puppet_inject_s2c(lrl::kOpDoStepAck, std::vector<uint8_t>(a.begin() + lrl::kHeader, a.end()), 0);
                t.state = 1;
                t.requester = requester;
                t.step_at = GetTickCount64() + kGatherMs;
            }
            return;
        }
    } catch (...) {
    }
}

// [dev] lootr_kill_hits=1 (tests only): every world actor the HOST hits dies at once (KillCheatReq 2210: u32 key |
// u32 0), so a one-PC test gets bodies to loot without winning a fight.
void kill_hits(const uint8_t* packet, size_t size) noexcept {
    try {
        static std::vector<uint32_t> done;  // the sniffer's writer thread only
        for (const uint32_t k : agl::targets(std::span<const uint8_t>(packet + 5, size - 5))) {
            if ((k >> 28) != 0xB || std::find(done.begin(), done.end(), k) != done.end()) continue;
            if (done.size() < 256) done.push_back(k);
            uint8_t pl[8] = {};
            std::memcpy(pl, &k, 4);
            const bool ok = net::sniffer_inject_c2s(lrl::make(2210, pl, 0x00));
            log::info("Lootr probe: [dev] the host's hit on 0x{:08X} kills it{}", k, ok ? "" : " (NOT sent)");
            {  // [dev] then both players loot it without the UI: the host 3 s later, the guest 6 s later (tick)
                std::lock_guard lk(g_mu);
                if (g_dev_bodies.size() < 64) g_dev_bodies.push_back({k, GetTickCount64(), 0});
            }
        }
    } catch (...) {
    }
}

void lootr_host_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept {
    if (size < 5) return;
    const uint16_t op = static_cast<uint16_t>(packet[0] | (packet[1] << 8));
    if (g_kill_hits.load() && !server_to_client && op == agl::kOpDoFrameEventReq) kill_hits(packet, size);
    if (g_on && !server_to_client && (op == lrl::kOpPreemptReq || op == lrl::kOpDoStepReq)) host_take(op, packet, size);
    if (!g_probe || !g_pickup) return;
    if (!server_to_client && (op == 3008 || op == 3073)) g_watch_until = GetTickCount64() + 3000;  // its answers
    static const uint16_t kWatch[] = {3008, 3073, 2120, 2794, 2066};
    bool watched = false;
    for (const uint16_t w : kWatch) watched = watched || w == op;
    // Only the players' interactions (tcB330: NPCs' own 3008 / 2120 used the whole budget).
    if (watched && size >= 9) {
        const uint8_t* pl = packet + 5;
        const uint32_t who = server_to_client ? (size >= 13 ? lrl::rd32(pl + 4) : 0) : lrl::rd32(pl);
        if ((who >> 28) != 0xA) watched = false;
    }
    const bool after = GetTickCount64() <= g_watch_until.load();
    if (!watched && !after) return;
    static std::atomic<int> n_watch{0}, n_after{0};
    if (after ? n_after.fetch_add(1) > 800 : n_watch.fetch_add(1) > 100) return;
    if (op == 2002 || op == 1001) return;
    char hex[3 * 120 + 1] = {};
    const size_t m = size < 120 ? size : (op == 3110 ? 120 : 40);
    for (size_t i = 0; i < m; ++i) std::snprintf(hex + i * 3, 4, "%02X ", packet[i]);
    log::info("Lootr probe: {} {} ({} bytes): {}", server_to_client ? "S>C" : "C>S", op, size, hex);
}

}  // namespace cdmp::session2
