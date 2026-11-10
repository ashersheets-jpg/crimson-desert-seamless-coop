#include "mod/session2/trade_probe.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include <safetyhook.hpp>

#include "mod/game/safe_memory.h"
#include "mod/log.h"

// 3576: handler 0x2C948C0 (MoveItemInventoryToOtherInventoryReq 3111) -> worker 0x2E0B2B0(char, int* err, u32 item,
// u16 b, u64 count, u32* giver, u32* receiver, u16 inventory key, u16 slot, u32 entry) -> the distance check
// 0x2301D20 -> the move 0x2DF04A0 (or 0x2DF2560 for an entry of kind 2).
namespace cdmp::session2 {
namespace {
SafetyHookInline g_worker;
SafetyHookMid g_handler, g_move, g_move2;
std::atomic<int> g_left{64};

// ---- Give (Co-op+, 3576): a 3111 with entry 0xC0DE0001 is not one of the game's fixed routes but "give this to my
// friend". It runs here, on the server thread (the workers use its TLS), with two of the game's own workers:
// discard exe+0x2E03330 on the giver's holder, then add exe+0x2DD8FC0 on the receiver's holder.
constexpr uint32_t kGiveEntry = 0xC0DE0001;
struct Give {
    int stage = 0;  // 1 receiver found, 2 holder, 3 slot checked, 4 discarded, 5 added; -1 = exception
    int e_dist = -1, e_discard = -1, e_add = -1, e_back = -1;
    int drop = -1;  // the item row's drop entry (+0x52), -1 = the item cannot be dropped
    uintptr_t actor = 0, holder = 0, entry = 0, giver_holder = 0;
    uint16_t inv_idx = 0xFFFF, item_idx = 0xFFFF, have_idx = 0xFFFF, slot = 0xFFFF;
    int64_t have = 0;
    uint32_t item_key = 0;
    struct Seen {
        int16_t slot;
        uint16_t idx;
        int64_t n;
        uint64_t raw[3];
    } seen[10] = {};
    int seen_n = 0;
};
// hint: the item index the giver's own client saw in that slot (0xFFFF = none). The client's slot numbers can differ
// from the server's; when the named slot does not hold the hinted item, the first slot that does (and has enough) is
// used instead.
void give_raw(Give& g, uintptr_t self, uint32_t item, uint64_t count, uint32_t receiver, uint16_t inv,
              uint16_t slot, uint16_t hint) noexcept {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        uint64_t out = 0;
        reinterpret_cast<void* (__fastcall*)(uint64_t*, uint16_t)>(base + 0x3D5930)(&out, inv);
        g.inv_idx = static_cast<uint16_t>(out);
        out = 0;
        const auto idx_of = reinterpret_cast<void* (__fastcall*)(uint64_t*, uint32_t)>(base + 0x3D7E10);
        if (item) {
            idx_of(&out, item);
            g.item_idx = static_cast<uint16_t>(out);
        }
        g.item_key = item;
        if (g.inv_idx == 0xFFFF || (item && g.item_idx == 0xFFFF)) return;
        if (!item && slot == 0xFFFF) return;  // "whatever the slot holds" needs a slot
        const uintptr_t mgr = *reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(base + 0x71C0610) + 0x50);
        uint64_t scope[8] = {};
        reinterpret_cast<void* (__fastcall*)(uintptr_t, uint64_t*, uint64_t)>(base + 0x2D11880)(mgr, scope, receiver);
        g.actor = scope[1];
        const bool valid = (scope[2] & 0xFF) != 0;
        if (valid && g.actor) {
            g.stage = 1;
            g.holder = reinterpret_cast<uintptr_t (__fastcall*)(uintptr_t)>(base + 0x22F1DC0)(g.actor);
            const uintptr_t giver_actor = *reinterpret_cast<uintptr_t*>(self + 8);
            if (g.holder && g.holder != self && giver_actor) {
                g.stage = 2;
                g.giver_holder = reinterpret_cast<uintptr_t (__fastcall*)(uintptr_t)>(base + 0x22F1DC0)(giver_actor);
                reinterpret_cast<int* (__fastcall*)(uintptr_t, int*, uintptr_t, uintptr_t, float)>(base + 0x2301D20)(
                    mgr, &g.e_dist, giver_actor, g.actor, 0.0f);
                const auto entry_of = reinterpret_cast<uintptr_t (__fastcall*)(uintptr_t, int16_t, int16_t)>(base + 0x22F7FE0);
                if (slot == 0xFFFF) {  // no slot named: the first one that holds enough of the item
                    for (int16_t i = 0; i < 512; ++i) {
                        const uintptr_t e = entry_of(self, static_cast<int16_t>(g.inv_idx), i);
                        if (e && *reinterpret_cast<uint16_t*>(e + 8) == g.item_idx &&
                            *reinterpret_cast<int64_t*>(e + 0x10) >= static_cast<int64_t>(count)) {
                            slot = static_cast<uint16_t>(i);
                            break;
                        }
                    }
                }
                if (!item && hint != 0xFFFF && slot != 0xFFFF) {
                    const uintptr_t e = entry_of(self, static_cast<int16_t>(g.inv_idx), static_cast<int16_t>(slot));
                    if (!e || *reinterpret_cast<uint16_t*>(e + 8) != hint) {
                        for (int16_t i = 0; i < 512; ++i) {
                            const uintptr_t f = entry_of(self, static_cast<int16_t>(g.inv_idx), i);
                            if (f && *reinterpret_cast<uint16_t*>(f + 8) == hint &&
                                *reinterpret_cast<int64_t*>(f + 0x10) >= static_cast<int64_t>(count)) {
                                slot = static_cast<uint16_t>(i);
                                break;
                            }
                        }
                    }
                }
                g.slot = slot;
                if (slot == 0xFFFF) {  // not there: what the bag does hold (for the log)
                    for (int16_t i = 0; i < 512 && g.seen_n < 10; ++i) {
                        const uintptr_t e = entry_of(self, static_cast<int16_t>(g.inv_idx), i);
                        if (!e || *reinterpret_cast<uint16_t*>(e + 8) == 0xFFFF || *reinterpret_cast<int64_t*>(e + 0x10) < 1) continue;
                        auto& sn = g.seen[g.seen_n++];
                        sn.slot = i;
                        sn.idx = *reinterpret_cast<uint16_t*>(e + 8);
                        sn.n = *reinterpret_cast<int64_t*>(e + 0x10);
                        std::memcpy(sn.raw, reinterpret_cast<void*>(e + 0x18), sizeof sn.raw);
                    }
                }
                g.entry = entry_of(self, static_cast<int16_t>(g.inv_idx), static_cast<int16_t>(slot));
                if (g.entry) {
                    g.have_idx = *reinterpret_cast<uint16_t*>(g.entry + 8);
                    g.have = *reinterpret_cast<int64_t*>(g.entry + 0x10);
                }
                if (slot != 0xFFFF && (!g.entry || g.have_idx == 0xFFFF)) {  // an empty slot: what the bag does hold
                    for (int16_t i = 0; i < 512 && g.seen_n < 10; ++i) {
                        const uintptr_t e = entry_of(self, static_cast<int16_t>(g.inv_idx), i);
                        if (!e || *reinterpret_cast<uint16_t*>(e + 8) == 0xFFFF || *reinterpret_cast<int64_t*>(e + 0x10) < 1) continue;
                        auto& sn = g.seen[g.seen_n++];
                        sn.slot = i;
                        sn.idx = *reinterpret_cast<uint16_t*>(e + 8);
                        sn.n = *reinterpret_cast<int64_t*>(e + 0x10);
                        std::memcpy(sn.raw, reinterpret_cast<void*>(e + 0x18), sizeof sn.raw);
                    }
                }
                if (!item && g.entry && g.have_idx != 0xFFFF) {  // item 0 = what the slot holds: its key, by search
                    for (uint32_t k = 1; k < 4000000; ++k) {
                        uint64_t o = 0xFFFF;
                        idx_of(&o, k);
                        if (static_cast<uint16_t>(o) == g.have_idx) {
                            item = k;
                            g.item_key = k;
                            g.item_idx = g.have_idx;
                            break;
                        }
                    }
                    if (!item) return;
                }
                if (g.entry && g.have_idx == g.item_idx && count >= 1 && g.have >= static_cast<int64_t>(count)) {
                    g.stage = 3;
                    // The transform the discard handler builds: identity, position (+0xC0) zero = "do not drop".
                    alignas(16) uint8_t tf[0x200] = {};
                    reinterpret_cast<void* (__fastcall*)(void*)>(base + 0x644DC0)(tf);
                    const float one = 1.0f;
                    std::memcpy(tf + 0x88, &one, 4);
                    std::memcpy(tf + 0x8C, &one, 4);
                    std::memcpy(tf + 0x90, &one, 4);
                    std::memcpy(tf + 0xA0, &one, 4);
                    std::memcpy(tf + 0xBC, &one, 4);
                    // tcB288: an item whose info row has a drop entry (+0x52 != -1) is DROPPED by the worker, and a
                    // zero position is then refused (exe+0x714B154). A give must never drop: the row's entry reads
                    // -1 for the length of this one call, so the worker takes its plain delete (reason 0x2F).
                    const uintptr_t row =
                        reinterpret_cast<uintptr_t (__fastcall*)(uintptr_t, uint32_t)>(base + 0x3DD8A0)(0, g.item_idx);
                    int16_t drop = -1;
                    if (row) drop = *reinterpret_cast<int16_t*>(row + 0x52);
                    g.drop = drop;
                    if (row && drop != -1) *reinterpret_cast<int16_t*>(row + 0x52) = -1;
                    reinterpret_cast<int* (__fastcall*)(uintptr_t, int*, uintptr_t, uint16_t, int16_t, int64_t, void*)>(
                        base + 0x2E03330)(self, &g.e_discard, giver_actor, inv, static_cast<int16_t>(slot),
                                          static_cast<int64_t>(count), tf);
                    if (row && drop != -1) *reinterpret_cast<int16_t*>(row + 0x52) = drop;
                    if (g.e_discard == 0) {
                        g.stage = 4;
                        const auto add = reinterpret_cast<int* (__fastcall*)(uintptr_t, int*, void*)>(base + 0x2DD8FC0);
                        const auto make = reinterpret_cast<void* (__fastcall*)(void*)>(base + 0x2641950);
                        alignas(16) static uint8_t v[0x400];
                        std::memset(v, 0, sizeof v);
                        make(v);
                        *reinterpret_cast<uint32_t*>(v + 8) = item;
                        *reinterpret_cast<uint16_t*>(v + 0xC) = 0;
                        *reinterpret_cast<int64_t*>(v + 0x10) = static_cast<int64_t>(count);
                        add(g.holder, &g.e_add, v);
                        if (g.e_add == 0) {
                            g.stage = 5;
                        } else {  // the friend could not take it (full bag): it goes back to the giver
                            std::memset(v, 0, sizeof v);
                            make(v);
                            *reinterpret_cast<uint32_t*>(v + 8) = item;
                            *reinterpret_cast<int64_t*>(v + 0x10) = static_cast<int64_t>(count);
                            add(self, &g.e_back, v);
                        }
                    }
                }
            }
        }
        reinterpret_cast<void (__fastcall*)(uint64_t*)>(base + 0x1618660)(scope);  // the scope's own release
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g.stage = -1;
    }
}

