#include "mod/session2/clock_push.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <format>

#include "mod/config.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/clock_gate.h"
#include "mod/session2/clock_push_logic.h"
#include "mod/session2/server.h"
#include "mod/session2/user2_funnels_logic.h"

namespace cdmp::session2 {
namespace {

// ---------------------------------------------------------------- T1 / T2 (host)

std::atomic<uint8_t> g_mode{0};  // u2f::Mode; written once by clock_push_configure (after g_base)
uintptr_t g_base = 0;

// The state both pushers share (T1: the server's thread in take_frame; T2: the watch thread). Short sections,
// no allocation, nothing of the game under it.
SRWLOCK g_lock = SRWLOCK_INIT;
ckp::PushWatch g_watch;   // under g_lock
uint32_t g_epoch = 0;     // under g_lock: the guest epoch g_watch belongs to
struct Locked {
    Locked() { AcquireSRWLockExclusive(&g_lock); }
    ~Locked() { ReleaseSRWLockExclusive(&g_lock); }
    Locked(const Locked&) = delete;
    Locked& operator=(const Locked&) = delete;
};

std::atomic<uint32_t> g_last_ack_actor{0};  // the actor of the last 2762 T1 saw (T2's key while 2756 is unknown)
std::atomic<uint64_t> c_t1_seen{0}, c_t1_queued{0}, c_t1_would{0}, c_t1_refused{0}, c_t1_unreadable{0},
    c_t1_implausible{0}, c_t1_no_key{0}, c_t1_key_differs{0};
std::atomic<uint64_t> c_t2_queued{0}, c_t2_would{0}, c_t2_refused{0}, c_t2_unreadable{0}, c_t2_implausible{0},
    c_t2_unsettled{0}, c_t2_no_key{0};
std::atomic<uint64_t> c_lines{0};
constexpr uint64_t kMaxLines = 128;

u2f::Mode mode() { return static_cast<u2f::Mode>(g_mode.load(std::memory_order_acquire)); }

template <class... Args>
void line(std::format_string<Args...> fmt, Args&&... args) {
    const uint64_t n = c_lines.fetch_add(1, std::memory_order_relaxed);
    if (n < kMaxLines) log::info(fmt, std::forward<Args>(args)...);
    if (n == kMaxLines) log::info("Session2 clock push: line limit ({}) reached; counters in the session2 report", kMaxLines);
}

std::string show(const ckp::FieldTime& t) {
    return std::format("day {} {:02}:{:02}:{:02}.{:03} x{} @{} (t0 {} ms)", t.day, t.hour, t.minute, t.second, t.ms,
                       t.scale, t.anchor, ckp::t0_game_ms(t));
}
std::string show(const uint8_t* mem) { return show(ckp::decode(mem)); }

// The server's world clock, two equal reads (up to three pairs). Fast: SEH-guarded direct reads (take_frame, a
// game thread; the global is in the exe's .data); otherwise VirtualQuery + SEH (the mod's own threads).
bool read_clock(uintptr_t addr, uint8_t (&out)[ckp::kFieldTimeMem], bool fast) {
    for (int i = 0; i < 3; ++i) {
        uint8_t a[ckp::kFieldTimeMem] = {}, b[ckp::kFieldTimeMem] = {};
        const bool ok = fast ? game::fast_read(addr, a, sizeof a) && game::fast_read(addr, b, sizeof b)
                             : game::safe_read(addr, a, sizeof a) && game::safe_read(addr, b, sizeof b);
        if (!ok) return false;
        if (ckp::consistent(a, b)) {
            std::memcpy(out, a, sizeof out);
            return true;
        }
    }
    return false;
}

// Under g_lock: another guest epoch means user 2 has been given nothing yet.
void sync_epoch_locked() {
    if (const uint32_t e = server_epoch(); e != g_epoch) {
        g_epoch = e;
        g_watch.reset();
        // P8 re-entry (s25/reentrada.md §2.2 H5), only with [session2] rejoin on (review of c4367f8, 6): the previous
        // user 2's last 2762 actor is not T2's key fallback now.
        static const bool rejoin_on = u2f::parse_mode(loaded_config().s2_rejoin).mode != u2f::Mode::Off;
        if (rejoin_on) g_last_ack_actor.store(0, std::memory_order_relaxed);
    }
}

}  // namespace

bool clock_push_configure(const std::string& raw, bool create_session) {
    const u2f::ParsedMode p = u2f::parse_mode(raw);
    if (!p.known) {
        log::warn("Session2: user2_clock_push='{}' is not 0, probe or 1; it stays off", raw);
        return false;
    }
    if (p.mode == u2f::Mode::Off) return false;
    if (!create_session || !clock_gate_active()) {
        log::warn("Session2: user2_clock_push='{}' needs create_session=1 and the clock gate (clock_gate=1, installed); "
                  "it stays off",
                  raw);
        return false;
    }
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_mode.store(static_cast<uint8_t>(p.mode), std::memory_order_release);
    log::info("Session2 clock push {} (user2_clock_push={}): T1 right after every LoadingCompleteAck {} queued for "
              "user 2, T2 when the world clock changes while user 2 is in the world (settled {} ms, at most one every "
              "{} ms, never during its loads): a native SetFieldTimeAck {} ({:02X} {:02X} FF 23 00 | user 2's character "
              "| FieldTime 31 B) with the server's world clock exe+0x{:X} (two equal reads) {}; world clock now {}",
              p.mode == u2f::Mode::On ? "ON" : "PROBE", u2f::mode_name(p.mode), ckp::kOpLoadingCompleteAck,
              g_watch.config().settle_ms, g_watch.config().min_gap_ms, ckp::kOpSetFieldTimeAck,
              ckp::kOpSetFieldTimeAck & 0xFF, ckp::kOpSetFieldTimeAck >> 8, ckp::kServerClock,
              p.mode == u2f::Mode::On ? "queued for user 2" : "logged only (WOULD queue), nothing sent", [] {
                  uint8_t mem[ckp::kFieldTimeMem] = {};
                  return read_clock(g_base + ckp::kServerClock, mem, false) ? show(mem) : std::string("unreadable");
              }());
    return true;
}

bool clock_push_active() { return mode() != u2f::Mode::Off; }

void clock_push_on_2762(std::span<const uint8_t> payload, uint32_t player_key, ClockPushFn push) noexcept {
    const u2f::Mode m = mode();
    if (m == u2f::Mode::Off) return;
    try {
        c_t1_seen.fetch_add(1, std::memory_order_relaxed);
        const uint64_t now_ms = GetTickCount64();
        {
            // This 2762 re-anchors user 2's client (flag 1): whatever it was given before no longer holds. Every
            // early return below leaves it so, and T2 gives it the clock once settled (review of 8047e5c).
            Locked l;
            sync_epoch_locked();
            g_watch.forget();
        }
        const auto ack = ckp::parse_loading_ack(payload);
        const uint32_t actor = ack ? ack->actor : 0;
        if (actor) g_last_ack_actor.store(actor, std::memory_order_relaxed);
        if (ack && player_key && actor != player_key) c_t1_key_differs.fetch_add(1, std::memory_order_relaxed);
        const std::string what = ack ? std::format("actor 0x{:08X}, flag {}, now {}", actor, ack->flag, ack->now)
                                     : std::string("payload too short");
        const uint32_t key = ckp::push_key(player_key, actor);
        if (!key) {
            c_t1_no_key.fetch_add(1, std::memory_order_relaxed);
            line("Session2 clock push [{}] T1: user 2's 2762 ({}) queued; NO 2816: user 2's character key unknown "
                 "(T2 tries again)",
                 u2f::mode_name(m), what);
            return;
        }
        uint8_t mem[ckp::kFieldTimeMem] = {};
        if (!read_clock(g_base + ckp::kServerClock, mem, /*fast=*/true)) {
            c_t1_unreadable.fetch_add(1, std::memory_order_relaxed);
            line("Session2 clock push [{}] T1: user 2's 2762 ({}) queued; NO 2816: the world clock exe+0x{:X} is "
                 "unreadable or changed under three double reads (T2 tries again)",
                 u2f::mode_name(m), what, ckp::kServerClock);
            return;
        }
        const ckp::FieldTime ft = ckp::decode(mem);
        if (!ckp::plausible(ft)) {
            c_t1_implausible.fetch_add(1, std::memory_order_relaxed);
            line("Session2 clock push [{}] T1: user 2's 2762 ({}) queued; NO 2816: the world clock is not a running "
                 "calendar ({}; T2 gives it once it is one)",
                 u2f::mode_name(m), what, show(ft));
            return;
        }
        const auto packet = ckp::make_set_field_time_ack(key, mem);
        const bool queued = m == u2f::Mode::On && push && push(std::span<const uint8_t>(packet));
        const bool given = queued || m == u2f::Mode::Probe;
        {
            Locked l;
            sync_epoch_locked();
            if (given) {
                g_watch.sent(mem, now_ms);
            } else {
                g_watch.failed(now_ms);
            }
        }
        (m == u2f::Mode::Probe ? c_t1_would : queued ? c_t1_queued : c_t1_refused)
            .fetch_add(1, std::memory_order_relaxed);
        const int64_t age = ack ? static_cast<int64_t>(ack->now) - static_cast<int64_t>(ft.anchor) : 0;
        line("Session2 clock push [{}] T1: user 2's 2762 ({}) queued -> 2816 for 0x{:08X} {}: world clock {} (its "
             "anchor {} ms before the 2762's now)",
             u2f::mode_name(m), what, key,
             m == u2f::Mode::Probe ? "WOULD queue (probe: nothing sent)"
             : queued              ? "QUEUED right behind it"
                                   : "NOT queued (the outgoing queue is over half its budget; T2 tries again)",
             show(ft), age);
    } catch (...) {
    }
}

void clock_push_tick(uint64_t now_ms) noexcept {
    const u2f::Mode m = mode();
    if (m == u2f::Mode::Off) return;
    try {
        if (!server_user2_present()) {
            Locked l;
            sync_epoch_locked();
            g_watch.reset();  // user 2 gone (or not there yet): its next 2762 starts over with T1
            return;
        }
        uint8_t mem[ckp::kFieldTimeMem] = {};
        if (!read_clock(g_base + ckp::kServerClock, mem, /*fast=*/false)) {
            c_t2_unreadable.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const bool in_world = server_user2_in_world();
        ckp::PushWatch::Verdict v;
        {
            Locked l;
            sync_epoch_locked();
            v = g_watch.sample(mem, in_world, now_ms);
        }
        static uint8_t implausible_logged[ckp::kFieldTimeMem] = {};  // the watch thread only
        switch (v) {
            case ckp::PushWatch::Verdict::Unsettled: c_t2_unsettled.fetch_add(1, std::memory_order_relaxed); return;
            case ckp::PushWatch::Verdict::Implausible:
                c_t2_implausible.fetch_add(1, std::memory_order_relaxed);
                if (!ckp::consistent(mem, implausible_logged)) {
                    std::memcpy(implausible_logged, mem, sizeof mem);
                    line("Session2 clock push [{}] T2: the world clock changed to something that is not a running "
                         "calendar ({}); not given to user 2",
                         u2f::mode_name(m), show(mem));
                }
                return;
            case ckp::PushWatch::Verdict::Push: break;
            default: return;
        }
        uint32_t key = server_user2_player_key();
        if (!key) key = g_last_ack_actor.load(std::memory_order_relaxed);
        if (!key) {
            c_t2_no_key.fetch_add(1, std::memory_order_relaxed);
            Locked l;
            g_watch.failed(now_ms);
            return;
        }
        const auto packet = ckp::make_set_field_time_ack(key, mem);
        const bool queued = m == u2f::Mode::On && server_push_extra(std::span<const uint8_t>(packet));
        const bool given = queued || m == u2f::Mode::Probe;
        bool first = false;
        {
            Locked l;
            sync_epoch_locked();
            first = !g_watch.has_sent();
            if (given) {
                g_watch.sent(mem, now_ms);
            } else {
                g_watch.failed(now_ms);
            }
        }
        (m == u2f::Mode::Probe ? c_t2_would : queued ? c_t2_queued : c_t2_refused)
            .fetch_add(1, std::memory_order_relaxed);
        line("Session2 clock push [{}] T2: {} while user 2 is in the world -> 2816 for 0x{:08X} {}: world clock {}",
             u2f::mode_name(m),
             first ? "user 2 has not been given the clock since its last 2762 (T1 refused or returned early)"
                   : "the world clock changed",
             key,
             m == u2f::Mode::Probe ? "WOULD queue (probe: nothing sent)"
             : queued              ? "QUEUED"
                                   : "NOT queued (the outgoing queue is over half its budget; tried again later)",
             show(mem));
    } catch (...) {
    }
}

std::string clock_push_describe() {
    const u2f::Mode m = mode();
    if (m == u2f::Mode::Off) return {};
    return std::format("clock push [{}]: T1 2762s {} -> 2816 queued {} would {} refused {} (unreadable clock {}, "
                       "implausible {}, no key {}, 2762 actor != user 2's character {}) | T2 queued {} would {} refused "
                       "{} (unreadable {}, implausible {}, unsettled samples {}, no key {})",
                       u2f::mode_name(m), c_t1_seen.load(), c_t1_queued.load(), c_t1_would.load(), c_t1_refused.load(),
                       c_t1_unreadable.load(), c_t1_implausible.load(), c_t1_no_key.load(), c_t1_key_differs.load(),
                       c_t2_queued.load(), c_t2_would.load(), c_t2_refused.load(), c_t2_unreadable.load(),
                       c_t2_implausible.load(), c_t2_unsettled.load(), c_t2_no_key.load());
}

// ---------------------------------------------------------------- clock probe (host and guest, read only)

namespace {

std::atomic<bool> g_probe_on{false};
bool g_probe_guest = false;  // written before the thread starts
uintptr_t g_probe_base = 0;
std::atomic<uint64_t> p_2816{0}, p_2762{0};
SRWLOCK g_probe_lock = SRWLOCK_INIT;
uint8_t g_last_2816[ckp::kFieldTimeMem] = {};  // under g_probe_lock
uint32_t g_last_2816_key = 0;                  // under g_probe_lock
bool g_have_2816 = false;                      // under g_probe_lock

std::string probe_clock(uintptr_t rva) {
    uint8_t mem[ckp::kFieldTimeMem] = {};
    return read_clock(g_probe_base + rva, mem, /*fast=*/false) ? show(mem) : std::string("unreadable");
}

std::string probe_manager(bool& ok) {
    ok = false;
    const auto world = game::read_ptr(g_probe_base + ckp::kWorldSystem);
    if (!world || !*world) return "unreadable (no world system)";
    const auto mgr = game::read_ptr(*world + ckp::kWorldTimeMgr);
    if (!mgr || !*mgr) return "unreadable (no time manager)";
    uint8_t d[ckp::kMgrDumpSize] = {};
    if (!game::safe_read(*mgr + ckp::kMgrFlags, d, sizeof d)) return "unreadable";
    ok = true;
    uint64_t tail = 0;
    std::memcpy(&tail, d + (0x50 - ckp::kMgrFlags), sizeof tail);
    return std::format("+28 active {} +29 private {} +2A {} +2B {} | private clock {} | +50 {:016X}", d[0], d[1], d[2],
                       d[3], show(d + (ckp::kMgrPrivateTime - ckp::kMgrFlags)), tail);
}

void probe_once(bool& last_unreadable) {
    const std::string client = probe_clock(ckp::kClientClock);
    const std::string server = probe_clock(ckp::kServerClock);
    bool mgr_ok = false;
    const std::string mgr = probe_manager(mgr_ok);
    const auto bias = game::read<int64_t>(g_probe_base + ckp::kClockBias);
    const bool unreadable = !mgr_ok && client == "unreadable" && server == "unreadable";
    if (unreadable && last_unreadable) return;  // nothing new to say (e.g. the title screen)
    last_unreadable = unreadable;
    std::string extra;
    if (g_probe_guest) {
        uint8_t last[ckp::kFieldTimeMem] = {};
        uint32_t key = 0;
        bool have = false;
        AcquireSRWLockExclusive(&g_probe_lock);
        have = g_have_2816;
        key = g_last_2816_key;
        std::memcpy(last, g_last_2816, sizeof last);
        ReleaseSRWLockExclusive(&g_probe_lock);
        extra = std::format(" | handed to the client: 2816 {} (last for 0x{:08X}: {}), 2762 {}", p_2816.load(), key,
                            have ? show(last) : std::string("none"), p_2762.load());
    } else {
        extra = std::format(" | user 2 in the world {}", server_user2_in_world() ? "yes" : "no");
    }
    log::info("ClockProbe [{}]: client clock exe+0x{:X} {} | server clock exe+0x{:X} {} | time manager {} | 2188 bias "
              "{}{}",
              g_probe_guest ? "guest" : "host", ckp::kClientClock, client, ckp::kServerClock, server, mgr,
              bias ? std::format("{} ms", *bias) : std::string("unreadable"), extra);
}

// Co-op+ 2026-11-15 (the weather port's research, read only, with clock_probe=1): the scene weather system X of
// 1.0.0.3663 = [[[exe+0x7160240]+0x60]+0xF70] (WeatherSimulatorScriptComponent's update 0xE9ECB0 reads it so), its
// vtable [X], viewer flag X+0x31, X+0x88, holder [X+0x60] and the raw words of [holder+0x18 .. +0x30] (the blocks:
// on 2976 A/B/C were +0x18/+0x20/+0x28; the 3663 rain getter 0x4142CC0 reads A at +0x20, B at +0x28).
std::string weather_words(uintptr_t blk, uint32_t from, uint32_t to) {
    std::string t;
    if (!blk) return "0";
    for (uint32_t o = from; o < to; o += 4) {
        uint32_t w = 0;
        if (!game::safe_read(blk + o, &w, 4)) return t + " unreadable";
        float f = 0;
        std::memcpy(&f, &w, 4);
        t += (w == 0x7F7FFFFFu)                      ? std::format("{:X}:MAX ", o)
             : (f > -1e6f && f < 1e6f && f == f)     ? std::format("{:X}:{:.4g} ", o, f)
                                                     : std::format("{:X}:#{:08X} ", o, w);
    }
    return t;
}

void weather_dump() {
    const auto pm = game::read_ptr(g_probe_base + 0x7160240);
    const uintptr_t m = pm ? *pm : 0;
    const auto ps = m ? game::read_ptr(m + 0x60) : std::nullopt;
    const uintptr_t sc = ps ? *ps : 0;
    const auto px = sc ? game::read_ptr(sc + 0xF70) : std::nullopt;
    const uintptr_t x = px ? *px : 0;
    if (!x) {
        log::info("WeatherDump [{}]: no X (module 0x{:X} scene 0x{:X})", g_probe_guest ? "guest" : "host", m, sc);
        return;
    }
    const auto vt = game::read_ptr(x);
    const auto mvt = game::read_ptr(m);
    const auto gvf = mvt && *mvt ? game::read_ptr(*mvt + 0x60) : std::nullopt;
    const auto flag = game::read<uint8_t>(x + 0x31);
    const auto wc = game::read<uint32_t>(x + 0x88);
    const auto ph = game::read_ptr(x + 0x60);
    const uintptr_t h = ph ? *ph : 0;
    uintptr_t blk[4] = {};
    for (int i = 0; i < 4; ++i) {
        const auto p = h ? game::read_ptr(h + 0x18 + 8 * i) : std::nullopt;
        blk[i] = p ? *p : 0;
    }
    const auto rel = [&](uintptr_t p) { return p > g_probe_base && p < g_probe_base + 0x20000000 ? p - g_probe_base : p; };
    const char* who = g_probe_guest ? "guest" : "host";
    log::info("WeatherDump [{}]: X 0x{:X} vtable exe+0x{:X} | module vf60 exe+0x{:X} | X+31 {} X+88 {} | holder 0x{:X}: "
              "+18 0x{:X} +20 0x{:X} +28 0x{:X} +30 0x{:X} | X+120..+130: {}",
              who, x, vt ? rel(*vt) : 0, gvf ? rel(*gvf) : 0, flag ? *flag : 0xFF, wc ? *wc : 0xFFFFFFFF, h, blk[0],
              blk[1], blk[2], blk[3], weather_words(x, 0x120, 0x130));
    for (int i = 0; i < 4; ++i) {
        if (!blk[i]) continue;
        log::info("WeatherDump [{}] holder+0x{:X} 0x60..0x200: {}", who, 0x18 + 8 * i, weather_words(blk[i], 0x60, 0x200));
    }
}

DWORD WINAPI probe_thread(LPVOID) {
    bool last_unreadable = false;
    for (uint64_t n = 1;; ++n) {
        Sleep(5000);
        try {
            probe_once(last_unreadable);
            if (n % 6 == 0) weather_dump();
        } catch (...) {
        }
    }
}

}  // namespace

void clock_probe_start(bool on, bool guest) {
    if (!on || g_probe_on.load()) return;
    g_probe_guest = guest;
    g_probe_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_probe_on.store(true);
    log::info("ClockProbe ON ({}, clock_probe=1, read only): every 5 s the client clock exe+0x{:X}, the server clock "
              "exe+0x{:X} (t0 = base - anchor x scale: the two-PC check compares the guest's client t0 with the host's "
              "server t0, within 2000 game ms), the time manager [[exe+0x{:X}]+0x{:X}] +0x28..+0x57 and the 2188 bias "
              "exe+0x{:X}{}",
              guest ? "guest" : "host", ckp::kClientClock, ckp::kServerClock, ckp::kWorldSystem, ckp::kWorldTimeMgr,
              ckp::kClockBias, guest ? "; and the 2816 / 2762 handed to this client" : "");
    if (HANDLE h = CreateThread(nullptr, 0, probe_thread, nullptr, 0, nullptr)) CloseHandle(h);
}

void clock_probe_on_delivered(std::span<const uint8_t> packet) noexcept {
    if (!g_probe_on.load(std::memory_order_relaxed) || packet.size() < 2) return;
    const auto op = static_cast<uint16_t>(packet[0] | (packet[1] << 8));
    if (op == ckp::kOpLoadingCompleteAck) {
        p_2762.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (op != ckp::kOpSetFieldTimeAck) return;
    p_2816.fetch_add(1, std::memory_order_relaxed);
    uint8_t mem[ckp::kFieldTimeMem] = {};
    uint32_t key = 0;
    if (!ckp::parse_set_field_time_ack(packet, key, mem)) return;
    AcquireSRWLockExclusive(&g_probe_lock);
    std::memcpy(g_last_2816, mem, sizeof mem);
    g_last_2816_key = key;
    g_have_2816 = true;
    ReleaseSRWLockExclusive(&g_probe_lock);
}

}  // namespace cdmp::session2
