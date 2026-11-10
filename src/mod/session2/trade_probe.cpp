#include "mod/session2/trade_probe.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
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

int* __fastcall worker_detour(uintptr_t self, int* err, uint32_t item, uint16_t b, uint64_t count, uint32_t* giver,
                              uint32_t* receiver, uint16_t inv, uint16_t slot, uint32_t entry) noexcept {
    uint32_t g = 0, r = 0;
    game::fast_read(reinterpret_cast<uintptr_t>(giver), &g, sizeof g);
    game::fast_read(reinterpret_cast<uintptr_t>(receiver), &r, sizeof r);
    if (entry == kGiveEntry) {
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