// ---- Shared stash (Co-op+ B12, 3576; build/decomp/notes_shared_stash.md, local). User 2026-11-10 (final): the HOST
// character's own WareHouse, once the story has unlocked it (0 slots before: tcB305); the game refuses moves into its
// hidden inventories (InvisibleInventory tcB313, index 7 tcB314). Both players put in (the inventory card) and take out
// (the Co-op tab) from anywhere; the host also has the game's own warehouse screen at storage keepers. The moves run the
// game's own WareHouse <-> bag routes. The host uses the game's warehouse screen as always; the guest's Co-op+ list sends a 3111 with entry
// 0xC0DE0002 (deposit: guest bag -> host WareHouse) or 0xC0DE0003 (withdraw: host WareHouse -> guest bag). Both run
// the game's own cross-actor move exe+0x2DF04A0 with the game's own move-table route (WareHouse <-> bag), so the item
// keeps its whole record (enchant, durability, dye) and both clients are told by the game. The distance check and
// the route's condition are skipped (the stash works anywhere); the mod checks who moves what.
constexpr uint32_t kStashDeposit = 0xC0DE0002, kStashWithdraw = 0xC0DE0003;
// 0xC0DE0004: the host's own Co-op+ layer asks its server for the host character's WareHouse (read here on the
// server thread, from the server's holder: the host's client only has it after a visit to a storage keeper). The
// layer reads the result through the export cdmp_stash_list.
constexpr uint32_t kStashList = 0xC0DE0004;
struct StashEntry {
    uint16_t slot, idx;
    uint32_t count;
};
SRWLOCK g_stash_lock = SRWLOCK_INIT;
StashEntry g_stash[512];
int g_stash_n = 0;
std::atomic<uint32_t> g_stash_gen{0};

