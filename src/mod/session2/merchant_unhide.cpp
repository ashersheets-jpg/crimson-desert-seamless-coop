// Co-op+ (2026-11-09, build 1.0.0.3576): merchant_unhide.h / merchant_unhide_logic.h.
#include "mod/session2/merchant_unhide.h"

#include <windows.h>

#include <safetyhook.hpp>

#include <atomic>
#include <cstring>
#include <format>
#include <mutex>
#include <thread>
#include <vector>

#include "mod/game/actor_lookup.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/merchant_unhide_logic.h"

namespace cdmp::session2 {
namespace {

constexpr uintptr_t k2380Point = 0xD3414E;  // cmp r14b,4 ; je (guest_seq_route's P2661 point)
constexpr uint8_t k2380Bytes[] = {0x41, 0x80, 0xFE, 0x04, 0x74, 0x47};
constexpr uintptr_t kSetter = 0x18CD090;  // (info, bits, set, fade, second mask)
constexpr uint8_t kSetterHead[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48};
constexpr size_t kHeader = 5, kResourceAt = 27, kBindingsAt = 92, kBindingSize = 11;

using SetterFn = uint64_t(__fastcall*)(uintptr_t info, uint64_t bits, char set, float fade, char second);

bool g_on = false, g_guest = false;
uintptr_t g_base = 0;
SafetyHookMid g_hook;
SetterFn g_setter = nullptr;

struct Bound {
    uint32_t key = 0, scene = 0;
    uint64_t at_ms = 0;
    bool done = false;
    uint8_t diag = 0;  // diagnostic snapshots taken (2.5 s and 8 s after the binding)
};
bool g_diag_only = false;  // merchant_unhide=2: snapshots only, nothing written
bool g_guard = false;      // merchant_unhide=3 (or 1): the login guard

// 2026-11-09 diagnostics: 0xAAABB0(field) is how the client's frame (0x9A0540 -> 0x99C5A0) picks the stage manager it
// ticks: the manager of the character of the field's OWNER user ([field+0x180]). Its entry is watched: which owner the
// client ticks for (a scene of another owner's manager never ticks, and its NPCs never finish their login).
SafetyHookMid g_tick_hook;
std::atomic<uintptr_t> g_tick_field{0};
std::atomic<uint32_t> g_tick_owner{0};
std::atomic<uint64_t> g_tick_calls{0};
void on_tick_mgr(safetyhook::Context& ctx) {
    g_tick_calls.fetch_add(1, std::memory_order_relaxed);
    uint32_t owner = 0;
    if (game::safe_read(ctx.rcx + 0x180, &owner, 4)) {
        g_tick_owner.store(owner, std::memory_order_relaxed);
        g_tick_field.store(ctx.rcx, std::memory_order_relaxed);
    }
}
// 2026-11-09 THE FIX (merchant_unhide=3, or 1): a character's login on this client (0x99BE90 calls 0x1928650 =
// jmp .xtls 0xB65B330, at 0x99C17A, then sets the character's +0xC2 = 1 at 0x99C182) puts the stage manager it is
// attached to back to state 1 (HideWatch mode 3, tcB145: write at .xtls 0x9AC803B from 0x99C17F). For this client's
// own login that is the start of its field load (the server's PlayAllPreLoadSequencerAck moves it on to 2, the world
// update to 3). For ANOTHER player's character (the guest's on the host; the host's on the guest) nothing moves it on
// again: this client's own manager stays at 1 for good, none of its scenes tick, and every scene NPC stays waiting
// (its scene component +0x3E0, login +0xCF 0, AI stopped after its first action, hide bit 9, no interactions).
// So: before the call, this client's own manager's state; after it, for another player's character only, a 3 that
// became lower is put back to 3.
SafetyHookMid g_login_pre, g_login_post;
thread_local uintptr_t t_login_mgr = 0;
thread_local uint8_t t_login_state = 0;
thread_local bool t_login_remote = false;
std::atomic<uint64_t> c_login_remote{0}, c_login_restored{0};
std::atomic<uint32_t> g_last_restored_key{0};
uintptr_t local_user() {
    uintptr_t g = 0, g30 = 0, user = 0;
    if (game::safe_read(g_base + 0x71C0100, &g, 8) && g && game::safe_read(g + 0x30, &g30, 8) && g30 &&
        game::safe_read(g30 + 0x58, &user, 8))
        return user;
    return 0;
}
uintptr_t mgr_of(uintptr_t ch) {
    uintptr_t comps = 0, seq = 0, mgr = 0;
    if (!ch || !game::safe_read(ch + 0x68, &comps, 8) || !comps || !game::safe_read(comps + 0xA8, &seq, 8) || !seq ||
        !game::safe_read(seq + 0x18, &mgr, 8))
        return 0;
    return mgr;
}
std::atomic<uintptr_t> g_dbg_mgr{0};
std::atomic<uint32_t> g_dbg_before{0}, g_dbg_after{0};
uintptr_t own_manager() {
    // the host: its character is always 0xA0100001; the guest: the local user's main character [user+0xD0]
    if (!g_guest)
        if (const auto ch = game::find_actor(0xA0100001u); ch && *ch) return mgr_of(*ch);
    const uintptr_t user = local_user();
    uintptr_t ch = 0, comps = 0, seq = 0, mgr = 0;
    if (!user || !game::safe_read(user + 0xD0, &ch, 8) || !ch) return 0;
    if (!game::safe_read(ch + 0x68, &comps, 8) || !comps || !game::safe_read(comps + 0xA8, &seq, 8) || !seq ||
        !game::safe_read(seq + 0x18, &mgr, 8))
        return 0;
    return mgr;
}
void on_login_pre(safetyhook::Context& ctx) {  // 0x99C17A: rcx = the character logging in
    t_login_mgr = 0;
    t_login_remote = false;
    uint32_t key = 0;
    uintptr_t owner = 0;
    if (!game::safe_read(ctx.rcx + 0x60, &key, 4) || (key >> 28) != 0xA) return;
    game::safe_read(ctx.rcx + 0xA0, &owner, 8);
    const uintptr_t me = local_user();
    if (!me || owner == me) return;  // this client's own character: its real field load
    t_login_remote = true;
    t_login_mgr = own_manager();
    if (t_login_mgr) game::safe_read(t_login_mgr + 0x259, &t_login_state, 1);
    c_login_remote.fetch_add(1);
    g_last_restored_key.store(key);
}
void on_login_post(safetyhook::Context&) {  // 0x99C17F: back from 0x1928650
    if (!t_login_remote || !t_login_mgr) return;
    uint8_t now = 0;
    game::safe_read(t_login_mgr + 0x259, &now, 1);
    g_dbg_mgr.store(t_login_mgr);
    g_dbg_before.store(t_login_state);
    g_dbg_after.store(now);
    if (t_login_state == 3 && now < 3) {
        *reinterpret_cast<volatile uint8_t*>(t_login_mgr + 0x259) = 3;
        c_login_restored.fetch_add(1);
    }
    t_login_remote = false;
    t_login_mgr = 0;
}

uint32_t local_user_key() {
    uintptr_t g = 0, g30 = 0, user = 0;
    uint32_t k = 0;
    if (game::safe_read(g_base + 0x71C0100, &g, 8) && g && game::safe_read(g + 0x30, &g30, 8) && g30 &&
        game::safe_read(g30 + 0x58, &user, 8) && user)
        game::safe_read(user + 0x60, &k, 4);
    return k;
}

// 2026-11-09 diagnostics: the NPC's sequencer state as guest_seq_route's [G5 actor] reads it (3576 offsets), and its
// scene manager's readiness, so solo and co-op can be compared.
template <class T>
T rv(uintptr_t a) {
    T v{};
    if (a) game::safe_read(a, &v, sizeof v);
    return v;
}
std::string snapshot(uint32_t key) {
    const auto a = game::find_actor(key);
    if (!a || !*a) return "not in this client";
    const uintptr_t act = *a, comps = rv<uintptr_t>(act + 0x68);
    const uintptr_t info = rv<uintptr_t>(comps + 0x20), play = rv<uintptr_t>(comps + 0xB0);
    const bool play_ok = play && rv<uintptr_t>(play) == g_base + 0x594FAF0;
    std::string t = std::format("+0x5E 0x{:02X} +0xC2 {} +0xCF {} hide 0x{:X}", rv<uint8_t>(act + 0x5E),
                                rv<uint8_t>(act + 0xC2), rv<uint8_t>(act + 0xCF), rv<uint64_t>(info + 0x3B8));
    // 0x9A1880 sets +0xCF = 1 (0x9A1D2A) only when no entry of the list {[act+0x78], u32 [act+0x80]} (16 bytes each,
    // the object first) answers its vf+0x88 with true: the entries' vtables (exe-relative) say what is still pending.
    {
        const uintptr_t list = rv<uintptr_t>(act + 0x78);
        const uint32_t n = rv<uint32_t>(act + 0x80);
        t += std::format(" | +0x78 list {}:", n);
        for (uint32_t i = 0; i < n && i < 12; ++i) {
            const uintptr_t obj = rv<uintptr_t>(list + 16ull * i);
            const uintptr_t vt = rv<uintptr_t>(obj);
            t += std::format(" vt 0x{:X}", vt > g_base ? vt - g_base : vt);
        }
    }
    if (!play_ok) return t + " | no play component";
    const uintptr_t stage = rv<uintptr_t>(play + 0x318);
    t += std::format(" | play: owner 0x{:08X} scene 0x{:X} +0x3D8 {} +0x320 0x{:X} +0x3D9 {} stage 0x{:X}",
                     rv<uint32_t>(play + 0x2D8), rv<uint64_t>(play + 0x2E0), rv<uint8_t>(play + 0x3D8),
                     rv<uint64_t>(play + 0x320), rv<uint8_t>(play + 0x3D9), stage);
    if (stage) {
        const uintptr_t mgr = rv<uintptr_t>(stage + 0x28);
        t += std::format(" (key 0x{:X} active +0x180 {} | manager 0x{:X}: ready +0x259 {} loading +0x174 {} created "
                         "+0x17C {})",
                         rv<uint64_t>(stage + 0x30), rv<uint8_t>(stage + 0x180), mgr, rv<uint8_t>(mgr + 0x259),
                         rv<uint32_t>(mgr + 0x174), rv<uint32_t>(mgr + 0x17C));
    }
    return t;
}
std::mutex g_mu;
std::vector<Bound> g_bound;  // at most 64, newest kept
std::atomic<uint64_t> c_noted{0}, c_unhidden{0}, c_failed{0};

template <class T>
bool rd(uintptr_t a, T& v) {
    return a && game::safe_read(a, &v, sizeof v);
}

void note(uintptr_t raw) {
    uint16_t len = 0;
    uint32_t scene = 0;
    if (!rd(raw + 3, len) || !rd(raw + kHeader + kResourceAt, scene) || !mul::is_shop_scene(scene)) return;
    std::lock_guard lk(g_mu);
    for (uint32_t o = kBindingsAt; o + kBindingSize <= len; o += kBindingSize) {
        uint32_t key = 0;
        if (!rd(raw + kHeader + o + 4, key) || (key >> 28) != 0xB) continue;
        bool known = false;
        for (Bound& b : g_bound)
            if (b.key == key) {
                b = {key, scene, GetTickCount64(), false};
                known = true;
            }
        if (!known) {
            if (g_bound.size() >= 64) g_bound.erase(g_bound.begin());
            g_bound.push_back({key, scene, GetTickCount64(), false});
        }
        c_noted.fetch_add(1);
    }
}

void on_2380(safetyhook::Context& ctx) {
    if ((ctx.r14 & 0xFF) != 4) return;
    uintptr_t wrap = 0, raw = 0;
    if (!rd(ctx.rbp + 0x100, wrap) || !rd(wrap + 0x18, raw) || !raw) return;
    note(raw);
}

// The call, SEH-guarded: the game's own setter on its own component (it takes the owner's lock itself).
bool call_setter(uintptr_t info) {
    __try {
        g_setter(info, mul::kHideBit, 0, 0.5f, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void worker() {
    uint32_t last_owner = 0xFFFFFFFF;
    uint64_t last_tick_log = 0;
    for (;;) {
        Sleep(500);
        if (g_tick_hook) {
            const uint32_t o = g_tick_owner.load();
            const uint64_t t = GetTickCount64();
            if (static uint64_t seen = 0; c_login_remote.load() != seen) {
            seen = c_login_remote.load();
            log::info("MerchantUnhide [login guard] another player's character 0x{:08X} logged in on this client: "
                      "logins seen {}, this client's stage manager put back to state 3 {} time(s) | manager 0x{:X} state "
                      "before {} after {}",
                      g_last_restored_key.load(), seen, c_login_restored.load(), g_dbg_mgr.load(), g_dbg_before.load(),
                      g_dbg_after.load());
        }
        if (o != last_owner || t - last_tick_log > 10000) {
                // this client's character's manager (0xA0100001 on the host; any 0xA key of ours on the guest is
                // not needed: the host's is the one whose shop scenes stall)
                uintptr_t mgr = 0;
                if (const auto ch = game::find_actor(0xA0100001u); ch && *ch) {
                    const uintptr_t comps = rv<uintptr_t>(*ch + 0x68), seq = rv<uintptr_t>(comps + 0xA8);
                    mgr = rv<uintptr_t>(seq + 0x18);
                }
                log::info("MerchantUnhide [field tick] the client ticks the stage manager of field 0x{:X}'s owner user "
                          "0x{:08X} (this client's user 0x{:08X}) | calls {} | character 0xA0100001's manager 0x{:X}: "
                          "state +0x259 {} pending +0x1C0 {} loading +0x174 {} created +0x17C {} list +0x1D0 {} / "
                          "+0x1F0 {}",
                          g_tick_field.load(), o, local_user_key(), g_tick_calls.load(), mgr, rv<uint8_t>(mgr + 0x259),
                          rv<uint32_t>(mgr + 0x1C0), rv<uint32_t>(mgr + 0x174), rv<uint32_t>(mgr + 0x17C),
                          rv<uint32_t>(mgr + 0x1D0), rv<uint32_t>(mgr + 0x1F0));
                last_owner = o;
                last_tick_log = t;
            }
        }
        std::vector<Bound> todo;
        {
            std::lock_guard lk(g_mu);
            for (const Bound& b : g_bound)
                if (!b.done || b.diag < 3) todo.push_back(b);  // snapshots go on after the fix steps
        }
        const uint64_t now = GetTickCount64();
        for (const Bound& b : todo) {
            const uint64_t age = now - b.at_ms;
            const uint8_t want = age >= 20000 ? 3 : age >= 8000 ? 2 : age >= 2500 ? 1 : 0;
            if (want > b.diag) {
                log::info("MerchantUnhide [state +{} ms] NPC 0x{:08X} (shop scene 0x{:08X}): {}", now - b.at_ms, b.key,
                          b.scene, snapshot(b.key));
                std::lock_guard lk(g_mu);
                for (Bound& x : g_bound)
                    if (x.key == b.key) x.diag = want;
            }
            if (b.done || g_diag_only || !g_guest) continue;  // the host needs only the login guard (its own scenes tick)
            // The guest: its shop NPCs belong to the host's scenes (copies in X), which this client never ticks, so
            // the scene-start step that clears their scene component's wait byte +0x3E0 (and lets 0x9A1880 finish
            // their login, +0xCF) never comes. After kStuckMs: the byte cleared as that step does; the hide bit 9
            // (their AI runs on the host only) with the game's setter.
            if (now - b.at_ms >= mul::kStuckMs) {
                if (const auto a = game::find_actor(b.key); a && *a) {
                    uintptr_t comps = 0, play = 0, vt = 0;
                    uint8_t wait = 0;
                    if (rd(*a + 0x68, comps) && rd(comps + 0xB0, play) && rd(play, vt) && vt == g_base + 0x594FAF0 &&
                        rd(play + 0x3E0, wait) && wait) {
                        *reinterpret_cast<volatile uint8_t*>(play + 0x3E0) = 0;
                        log::info("MerchantUnhide [guest] NPC 0x{:08X} of shop scene 0x{:08X}: its scene component "
                                  "still waited for the host's scene {} ms after the binding: released (+0x3E0 0)",
                                  b.key, b.scene, now - b.at_ms);
                        continue;  // next pass: its login completes, then the hide bit
                    }
                }
            }
            if (now - b.at_ms > 120000) {  // two minutes: whatever happened, stop watching it
                std::lock_guard lk(g_mu);
                for (Bound& x : g_bound)
                    if (x.key == b.key) x.done = true;
                continue;
            }
            const auto a = game::find_actor(b.key);
            uintptr_t comps = 0, info = 0;
            uint64_t hide = 0;
            if (!a || !*a || !rd(*a + 0x68, comps) || !rd(comps + 0x20, info) || !rd(info + 0x3B8, hide)) continue;
            if (!mul::should_unhide(hide, b.at_ms, now)) continue;
            const bool ok = call_setter(info);
            uint64_t after = 0;
            rd(info + 0x3B8, after);
            (ok ? c_unhidden : c_failed).fetch_add(1);
            log::info("MerchantUnhide: NPC 0x{:08X} of shop scene 0x{:08X} still hidden {} ms after its binding (hide "
                      "0x{:X}): {} -> 0x{:X}", b.key, b.scene, now - b.at_ms, hide,
                      ok ? "the game's setter cleared bit 9" : "the setter FAULTED", after);
            std::lock_guard lk(g_mu);
            for (Bound& x : g_bound)
                if (x.key == b.key) x.done = true;
        }
    }
}

}  // namespace

bool merchant_unhide_is_shop_npc(uint32_t key) noexcept {
    if (!g_on || !key) return false;
    std::lock_guard lk(g_mu);
    for (const Bound& b : g_bound)
        if (b.key == key) return true;
    return false;
}

void merchant_unhide_note_2380(uintptr_t raw) noexcept {
    if (!g_on || !raw) return;
    try {
        note(raw);
    } catch (...) {
    }
}

void merchant_unhide_start(bool on, bool guest, bool diag_only, bool guard) {
    g_diag_only = diag_only;
    g_guard = guard;
    g_guest = guest;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!on) {
        log::info("MerchantUnhide off (merchant_unhide=0): shop NPCs left as the game leaves them (hidden in co-op)");
        return;
    }
    uint8_t h[sizeof kSetterHead] = {};
    if (!game::safe_read(g_base + kSetter, h, sizeof h) || std::memcmp(h, kSetterHead, sizeof h) != 0) {
        log::warn("MerchantUnhide NOT started: the hide setter 0x{:X} has other bytes", kSetter);
        return;
    }
    g_setter = reinterpret_cast<SetterFn>(g_base + kSetter);
    if (!guest) {  // the guest: guest_seq_route's hook of the same point feeds merchant_unhide_note_2380
        uint8_t b[sizeof k2380Bytes] = {};
        if (!game::safe_read(g_base + k2380Point, b, sizeof b) || std::memcmp(b, k2380Bytes, sizeof b) != 0) {
            log::warn("MerchantUnhide NOT started: the 2380 point 0x{:X} has other bytes", k2380Point);
            return;
        }
        g_hook = safetyhook::create_mid(reinterpret_cast<void*>(g_base + k2380Point), &on_2380);
        if (!g_hook) {
            log::warn("MerchantUnhide NOT started: the 2380 point could not be hooked");
            return;
        }
    }
    {
        constexpr uint8_t kTickHead[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18};
        uint8_t th[sizeof kTickHead] = {};
        if (game::safe_read(g_base + 0xAAABB0, th, sizeof th) && std::memcmp(th, kTickHead, sizeof th) == 0)
            g_tick_hook = safetyhook::create_mid(reinterpret_cast<void*>(g_base + 0xAAABB0), &on_tick_mgr);
    }
    if (g_guard) {
        constexpr uint8_t kPre[] = {0xE8, 0xD1, 0xC4, 0xF8, 0x00, 0x48, 0x8B, 0x06, 0xC6, 0x80, 0xC2, 0x00, 0x00, 0x00, 0x01};
        uint8_t got[sizeof kPre] = {};
        if (game::safe_read(g_base + 0x99C17A, got, sizeof got) && std::memcmp(got, kPre, sizeof kPre) == 0) {
            g_login_pre = safetyhook::create_mid(reinterpret_cast<void*>(g_base + 0x99C17A), &on_login_pre);
            g_login_post = safetyhook::create_mid(reinterpret_cast<void*>(g_base + 0x99C17F), &on_login_post);
        }
        log::info("MerchantUnhide [login guard] {}: another player's character logging in on this client no longer "
                  "stops this client's own stage manager (0x99C17A / 0x99C17F)",
                  g_login_pre && g_login_post ? "ON" : "NOT installed (other bytes or hook failed)");
    }
    g_on = true;
    std::thread(worker).detach();
    log::info("MerchantUnhide ON (merchant_unhide=1, {}): NPCs bound to the {} shop scenes keeping hide bit 0x200 for "
              "{} ms are unhidden with the game's setter 0x{:X}{}",
              guest ? "guest" : "host", mul::kShopScenes.size(), mul::kStuckMs, kSetter,
              guest ? " (bindings from guest_seq_route's 2380 point)" : " (own hook of the 2380 point 0xD3414E)");
}

std::string merchant_unhide_describe() {
    return std::format("MerchantUnhide ({}): shop NPCs noted {}, unhidden {}, setter faults {}", g_on ? "on" : "off",
                       c_noted.load(), c_unhidden.load(), c_failed.load());
}

}  // namespace cdmp::session2
