// Co-op+ (2026-11-10): lootr.h.
#include "mod/session2/lootr.h"

#include <windows.h>

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
    const bool guest_first = g_on && s.picker != kHostCharacter && (s.picker >> 28) == 0xA && s.gimmick;
    if (guest_first) {
        keep_for_host(s.gimmick, s.key16);  // phase 2a: the guest took it first; the host still sees it
        c_guest_first.fetch_add(1);
    }
    if (g_on && (s.picker == kHostCharacter || guest_first) && s.gimmick) {
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
                  g_on && s.picker == kHostCharacter ? " | kept for the guest (Lootr)" : "");
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
int* __fastcall body_loot_detour(uintptr_t comp, int* err, uintptr_t looter, uint8_t a, uint8_t b) noexcept {
    BodySeen before, after;
    read_body(before, comp, looter);
    int* r = g_body_loot.call<int*>(comp, err, looter, a, b);
    int e = -1;
    if (err) game::fast_read(reinterpret_cast<uintptr_t>(err), &e, 4);
    read_body(after, comp, 0);  // the comp may be gone when the body was logged out: SEH-guarded
    static std::atomic<int> said{0};
    if (said.fetch_add(1) < 40) {
        log::info("Lootr probe: body loot by 0x{:08X} of body 0x{:08X} a {} b {} | rolled {} -> {} | pool {} -> {} | "
                  "sub {} -> {} | body after 0x{:08X} | err {}", before.looter, before.body, a, b, before.rolled,
                  after.rolled, before.pool, after.pool, before.sub, after.sub, after.body, e);
    }
    g_watch_until = GetTickCount64() + 3000;
    return r;
}

}  // namespace

std::string lootr_describe();

void lootr_start(bool on, bool probe, bool guest) {
    g_on = on && !guest;
    g_probe = probe && !guest;
    if (!g_on && !g_probe) return;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint8_t head[2]{};
    if (!game::safe_read(base + 0x2D37B10, head, sizeof head)) return;
    if (auto h = safetyhook::InlineHook::create(reinterpret_cast<void*>(base + 0x2D37B10), pickup_detour)) {
        g_pickup = std::move(*h);
    }
    if (g_probe) {
        static const uint8_t kHead[10] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x44, 0x88, 0x4C, 0x24, 0x20};
        uint8_t got[10] = {};
        if (game::safe_read(base + 0x2E224F0, got, sizeof got) && std::memcmp(got, kHead, sizeof got) == 0) {
            if (auto h = safetyhook::InlineHook::create(reinterpret_cast<void*>(base + 0x2E224F0), body_loot_detour)) {
                g_body_loot = std::move(*h);
            }
        }
        log::info("Lootr probe: the body loot worker {}", g_body_loot ? "hooked (log only)" : "NOT hooked");
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
        static uint64_t said = 0;
        if (now - said >= 30000) {
            said = now;
            log::info("{}", lootr_describe());
        }
        std::lock_guard lk(g_mu);
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
                       c_host_blanked.load());
}

void lootr_host_recv(uint8_t* packet, uint16_t size) noexcept {
    if (!g_on || size < lrl::kHeader + 4) return;
    const uint16_t op = static_cast<uint16_t>(packet[0] | (packet[1] << 8));
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

void lootr_host_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept {
    if (size < 5) return;
    const uint16_t op = static_cast<uint16_t>(packet[0] | (packet[1] << 8));
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