// The game's name of an error code ("eErrNo..."), as the inventory's own error path gets it (exe+0xEF6560:
// exe+0x160B6A0 the table, exe+0x160BBF0 the lookup).
const char* err_name(int code) noexcept {
    if (code == 0) return "none";
    if (code == -1) return "-";
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        const uintptr_t table = reinterpret_cast<uintptr_t (__fastcall*)()>(base + 0x160B6A0)();
        if (!table) return "?";
        uint32_t c = static_cast<uint32_t>(code);
        const auto e = reinterpret_cast<uintptr_t* (__fastcall*)(uintptr_t, uint32_t*)>(base + 0x160BBF0)(table, &c);
        if (!e || !*e) return "?";
        const char* s = *reinterpret_cast<const char**>(*e);
        return s ? s : "?";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "?";
    }
}
constexpr uint32_t kHostCharacter = 0xA0100001;
struct Stash {
    int stage = 0;  // 1 inventories known, 2 route, 3 actors, 4 slot checked, 5 moved; negative = refused (see why)
    const char* why = "";
    int err = -1;
    uint16_t bag = 0xFFFF, wh = 0xFFFF, wh_key = 0xFFFF, from = 0xFFFF, to = 0xFFFF, slot = 0xFFFF;
    uint16_t have_idx = 0xFFFF, sub = 0;
    int64_t have = 0;
    uint32_t requester = 0;
};

