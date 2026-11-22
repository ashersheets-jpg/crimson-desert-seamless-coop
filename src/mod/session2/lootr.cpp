// Co-op+ (2026-11-10): lootr.h.
#include "mod/session2/lootr.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <safetyhook.hpp>

#include "mod/game/safe_memory.h"
#include "mod/net/remote_puppet.h"
#include "mod/net/troc_sniffer.h"
#include "mod/log.h"
#include "mod/session2/anti_grief_logic.h"
#include "mod/session2/lootr_logic.h"
#include "mod/session2/lootr_persist_logic.h"
#include "mod/session2/save_hooks.h"
#include "mod/session2/server.h"
#include "mod/game/info_row.h"

namespace cdmp::session2 {
namespace {

constexpr uint32_t kHostCharacter = 0xA0100001;
constexpr uint32_t kLootrGiveEntry = 0xC0DE0005;  // trade_probe's worker: add an item to the requester
constexpr uint64_t kGatherMs = 1700;              // the 3073 -> pickup delay the game shows (tcB324: ~1.76 s)
constexpr uint64_t kKeepMs = 30 * 60 * 1000;      // an entry the guest never takes is forgotten after 30 min
                                                  // (B19 lootr_persist, armed: it waits at its place instead)

// B19: every share gets an id (lootr_persist_logic.h make_id) and its start time, kept across sessions.
int64_t unix_now() noexcept {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
std::atomic<int64_t> g_session_start{0};
std::atomic<uint32_t> g_share_counter{0};
uint64_t next_share_id() noexcept {
    int64_t start = g_session_start.load();
    if (!start) {
        start = unix_now();
        g_session_start.store(start);
    }
    return lpl::make_id(start, g_share_counter.fetch_add(1) + 1);
}
bool g_persist_on = false;         // [session2] lootr_persist
std::atomic<bool> g_armed{false};  // B19: the host loaded a co-op world slot through guest_save_host's hook

bool g_on = false, g_probe = false;
safetyhook::InlineHook g_pickup;
std::atomic<uint64_t> g_watch_until{0};
std::atomic<int> g_logged{0};
std::atomic<int> g_kill_hits{0};  // [dev] lootr_kill_hits: 1 kill + both loot by request, 2 kill only (the guest
                                  // loots by request 5 s after the HOST's own first loot of a body)

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
        const uintptr_t slot = *reinterpret_cast<uintptr_t*>(base + 0x724B6E8);
        const uintptr_t root = *reinterpret_cast<uintptr_t*>(slot);
        const uintptr_t info = *reinterpret_cast<uintptr_t*>(root + 8);
        const uintptr_t mgr = *reinterpret_cast<uintptr_t*>(info + 0x220);  // 3663 (3576 +0x240)
        const uint16_t bag = *reinterpret_cast<uint16_t*>(mgr + 0x7A);  // 3663 (3576 +0xA2)
        const uintptr_t holder = reinterpret_cast<uintptr_t (__fastcall*)(uintptr_t)>(base + 0x23643B0)(picker);
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
        const uintptr_t row = game::info_row(base + 0x3DB320, idx);
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
    // B19 (lootr_persist): where the gimmick stood (read only while armed), the share's id and start (unix s).
    float x = 0, y = 0, z = 0;
    bool pos_ok = false;
    uint64_t share_id = 0;
    int64_t made = 0;
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
    uint16_t row = 0xFFFF;  // the gimmick info row and its name (tcB373: world-placed gear is "concrete" too)
    char name[48] = {};
    int has_drop = -1;
    uint32_t state = 0;
    // The gimmick actor's SpawnReason, u8 [[[comp+8]+0x68]+0x20]+0x2F0 (notes_dropped_vs_placed.md, local): 0 = placed
    // in the level, 0x16 = a player dropped it from the inventory, 0x15 = thrown out by a gimmick's Drop event,
    // 0x1A / 0x1B from a dead body, 0x2A never set. 0xFF = unreadable.
    uint8_t reason = 0xFF;
    uint32_t spawner = 0;  // u32 comp+0x288: who spawned it (0 for level gimmicks)
};
int drop_handler(uintptr_t base, uintptr_t b, uint8_t& mode) noexcept;
void read_pickup(Seen& s, uintptr_t comp, uintptr_t picker) noexcept {
    __try {
        {
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            s.row = *reinterpret_cast<uint16_t*>(comp + 0x48);
            s.state = *reinterpret_cast<uint32_t*>(comp + 0x270);
            uint8_t mode = 0xFF;
            s.has_drop = drop_handler(base, *reinterpret_cast<uintptr_t*>(comp + 0x40), mode);
            const uintptr_t row = game::info_row(base + 0x3DB080, s.row);
            const uintptr_t h = row ? *reinterpret_cast<uintptr_t*>(row + 8) : 0;
            const char* text = h ? *reinterpret_cast<const char**>(h) : nullptr;
            for (size_t c = 0; text && c + 1 < sizeof s.name && text[c]; ++c) s.name[c] = text[c];
        }
        if (picker) s.picker = *reinterpret_cast<uint32_t*>(picker + 0x60);
        const uintptr_t actor = *reinterpret_cast<uintptr_t*>(comp + 8);
        if (actor) s.gimmick = *reinterpret_cast<uint32_t*>(actor + 0x60);
        if (actor) {
            const uintptr_t comps = *reinterpret_cast<uintptr_t*>(actor + 0x68);
            const uintptr_t status = comps ? *reinterpret_cast<uintptr_t*>(comps + 0x20) : 0;
            if (status) s.reason = *reinterpret_cast<uint8_t*>(status + 0x2F0);
        }
        s.spawner = *reinterpret_cast<uint32_t*>(comp + 0x288);
        const uintptr_t rec = *reinterpret_cast<uintptr_t*>(comp + 0xC0);
        if (rec) {
            s.idx = *reinterpret_cast<uint16_t*>(rec + 8);
            s.count = *reinterpret_cast<int64_t*>(rec + 0x10);
        }
        std::memcpy(s.key16, reinterpret_cast<void*>(comp + 0x30), 16);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

thread_local int t_in_pickup = 0;  // a Drop event inside the pickup worker is the pickup's own business
bool gimmick_pos(uintptr_t comp, float& x, float& y, float& z) noexcept;

int* __fastcall pickup_detour(uintptr_t comp, int* err, uintptr_t picker, uint64_t kind) noexcept {
    Seen s;
    read_pickup(s, comp, picker);
    // Per player: things that MAKE their item when taken (plants, herbs, nodes: comp+0xC0 empty before the pickup)
    // and concrete items PLACED IN THE LEVEL by the game (gear, books, arrows lying in the world: spawn reason 0,
    // tcB373). A concrete item a player dropped or a gimmick threw out (any other spawn reason, or unreadable) is one
    // object: per player it would be a duplication (drop it, the friend takes it, take "your copy").
    const bool concrete = s.idx != 0xFFFF && s.reason != 0;
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
            if (g_armed.load()) t.pos_ok = gimmick_pos(comp, t.x, t.y, t.z);
            t.share_id = next_share_id();
            t.made = unix_now();
            snap_bag(picker, t.before);
            std::lock_guard lk(g_mu);
            if (g_taken.size() < 512) g_taken.push_back(std::move(t));
            c_taken.fetch_add(1);
        } catch (...) {
        }
    }
    ++t_in_pickup;
    int* r = g_pickup.call<int*>(comp, err, picker, kind);
    --t_in_pickup;
    int e = -1;
    if (err) game::fast_read(reinterpret_cast<uintptr_t>(err), &e, 4);
    if ((g_probe || g_on) && g_logged.fetch_add(1) < 40) {
        log::info("Lootr: pickup by 0x{:08X} kind {} gimmick 0x{:08X} '{}' (row {}, {} Drop event, state 0x{:08X}, spawn "
                  "reason 0x{:02X}, spawner 0x{:08X}) item idx 0x{:X} x{} key {:02X}{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}"
                  "{:02X} -> err {}{}",
                  s.picker, kind & 0xFF, s.gimmick, s.name, s.row, s.has_drop == 1 ? "has a" : "no", s.state, s.reason,
                  s.spawner, s.idx, s.count, s.key16[0], s.key16[1], s.key16[2], s.key16[3],
                  s.key16[4], s.key16[5], s.key16[6], s.key16[7], e,
                  !g_on ? "" : concrete ? " | a dropped / thrown item: one for whoever takes it"
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
            reinterpret_cast<void (__fastcall*)(uintptr_t, uint32_t)>(base + 0x23823B0)(comp + 0x58, count - 1);
            const uint32_t now = *reinterpret_cast<uint32_t*>(comp + 0x60);
            if (now >= count) break;  // nothing went: stop
            count = now;
            ++*removed;
        }
        if (*reinterpret_cast<uint32_t*>(comp + 0x50) > s) *reinterpret_cast<uint32_t*>(comp + 0x50) = s;
        reinterpret_cast<void (__fastcall*)(uintptr_t)>(base + 0x2E91F20)(comp);
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
        game::fast_read(base + 0x71D6EC4, &none, sizeof none);  // "nothing to loot"
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
            if (g_kill_hits.load() == 2 && s.looter == kHostCharacter) {  // [dev] the guest's turn, by request, 5 s on
                try {
                    std::lock_guard lk(g_mu);
                    if (g_dev_bodies.size() < 64) g_dev_bodies.push_back({s.body, GetTickCount64() - 1000, 1});
                } catch (...) {
                }
            }
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

bool g_body_share_on = false;
void body_looted(uintptr_t comp, uint32_t body, uint32_t looter_key, const BagSnap& before) noexcept;

int* __fastcall body_loot_detour(uintptr_t comp, int* err, uintptr_t looter, uint8_t a, uint8_t b) noexcept {
    BodySeen before, after;
    read_body(before, comp, looter);
    bool handled = false;
    static BagSnap bag0;  // the server thread only
    const bool share = g_on && g_body_share_on && !g_bodies_on && looter && (before.looter >> 28) == 0xA &&
                       before.body && snap_bag(looter, bag0);
    int* r = body_loot_per_player(comp, err, looter, a, b, &handled);
    if (!handled) r = g_body_loot.call<int*>(comp, err, looter, a, b);
    int e = -1;
    if (err) game::fast_read(reinterpret_cast<uintptr_t>(err), &e, 4);
    if (share && e == 0) body_looted(comp, before.body, before.looter, bag0);
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

// ---- chests / item boxes (probe only, notes_lootr_bodies_chests.md B): the gimmick event "Drop" worker
// exe+0x2D4EC10(comp, err, field, actor, target, targets, params, ...) and its roll exe+0x2D4DE40(comp, actor, target,
// items {ptr, u32 count}, others, decrease). Logged: who, the gimmick and its save key, the mode (params[1]: 0 ground,
// 1 bag) and the rolled items.
safetyhook::InlineHook g_drop, g_drop_roll;
bool g_chests_on = false;
struct ChestShare {
    uint32_t gimmick = 0, opener = 0;
    float x = 0, y = 0, z = 0;
    uint32_t item[12] = {};
    int64_t n[12] = {};
    int items = 0;
    uint64_t at = 0;
    // tcB366: the chest puts ONE bundle item into the bag, which the game then turns into the real content (Meat Soup
    // x10, Lean Meat x13, Lentils x3). So the bag is compared again once that has settled.
    bool settled = false;
    bool told = false;  // the layer was told that this share waits (the other player was not at the chest)
    bool body = false;  // a dead body's loot: no notice, 30 m, kept 10 minutes
    BagSnap before;
    uint64_t share_id = 0;  // B19
    int64_t made = 0;
    // B19 v4.2 (review of v4.1, item 6): its adds are running (lootr_chest_shares): a save inside one does not list it
    // (the bag may hold its items already). It leaves the list right after, given or not.
    bool giving = false;
};
void persist_keep(const ChestShare& c, const char* why) noexcept;  // B19: below
constexpr uint64_t kSettleMs = 1500;
constexpr uint64_t kShareKeepMs = 3ull * 60 * 60 * 1000;  // a share nobody fetches is forgotten after 3 hours
constexpr uint64_t kBodyShareKeepMs = 10ull * 60 * 1000;
constexpr int kShareMax = 48;
// For the Co-op+ layer (export cdmp_chest_events): 1 = a share waits for `player` at (x, y, z), 2 = `player` got it.
struct ChestEvent {
    uint8_t kind = 0;
    uint32_t player = 0;
    float x = 0, y = 0, z = 0;
    uint16_t kinds = 0, pieces = 0;
};
SRWLOCK g_chest_ev_lock = SRWLOCK_INIT;
ChestEvent g_chest_ev[16];
int g_chest_ev_n = 0;
void chest_event(uint8_t kind, uint32_t player, const ChestShare& sh) noexcept {
    ChestEvent e;
    e.kind = kind;
    e.player = player;
    e.x = sh.x;
    e.y = sh.y;
    e.z = sh.z;
    e.kinds = static_cast<uint16_t>(sh.items);
    int64_t pieces = 0;
    for (int i = 0; i < sh.items; ++i) pieces += sh.n[i];
    e.pieces = static_cast<uint16_t>(pieces > 0xFFFF ? 0xFFFF : pieces);
    AcquireSRWLockExclusive(&g_chest_ev_lock);
    if (g_chest_ev_n < 16) g_chest_ev[g_chest_ev_n++] = e;
    ReleaseSRWLockExclusive(&g_chest_ev_lock);
}
ChestShare g_shares[kShareMax];  // the server thread only
int g_share_n = 0;
std::atomic<int> g_shares_pending{0};
std::atomic<uint64_t> c_chest_opened{0}, c_chest_shared{0}, c_chest_expired{0}, c_body_looted{0}, c_body_shared{0};
void chest_opened(uintptr_t comp, uintptr_t opener, uint32_t gimmick, uint32_t opener_key, const BagSnap& before) noexcept;
struct DropSeen {
    uint32_t gimmick = 0, actor = 0, target = 0;
    uint8_t key16[16] = {};
    uint8_t p0 = 0xFF, p1 = 0xFF, dec = 0xFF;
};
void read_drop(DropSeen& s, uintptr_t comp, uintptr_t actor, uintptr_t target, uintptr_t params) noexcept {
    __try {
        const uintptr_t g = *reinterpret_cast<uintptr_t*>(comp + 8);
        if (g) s.gimmick = *reinterpret_cast<uint32_t*>(g + 0x60);
        std::memcpy(s.key16, reinterpret_cast<void*>(comp + 0x30), 16);
        if (actor) s.actor = *reinterpret_cast<uint32_t*>(actor + 0x60);
        if (target) s.target = *reinterpret_cast<uint32_t*>(target + 0x60);
        if (params) {
            s.p0 = *reinterpret_cast<uint8_t*>(params);
            s.p1 = *reinterpret_cast<uint8_t*>(params + 1);
            s.dec = *reinterpret_cast<uint8_t*>(params + 0x49);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
// Up to 8 entries of a roll list as "type:index xcount".
int read_roll(uintptr_t list, char* out, size_t cap) noexcept {
    int n = -1;
    out[0] = 0;
    __try {
        const uintptr_t p = *reinterpret_cast<uintptr_t*>(list);
        n = static_cast<int>(*reinterpret_cast<uint32_t*>(list + 8));
        size_t at = 0;
        for (int i = 0; i < n && i < 8 && p; ++i) {
            const uintptr_t e = p + static_cast<uintptr_t>(i) * 0x38;
            const uint8_t type = *reinterpret_cast<uint8_t*>(e);
            const uintptr_t rec = *reinterpret_cast<uintptr_t*>(e + 0x30);
            uint16_t idx = 0xFFFF;
            int64_t count = -1;
            if (rec > 0x10000) {
                idx = *reinterpret_cast<uint16_t*>(rec + 8);
                count = *reinterpret_cast<int64_t*>(rec + 0x10);
            }
            const int w = std::snprintf(out + at, cap - at, "%s%u:%u x%lld", i ? ", " : "", type, idx,
                                        static_cast<long long>(count));
            if (w <= 0 || static_cast<size_t>(w) >= cap - at) break;
            at += static_cast<size_t>(w);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return n;
}
thread_local DropSeen* t_drop = nullptr;

uint8_t __fastcall drop_roll_detour(uintptr_t comp, uintptr_t actor, uintptr_t target, uintptr_t items,
                                    uintptr_t others, uint8_t decrease) noexcept {
    const uint8_t r = g_drop_roll.call<uint8_t>(comp, actor, target, items, others, decrease);
    static std::atomic<int> said{0};
    if (said.fetch_add(1) < 60) {
        char a[200], b[200];
        const int na = read_roll(items, a, sizeof a), nb = read_roll(others, b, sizeof b);
        DropSeen s;
        read_drop(s, comp, actor, target, 0);
        log::info("Lootr probe: drop roll of gimmick 0x{:08X} for 0x{:08X} / 0x{:08X} (decrease {}){}: {} | {} item(s) "
                  "[{}] | {} other(s) [{}]", s.gimmick, s.actor, s.target, decrease, t_drop ? "" : " (not in the Drop "
                  "worker)", r ? "rolled" : "nothing (no roll left)", na, a, nb, b);
    }
    return r;
}

uint32_t* __fastcall drop_detour(uintptr_t comp, uint32_t* err, uintptr_t field, uintptr_t actor, uintptr_t target,
                                 uintptr_t targets, uintptr_t params, int a8, uint32_t a9, uintptr_t a10,
                                 uint8_t a11) noexcept {
    DropSeen s;
    read_drop(s, comp, actor, target, params);
    DropSeen* const outer = t_drop;
    t_drop = &s;
    // Chests per player: mode 1 puts the content straight into the opener's bag; what the bag gains is the share.
    static BagSnap before;  // the server thread only
    const bool share = g_on && g_chests_on && !outer && !t_in_pickup && s.p1 == 1 && target && (s.target >> 28) == 0xA &&
                       snap_bag(target, before);
    uint32_t* const r = g_drop.call<uint32_t*>(comp, err, field, actor, target, targets, params, a8, a9, a10, a11);
    t_drop = outer;
    uint32_t e = 0xFFFFFFFF;
    if (err) game::fast_read(reinterpret_cast<uintptr_t>(err), &e, 4);
    if (share && e == 0) chest_opened(comp, target, s.gimmick, s.target, before);
    static std::atomic<int> said{0};
    if (g_probe && said.fetch_add(1) < 60) {
        char key[3 * 16 + 1] = {};
        for (int i = 0; i < 16; ++i) std::snprintf(key + i * 3, 4, "%02X ", s.key16[i]);
        log::info("Lootr probe: DROP event of gimmick 0x{:08X} (save key {}) actor 0x{:08X} target 0x{:08X} | params "
                  "[0] {} [1] {} ({}) decrease {} | err {}", s.gimmick, key, s.actor, s.target, s.p0, s.p1,
                  s.p1 == 0 ? "on the ground" : s.p1 == 1 ? "into the bag" : "?", s.dec, e);
    }
    g_watch_until = GetTickCount64() + 3000;
    return r;
}

// ---- where are the chests (probe only; notes_find_chests.md, local). Walk A: the server actor manager's dense list
// (sam = [[exe+0x71C0608]], u32 count +0x9C, array +0xB0, entry -> actor at +8); a gimmick has type byte
// [[actor+0x88]+1] == 7, its component [[actor+0x68]+0x30] holds the info index (u16 +0x48), the save key (+0x30) and
// the state data B (+0x40) whose handler lists tell a Drop event (vtable exe+0x5D7CC28). Walk B: the field's sync
// scene objects ([field+0x80]: u32 count +0x64, array +0x78, entry: key16 +8, data +0x18 with u32 +0x10C rolls left).
struct ChestFound {
    uint32_t key = 0;
    uint16_t idx = 0xFFFF;
    uint32_t row_rolls = 0, left = 0xFFFFFFFF;
    uint8_t mode = 0xFF;
    float x = 0, y = 0, z = 0, dist = 0;
    char name[56] = {};
    uint8_t key16[16] = {};
};
constexpr int kChestMax = 48;
// Walk B by name: every scene object of the field that still has drop rolls; the names are looked up once per info
// index (kept across scans) and the objects whose name looks like a chest are listed, the nearest first.
struct RowName {
    uint16_t idx = 0xFFFF;
    bool chest = false, said = false;
    uint32_t seen = 0;
    char name[48] = {};
};
constexpr int kRowNames = 1500;
RowName g_row_names[kRowNames];  // the server thread only
int g_row_name_n = 0;
struct ChestScan {
    ChestFound f[kChestMax];
    int n = 0;
    ChestFound wide[kChestMax];  // by name, from the scene objects (no actor key)
    int wide_n = 0;
    ChestFound near_[24];  // every gimmick actor within 8 m, with or without a Drop event (left = its state hash)
    int near_n = 0;
    uint32_t with_rolls = 0;
    uint32_t actors = 0, gimmicks = 0, droppers = 0, objects = 0;
    float hx = 0, hy = 0, hz = 0;
    int stage = 0;
};
bool actor_pos(uintptr_t base, uintptr_t actor, float& x, float& y, float& z) noexcept {
    const uintptr_t comps = *reinterpret_cast<uintptr_t*>(actor + 0x68);
    const uintptr_t ts = comps ? *reinterpret_cast<uintptr_t*>(comps + 0x1A0) : 0;
    if (!ts) return false;
    alignas(16) uint8_t buf[0x80] = {};
    reinterpret_cast<void* (__fastcall*)(uintptr_t, void*)>(base + 0x5D8880)(ts, buf);
    const float* f = reinterpret_cast<const float*>(buf);
    x = f[0] + 1000.0f * static_cast<float>(*reinterpret_cast<int16_t*>(buf + 0xC));
    y = f[1];
    z = f[2] + 1000.0f * static_cast<float>(*reinterpret_cast<int16_t*>(buf + 0xE));
    return true;
}
// 0 = no Drop handler, 1 = has one (mode = its delivery byte).
int drop_handler(uintptr_t base, uintptr_t b, uint8_t& mode) noexcept {
    if (!b) return 0;
    const uintptr_t vt = base + 0x5DFE398;
    auto list = [&](uintptr_t p, uint32_t cnt) {
        if (!p || cnt > 64) return false;
        for (uint32_t j = 0; j < cnt; ++j) {
            const uintptr_t data = *reinterpret_cast<uintptr_t*>(p + 0x10ull * j + 8);
            if (data && *reinterpret_cast<uintptr_t*>(data) == vt) {
                mode = *reinterpret_cast<uint8_t*>(data + 0x90);
                return true;
            }
        }
        return false;
    };
    const uint32_t n = *reinterpret_cast<uint32_t*>(b + 0x14);
    const uintptr_t arr = *reinterpret_cast<uintptr_t*>(b + 0x28);
    for (uint32_t i = 0; arr && i < n && i < 256; ++i) {
        const uintptr_t e = *reinterpret_cast<uintptr_t*>(arr + 8ull * i);
        const uintptr_t st = e ? *reinterpret_cast<uintptr_t*>(e + 8) : 0;
        if (!st) continue;
        for (int k = 0; k < 3; ++k) {
            if (list(*reinterpret_cast<uintptr_t*>(st + 0x20 + 0x10 * k), *reinterpret_cast<uint32_t*>(st + 0x28 + 0x10 * k)))
                return 1;
        }
    }
    const uintptr_t p = *reinterpret_cast<uintptr_t*>(b + 0x30);
    const uint32_t m = *reinterpret_cast<uint32_t*>(b + 0x38);
    for (uint32_t k = 0; p && k < m && k < 4096; ++k) {
        const uintptr_t e = p + 0x28ull * k;
        if (list(*reinterpret_cast<uintptr_t*>(e + 8), *reinterpret_cast<uint32_t*>(e + 0x10))) return 1;
    }
    return 0;
}
void chest_scan_read(ChestScan& s, uintptr_t base, uintptr_t host) noexcept {
    __try {
        if (!actor_pos(base, host, s.hx, s.hy, s.hz)) return;
        s.stage = 1;
        const uintptr_t slot = *reinterpret_cast<uintptr_t*>(base + 0x724B860);
        const uintptr_t sam = slot ? *reinterpret_cast<uintptr_t*>(slot) : 0;
        if (!sam) return;
        const uint32_t n = *reinterpret_cast<uint32_t*>(sam + 0x9C);
        const uintptr_t arr = *reinterpret_cast<uintptr_t*>(sam + 0xB0);
        if (!arr || n > 200000) return;
        s.actors = n;
        s.stage = 2;
        for (uint32_t i = 0; i < n; ++i) {
            const uintptr_t e = *reinterpret_cast<uintptr_t*>(arr + 8ull * i);
            const uintptr_t actor = e ? *reinterpret_cast<uintptr_t*>(e + 8) : 0;
            if (!actor) continue;
            const uintptr_t kind = *reinterpret_cast<uintptr_t*>(actor + 0x88);
            if (!kind || *reinterpret_cast<uint8_t*>(kind + 1) != 7) continue;
            ++s.gimmicks;
            const uintptr_t comps = *reinterpret_cast<uintptr_t*>(actor + 0x68);
            const uintptr_t comp = comps ? *reinterpret_cast<uintptr_t*>(comps + 0x30) : 0;
            if (!comp) continue;
            ChestFound f;
            const bool has_drop = drop_handler(base, *reinterpret_cast<uintptr_t*>(comp + 0x40), f.mode) != 0;
            if (!actor_pos(base, actor, f.x, f.y, f.z)) continue;
            const float dx = f.x - s.hx, dy = f.y - s.hy, dz = f.z - s.hz;
            f.dist = dx * dx + dy * dy + dz * dz;
            if (f.dist < 64.0f && s.near_n < 24) {
                ChestFound g = f;
                g.key = *reinterpret_cast<uint32_t*>(actor + 0x60);
                g.idx = *reinterpret_cast<uint16_t*>(comp + 0x48);
                g.left = *reinterpret_cast<uint32_t*>(comp + 0x270);
                g.row_rolls = has_drop ? 1 : 0;
                s.near_[s.near_n++] = g;
            }
            if (!has_drop) continue;
            ++s.droppers;
            int at = s.n;
            if (s.n == kChestMax) {  // keep the nearest
                at = -1;
                float worst = f.dist;
                for (int k = 0; k < kChestMax; ++k) {
                    if (s.f[k].dist > worst) {
                        worst = s.f[k].dist;
                        at = k;
                    }
                }
                if (at < 0) continue;
            } else {
                ++s.n;
            }
            f.key = *reinterpret_cast<uint32_t*>(actor + 0x60);
            f.idx = *reinterpret_cast<uint16_t*>(comp + 0x48);
            std::memcpy(f.key16, reinterpret_cast<void*>(comp + 0x30), 16);
            s.f[at] = f;
        }
        for (int k = 0; k < s.near_n; ++k) {
            ChestFound& f = s.near_[k];
            const uintptr_t row =
                game::info_row(base + 0x3DB080, f.idx);
            const uintptr_t h = row ? *reinterpret_cast<uintptr_t*>(row + 8) : 0;
            const char* text = h ? *reinterpret_cast<const char**>(h) : nullptr;
            for (size_t c = 0; text && c + 1 < sizeof f.name && text[c]; ++c) f.name[c] = text[c];
        }
        s.stage = 3;
        // The rows (name, starting rolls): only for the kept ones (the call may load a row).
        for (int k = 0; k < s.n; ++k) {
            ChestFound& f = s.f[k];
            const uintptr_t row =
                game::info_row(base + 0x3DB080, f.idx);
            if (!row) continue;
            f.row_rolls = *reinterpret_cast<uint32_t*>(row + 0x268);
            const uintptr_t h = *reinterpret_cast<uintptr_t*>(row + 8);
            const char* text = h ? *reinterpret_cast<const char**>(h) : nullptr;
            for (size_t c = 0; text && c + 1 < sizeof f.name && text[c]; ++c) f.name[c] = text[c];
        }
        s.stage = 4;
        // Walk B: the rolls left, by the save key.
        const uintptr_t hc = *reinterpret_cast<uintptr_t*>(host + 0x68);
        const uintptr_t ts = hc ? *reinterpret_cast<uintptr_t*>(hc + 0x1A0) : 0;
        if (!ts || *reinterpret_cast<uint8_t*>(ts + 0x550) != 0) return;
        const uintptr_t field = *reinterpret_cast<uintptr_t*>(ts + 0x518);
        const uintptr_t ssom = field ? *reinterpret_cast<uintptr_t*>(field + 0x80) : 0;
        if (!ssom) return;
        const uint32_t m = *reinterpret_cast<uint32_t*>(ssom + 0x64);
        const uintptr_t oarr = *reinterpret_cast<uintptr_t*>(ssom + 0x78);
        if (!oarr || m > 2000000) return;
        s.objects = m;
        for (uint32_t o = 0; o < m; ++o) {
            const uintptr_t e = *reinterpret_cast<uintptr_t*>(oarr + 8ull * o);
            if (!e) continue;
            const uintptr_t d = *reinterpret_cast<uintptr_t*>(e + 0x18);
            if (!d) continue;
            for (int k = 0; k < s.n; ++k) {
                if (std::memcmp(reinterpret_cast<void*>(e + 8), s.f[k].key16, 16) != 0) continue;
                s.f[k].left = *reinterpret_cast<uint32_t*>(d + 0x10C);
            }
            const uint16_t idx = *reinterpret_cast<uint16_t*>(d + 0xB0);
            const uint32_t left = *reinterpret_cast<uint32_t*>(d + 0x10C);
            if (idx == 0xFFFF || left == 0 || left > 1000) continue;
            ++s.with_rolls;
            int at = -1;
            for (int k = 0; k < g_row_name_n; ++k) {
                if (g_row_names[k].idx == idx) {
                    at = k;
                    break;
                }
            }
            if (at < 0 && g_row_name_n < kRowNames) {
                at = g_row_name_n++;
                RowName& rn = g_row_names[at];
                rn = RowName{};
                rn.idx = idx;
                const uintptr_t row =
                    game::info_row(base + 0x3DB080, idx);
                const uintptr_t h = row ? *reinterpret_cast<uintptr_t*>(row + 8) : 0;
                const char* text = h ? *reinterpret_cast<const char**>(h) : nullptr;
                char low[sizeof rn.name] = {};
                for (size_t c = 0; text && c + 1 < sizeof rn.name && text[c]; ++c) {
                    rn.name[c] = text[c];
                    low[c] = (text[c] >= 'A' && text[c] <= 'Z') ? static_cast<char>(text[c] + 32) : text[c];
                }
                rn.chest = std::strstr(low, "treasure") || std::strstr(low, "chest") || std::strstr(low, "coffer") ||
                           std::strstr(low, "strongbox") || std::strstr(low, "safe");
            }
            if (at < 0) continue;
            ++g_row_names[at].seen;
            if (!g_row_names[at].chest) continue;
            ChestFound f;
            f.x = *reinterpret_cast<float*>(d + 0xD0);
            f.y = *reinterpret_cast<float*>(d + 0xD4);
            f.z = *reinterpret_cast<float*>(d + 0xD8);
            const float dx = f.x - s.hx, dy = f.y - s.hy, dz = f.z - s.hz;
            f.dist = dx * dx + dy * dy + dz * dz;
            int put = s.wide_n;
            if (s.wide_n == kChestMax) {
                put = -1;
                float worst = f.dist;
                for (int k = 0; k < kChestMax; ++k) {
                    if (s.wide[k].dist > worst) {
                        worst = s.wide[k].dist;
                        put = k;
                    }
                }
                if (put < 0) continue;
            } else {
                ++s.wide_n;
            }
            f.idx = idx;
            f.left = left;
            std::memcpy(f.name, g_row_names[at].name, sizeof f.name < sizeof g_row_names[at].name ? sizeof f.name
                                                                                                   : sizeof g_row_names[at].name);
            f.name[sizeof f.name - 1] = 0;
            std::memcpy(f.key16, reinterpret_cast<void*>(e + 8), 16);
            s.wide[put] = f;
        }
        s.stage = 5;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s.stage = -s.stage - 1;
    }
}
std::atomic<int> g_chest_scans{0};

// ---- chests per player, phase 1 (lootr.h).
bool gimmick_pos(uintptr_t comp, float& x, float& y, float& z) noexcept {
    __try {
        const uintptr_t actor = *reinterpret_cast<uintptr_t*>(comp + 8);
        return actor && actor_pos(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), actor, x, y, z);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void chest_opened(uintptr_t comp, uintptr_t opener, uint32_t gimmick, uint32_t opener_key,
                  const BagSnap& before) noexcept {
    (void)opener;
    c_chest_opened.fetch_add(1);
    for (int k = 0; k < g_share_n; ++k) {  // one settling share per player (its first bag snapshot covers this too)
        ChestShare& o = g_shares[k];
        if (o.settled || o.opener != opener_key) continue;
        o.at = GetTickCount64();
        if (o.body) {  // a chest is announced and kept longer
            o.body = false;
            o.told = false;
            o.gimmick = gimmick;
            gimmick_pos(comp, o.x, o.y, o.z);
        }
        return;
    }
    static ChestShare sh;  // the server thread only
    sh = ChestShare{};
    sh.gimmick = gimmick;
    sh.opener = opener_key;
    sh.at = GetTickCount64();
    sh.before = before;
    sh.share_id = next_share_id();
    sh.made = unix_now();
    if (!gimmick_pos(comp, sh.x, sh.y, sh.z)) {
        log::info("Lootr: chest 0x{:08X} opened by 0x{:08X}: the chest's position is unreadable (no share kept)",
                  gimmick, opener_key);
        return;
    }
    int slot = g_share_n < kShareMax ? g_share_n++ : 0;  // full: the oldest goes
    if (slot == 0 && g_share_n == kShareMax && g_shares[0].gimmick) {
        for (int k = 1; k < kShareMax; ++k) {
            if (g_shares[k].at < g_shares[slot].at) slot = k;
        }
    }
    if (g_share_n == kShareMax && g_shares[slot].gimmick) persist_keep(g_shares[slot], "the shares' list was full");
    g_shares[slot] = sh;
    g_shares_pending = g_share_n;
}
// A dead body looted (the loot worker returned err 0). One share per looter while it settles: a "take all" may be
// several calls, and each new call only pushes the settle time on (the first call's bag snapshot covers them all).
void body_looted(uintptr_t comp, uint32_t body, uint32_t looter_key, const BagSnap& before) noexcept {
    const uint64_t now = GetTickCount64();
    for (int k = 0; k < g_share_n; ++k) {
        ChestShare& o = g_shares[k];
        if (o.settled || o.opener != looter_key) continue;
        o.at = now;
        if (o.body) gimmick_pos(comp, o.x, o.y, o.z);
        return;
    }
    static ChestShare sh;  // the server thread only
    sh = ChestShare{};
    sh.gimmick = body;
    sh.opener = looter_key;
    sh.at = now;
    sh.before = before;
    sh.body = true;
    sh.share_id = next_share_id();
    sh.made = unix_now();
    sh.told = true;  // never announced
    if (!gimmick_pos(comp, sh.x, sh.y, sh.z)) return;
    int slot = g_share_n < kShareMax ? g_share_n++ : 0;
    if (slot == 0 && g_share_n == kShareMax && g_shares[0].gimmick) {
        for (int k = 1; k < kShareMax; ++k) {
            if (g_shares[k].at < g_shares[slot].at) slot = k;
        }
    }
    if (g_share_n == kShareMax && g_shares[slot].gimmick) persist_keep(g_shares[slot], "the shares' list was full");
    g_shares[slot] = sh;
    g_shares_pending = g_share_n;
    c_body_looted.fetch_add(1);
}
// What the opener's bag gained since the chest was opened. False = its bag cannot be read now.
bool share_settle(ChestShare& sh, uintptr_t opener_actor) noexcept {
    static BagSnap after;  // the server thread only
    if (!snap_bag(opener_actor, after)) return false;
    const BagSnap& before = sh.before;
    // By item kind, whatever slot: totals after minus totals before.
    for (int i = 0; i < after.slots; ++i) {
        const uint16_t idx = after.idx[i];
        if (idx == 0xFFFF || after.n[i] <= 0) continue;
        bool counted = false;
        for (int j = 0; j < i; ++j) counted = counted || after.idx[j] == idx;
        if (counted) continue;
        int64_t now = 0, was = 0;
        for (int j = 0; j < after.slots; ++j) now += after.idx[j] == idx && after.n[j] > 0 ? after.n[j] : 0;
        for (int j = 0; j < before.slots; ++j) was += before.idx[j] == idx && before.n[j] > 0 ? before.n[j] : 0;
        if (now <= was || sh.items == 12) continue;
        const uint32_t key = item_key_of(idx);
        if (!key) continue;
        sh.item[sh.items] = key;
        sh.n[sh.items++] = now - was;
    }
    sh.settled = true;
    return true;
}
struct PlayerAt {
    uint32_t key = 0;
    uintptr_t actor = 0, holder = 0;
    float x = 0, y = 0, z = 0;
};
int players_now(PlayerAt* out, int cap) noexcept {
    int n = 0;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        const uintptr_t slot = *reinterpret_cast<uintptr_t*>(base + 0x724B860);
        const uintptr_t sam = slot ? *reinterpret_cast<uintptr_t*>(slot) : 0;
        if (!sam) return 0;
        const uint32_t cnt = *reinterpret_cast<uint32_t*>(sam + 0x9C);
        const uintptr_t arr = *reinterpret_cast<uintptr_t*>(sam + 0xB0);
        if (!arr || cnt > 200000) return 0;
        for (uint32_t i = 0; i < cnt && n < cap; ++i) {
            const uintptr_t e = *reinterpret_cast<uintptr_t*>(arr + 8ull * i);
            const uintptr_t actor = e ? *reinterpret_cast<uintptr_t*>(e + 8) : 0;
            if (!actor) continue;
            const uint32_t key = *reinterpret_cast<uint32_t*>(actor + 0x60);
            if ((key >> 28) != 0xA || !*reinterpret_cast<uintptr_t*>(actor + 0xA0)) continue;  // a user's character
            PlayerAt p;
            p.key = key;
            p.actor = actor;
            if (!actor_pos(base, actor, p.x, p.y, p.z)) continue;
            p.holder = reinterpret_cast<uintptr_t (__fastcall*)(uintptr_t)>(base + 0x23643B0)(actor);
            if (p.holder) out[n++] = p;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return n;
}

// ---- B19 (lootr_persist, armed): the shares kept across sessions (lootr.h, lootr_persist_logic.h). The game's
// LoadSlot / SavePlayerToSlot (guest_save_host's hooks) and Lootr's entry all run on the server thread; g_near and
// g_given_guest are also touched by lootr_tick (the network thread), under g_mu. v4 (2026-11-21 late, the review of
// v3.1): a slot's file is written when the game's SavePlayerToSlot of it returns (no commit hook); a failed ledger
// write rewrites every slot file without its marks (or sets the file aside, confirmed); the host's own gives are not
// subtracted at a load (its bag is the slot's).
std::vector<lpl::Share> g_near;  // guarded by g_mu: shares waiting at their place (6 m)
struct GivenGuest {
    uint64_t id;
    uint64_t at;  // GetTickCount64 at the give (its add has run, on the server thread)
};
std::vector<GivenGuest> g_given_guest;  // guarded by g_mu: given to the joined player (added), not on the ledger yet
// v3: the in-session "the guest took it too" give runs later on the server thread (trade_probe's 0xC0DE0005 worker):
// lootr_tick queues it here, lootr_server_added moves it to g_given_guest once the add ran.
struct PendingAdd {
    uint64_t id;
    uint32_t requester, item;
    int64_t count;
    uint64_t at;
};
std::vector<PendingAdd> g_pending_adds;  // guarded by g_mu
std::vector<lpl::Mark> g_ledger_due;     // the server thread: marks of saves whose ledger write failed (tried again)
std::atomic<bool> g_ledger_retry{false};
std::atomic<size_t> g_ledger_due_n{0};  // its size, for the status line (another thread)
std::unordered_set<uint64_t> g_filed;    // guarded by g_mu: ids ever in a slot file (written or read) in this process
std::atomic<bool> g_tracking{false};     // armed once in this process (the folder known): gives are tracked
std::atomic<uint64_t> c_p_unfiled{0}, c_p_setaside{0};
// v3.1: the server thread: the share ids each slot's CURRENT file lists (written or read here), for setting aside the
// files that would give a share again whose ledger mark could not be written (review B2).
std::unordered_map<uint32_t, std::unordered_set<uint64_t>> g_slot_ids;
// v4: v3.1's record of the host's own gives (g_given_host, review B4) is gone: the host's bag is in the slot it loads,
// so a share the host got after that save is owed to it again there ("a host reload loses shares given in this
// process", review of v3.1), and one it got before that save is not in that save's file.
std::atomic<uint64_t> c_p_scrubbed{0}, c_p_aside_failed{0}, c_p_unchanged{0};
// v4.1 (review of v4, item 3): every persistence entry (LoadSlot, SavePlayerToSlot, the joined player's save, Lootr's
// 2 s entry) holds this, taken BEFORE g_mu. The logs put them all on one thread (the "server realm" tid of LoadSlot,
// SavePlayerToSlot and the 0xC0DE entries, cs83w2 / cs86f); the lock keeps it safe if a path ever is not. Recursive: an
// entry that ends up in another on the same thread does not deadlock.
// v4.2 (review of v4.1): timed. The server thread holds it across its adds (lootr_add_item); a game hook's entry
// (pmu_take) waits at most 2 s and then takes its safe path, so a save on another thread that holds a game lock the
// add needs cannot hang the game; Lootr's own 2 s entry skips a round instead of waiting.
std::recursive_timed_mutex g_pmu;
using PmuLock = std::unique_lock<std::recursive_timed_mutex>;
std::atomic<uint64_t> c_p_lock_missed{0};
std::atomic<bool> g_guest_save_missed{false};  // a joined player's save could not take g_pmu: its marks later
// v4.3 (review of v4.2, finding 3): counts clear_world() (a host load); a loop that runs adds checks it after them,
// since a load inside an add (g_pmu is recursive) would have emptied the lists it walks.
std::atomic<uint32_t> g_world_gen{0};
std::atomic<uint32_t> g_guest_marks_runs{0};  // v4.4: counts guest_marks (persist_give undoes a record only if none ran)
// v4.3 (review of v4.2, finding 7): a failed ledger write is tried again at 2, 4, 8 ... 60 s (two durable writes per
// try on the server thread; a file a scanner keeps locked must not flush the disk every 2 s).
int g_ledger_fails = 0;           // g_pmu
uint64_t g_ledger_next_ms = 0;    // g_pmu: GetTickCount64 of the next try
std::unordered_set<uint32_t> g_aside_failed;  // g_pmu: slots whose file could not be cleared (said once each)
std::atomic<int> g_near_n{0};
std::atomic<bool> g_write_due{false};
std::wstring g_pdir;  // the server thread: <co-op folder>\cdmp_lootr
struct PendingSlot {  // the server thread: a host save's snapshot whose write failed (tried again every 2 s)
    uint32_t slot = 0;
    std::vector<lpl::Share> list;
    bool committed = false;  // its save went through and the write failed: Lootr's entry tries again
    int64_t stamp = 0;
    int tries = 0;
};
std::vector<PendingSlot> g_pending;
std::atomic<uint64_t> c_p_read{0}, c_p_written{0}, c_p_failed{0}, c_p_given{0}, c_p_late{0}, c_p_ledger{0},
    c_p_stale{0};

// v4.2: a game hook's persistence entry takes g_pmu for at most 2 s (see g_pmu); false = not taken (said 10 times).
bool pmu_take(PmuLock& lk, const char* who) {
    try {
        if (lk.try_lock_for(std::chrono::seconds(2))) return true;
    } catch (...) {
    }
    c_p_lock_missed.fetch_add(1);
    static std::atomic<int> said{0};
    if (said.fetch_add(1) < 10)
        log::error("Lootr: {}: the persistence lock was not free in 2 s (thread {}): the safe path is taken", who,
                   GetCurrentThreadId());
    return false;
}
enum class ReadResult { Ok, Missing, Error };
// v4.2: `share_write`: read a file another handle may hold open for writing (blank_in_place's check).
ReadResult read_small_file(const std::wstring& path, std::string& out, DWORD& err, bool share_write = false) {
    err = 0;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_DELETE | (share_write ? FILE_SHARE_WRITE : 0), nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        err = GetLastError();
        return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND ? ReadResult::Missing : ReadResult::Error;
    }
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart >= 0 && size.QuadPart < (4ll << 20);
    if (ok) {
        out.resize(static_cast<size_t>(size.QuadPart));
        DWORD got = 0;
        ok = out.empty() || (ReadFile(f, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size());
    }
    if (!ok) err = GetLastError() ? GetLastError() : ERROR_READ_FAULT;
    CloseHandle(f);
    return ok ? ReadResult::Ok : ReadResult::Error;
}
// <path>.tmp written whole, then moved over <path>: a crash mid-write leaves the last good file. 0 = written, else the
// Windows error. No flush to the disk (the server thread waits for this) unless `durable` (v4.1: the ledger, written
// only at the joined player's saves: an OS crash must not bring back an older one, review of v4 item 6).
DWORD write_small_file(const std::wstring& path, const std::string& text, bool durable = false) {
    CreateDirectoryW(g_pdir.c_str(), nullptr);  // the folder, the first time (an existing one is fine)
    const std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
    DWORD put = 0;
    const bool ok = WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &put, nullptr) && put == text.size() &&
                    (!durable || FlushFileBuffers(f));
    const DWORD e = ok ? 0 : (GetLastError() ? GetLastError() : ERROR_WRITE_FAULT);
    CloseHandle(f);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return e;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | (durable ? MOVEFILE_WRITE_THROUGH : 0))) {
        const DWORD m = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
        DeleteFileW(tmp.c_str());
        return m;
    }
    if (durable) {
        // v4.2 (review of v4.1, item 6): MOVEFILE_WRITE_THROUGH does nothing for a rename on one volume; flushing the
        // renamed file makes the rename itself durable (NTFS writes its log up to it). v4.3 (review of v4.2, finding
        // 7): a flush that cannot be made is the write's failure (else an OS crash could bring the older file back).
        HANDLE d = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (d == INVALID_HANDLE_VALUE) return GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
        const bool flushed = FlushFileBuffers(d) != 0;
        const DWORD fe = flushed ? 0 : (GetLastError() ? GetLastError() : ERROR_WRITE_FAULT);
        CloseHandle(d);
        if (!flushed) return fe;
    }
    return 0;
}
std::wstring slot_path(uint32_t slot) { return g_pdir + L"\\slot" + std::to_wstring(slot) + L".txt"; }
std::wstring ledger_path() { return g_pdir + L"\\given_guest.txt"; }
std::wstring ledger_bak_path() { return g_pdir + L"\\given_guest.bak"; }  // v4.1: a second copy (read and unioned)
// The newest write time (v4.1: FILETIME ticks, 100 ns; it was unix seconds) of the game's own files of save slot N
// (<co-op folder>\<account>\slot<N>\*); 0 when none is found. A slot file's stamp is this value measured right after
// the save it was written for; at a load the slot's files must still show exactly that (review of v4, item 1: a newer
// save without its file, or OLDER files put back, both mean the file is not that save's).
// v4.2 (review of v4.1, item 4): -1 when more than one account folder has files in slot<N> (two Steam accounts of one
// Windows user hosting the same friend share the co-op folder and its cdmp_lootr files): no file is that save's then.
int64_t newest_save_ticks(const std::wstring& coop, uint32_t slot) {
    if (coop.empty()) return 0;
    int64_t best = 0;
    int accounts = 0;
    WIN32_FIND_DATAW a;
    HANDLE ha = FindFirstFileExW((coop + L"\\*").c_str(), FindExInfoBasic, &a, FindExSearchLimitToDirectories, nullptr, 0);
    if (ha == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || a.cFileName[0] == L'.') continue;
        const std::wstring dir = coop + L"\\" + a.cFileName + L"\\slot" + std::to_wstring(slot);
        WIN32_FIND_DATAW f;
        HANDLE hf = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &f, FindExSearchNameMatch, nullptr, 0);
        if (hf == INVALID_HANDLE_VALUE) continue;
        bool any = false;
        do {
            if (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            any = true;
            ULARGE_INTEGER u;
            u.LowPart = f.ftLastWriteTime.dwLowDateTime;
            u.HighPart = f.ftLastWriteTime.dwHighDateTime;
            const int64_t t = static_cast<int64_t>(u.QuadPart & 0x7FFFFFFFFFFFFFFFull);
            if (t > best) best = t;
        } while (FindNextFileW(hf, &f));
        FindClose(hf);
        if (any) ++accounts;
    } while (FindNextFileW(ha, &a));
    FindClose(ha);
    return accounts > 1 ? -1 : best;
}
// v4.2 (review of v4.1, item 4): the co-op folder's file system keeps 100 ns write times (NTFS, ReFS). On FAT32 (2 s)
// or exFAT (10 ms) two saves can show the same time, so nothing is kept across sessions there (checked at each load).
bool fine_file_times(const std::wstring& dir) {
    wchar_t root[MAX_PATH + 1] = {};
    wchar_t fs[64] = {};
    if (!GetVolumePathNameW(dir.c_str(), root, MAX_PATH)) return false;
    if (!GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, fs, 64)) return false;
    return _wcsicmp(fs, L"NTFS") == 0 || _wcsicmp(fs, L"ReFS") == 0;
}
lpl::Share share_of(const Taken& t) {
    lpl::Share s;
    s.id = t.share_id;
    s.for_host = t.host_waits;  // the guest took it first: the host's copy waits
    s.x = t.x;
    s.y = t.y;
    s.z = t.z;
    s.made = t.made;
    s.kinds = 1;
    s.item[0] = t.item_key;
    s.n[0] = t.count > lpl::kMaxCount ? lpl::kMaxCount : t.count;
    return s;
}
lpl::Share share_of(const ChestShare& c) {
    lpl::Share s;
    s.id = c.share_id;
    s.for_host = c.opener != kHostCharacter;  // the other player's share
    s.chest = true;
    s.x = c.x;
    s.y = c.y;
    s.z = c.z;
    s.made = c.made;
    for (int i = 0; i < c.items && i < lpl::kMaxKinds; ++i) {
        s.item[s.kinds] = c.item[i];
        s.n[s.kinds++] = c.n[i] > lpl::kMaxCount ? lpl::kMaxCount : c.n[i];
    }
    return s;
}
void near_add(const lpl::Share& s) {  // under g_mu
    if (g_near.size() >= lpl::kMaxShares) g_near.erase(g_near.begin());
    g_near.push_back(s);
    g_near_n = static_cast<int>(g_near.size());
}
// What each player is still owed now (the server thread).
std::vector<lpl::Share> persist_list() {
    std::vector<lpl::Share> list;
    {
        std::lock_guard lk(g_mu);
        for (const Taken& t : g_taken) {
            if (t.state == 0 && t.item_known && t.pos_ok && t.count > 0) list.push_back(share_of(t));
        }
        list.insert(list.end(), g_near.begin(), g_near.end());
    }
    for (int k = 0; k < g_share_n; ++k) {
        const ChestShare& c = g_shares[k];
        if (c.settled && !c.body && c.items > 0 && !c.giving) list.push_back(share_of(c));
    }
    std::stable_sort(list.begin(), list.end(), [](const lpl::Share& a, const lpl::Share& b) { return a.made < b.made; });
    return list;
}
// v4.1 (review of v4, item 4): the line a cleared file holds; no build reads it as shares (a load sets it aside as
// .unread and restores none).
constexpr char kAsideLine[] = "cdmp-lootr-set-aside 1\n";
// A file overwritten IN PLACE with kAsideLine: the handle shares read / write / delete, so a file that another handle
// keeps open without FILE_SHARE_DELETE (which stops a rename and a delete) can still be truncated; read back to check.
bool blank_in_place(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = SetFilePointer(f, 0, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER && SetEndOfFile(f) &&
                    WriteFile(f, kAsideLine, sizeof kAsideLine - 1, &put, nullptr) && put == sizeof kAsideLine - 1 &&
                    FlushFileBuffers(f);
    CloseHandle(f);
    std::string back;
    DWORD e = 0;
    // v4.2: read back sharing writes too (the other handle that stopped the rename may be a writer's).
    return ok && read_small_file(path, back, e, true) == ReadResult::Ok && back == kAsideLine;
}
// A slot's file that may no longer be that save's (its newer write failed, it lists a share the joined player saved
// while the ledger could not be written...): renamed .stale (deleted if that fails, overwritten in place if that fails
// too), so a load of the slot restores nothing rather than an older save's shares. Confirmed (v4); said once per slot
// when nothing worked (v4.1).
void set_aside(uint32_t slot, const char* why) {
    const std::wstring path = slot_path(slot), aside = path + L".stale";
    const bool moved = MoveFileExW(path.c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
    const DWORD e = moved ? 0 : GetLastError();
    const bool absent = !moved && (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND);
    g_slot_ids.erase(slot);
    if (absent) return;  // no file: a load of that slot restores nothing anyway
    const bool deleted = !moved && DeleteFileW(path.c_str()) != 0;
    const bool gone = GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES;
    const bool blanked = !gone && blank_in_place(path);
    if (gone || blanked) {
        g_aside_failed.erase(slot);
        c_p_setaside.fetch_add(1);
        static std::atomic<int> said{0};
        if (said.fetch_add(1) < 30)
            log::warn("Lootr: slot {}'s shares file {} ({}): a load of that slot restores none", slot,
                      moved ? "set aside (.stale)" : deleted ? "deleted" : "overwritten in place", why);
        return;
    }
    c_p_aside_failed.fetch_add(1);
    if (g_aside_failed.insert(slot).second)
        log::warn("Lootr: slot {}'s shares file could NOT be moved, deleted or overwritten (error {}; {}): a load of "
                  "that slot may give its shares again", slot, e, why);
}
bool persist_write_slot(PendingSlot& p) {
    // v4: a share whose give the joined player saved while the ledger could not be written is never filed again (a
    // later session would give it again if the host quit before the ledger's retry). Both on the server thread.
    if (!g_ledger_due.empty()) {
        std::unordered_set<uint64_t> due;
        for (const lpl::Mark& m : g_ledger_due) due.insert(m.id);
        std::erase_if(p.list, [&](const lpl::Share& s) { return due.count(s.id) != 0; });
    }
    {
        std::lock_guard lk(g_mu);
        for (const lpl::Share& s : p.list) g_filed.insert(s.id);  // filed (or about to be): its give is ledgered
    }
    const std::string text =
        lpl::to_text(p.list, unix_now(), std::format("slot {}: the shares as of the host's save of it", p.slot), p.stamp);
    const DWORD e = write_small_file(slot_path(p.slot), text);
    if (!e) {
        auto& ids = g_slot_ids[p.slot];
        ids.clear();
        for (const lpl::Share& s : p.list) ids.insert(s.id);
        c_p_written.fetch_add(1);
        static std::atomic<int> said{0};
        if (said.fetch_add(1) < 30)
            log::info("Lootr: slot {}'s shares written at the game's commit of that save: {} share(s) kept across "
                      "sessions", p.slot, p.list.size());
        return true;
    }
    c_p_failed.fetch_add(1);
    if (p.tries == 0) set_aside(p.slot, "its newer write failed");
    if (++p.tries <= 3 || p.tries % 100 == 0)
        log::warn("Lootr: slot {}'s shares file could NOT be written (error {}, try {}): tried again every 2 s",
                  p.slot, e, p.tries);
    return false;
}
// Each waiting share whose player stands within 6 m of its place goes into that player's bag; kinds the add refused
// (a full bag) stay for another try: the host's under a new id, the joined player's under its own (v4.4, see below).
void persist_give() {
    if (g_near_n.load() <= 0) return;
    PlayerAt pl[4];
    const int n = players_now(pl, 4);
    if (!n) return;
    std::vector<lpl::Share> due;
    std::vector<int> to;
    {
        std::lock_guard lk(g_mu);
        for (const lpl::Share& s : g_near) {
            for (int p = 0; p < n; ++p) {
                if ((pl[p].key == kHostCharacter) != s.for_host) continue;
                const float dx = pl[p].x - s.x, dy = pl[p].y - s.y, dz = pl[p].z - s.z;
                if (dx * dx + dy * dy + dz * dz > 36.0f) continue;
                due.push_back(s);
                to.push_back(p);
                break;
            }
        }
        // v4.1 (review of v4): out of the waiting list BEFORE the adds, so a save that happened inside an add could
        // never list a share whose item the bag already holds (a kind that is not added comes back below).
        for (const lpl::Share& s : due) std::erase_if(g_near, [&](const lpl::Share& x) { return x.id == s.id; });
        g_near_n = static_cast<int>(g_near.size());
    }
    if (due.empty()) return;
    const int64_t now = unix_now();
    // v4.3 (review of v4.2, finding 2): a share for the joined player is recorded as given BEFORE its adds, so a save of
    // its inside an add marks it too (losing the share beats giving it twice). v4.4 (review of v4.3, item 1): what its
    // bag did not take comes back under the SAME id (a new id would escape the old id's ledger mark in a slot file
    // written meanwhile: given twice across sessions), and when nothing at all was added the record is undone, unless
    // a save of the joined player's took it in during the adds (then the leftover is lost, never given twice).
    {
        std::lock_guard lk(g_mu);
        const uint64_t tick = GetTickCount64();
        for (const lpl::Share& s : due) {
            if (!s.for_host) g_given_guest.push_back({s.id, tick});
        }
    }
    const uint32_t gen = g_world_gen.load();
    const uint32_t marks_before = g_guest_marks_runs.load();
    std::vector<uint64_t> undo;  // the joined player's shares nothing of which was added
    std::vector<lpl::Share> rest;
    static std::atomic<int> said{0};
    for (size_t i = 0; i < due.size(); ++i) {
        const lpl::Share& s = due[i];
        const PlayerAt& p = pl[to[i]];
        lpl::Share left = s;
        left.kinds = 0;
        std::string list;
        for (int k = 0; k < s.kinds; ++k) {
            const int e = lootr_add_item(p.holder, s.item[k], static_cast<uint64_t>(s.n[k]));
            list += std::format("{}{} x{}{}", k ? ", " : "", s.item[k], s.n[k],
                                e == 0 ? "" : std::format(" (NOT added, err {})", e));
            if (e != 0) {
                left.item[left.kinds] = s.item[k];
                left.n[left.kinds++] = s.n[k];
            }
        }
        if (left.kinds < s.kinds) c_p_given.fetch_add(1);
        if (left.kinds) {
            // the part it did not get lives on by itself: the host's under a new id (its gives are not recorded), the
            // joined player's under its own (see above)
            if (left.kinds < s.kinds && s.for_host) left.id = next_share_id();
            rest.push_back(left);
            if (!s.for_host && left.kinds == s.kinds) undo.push_back(s.id);
        }
        if (left.kinds < s.kinds || said.fetch_add(1) < 20)
            log::info("Lootr: a kept share ({} at ({:.1f}, {:.1f}, {:.1f}), {} h old): 0x{:08X} is there: [{}]",
                      s.chest ? "a chest's" : "a world item", s.x, s.y, s.z, (now - s.made) / 3600, p.key, list);
        // v4.3 (review of v4.2, finding 3): a load inside an add replaced the world: nothing of the old one comes back
        // (the shares not reached yet were the old world's too; the joined player's are recorded: lost, never twice).
        if (g_world_gen.load() != gen) return;
    }
    std::lock_guard lk(g_mu);
    if (!undo.empty() && g_guest_marks_runs.load() == marks_before) {
        // only the record made above (the last one of that id): an earlier give of the same share that did add some
        // kinds keeps its own record
        for (const uint64_t id : undo) {
            for (auto it = g_given_guest.rbegin(); it != g_given_guest.rend(); ++it) {
                if (it->id == id) {
                    g_given_guest.erase(std::next(it).base());
                    break;
                }
            }
        }
    }
    for (const lpl::Share& r : rest) near_add(r);
    g_near_n = static_cast<int>(g_near.size());
}
// A chest share that would be forgotten (its 3 hours, the list full) waits at its place instead, while armed.
void persist_keep(const ChestShare& c, const char* why) noexcept {
    if (!g_armed.load() || c.body || !c.settled || c.items <= 0 || !c.share_id) return;
    try {
        std::lock_guard lk(g_mu);
        near_add(share_of(c));
        c_p_late.fetch_add(1);
        log::info("Lootr: chest 0x{:08X}'s share for the {} ({}) now waits at its place (6 m), across sessions too",
                  c.gimmick, c.opener != kHostCharacter ? "host" : "guest", why);
    } catch (...) {
    }
}
// A share the joined player got (not under g_mu): on the ledger at its next save.
void persist_given_guest(uint64_t id) noexcept {
    if (!g_tracking.load() || !id) return;
    try {
        std::lock_guard lk(g_mu);
        g_given_guest.push_back({id, GetTickCount64()});
    } catch (...) {
    }
}
// v4: every slot<N>.txt in the folder rewritten without the shares in `marks`, each read back to check; a file that
// cannot be read, is not one this build reads, cannot be written or does not read back the same is set aside. The
// server thread (as ledger_write, its only caller).
void scrub_slot_files(const std::vector<lpl::Mark>& marks) {
    if (marks.empty() || g_pdir.empty()) return;
    std::unordered_set<uint64_t> ids;
    for (const lpl::Mark& m : marks) ids.insert(m.id);
    std::vector<uint32_t> slots;
    WIN32_FIND_DATAW f;
    HANDLE h = FindFirstFileExW((g_pdir + L"\\slot*.txt").c_str(), FindExInfoBasic, &f, FindExSearchNameMatch, nullptr, 0);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring name = f.cFileName;  // slot<digits>.txt only (not .tmp / .stale / .unread)
            if (name.size() < 9 || name.compare(0, 4, L"slot") != 0 || name.compare(name.size() - 4, 4, L".txt") != 0)
                continue;
            uint32_t n = 0;
            bool digits = true;
            for (size_t i = 4; i + 4 < name.size() && digits; ++i) {
                digits = name[i] >= L'0' && name[i] <= L'9' && n < 100000000u;
                if (digits) n = n * 10 + static_cast<uint32_t>(name[i] - L'0');
            }
            if (digits) slots.push_back(n);
        } while (FindNextFileW(h, &f));
        FindClose(h);
    }
    const int64_t now = unix_now();
    for (const uint32_t slot : slots) {
        std::string text;
        DWORD e = 0;
        const ReadResult r = read_small_file(slot_path(slot), text, e);
        if (r == ReadResult::Missing || (r == ReadResult::Ok && text == kAsideLine)) continue;  // nothing / cleared
        lpl::Rewrite w;
        if (r == ReadResult::Ok) w = lpl::without_ids(text, ids, now);
        if (r == ReadResult::Ok && w.ok && !w.changed) continue;  // it lists none of them
        bool done = false;
        if (r == ReadResult::Ok && w.ok && write_small_file(slot_path(slot), w.text) == 0) {
            std::string back;
            DWORD e2 = 0;
            done = read_small_file(slot_path(slot), back, e2) == ReadResult::Ok && back == w.text;
        }
        if (done) {
            if (const auto it = g_slot_ids.find(slot); it != g_slot_ids.end()) {
                for (const uint64_t id : ids) it->second.erase(id);
            }
            c_p_scrubbed.fetch_add(1);
            log::warn("Lootr: slot {}'s shares file rewritten without the share(s) the joined player saved (the ledger "
                      "could not be written yet)", slot);
        } else {
            set_aside(slot, "it lists a share the joined player saved and could not be rewritten without it");
        }
    }
}
// v4.1 (review of v4, item 6): the ledger and its second copy (given_guest.bak, written right after it, both flushed),
// their marks together: an OS crash can leave either one old, zeroed or missing, and a mark in either one counts.
// v4.2 (review of v4.1, item 3): false (nothing trusted, lpl::ledger_trusted) when a copy exists but cannot be read now
// (it may be the newer one), or when the only copy there is damaged. A cleared copy (kAsideLine: a .bak whose write
// failed, see ledger_write) counts as none.
bool read_ledger(lpl::Ledger& out, DWORD& err, int64_t now) {
    out = lpl::Ledger{};
    out.header_ok = true;
    bool damaged = false, unreadable = false;
    int good = 0;
    for (const std::wstring& path : {ledger_path(), ledger_bak_path()}) {
        std::string text;
        DWORD e = 0;
        const ReadResult r = read_small_file(path, text, e);
        if (r == ReadResult::Missing || (r == ReadResult::Ok && text == kAsideLine)) continue;
        if (r == ReadResult::Error) {
            unreadable = true;
            if (e) err = e;
            continue;
        }
        const lpl::Ledger l = lpl::ledger_parse(text, now);
        if (!l.header_ok) {
            damaged = true;
            continue;
        }
        ++good;
        out.marks.insert(out.marks.end(), l.marks.begin(), l.marks.end());
        out.bad_lines += l.bad_lines;
    }
    return lpl::ledger_trusted(good, damaged, unreadable);
}
// The ledger with `add` (and every mark still due) written; false = not written (they all stay due). Server thread.
bool ledger_write(const std::vector<lpl::Mark>& add) {
    const int64_t now = unix_now();
    std::vector<lpl::Mark> all = g_ledger_due;
    all.insert(all.end(), add.begin(), add.end());
    if (all.empty()) return true;
    lpl::Ledger l;
    DWORD e = 0, w = 0;
    std::string text;
    bool bak_ok = true;
    if (read_ledger(l, e, now) &&
        (w = write_small_file(ledger_path(), text = lpl::ledger_text(l.marks, all, now), true)) == 0) {
        // The second copy. v4.2 (review of v4.1, item 3): one that could not be written must never be read alone
        // later (it lacks these marks): deleted, or cleared in place (read as no copy); if neither works the marks stay
        // due like a failed write (the next try writes both copies again).
        if (const DWORD wb = write_small_file(ledger_bak_path(), text, true); wb != 0) {
            const std::wstring bak = ledger_bak_path();
            bool gone = DeleteFileW(bak.c_str()) != 0;
            if (!gone) {
                const DWORD de = GetLastError();
                gone = de == ERROR_FILE_NOT_FOUND || de == ERROR_PATH_NOT_FOUND;
            }
            const bool cleared = !gone && blank_in_place(bak);
            bak_ok = gone || cleared;
            w = bak_ok ? 0 : wb;
            static std::atomic<int> said_bak{0};
            if (said_bak.fetch_add(1) < 3)
                log::warn("Lootr: the ledger's second copy could not be written (error {}): {}", wb,
                          gone      ? "removed (the ledger itself is written)"
                          : cleared ? "cleared (the ledger itself is written)"
                                    : "nor removed: the marks stay due");
        }
    }
    if (e != 0 || w != 0 || !bak_ok || text.empty()) {
        g_ledger_due = std::move(all);
        g_ledger_due_n = g_ledger_due.size();
        g_ledger_retry = true;
        // v3.1 (review B2) / v4: until these marks are on the ledger, ANY slot file listing one of them (this session's
        // or an earlier one's, which g_slot_ids does not know) would give it again in a later session (the host may
        // quit before the retry succeeds): every slot file is rewritten without them, or set aside.
        scrub_slot_files(g_ledger_due);
        c_p_failed.fetch_add(1);
        // v4.3: the next try in 2, 4, 8, 16, 32, then every 60 s (a new save of the joined player tries at once)
        g_ledger_fails = g_ledger_fails < 30 ? g_ledger_fails + 1 : 30;
        const uint64_t wait_ms = g_ledger_fails >= 6 ? 60000ull : 2000ull << (g_ledger_fails - 1);
        g_ledger_next_ms = GetTickCount64() + wait_ms;
        static std::atomic<int> said{0};
        if (said.fetch_add(1) < 20)
            log::warn("Lootr: the ledger could NOT be written (read {} / write {}): its {} mark(s) are tried again in "
                      "{} s", e, w, g_ledger_due.size(), wait_ms / 1000);
        return false;
    }
    g_ledger_fails = 0;
    g_ledger_next_ms = 0;
    c_p_ledger.fetch_add(all.size());  // v4.2: marks written on a retry count too
    g_ledger_due.clear();
    g_ledger_due_n = 0;
    g_ledger_retry = false;
    return true;
}
// The joined player's save went through: the gives recorded before it go on the ledger (g_pmu held). v3: every give
// recorded before this save (all on the server thread, as this call) is in it: each one whose share was ever in a slot
// file or waits in a snapshot is marked; the others can never come back (no mark). A failed ledger write keeps the
// marks due (persist_server_work tries again every 2 s).
void guest_marks() {
    g_guest_marks_runs.fetch_add(1);  // v4.4: persist_give undoes a record only when no save took it in meanwhile
    const int64_t now = unix_now();
    std::vector<lpl::Mark> add;
    {
        std::unordered_set<uint64_t> waiting;
        for (const PendingSlot& p : g_pending) {
            for (const lpl::Share& s : p.list) waiting.insert(s.id);
        }
        std::lock_guard lk(g_mu);
        for (const GivenGuest& g : g_given_guest) {
            if (g_filed.count(g.id) || waiting.count(g.id)) add.push_back({g.id, now});
            else c_p_unfiled.fetch_add(1);
        }
        g_given_guest.clear();
    }
    if (!add.empty()) ledger_write(add);
}
// Lootr's entry (the server thread, every 2 s while armed with work): a failed write again, the waiting shares.
void persist_server_work() noexcept {
    try {
        // v4.2: a joined player's save whose entry could not take the lock (pmu_take): its marks now (every give
        // recorded so far: a give it did not save yet costs that share, never a second give).
        if (g_guest_save_missed.exchange(false)) guest_marks();
        if (!g_ledger_due.empty() && GetTickCount64() >= g_ledger_next_ms) ledger_write({});  // v4.3: backed off
        bool due = false;
        for (size_t i = 0; i < g_pending.size();) {
            PendingSlot& p = g_pending[i];
            if (p.committed && persist_write_slot(p)) {
                g_pending.erase(g_pending.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            due = due || p.committed;
            ++i;
        }
        g_write_due = due;
        // v4.1 (review of v4, item 2): only while armed. A ledger retry keeps this entry running after a load that could
        // not arm, and the shares of the world before that load must not be given in this one.
        if (g_armed.load()) persist_give();
    } catch (...) {
    }
}
// A host load: the world before it is gone, and so are its shares (g_pmu held). v4.1 (review of v4, item 2): before any
// early return of a load that cannot arm (one that left them waiting, and a ledger retry kept giving them).
void clear_world() {
    g_armed = false;
    g_world_gen.fetch_add(1);  // v4.3: a loop with adds running (a load inside an add) stops after them
    {
        std::lock_guard lk(g_mu);
        g_taken.clear();
        g_near.clear();
        g_near_n = 0;
    }
    for (int k = 0; k < g_share_n; ++k) g_shares[k] = ChestShare{};
    g_share_n = 0;
    g_shares_pending = 0;
    g_pending.clear();
    g_write_due = false;
}

}  // namespace

void lootr_set_chests(bool on) { g_chests_on = on; }
void lootr_set_persist(bool on) { g_persist_on = on; }

// B19 (lootr.h): the host loaded world slot N. The world is that save's now: what was taken since is not taken any
// more (the in-memory shares go), and that save's own shares come back, less the ledger and what the joined player got
// in this process. Arms the persistence (its hooks are live: this call came through them).
// B19 v4.2 (review of v4.1, item 1): any load of the host's character, whatever its caller, first goes through
// lootr_host_loading (before the game's call): it disarms and drops the old world's shares, so a save inside the load
// writes nothing and a load that does not end here gives nothing of the old world. Only the character's own 3093 call
// that went through ends here.
void lootr_host_loading() noexcept {
    if (!g_persist_on || !g_on) return;
    g_armed = false;  // first, lock or not: nothing is given or written while not armed
    try {
        PmuLock plk(g_pmu, std::defer_lock);
        if (!pmu_take(plk, "a host load")) return;  // its shares stay in memory, never given (not armed)
        clear_world();
    } catch (...) {
    }
}
void lootr_host_loaded(uint32_t slot) noexcept {
    if (!g_persist_on || !g_on) return;
    g_armed = false;
    try {
        PmuLock plk(g_pmu, std::defer_lock);
        if (!pmu_take(plk, "a host load's restore")) return;  // not armed: nothing kept across sessions in this world
        clear_world();  // lootr_host_loading did it before the game's call; again (a give may have run since)
        const std::wstring coop = save_hooks_coop_dir();
        if (coop.empty()) {
            static bool said = false;
            if (!said) {
                said = true;
                log::warn("Lootr: lootr_persist is on, but this run saves in no co-op folder: nothing is kept across "
                          "sessions");
            }
            return;
        }
        // v4.2 (review of v4.1, item 4): FAT32 (2 s) and exFAT (10 ms) file times cannot tell two saves apart.
        if (!fine_file_times(coop)) {
            static bool said = false;
            if (!said) {
                said = true;
                log::warn("Lootr: lootr_persist is on, but the co-op folder's drive keeps coarse file times (not NTFS "
                          "or ReFS): nothing is kept across sessions");
            }
            return;
        }
        g_pdir = coop + L"\\cdmp_lootr";
        std::string text;
        DWORD e1 = 0, e2 = 0;
        const ReadResult r1 = read_small_file(slot_path(slot), text, e1);
        const int64_t now = unix_now();
        lpl::Ledger l;
        const bool ledger_ok = read_ledger(l, e2, now);
        if (r1 == ReadResult::Error || !ledger_ok) {
            // nothing read, nothing written: a later run reads them as they are
            log::warn("Lootr: slot {}'s shares or the ledger could not be read (errors {} / {}{}): nothing is kept "
                      "across sessions in this run", slot, e1, e2,
                      ledger_ok ? "" : ", the ledger (or its copy) is unreadable or not one this build reads");
            return;
        }
        lpl::Parsed p;
        if (r1 == ReadResult::Ok) p = lpl::from_text(text, now);
        {
            auto& ids = g_slot_ids[slot];  // what this slot's current file lists
            ids.clear();
            if (p.header_ok) {
                for (const lpl::Share& s : p.shares) ids.insert(s.id);
            }
        }
        if (r1 == ReadResult::Ok && !p.header_ok) {
            const std::wstring aside = slot_path(slot) + L".unread";
            MoveFileExW(slot_path(slot).c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING);
            log::warn("Lootr: slot {}'s shares file is not one this build reads: set aside (.unread), none restored",
                      slot);
        }
        // v4.1 (review of v4, item 1): the file's stamp is the slot's newest save-file time right after the save it was
        // written for; the slot's files must show EXACTLY that now. Newer = a later save whose file is missing; older =
        // older files put back (a restored backup, the game's own slotN_backup); none = nothing to compare with. In
        // every such case nothing is restored (losing the shares beats giving them twice).
        const int64_t saved = newest_save_ticks(coop, slot);
        const bool stale = p.header_ok && !lpl::stamp_matches(p.stamp, saved);
        if (stale) {
            c_p_stale.fetch_add(1);
            log::warn("Lootr: slot {}'s shares file (stamp {}) is not that save's (its files now: {}): not restored", slot,
                      p.stamp,
                      saved < 0     ? std::string("more than one account's saves in the co-op folder")
                      : saved == 0  ? std::string("none found")
                      : p.stamp <= 0 ? std::format("{}; the file has no stamp", saved)
                                     : std::format("{}, {}", saved, saved > p.stamp ? "newer" : "older"));
        }
        std::vector<uint64_t> gone = lpl::ids_of(l.marks);
        std::vector<lpl::Share> back;
        {
            std::lock_guard lk(g_mu);
            for (const GivenGuest& g : g_given_guest) gone.push_back(g.id);
            for (const PendingAdd& a : g_pending_adds) gone.push_back(a.id);  // queued gives: maybe added already
            for (const lpl::Mark& m : g_ledger_due) gone.push_back(m.id);    // saved, not on the ledger file yet
            if (p.header_ok) {
                for (const lpl::Share& s : p.shares) g_filed.insert(s.id);
            }
            if (p.header_ok && !stale) back = lpl::minus_ids(p.shares, gone);
            g_near = back;
            g_near_n = static_cast<int>(g_near.size());
        }
        g_armed = true;
        g_tracking = true;
        c_p_read.fetch_add(back.size());
        int for_host = 0;
        for (const lpl::Share& s : back) for_host += s.for_host ? 1 : 0;
        log::info("Lootr: shares kept across sessions ARMED: the host loaded slot {}: {} share(s) wait at their place "
                  "({} for the host, {} for the friend; in the file {}{}, older than 30 days {}, unreadable lines {})",
                  slot, back.size(), for_host, back.size() - static_cast<size_t>(for_host), p.shares.size(),
                  stale ? std::string(", none restored: not that save's")
                        : std::format(", on the ledger or given before {}", p.shares.size() - back.size()),
                  p.expired, p.bad_lines);
    } catch (...) {
    }
}
// B19 v4: the host saved world slot N and SavePlayerToSlot returned 0. The game writes that save's own files INSIDE the
// call, on this (the server) thread, for an autosave and a manual save alike (the native base's
// 5-guardado-anfitrion.md section 3; FlushPendingSaveData does no I/O and names a slot only for some autosaves, so v3's
// commit point missed every manual save, review of v3.1). So what each player is owed at this moment goes to
// slot<N>.txt now; a failed write sets the slot's old file aside at once and is tried again every 2 s (Lootr's entry)
// with this snapshot, which stays the save's.
// v4.1 (review of v4, item 1): the stamp is the slot's newest save-file time measured now (FILETIME ticks).
// v4.2 (review of v4.1, item 2): compared with the same time measured BEFORE the game's call (lootr_host_saving):
// the file is written only when the save moved the slot's files; when it did not, the existing file stays only if it
// is stamped with exactly their time (else it would carry the stamp of files it does not belong to); anything else
// sets it aside (lpl::after_save). Lock not free: nothing written (the slot's files moved: the old file is stale).
int64_t lootr_host_saving(uint32_t slot) noexcept {
    if (!g_armed.load()) return -1;
    try {
        return newest_save_ticks(save_hooks_coop_dir(), slot);
    } catch (...) {
        return -1;
    }
}
void lootr_host_saved(uint32_t slot, int64_t before) noexcept {
    if (!g_armed.load()) return;
    try {
        PmuLock plk(g_pmu, std::defer_lock);
        if (!pmu_take(plk, "a host save")) return;
        // (v4.4, the review of v4.3: no "only the loaded slot" rule: the host's saves go to other slots as a matter of
        // course, autosaves rotating 0-2 and manual saves 100-108, and each must get its own shares file.)
        const int64_t after = newest_save_ticks(save_hooks_coop_dir(), slot);
        // v4.4 (review of v4.3, item 4): a save that did not move the slot's files while an earlier save's snapshot
        // still waits to be written for them (its write failed; the old file was set aside then): the snapshot is
        // still theirs and stays.
        if (before >= 0 && after > 0 && after == before &&
            std::any_of(g_pending.begin(), g_pending.end(),
                        [&](const PendingSlot& q) { return q.slot == slot && q.stamp == after; })) {
            c_p_unchanged.fetch_add(1);
            return;
        }
        int64_t stamp = 0;
        {
            std::string old;
            DWORD e = 0;
            if (read_small_file(slot_path(slot), old, e) == ReadResult::Ok) {
                const lpl::Parsed was = lpl::from_text(old, unix_now());
                if (was.header_ok) stamp = was.stamp;
            }
        }
        const lpl::AfterSave what = lpl::after_save(before, after, stamp);
        if (what == lpl::AfterSave::Keep) {
            c_p_unchanged.fetch_add(1);
            static std::atomic<int> said{0};
            if (said.fetch_add(1) < 10)
                log::info("Lootr: slot {}'s save did not change its files: its shares file stays as it is", slot);
            return;
        }
        // an older save's snapshot still waiting for this slot: superseded (Write) or no longer the files' (SetAside)
        std::erase_if(g_pending, [&](const PendingSlot& q) { return q.slot == slot; });
        if (what == lpl::AfterSave::SetAside) {
            set_aside(slot, after < 0    ? "more than one account's saves in the co-op folder"
                            : after == 0 ? "the slot's save files were not found after its save"
                            : before < 0 ? "its save was not measured before the game's call"
                            : after < before
                                ? "the slot's save files went back during its save"
                                : "its save did not change the slot's files, and the shares file is not theirs");
            return;
        }
        PendingSlot p;
        p.slot = slot;
        p.list = persist_list();
        p.committed = true;
        p.stamp = after;
        if (!persist_write_slot(p)) {
            g_pending.push_back(std::move(p));
            g_write_due = true;
        }
    } catch (...) {
    }
}
// B19 v4: no longer a commit point (see lootr_host_saved): nothing to do. Kept for guest_save_host's call.
void lootr_host_flushed(uint32_t slot) noexcept { (void)slot; }
// B19: the joined player's save went through: the shares it got go on the ledger (guest_marks). Nothing is dropped
// until the ledger is written. v4.2: lock not free in 2 s: Lootr's next 2 s entry writes the marks.
void lootr_guest_saved() noexcept {
    if (!g_tracking.load()) return;
    try {
        PmuLock plk(g_pmu, std::defer_lock);
        if (!pmu_take(plk, "the joined player's save")) {
            g_guest_save_missed = true;
            return;
        }
        guest_marks();
    } catch (...) {
    }
}
// v3 (trade_probe's 0xC0DE0005 worker, the server thread): an in-session give ran its add.
void lootr_server_added(uint32_t requester, uint32_t item, uint64_t count, bool ok) noexcept {
    if (!g_tracking.load() || requester == kHostCharacter) return;
    try {
        std::lock_guard lk(g_mu);
        for (size_t i = 0; i < g_pending_adds.size(); ++i) {
            const PendingAdd& a = g_pending_adds[i];
            if (a.requester != requester || a.item != item || a.count != static_cast<int64_t>(count)) continue;
            if (ok) g_given_guest.push_back({a.id, GetTickCount64()});
            g_pending_adds.erase(g_pending_adds.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    } catch (...) {
    }
}
void lootr_set_body_share(bool on) { g_body_share_on = on; }

void lootr_chest_shares(uintptr_t host_actor) noexcept {
    if (!g_on || !host_actor) return;
    // v4.1: with lootr_persist on, this entry (which also walks g_shares, cleared by a host load) holds the persistence
    // lock; with it off nothing here changes. v4.2: not free (a game hook's entry has it): this round is skipped.
    PmuLock plock(g_pmu, std::defer_lock);  // (pl is the players' array below)
    if (g_persist_on) {
        try {
            if (!plock.try_lock()) return;
        } catch (...) {
            return;
        }
    }
    if (g_armed.load() || g_ledger_retry.load() || g_guest_save_missed.load()) persist_server_work();  // B19
    if (!g_share_n) return;
    PlayerAt pl[4];
    const int n = players_now(pl, 4);
    const uint64_t now = GetTickCount64();
    for (int k = g_share_n - 1; k >= 0; --k) {
        ChestShare& sh = g_shares[k];
        bool done = false;
        if (now - sh.at > (sh.body ? kBodyShareKeepMs : kShareKeepMs)) {
            if (!sh.body) c_chest_expired.fetch_add(1);
            if (!sh.body && !(g_armed.load() && sh.settled && sh.items > 0))
                log::info("Lootr: the share of chest 0x{:08X} was not fetched in 3 hours: forgotten", sh.gimmick);
            if (!sh.body) persist_keep(sh, "not fetched in 3 hours");
            done = true;
        }
        if (!done && !sh.settled) {
            if (now - sh.at < kSettleMs) continue;
            bool read = false;
            for (int p = 0; p < n; ++p) {
                if (pl[p].key == sh.opener) read = share_settle(sh, pl[p].actor);
            }
            if (!read) {
                if (now - sh.at < 20000) continue;  // the opener is not there right now: try again
                done = true;
            } else if (!sh.items) {
                if (!sh.body)
                    log::info("Lootr: chest 0x{:08X} opened by 0x{:08X}: its bag gained nothing we can see (no share)",
                              sh.gimmick, sh.opener);
                done = true;
            } else {
                std::string list;
                for (int i = 0; i < sh.items; ++i) list += std::format("{}{} x{}", i ? ", " : "", sh.item[i], sh.n[i]);
                log::info("Lootr: {} 0x{:08X} at ({:.1f}, {:.1f}, {:.1f}) {} by 0x{:08X}: {} item kind(s) [{}] "
                          "kept as the other player's share", sh.body ? "body" : "chest", sh.gimmick, sh.x, sh.y, sh.z,
                          sh.body ? "looted" : "opened", sh.opener, sh.items, list);
            }
        }
        for (int p = 0; p < n && !done; ++p) {
            if (pl[p].key == sh.opener) continue;
            const float dx = pl[p].x - sh.x, dy = pl[p].y - sh.y, dz = pl[p].z - sh.z;
            if (dx * dx + dy * dy + dz * dz > (sh.body ? 900.0f : 36.0f)) continue;  // a body: 30 m (the fight)
            static BagSnap bag0, bag1;  // the server thread only
            const bool snap0 = snap_bag(pl[p].actor, bag0);
            std::string list;
            sh.giving = true;  // B19 v4.2: a save inside an add below does not list this share
            // B19 v4.3 (review of v4.2, finding 2): recorded as given BEFORE the adds (a save of the joined player's
            // inside one then marks it; v4: the host's own gives are not recorded)
            if (pl[p].key != kHostCharacter) persist_given_guest(sh.share_id);
            const uint32_t gen = g_world_gen.load();
            for (int i = 0; i < sh.items; ++i) {
                const int e = lootr_add_item(pl[p].holder, sh.item[i], static_cast<uint64_t>(sh.n[i]));
                list += std::format("{}{} x{}{}", i ? ", " : "", sh.item[i], sh.n[i],
                                    e == 0 ? "" : std::format(" (NOT added, err {})", e));
            }
            // B19 v4.3 (finding 3): a load inside an add emptied g_shares (clear_world): nothing here is valid now
            if (g_world_gen.load() != gen) return;
            int64_t gained = -1;
            if (snap0 && snap_bag(pl[p].actor, bag1)) {
                gained = 0;
                for (int i = 0; i < bag1.slots; ++i) gained += bag1.idx[i] != 0xFFFF && bag1.n[i] > 0 ? bag1.n[i] : 0;
                for (int i = 0; i < bag0.slots; ++i) gained -= bag0.idx[i] != 0xFFFF && bag0.n[i] > 0 ? bag0.n[i] : 0;
            }
            (sh.body ? c_body_shared : c_chest_shared).fetch_add(1);
            if (sh.told && !sh.body) chest_event(2, pl[p].key, sh);
            static std::atomic<int> said_body{0};
            if (!sh.body || said_body.fetch_add(1) < 40)
                log::info("Lootr: {} 0x{:08X}: 0x{:08X} is there and got its own share [{}] ({} by 0x{:08X} {} s ago) | "
                          "its bag gained {} piece(s)", sh.body ? "body" : "chest", sh.gimmick, pl[p].key, list,
                          sh.body ? "looted" : "opened", sh.opener, (now - sh.at) / 1000, gained);
            done = true;
        }
        if (!done && sh.settled && !sh.told) {  // the other player is somewhere else: the layer tells them
            for (int p = 0; p < n; ++p) {
                if (pl[p].key == sh.opener) continue;
                sh.told = true;
                chest_event(1, pl[p].key, sh);
                log::info("Lootr: chest 0x{:08X}: the share waits for 0x{:08X}, who is not at the chest", sh.gimmick,
                          pl[p].key);
                break;
            }
        }
        if (done) {
            g_shares[k] = g_shares[g_share_n - 1];
            g_shares[--g_share_n] = ChestShare{};
        }
    }
    g_shares_pending = g_share_n;
}

}  // namespace cdmp::session2

// The Co-op+ layer (host) polls this: up to `cap` events of 20 bytes each (u8 kind | 3 B | u32 player | f32 x, y, z |
// u16 kinds | u16 pieces), taken out of the queue.
extern "C" __declspec(dllexport) int cdmp_chest_events(uint8_t* out, int cap) {
    using namespace cdmp::session2;
    if (!out || cap <= 0) return 0;
    AcquireSRWLockExclusive(&g_chest_ev_lock);
    const int n = g_chest_ev_n < cap ? g_chest_ev_n : cap;
    for (int i = 0; i < n; ++i) {
        const ChestEvent& e = g_chest_ev[i];
        uint8_t* p = out + 20 * i;
        std::memset(p, 0, 20);
        p[0] = e.kind;
        std::memcpy(p + 4, &e.player, 4);
        std::memcpy(p + 8, &e.x, 4);
        std::memcpy(p + 12, &e.y, 4);
        std::memcpy(p + 16, &e.z, 4);
    }
    for (int i = n; i < g_chest_ev_n; ++i) g_chest_ev[i - n] = g_chest_ev[i];
    g_chest_ev_n -= n;
    ReleaseSRWLockExclusive(&g_chest_ev_lock);
    return n;
}

namespace cdmp::session2 {

void lootr_chest_scan(uintptr_t host_actor) noexcept {
    if (!g_probe || !host_actor) return;
    static ChestScan s;  // the server thread only
    s = ChestScan{};
    chest_scan_read(s, reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), host_actor);
    std::sort(s.f, s.f + s.n, [](const ChestFound& a, const ChestFound& b) { return a.dist < b.dist; });
    log::info("Lootr probe: chest scan {} (stage {}): host at ({:.0f}, {:.0f}, {:.0f}) | {} server actors, {} gimmicks, "
              "{} with a Drop event, {} scene objects | the nearest {}:", g_chest_scans.fetch_add(1) + 1, s.stage, s.hx,
              s.hy, s.hz, s.actors, s.gimmicks, s.droppers, s.objects, s.n);
    for (int k = 0; k < s.n; ++k) {
        const ChestFound& f = s.f[k];
        log::info("Lootr probe:   0x{:08X} '{}' (row {}) at ({:.1f}, {:.1f}, {:.1f}) {:.0f} m | rolls: row {} left {} | "
                  "{}", f.key, f.name, f.idx, f.x, f.y, f.z, std::sqrt(f.dist), f.row_rolls,
                  f.left == 0xFFFFFFFF ? std::string("?") : std::to_string(f.left),
                  f.mode == 0 ? "on the ground" : f.mode == 1 ? "into the bag" : "mode ?");
    }
    for (int k = 0; k < s.near_n; ++k) {
        const ChestFound& f = s.near_[k];
        log::info("Lootr probe:   within 8 m: 0x{:08X} '{}' (row {}) at ({:.1f}, {:.1f}, {:.1f}) {:.1f} m | state hash "
                  "0x{:08X} | {}", f.key, f.name, f.idx, f.x, f.y, f.z, std::sqrt(f.dist), f.left,
                  f.row_rolls ? "has a Drop event" : "no Drop event");
    }
    std::sort(s.wide, s.wide + s.wide_n, [](const ChestFound& a, const ChestFound& b) { return a.dist < b.dist; });
    log::info("Lootr probe: scene objects with drop rolls left: {} of {} kinds so far; {} look like chests:",
              s.with_rolls, g_row_name_n, s.wide_n);
    for (int k = 0; k < s.wide_n && k < 24; ++k) {
        const ChestFound& f = s.wide[k];
        log::info("Lootr probe:   chest? '{}' (row {}) at ({:.1f}, {:.1f}, {:.1f}) {:.0f} m | rolls left {} | key "
                  "{:02X}{:02X}{:02X}{:02X}", f.name, f.idx, f.x, f.y, f.z, std::sqrt(f.dist), f.left, f.key16[0],
                  f.key16[1], f.key16[2], f.key16[3]);
    }
    std::string names;  // the kinds not said yet (the vocabulary), 12 a line
    int on_line = 0, lines = 0;
    for (int k = 0; k < g_row_name_n && lines < 60; ++k) {
        RowName& rn = g_row_names[k];
        if (rn.said) continue;
        rn.said = true;
        names += std::format("{}{} x{}", on_line ? ", " : "", rn.name[0] ? rn.name : "?", rn.seen);
        if (++on_line == 12) {
            log::info("Lootr probe:   kinds: {}", names);
            names.clear();
            on_line = 0;
            ++lines;
        }
    }
    if (on_line) log::info("Lootr probe:   kinds: {}", names);
    for (int k = 0; k < g_row_name_n; ++k) g_row_names[k].seen = 0;
}

std::string lootr_describe();

void lootr_set_kill_hits(int mode) { g_kill_hits = mode; }

void lootr_start(bool on, bool probe, bool guest, bool bodies) {
    g_on = on && !guest;
    g_probe = probe && !guest;
    g_bodies_on = bodies && g_on;
    g_persist_on = g_persist_on && g_on;
    if (!g_session_start.load()) g_session_start = unix_now();
    if (g_persist_on)
        log::info("Lootr: shares kept across sessions (lootr_persist) armed at the host's load of a co-op world slot: "
                  "<co-op folder>\\cdmp_lootr\\slot<N>.txt per save, given_guest.txt");
    if (!g_on && !g_probe) return;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint8_t head[2]{};
    if (!game::safe_read(base + 0x2DA2F90, head, sizeof head)) return;
    if (auto h = safetyhook::InlineHook::create(reinterpret_cast<void*>(base + 0x2DA2F90), pickup_detour)) {
        g_pickup = std::move(*h);
    }
    if (g_probe || g_bodies_on || (g_on && g_body_share_on)) {
        static const uint8_t kHead[10] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x44, 0x88, 0x4C, 0x24, 0x20};
        uint8_t got[10] = {};
        if (game::safe_read(base + 0x2E8E390, got, sizeof got) && std::memcmp(got, kHead, sizeof got) == 0) {
            if (auto h = safetyhook::InlineHook::create(reinterpret_cast<void*>(base + 0x2E8E390), body_loot_detour)) {
                g_body_loot = std::move(*h);
            }
        }
        if (g_bodies_on && g_body_loot) {
            static const uint8_t kLea[7] = {0x48, 0x8D, 0x35, 0x96, 0xF2, 0x0D, 0x03};
            uint8_t at[7] = {};
            if (game::safe_read(base + 0x2E8E723, at, sizeof at) && std::memcmp(at, kLea, sizeof at) == 0) {
                if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(base + 0x2E8E723), on_keep_body)) {
                    g_keep_body_hook = std::move(*h);
                }
            }
        }
        log::info("Lootr: the body loot worker {}{}", g_body_loot ? "hooked" : "NOT hooked",
                  !g_bodies_on ? (g_on && g_body_share_on ? ": what one player loots from a body, the other player "
                                                            "nearby gets too" : " (log only)")
                  : g_keep_body_hook ? ": enemy loot is per player (the second looter gets its own roll)"
                                     : ": per player, but the keep-the-body hook is NOT in");
    }
    if (g_probe || (g_on && g_chests_on)) {
        static const uint8_t kDrop[15] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C,
                                          0x24, 0x18};
        static const uint8_t kRoll[15] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C,
                                          0x24, 0x20};
        uint8_t got[15] = {};
        if (game::safe_read(base + 0x2DBA1A0, got, sizeof got) && std::memcmp(got, kDrop, sizeof got) == 0) {
            if (auto h = safetyhook::InlineHook::create(reinterpret_cast<void*>(base + 0x2DBA1A0), drop_detour)) {
                g_drop = std::move(*h);
            }
        }
        if (g_probe && game::safe_read(base + 0x2DB93D0, got, sizeof got) && std::memcmp(got, kRoll, sizeof got) == 0) {
            if (auto h = safetyhook::InlineHook::create(reinterpret_cast<void*>(base + 0x2DB93D0), drop_roll_detour)) {
                g_drop_roll = std::move(*h);
            }
        }
        log::info("Lootr: the gimmick Drop worker {}{}: {}", g_drop ? "hooked" : "NOT hooked",
                  g_drop_roll ? ", its roll logged" : "",
                  g_on && g_chests_on ? "a chest one player opens gives the other player the same content when they "
                                        "are at the chest" : "chests log only");
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
        if (lrl::opcode_of(p) == 2106) {  // LootDropCharacterRewardListAck: u8 4 | u32 body | ...
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
        static uint64_t next_share = 0;
        // v4.3 (review of v4.2, finding 1): a joined player's save that missed the lock needs this entry too (its marks
        // are written there), even with nothing else waiting.
        if ((g_shares_pending.load() > 0 || g_ledger_retry.load() || g_guest_save_missed.load() ||
             (g_armed.load() && (g_near_n.load() > 0 || g_write_due.load()))) &&
            now >= next_share) {  // the host's own request: the server thread
            next_share = now + 2000;
            std::vector<uint8_t> pl(30, 0);
            const uint32_t entry = 0xC0DE0007;
            std::memcpy(&pl[14], &kHostCharacter, 4);
            std::memcpy(&pl[18], &kHostCharacter, 4);
            std::memcpy(&pl[26], &entry, 4);
            net::sniffer_inject_c2s(lrl::make(3104, pl, 0x00));
        }
        static uint64_t next_scan = 0;
        if (g_probe && g_chest_scans.load() < 8 && now >= next_scan) {  // the host's own request: the server thread
            const bool first = next_scan == 0;
            next_scan = now + (first ? 45000 : 60000);
            if (!first) {
                std::vector<uint8_t> pl(30, 0);
                const uint32_t entry = 0xC0DE0006;
                std::memcpy(&pl[14], &kHostCharacter, 4);
                std::memcpy(&pl[18], &kHostCharacter, 4);
                std::memcpy(&pl[26], &entry, 4);
                net::sniffer_inject_c2s(lrl::make(3104, pl, 0x00));
            }
        }
        std::lock_guard lk(g_mu);
        for (DevBody& b : g_dev_bodies) {  // [dev] LootingDropFromDeadBodyReq 2491: u32 body | u8 remove | u8 send list
            uint8_t pl[6] = {0, 0, 0, 0, 0, 1};
            std::memcpy(pl, &b.key, 4);
            if (b.step == 0 && now - b.at >= 3000) {
                b.step = 1;
                const bool ok = net::sniffer_inject_c2s(lrl::make(2382, pl, 0x00));
                log::info("Lootr probe: [dev] the host loots body 0x{:08X} (2491){}", b.key, ok ? "" : " NOT sent");
            } else if (b.step == 1 && now - b.at >= 6000 && g_kill_hits.load() == 1) {
                b.step = 2;
                to_server.push_back(lrl::make(2382, pl, 0x00));
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
                const bool sent = net::sniffer_inject_c2s(lrl::make(3104, pl, 0x00));
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
                to_server.push_back(lrl::make(3104, pl, 0x00));  // a client Req
                for (const auto& h : t.held) server_push_extra(h);
                c_given.fetch_add(1);
                log::info("Lootr: the guest took gimmick 0x{:08X} too: item key {} x{} for 0x{:08X}, {} packet(s) "
                          "replayed", t.g, t.item_key, t.count, t.requester, t.held.size());
                t.state = 2;
                if (g_tracking.load() && t.share_id)  // B19 v3: recorded when the worker's add ran (under g_mu)
                    g_pending_adds.push_back({t.share_id, t.requester, t.item_key, static_cast<int64_t>(t.count), now});
            }
        }
        if (g_armed.load()) {  // B19: not taken within kKeepMs: it waits at its place (and across sessions) instead
            for (const Taken& t : g_taken) {
                if (t.state != 0 || now - t.at <= kKeepMs || !t.item_known || !t.pos_ok || t.count <= 0) continue;
                near_add(share_of(t));
                c_p_late.fetch_add(1);
                log::info("Lootr: gimmick 0x{:08X}'s item for the {} was not taken in 30 min: it now waits at its "
                          "place (6 m), across sessions too", t.g, t.host_waits ? "host" : "guest");
            }
        }
        std::erase_if(g_taken, [&](const Taken& t) { return t.state == 2 || now - t.at > kKeepMs; });
        // v3.1 (review B1): a give the worker did not confirm in 60 s may still run later (its request held while the
        // server takes none of the joined player's): counted as given (losing it if it never ran beats giving twice).
        for (const PendingAdd& a : g_pending_adds) {
            if (now - a.at > 60000) g_given_guest.push_back({a.id, now});
        }
        std::erase_if(g_pending_adds, [&](const PendingAdd& a) { return now - a.at > 60000; });
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
                       c_body_refused.load(), c_body_2414.load()) +
           std::format(" | chests {}: opened {} shares given {} waiting {} forgotten {} | body loot {}: looted {} "
                       "shares given {}",
                       g_chests_on ? "per player" : "as the game", c_chest_opened.load(), c_chest_shared.load(),
                       g_shares_pending.load(), c_chest_expired.load(),
                       g_body_share_on && !g_bodies_on ? "shared" : "not shared", c_body_looted.load(),
                       c_body_shared.load()) +
           (!g_persist_on ? std::string(" | kept across sessions: off")
                          : std::format(" | kept across sessions: {} | restored {} given {} waiting {} kept late {} | "
                                        "slot files written {} failed {} unchanged {} stale {} set aside {} (could not "
                                        "{}) scrubbed {} | ledger marks {} (due {}, unfiled gives {}) | lock missed {}",
                                        g_armed.load() ? "armed" : "not armed yet", c_p_read.load(), c_p_given.load(),
                                        g_near_n.load(), c_p_late.load(), c_p_written.load(), c_p_failed.load(),
                                        c_p_unchanged.load(), c_p_stale.load(), c_p_setaside.load(),
                                        c_p_aside_failed.load(), c_p_scrubbed.load(), c_p_ledger.load(),
                                        g_ledger_due_n.load(), c_p_unfiled.load(), c_p_lock_missed.load()));
}

void lootr_host_recv(uint8_t* packet, uint16_t size) noexcept {
    if (!g_on || size < lrl::kHeader + 4) return;
    const uint16_t op = static_cast<uint16_t>(packet[0] | (packet[1] << 8));
    if (op == 2106 && size >= lrl::kHeader + 5) {  // the leftover list of a body the guest looted first
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
    if (op == 2946 && size >= lrl::kHeader + 8) {  // InteractionPreemptionStepFailAck: u32 actor | u32 target | ...
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
            const bool ok = net::sniffer_inject_c2s(lrl::make(2322, pl, 0x00));
            log::info("Lootr probe: [dev] the host's hit on 0x{:08X} kills it{}", k, ok ? "" : " (NOT sent)");
            if (g_kill_hits.load() == 1 || g_kill_hits.load() == 3) {  // then loot it without the UI: the host 3 s
                                                                       // later, (mode 1) the guest 6 s later
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
    if (!server_to_client && (op == 2642 || op == 2304)) g_watch_until = GetTickCount64() + 3000;  // its answers
    static const uint16_t kWatch[] = {2642, 2304, 2308, 2363, 2066};
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
    const size_t m = size < 120 ? size : (op == 2210 ? 120 : 40);
    for (size_t i = 0; i < m; ++i) std::snprintf(hex + i * 3, 4, "%02X ", packet[i]);
    log::info("Lootr probe: {} {} ({} bytes): {}", server_to_client ? "S>C" : "C>S", op, size, hex);
}

}  // namespace cdmp::session2