uint16_t stash_inventory_index(uintptr_t base) noexcept;
bool inventory_indices(uintptr_t base, uint16_t& bag, uint16_t& wh) noexcept {
    __try {
        // The game's own chain (exe+0xEC03A0, exe+0x2D45EE0): *(*(*(*[exe+0x71C0498]) + 8) + 0x240) + 0xA2 / +0xB2.
        const uintptr_t slot = *reinterpret_cast<uintptr_t*>(base + 0x71C0498);
        if (!slot) return false;
        const uintptr_t root = *reinterpret_cast<uintptr_t*>(slot);
        if (!root) return false;
        const uintptr_t info = *reinterpret_cast<uintptr_t*>(root + 8);
        if (!info) return false;
        const uintptr_t mgr = *reinterpret_cast<uintptr_t*>(info + 0x240);
        if (!mgr) return false;
        bag = *reinterpret_cast<uint16_t*>(mgr + 0xA2);  // "Character", the main bag
        wh = *reinterpret_cast<uint16_t*>(mgr + 0xB2);   // "WareHouse"
        return bag != 0xFFFF && wh != 0xFFFF && bag != wh;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The game's move-table entry of inventory `from` that goes to `to` (row+0x38: 0xA0-byte entries, count at +0x40).
uintptr_t move_route(uintptr_t base, uint16_t from, uint16_t to) noexcept {
    __try {
        const uintptr_t row = reinterpret_cast<uintptr_t (__fastcall*)(uint64_t, uint32_t)>(base + 0x5560E0)(0, from);
        if (!row) return 0;
        const uintptr_t list = *reinterpret_cast<uintptr_t*>(row + 0x38);
        const uint32_t n = *reinterpret_cast<uint32_t*>(row + 0x40);
        if (!list) return 0;
        for (uint32_t i = 0; i < n && i < 64; ++i) {
            const uintptr_t e = list + static_cast<uintptr_t>(i) * 0xA0;
            if (*reinterpret_cast<uint16_t*>(e + 2) == from && *reinterpret_cast<uint16_t*>(e + 4) == to) return e;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return 0;
}

uint16_t inventory_key_of(uintptr_t base, uint16_t index) noexcept {
    __try {
        const uintptr_t row = reinterpret_cast<uintptr_t (__fastcall*)(uint64_t, uint32_t)>(base + 0x5560E0)(0, index);
        return row ? *reinterpret_cast<uint16_t*>(row + 8) : 0xFFFF;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xFFFF;
    }
}

struct StashSlot {
    uint16_t idx = 0xFFFF, sub = 0;
    int64_t n = 0;
};
StashSlot stash_slot(uintptr_t base, uintptr_t holder, uint16_t inv, uint16_t slot) noexcept {
    StashSlot out;
    __try {
        const uintptr_t e = reinterpret_cast<uintptr_t (__fastcall*)(uintptr_t, int16_t, int16_t)>(base + 0x22F7FE0)(
            holder, static_cast<int16_t>(inv), static_cast<int16_t>(slot));
        if (!e) return out;
        out.idx = *reinterpret_cast<uint16_t*>(e + 8);
        out.n = *reinterpret_cast<int64_t*>(e + 0x10);
        out.sub = *reinterpret_cast<uint16_t*>(e + 0xA);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = {};
    }
    return out;
}

// The give (B11) through the same cross-actor move: the giver's bag -> the receiver's bag, whole record kept. The
// game has no bag -> bag route, so a copy of its WareHouse -> bag route with the source set to the bag is used (the
// row's item check then is the WareHouse -> bag one). Stage 5 = moved; anything else = not moved (the old discard +
// add path runs then).
struct GiveMove {
    int stage = 0;
    int err = -1;
    uint16_t bag = 0xFFFF, slot = 0xFFFF, idx = 0xFFFF, sub = 0;
    int64_t have = 0;
};
alignas(16) uint8_t g_give_route[0xA0];
void give_move(GiveMove& m, uintptr_t self, uint32_t receiver, uint16_t inv_key, uint16_t slot, uint16_t hint,
               uint64_t count) noexcept {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint16_t wh = 0xFFFF;
    if (!inventory_indices(base, m.bag, wh)) return;
    m.stage = 1;
    __try {
        if (inventory_key_of(base, m.bag) != inv_key || count < 1) return;  // only from the bag
        const uintptr_t route = move_route(base, wh, m.bag);
        if (!route) return;
        std::memcpy(g_give_route, reinterpret_cast<void*>(route), sizeof g_give_route);
        *reinterpret_cast<uint16_t*>(g_give_route + 2) = m.bag;
        m.stage = 2;
        const uintptr_t giver_actor = *reinterpret_cast<uintptr_t*>(self + 8);
        const uintptr_t mgr = *reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(base + 0x71C0610) + 0x50);
        uint64_t scope[8] = {};
        reinterpret_cast<void* (__fastcall*)(uintptr_t, uint64_t*, uint64_t)>(base + 0x2D11880)(mgr, scope, receiver);
        const uintptr_t receiver_actor = scope[1];
        if ((scope[2] & 0xFF) != 0 && receiver_actor && giver_actor && receiver_actor != giver_actor) {
            m.stage = 3;
            auto fits = [&](const StashSlot& x) {
                return x.idx != 0xFFFF && x.n >= static_cast<int64_t>(count) && (hint == 0xFFFF || x.idx == hint);
            };
            StashSlot pick;
            if (slot != 0xFFFF && fits(pick = stash_slot(base, self, m.bag, slot))) {
                m.slot = slot;
            } else if (hint != 0xFFFF) {
                for (uint16_t i = 0; i < 512; ++i) {
                    if (fits(pick = stash_slot(base, self, m.bag, i))) {
                        m.slot = i;
                        break;
                    }
                }
            }
            if (m.slot != 0xFFFF) {
                m.stage = 4;
                m.idx = pick.idx;
                m.sub = pick.sub;
                m.have = pick.n;
                int64_t n = static_cast<int64_t>(count);
                reinterpret_cast<int* (__fastcall*)(uintptr_t, int*, uintptr_t, uintptr_t, uintptr_t, uint16_t, uint32_t,
                                                    uint16_t, int64_t*)>(base + 0x2DF04A0)(
                    self, &m.err, giver_actor, receiver_actor, reinterpret_cast<uintptr_t>(g_give_route), m.slot,
                    m.idx, m.sub, &n);
                if (m.err == 0) m.stage = 5;
            }
        }
        reinterpret_cast<void (__fastcall*)(uint64_t*)>(base + 0x1618660)(scope);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        m.stage = -2;
    }
}

// One-time diagnostics (B12): the 24 named inventories (manager +0xA0..+0xCE: index, key, slots and used slots in
// this holder) and every move route of the bag and the warehouse (from, to, the first bytes of the entry).
uintptr_t inventory_manager(uintptr_t base) noexcept {
    __try {
        const uintptr_t slot = *reinterpret_cast<uintptr_t*>(base + 0x71C0498);
        const uintptr_t root = slot ? *reinterpret_cast<uintptr_t*>(slot) : 0;
        const uintptr_t info = root ? *reinterpret_cast<uintptr_t*>(root + 8) : 0;
        return info ? *reinterpret_cast<uintptr_t*>(info + 0x240) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
int16_t inventory_slots(uintptr_t holder, uint16_t idx) noexcept {
    __try {
        const uintptr_t list = *reinterpret_cast<uintptr_t*>(holder + 0x18);
        const uint32_t n = *reinterpret_cast<uint32_t*>(holder + 0x20);
        for (uint32_t k = 0; list && k < n && k < 64; ++k) {
            const uintptr_t inv = *reinterpret_cast<uintptr_t*>(list + k * 8);
            if (inv && *reinterpret_cast<int16_t*>(inv + 0x10) == static_cast<int16_t>(idx))
                return *reinterpret_cast<int16_t*>(inv + 8);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return -1;
}
void route_text(uintptr_t base, uint16_t from, char* out, size_t cap) noexcept {
    out[0] = 0;
    size_t at = 0;
    __try {
        const uintptr_t row = reinterpret_cast<uintptr_t (__fastcall*)(uint64_t, uint32_t)>(base + 0x5560E0)(0, from);
        const uintptr_t list = row ? *reinterpret_cast<uintptr_t*>(row + 0x38) : 0;
        const uint32_t n = row ? *reinterpret_cast<uint32_t*>(row + 0x40) : 0;
        for (uint32_t i = 0; list && i < n && i < 40 && at + 96 < cap; ++i) {
            const uint8_t* e = reinterpret_cast<const uint8_t*>(list + static_cast<uintptr_t>(i) * 0xA0);
            const int w = std::snprintf(out + at, cap - at, " [%u: %u->%u %02X%02X %02X%02X%02X%02X cond %llX]", i,
                                        *reinterpret_cast<const uint16_t*>(e + 2), *reinterpret_cast<const uint16_t*>(e + 4),
                                        e[0], e[1], e[6], e[7], e[8], e[9],
                                        static_cast<unsigned long long>(*reinterpret_cast<const uintptr_t*>(e + 0x78)));
            if (w > 0) at += static_cast<size_t>(w);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[at] = 0;
    }
}
void stash_dump(uintptr_t base, uintptr_t holder) {
    static const char* const kNames[] = {"Money", "Character", "PearlUser", "PearlCharacter", "Quest", "PetAndVehicle",
                                         "Wagon", "Ship", "CampWareHouse", "WareHouse", "Bank", "CampStraw", "BirdFeed",
                                         "Recovery", "Dresser", "Refrigerator", "Symbol", "Collecting", "Gathered",
                                         "Kuku", "Invisible", "Xamaca", "Spagnola", "Dragnar"};
    const uintptr_t mgr = inventory_manager(base);
    if (!mgr) return;
    std::string line;
    for (int i = 0; i < 24; ++i) {
        uint16_t idx = 0xFFFF;
        game::fast_read(mgr + 0xA0 + i * 2, &idx, 2);
        const int16_t slots = idx == 0xFFFF ? -1 : inventory_slots(holder, idx);
        int used = 0;
        for (int16_t k = 0; k < slots && k < 512; ++k) {
            const StashSlot x = stash_slot(base, holder, idx, static_cast<uint16_t>(k));
            if (x.idx != 0xFFFF && x.n > 0) ++used;
        }
        line += std::format(" | {} {}/key {} slots {} used {}", kNames[i], idx,
                            idx == 0xFFFF ? 0 : inventory_key_of(base, idx), slots, used);
    }
    log::info("Stash dump: inventories{}", line);
    uint16_t bag = 0xFFFF, wh = 0xFFFF;
    if (!inventory_indices(base, bag, wh)) return;
    static char buf[4200];
    for (uint16_t from : {bag, wh, static_cast<uint16_t>(wh + 1)}) {
        route_text(base, from, buf, sizeof buf);
        log::info("Stash dump: routes of inventory {}:{}", from, buf);
    }
}

// The stash's inventory: [session2] stash_inventory = an index (research), else the personal WareHouse: of the
// manager's named storages (+0xB2, +0xB4), the one the bag has a route to AND whose route goes back to the bag
// (tcB305: index 9; index 8's routes go to the money pouch instead).
std::atomic<int> g_stash_inventory_override{-1};
std::atomic<int> g_stash_slots{-1};  // the WareHouse's slots in the host's holder at the last list read
uint16_t stash_inventory_index(uintptr_t base) noexcept {
    const int o = g_stash_inventory_override.load();
    if (o >= 0 && o < 0xFFFF) return static_cast<uint16_t>(o);
    const uintptr_t mgr = inventory_manager(base);
    uint16_t bag = 0xFFFF, unused = 0xFFFF;
    if (!mgr || !inventory_indices(base, bag, unused)) return 0xFFFF;
    for (const uintptr_t off : {uintptr_t{0xB4}, uintptr_t{0xB2}}) {
        uint16_t idx = 0xFFFF;
        game::fast_read(mgr + off, &idx, 2);
        if (idx != 0xFFFF && idx != bag && move_route(base, bag, idx) && move_route(base, idx, bag)) return idx;
    }
    return 0xFFFF;
}

// A route the game's move accepts from `from` to `to`: a copy of a plain "storage -> bag" route of the game (no
// condition) with its two indices set. One buffer per direction (the move runs on the server thread only).
alignas(16) uint8_t g_route_in[0xA0], g_route_out[0xA0];
uintptr_t stash_route(uintptr_t base, uint16_t from, uint16_t to) noexcept {
    if (const uintptr_t own = move_route(base, from, to)) return own;  // the game's own route
    uint16_t bag = 0xFFFF, wh = 0xFFFF;
    if (!inventory_indices(base, bag, wh)) return 0;
    uintptr_t tpl = 0;
    __try {
        for (uint16_t i = 0; i < 32 && !tpl; ++i) {
            if (i == bag) continue;
            const uintptr_t r = move_route(base, i, bag);
            if (r && *reinterpret_cast<uintptr_t*>(r + 0x78) == 0) tpl = r;
        }
        if (!tpl) return 0;
        uint8_t* buf = to == bag ? g_route_out : g_route_in;
        std::memcpy(buf, reinterpret_cast<void*>(tpl), 0xA0);
        *reinterpret_cast<uint16_t*>(buf + 2) = from;
        *reinterpret_cast<uint16_t*>(buf + 4) = to;
        return reinterpret_cast<uintptr_t>(buf);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// self = the requester's holder (the worker's own first argument), g / r = the packet's giver / receiver keys,
// hint = the item index the requester's list showed (0xFFFF = whatever the slot holds).
void stash_move(Stash& s, uintptr_t self, bool deposit, uint32_t g, uint32_t r, uint16_t slot, uint16_t hint,
                uint64_t count) noexcept {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!inventory_indices(base, s.bag, s.wh)) {
        s.stage = -1;
        s.why = "the bag / warehouse indices are unknown";
        return;
    }
    s.stage = 1;
    s.wh_key = inventory_key_of(base, s.wh);
    uintptr_t me = 0;
    if (!game::fast_read(self + 8, &me, sizeof me) || !me || !game::fast_read(me + 0x60, &s.requester, 4)) {
        s.stage = -1;
        s.why = "no requester actor";
        return;
    }
    // Either player, between its own bag and the stash on the host's character.
    const uint32_t player = deposit ? g : r, host = deposit ? r : g;
    if (player != s.requester || host != kHostCharacter || (player >> 28) != 0xA) {
        s.stage = -1;
        s.why = "not a player moving between its own bag and the host's stash";
        return;
    }
    s.wh = stash_inventory_index(base);
    if (s.wh == 0xFFFF) {
        s.stage = -1;
        s.why = "the stash inventory is unknown";
        return;
    }
    if (count < 1 || count > 1000000) {
        s.stage = -1;
        s.why = "bad count";
        return;
    }
    s.from = deposit ? s.bag : s.wh;
    s.to = deposit ? s.wh : s.bag;
    const uintptr_t route = stash_route(base, s.from, s.to);
    if (!route) {
        s.stage = -1;
        s.why = "no route template";
        return;
    }
    s.stage = 2;
    __try {
        const uintptr_t mgr = *reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(base + 0x71C0610) + 0x50);
        uint64_t scope[8] = {};
        reinterpret_cast<void* (__fastcall*)(uintptr_t, uint64_t*, uint64_t)>(base + 0x2D11880)(mgr, scope, kHostCharacter);
        const uintptr_t host_actor = scope[1];
        if ((scope[2] & 0xFF) != 0 && host_actor && (s.requester != kHostCharacter || host_actor == me)) {
            s.stage = 3;
            const uintptr_t giver_actor = deposit ? me : host_actor, receiver_actor = deposit ? host_actor : me;
            const uintptr_t src = reinterpret_cast<uintptr_t (__fastcall*)(uintptr_t)>(base + 0x22F1DC0)(giver_actor);
            auto fits = [&](const StashSlot& x) {
                return x.idx != 0xFFFF && x.n >= static_cast<int64_t>(count) && (hint == 0xFFFF || x.idx == hint);
            };
            StashSlot pick;
            if (src && slot != 0xFFFF && fits(pick = stash_slot(base, src, s.from, slot))) {
                s.slot = slot;
            } else if (src && hint != 0xFFFF) {  // the list was stale or the slots differ: the same item elsewhere
                for (uint16_t i = 0; i < 512; ++i) {
                    if (fits(pick = stash_slot(base, src, s.from, i))) {
                        s.slot = i;
                        break;
                    }
                }
            }
            if (s.slot != 0xFFFF) {
                s.stage = 4;
                s.have_idx = pick.idx;
                s.have = pick.n;
                s.sub = pick.sub;
                int64_t n = static_cast<int64_t>(count);
                reinterpret_cast<int* (__fastcall*)(uintptr_t, int*, uintptr_t, uintptr_t, uintptr_t, uint16_t, uint32_t,
                                                    uint16_t, int64_t*)>(base + 0x2DF04A0)(
                    self, &s.err, giver_actor, receiver_actor, route, s.slot, s.have_idx, s.sub, &n);
                if (s.err == 0) s.stage = 5;
            } else {
                s.why = "the item is not there (any more)";
            }
        } else {
            s.why = "the host's character is not on the server";
        }
        reinterpret_cast<void (__fastcall*)(uint64_t*)>(base + 0x1618660)(scope);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s.stage = -2;
        s.why = "exception";
    }
}

int* __fastcall worker_detour(uintptr_t self, int* err, uint32_t item, uint16_t b, uint64_t count, uint32_t* giver,
                              uint32_t* receiver, uint16_t inv, uint16_t slot, uint32_t entry) noexcept {
    uint32_t g = 0, r = 0;
    game::fast_read(reinterpret_cast<uintptr_t>(giver), &g, sizeof g);
    game::fast_read(reinterpret_cast<uintptr_t>(receiver), &r, sizeof r);
    if (entry == kStashList) {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        uint16_t bag = 0xFFFF, wh = 0xFFFF;
        uint32_t me = 0;
        uintptr_t actor = 0;
        int n = -1;
        if (g == kHostCharacter && inventory_indices(base, bag, wh) && (wh = stash_inventory_index(base)) != 0xFFFF &&
            game::fast_read(self + 8, &actor, 8) && actor && game::fast_read(actor + 0x60, &me, 4) &&
            me == kHostCharacter) {
            StashEntry tmp[512];
            n = 0;
            for (uint16_t i = 0; i < 512 && n < 512; ++i) {
                const StashSlot x = stash_slot(base, self, wh, i);
                if (x.idx == 0xFFFF || x.n < 1) continue;
                tmp[n++] = {i, x.idx, static_cast<uint32_t>(x.n > 0xFFFFFFFF ? 0xFFFFFFFF : x.n)};
            }
            AcquireSRWLockExclusive(&g_stash_lock);
            std::memcpy(g_stash, tmp, sizeof(StashEntry) * n);
            g_stash_n = n;
            ReleaseSRWLockExclusive(&g_stash_lock);
            g_stash_slots = inventory_slots(self, wh);
            g_stash_gen.fetch_add(1);
        }
        static int logged = 0;
        if (logged++ < 6)
            log::info("Stash: the host's warehouse (index {}, {} slots) read for its layer: {} stack(s)", wh,
                      g_stash_slots.load(), n);
        static bool dumped = false;
        if (!dumped && g == kHostCharacter) {
            dumped = true;
            stash_dump(base, self);
        }
        *err = 0;
        return err;
    }
    if (entry == kStashDeposit || entry == kStashWithdraw) {
        Stash st;
        const bool deposit = entry == kStashDeposit;
        stash_move(st, self, deposit, g, r, slot, b, count);
        log::info("Stash: {} x{} slot {} (item index 0x{:X}) {} 0x{:08X} -> 0x{:08X}: stage {} (5 = moved) err {} ({}) | bag {} "
                  "warehouse {} (key {}) route {} -> {} | slot used {} holds idx 0x{:X} x{} sub {}{}{}",
                  deposit ? "deposit" : "withdraw", count, slot, b, deposit ? "bag -> warehouse" : "warehouse -> bag",
                  g, r, st.stage, st.err, err_name(st.err), st.bag, st.wh, st.wh_key, st.from, st.to, st.slot, st.have_idx, st.have,
                  st.sub, *st.why ? " | " : "", st.why);
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        int bad = 0;
        game::fast_read(base + 0x712DFEC, &bad, sizeof bad);
        *err = st.stage == 5 ? 0 : st.err > 0 ? st.err : bad;
        return err;
    }
    if (entry == kGiveEntry) {
        if ((r & 0xF0000000) == 0xA0000000 && r != g) {
            GiveMove m;
            give_move(m, self, r, inv, slot, b, count);
            log::info("Give (whole record): x{} from 0x{:08X} to 0x{:08X}: stage {} (5 = moved) err {} ({}) | bag {} slot {} "
                      "idx 0x{:X} x{} sub {}{}", count, g, r, m.stage, m.err, err_name(m.err), m.bag, m.slot, m.idx, m.have, m.sub,
                      m.stage == 5 ? "" : " -> the discard + add path");
            if (m.stage == 5) {
                *err = 0;
                return err;
            }
        }
        Give gv;
        const bool to_character = (r & 0xF0000000) == 0xA0000000 && r != g;
        if (to_character) give_raw(gv, self, item, count, r, inv, slot, b);
        log::info("Give: item {} x{} from 0x{:08X} (inventory 0x{:X} slot {} -> {}) to 0x{:08X}: stage {} (5 = given) | "
                  "receiver actor 0x{:X} holder 0x{:X} | slot has item idx 0x{:X} x{} (asked idx 0x{:X}) | distance err "
                  "{} discard err {} add err {} back err {} | drop entry {}", gv.item_key, count, g, inv, slot, gv.slot, r, gv.stage, gv.actor, gv.holder,
                  gv.have_idx, gv.have, gv.item_idx, gv.e_dist, gv.e_discard, gv.e_add, gv.e_back, gv.drop);
        if (gv.stage < 3) {
            log::info("Give: giver holder 0x{:X} (its actor's holder 0x{:X}) entry 0x{:X} inventory index 0x{:X}", self,
                      gv.giver_holder, gv.entry, gv.inv_idx);
        }
        for (int i = 0; i < gv.seen_n; ++i) {
            log::info("Give: the bag's slot {} holds item idx 0x{:X} x{} (+0x18: {:X} {:X} {:X})", gv.seen[i].slot,
                      gv.seen[i].idx, gv.seen[i].n, gv.seen[i].raw[0], gv.seen[i].raw[1], gv.seen[i].raw[2]);
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        int bad = 0;
        game::fast_read(base + 0x712DFEC, &bad, sizeof bad);
        *err = gv.stage == 5 ? 0 : gv.e_discard > 0 ? gv.e_discard : gv.e_add > 0 ? gv.e_add : bad;
        return err;
    }
    const bool say = g_left.fetch_sub(1) > 0;
    if (say) {
        log::info("TradeProbe [worker in] self 0x{:X} item {} b 0x{:X} count {} giver 0x{:08X} receiver 0x{:08X} "
                  "inventory 0x{:X} slot 0x{:X} entry {}", self, item, b, count, g, r, inv, slot, entry);
    }
    static std::atomic<bool> g_listed{false};
    if (say && !g_listed.exchange(true)) {
        // Which u16 inventory keys the worker's lookup knows (its own call, exe+0x3D5930: key -> index, 0xFFFF =
        // unknown), and what the item key maps to (exe+0x3D7E10). Read only.
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        using InvFn = void* (__fastcall*)(uint64_t*, uint16_t);
        using ItemFn = void* (__fastcall*)(uint64_t*, uint32_t);
        const auto inv_fn = reinterpret_cast<InvFn>(base + 0x3D5930);
        std::string known;
        int n = 0;
        for (uint32_t k = 0; k < 0x10000; ++k) {
            uint64_t out = 0;
            inv_fn(&out, static_cast<uint16_t>(k));
            if (static_cast<uint16_t>(out) != 0xFFFF && ++n <= 64) {
                known += std::format(" 0x{:X}->{}", k, static_cast<uint16_t>(out));
            }
        }
        log::info("TradeProbe [keys] {} inventory keys known:{}", n, known);
        uint64_t out = 0;
        reinterpret_cast<ItemFn>(base + 0x3D7E10)(&out, item);
        log::info("TradeProbe [keys] item {} -> index 0x{:X}", item, static_cast<uint16_t>(out));
    }
    int* ret = g_worker.unsafe_call<int*>(self, err, item, b, count, giver, receiver, inv, slot, entry);
    int e = -1;
    game::fast_read(reinterpret_cast<uintptr_t>(err), &e, sizeof e);
    if (say) {
        // The worker's two own errors: "no such item / inventory / entry" (0x712DFEC) and "an actor is missing"
        // (0x712E058); anything else comes from the distance check or the move itself.
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        int bad = 0, actor = 0;
        game::fast_read(base + 0x712DFEC, &bad, sizeof bad);
        game::fast_read(base + 0x712E058, &actor, sizeof actor);
        log::info("TradeProbe [worker out] err {} (0x{:X}) = {}", e, static_cast<uint32_t>(e),
                  e == 0 ? "none" : e == bad ? "bad item / inventory / entry" : e == actor ? "actor missing"
                                                                                           : "distance or move");
        if (e != 0) {  // which of the game's error constants it is (exe+0x7120000..0x7160000), for the decomp
            std::string where;
            std::vector<int> block(0x1000 / 4);
            for (uintptr_t at = base + 0x7120000; at < base + 0x7160000; at += 0x1000) {
                if (!game::fast_read(at, block.data(), 0x1000)) continue;
                for (size_t i = 0; i < block.size(); ++i) {
                    if (block[i] == e) where += std::format(" exe+0x{:X}", at + i * 4 - base);
                }
            }
            log::info("TradeProbe [worker out] that error constant is at:{}", where.empty() ? " (not found)" : where);
        }
    }
    return ret;
}
void on_handler(SafetyHookContext&) {
    if (g_left.load() > 0) log::info("TradeProbe [handler] 3111 reached the server's handler");
}
void on_move(SafetyHookContext&) {
    if (g_left.load() > 0) log::info("TradeProbe [move] 0x2DF04A0 entered (kind 1)");
}
void on_move2(SafetyHookContext&) {
    if (g_left.load() > 0) log::info("TradeProbe [move] 0x2DF2560 entered (kind 2)");
}
SafetyHookMid g_node, g_lookup;
std::atomic<int> g_node_left{600}, g_lookup_left{80};

template <typename T>
T rd(uintptr_t a) noexcept {
    T v{};
    if (a < 0x10000 || !game::fast_read(a, &v, sizeof v)) return T{};
    return v;
}
std::string dyn_list(uintptr_t actor) {
    const uintptr_t ic = rd<uintptr_t>(rd<uintptr_t>(actor + 0x68) + 0x98);
    const uintptr_t arr = rd<uintptr_t>(ic + 0x90);
    const uint32_t n = rd<uint32_t>(ic + 0x98);
    std::string t = std::format("IC 0x{:X} dynamic {}:", ic, n);
    for (uint32_t i = 0; i < n && i < 12; ++i) {
        const uintptr_t e = arr + i * 0x38ull;
        t += std::format(" [idx 0x{:X} q20 0x{:X} d28 0x{:X} b2c {} w2e 0x{:X} b30 {}]", rd<uint16_t>(e + 0x10),
                         rd<uint64_t>(e + 0x20), rd<uint32_t>(e + 0x28), rd<uint8_t>(e + 0x2C), rd<uint16_t>(e + 0x2E),
                         rd<uint8_t>(e + 0x30));
    }
    return t;
}
void on_node(SafetyHookContext& c) {
    if (g_node_left.fetch_sub(1) <= 0) return;
    const uintptr_t node = c.rcx, ctx = c.rdx;
    const uintptr_t scene = rd<uintptr_t>(ctx);
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::string t;
    for (uint32_t o = 0x20; o < 0x70; o += 8) t += std::format(" +0x{:X}={:X}", o, rd<uint64_t>(scene + o));
    log::info("InterProbe [node {}] node 0x{:X} idx 0x{:X} b3b {} b3c {} | scene 0x{:X} vt exe+0x{:X}:{} | ctx+0x48 "
              "0x{:X}", (c.r8 & 0xFF) ? "ADD" : "REMOVE", node, rd<uint16_t>(node + 0x38), rd<uint8_t>(node + 0x3B),
              rd<uint8_t>(node + 0x3C), scene, rd<uintptr_t>(scene) - base, t, rd<uint64_t>(ctx + 0x48));
}
void on_lookup(SafetyHookContext& c) {
    const uintptr_t actor = c.rcx;
    // Only the shop interaction or a target with dynamic entries (NPC AI lookups flood otherwise).
    if (rd<uint32_t>(c.r8) != 0x000F4321 && rd<uint32_t>(rd<uintptr_t>(rd<uintptr_t>(actor + 0x68) + 0x98) + 0x98) == 0) {
        return;
    }
    if (g_lookup_left.fetch_sub(1) <= 0) return;
    log::info("InterProbe [lookup] target 0x{:08X} (actor 0x{:X}) flag {} key 0x{:08X} | {}", rd<uint32_t>(actor + 0x60),
              actor, static_cast<int>(c.rdx & 0xFF), rd<uint32_t>(c.r8), dyn_list(actor));
}
}  // namespace

void inter_probe_start() {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint8_t head[2]{};
    if (!game::safe_read(base + 0x255E100, head, sizeof head)) {
        log::warn("InterProbe: the node is unreadable; off");
        return;
    }
    if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(base + 0x255E100), on_node)) g_node = std::move(*h);
    if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(base + 0x1940AD0), on_lookup)) {
        g_lookup = std::move(*h);
    }
    log::info("InterProbe: node {} lookup {} (read only)", static_cast<bool>(g_node), static_cast<bool>(g_lookup));
}

}  // namespace cdmp::session2

// Co-op+ B12: the host character's WareHouse as last read on the server thread (entry 0xC0DE0004). Returns the number
// of stacks copied (at most max), -1 before any read; *gen counts the reads.
// The host's WareHouse slots at the last read (0 = not unlocked yet; -1 = never read / unknown).
extern "C" __declspec(dllexport) int cdmp_stash_slots() { return cdmp::session2::g_stash_slots.load(); }

extern "C" __declspec(dllexport) int cdmp_stash_list(uint32_t* gen, uint16_t* slots, uint16_t* idxs, uint32_t* counts,
                                                     int max) {
    using namespace cdmp::session2;
    if (gen) *gen = g_stash_gen.load();
    if (g_stash_gen.load() == 0) return -1;
    AcquireSRWLockShared(&g_stash_lock);
    const int n = g_stash_n < max ? g_stash_n : max;
    for (int i = 0; i < n; ++i) {
        if (slots) slots[i] = g_stash[i].slot;
        if (idxs) idxs[i] = g_stash[i].idx;
        if (counts) counts[i] = g_stash[i].count;
    }
    ReleaseSRWLockShared(&g_stash_lock);
    return n;
}

namespace cdmp::session2 {

void trade_probe_set_stash_inventory(int index) { g_stash_inventory_override = index; }

void trade_probe_start() {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint8_t head[2]{};
    if (!game::safe_read(base + 0x2E0B2B0, head, sizeof head)) {
        log::warn("TradeProbe: the worker is unreadable; off");
        return;
    }
    if (auto h = safetyhook::InlineHook::create(reinterpret_cast<void*>(base + 0x2E0B2B0), worker_detour)) {
        g_worker = std::move(*h);
    }
    if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(base + 0x2C948C0), on_handler)) {
        g_handler = std::move(*h);
    }
    if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(base + 0x2DF04A0), on_move)) g_move = std::move(*h);
    if (auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(base + 0x2DF2560), on_move2)) g_move2 = std::move(*h);
    log::info("TradeProbe: worker {} handler {} move {} move2 {} (read only)", static_cast<bool>(g_worker),
              static_cast<bool>(g_handler), static_cast<bool>(g_move), static_cast<bool>(g_move2));
}
}  // namespace cdmp::session2
