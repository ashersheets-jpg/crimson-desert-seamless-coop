#include "mod/session2/voice_probe.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include <safetyhook.hpp>

#include "mod/game/actor_lookup.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/net/local_capture.h"
#include "mod/session2/guest_seq_route.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

// 1.0.0.3732 (build/port3732 fmap from the 3576 / 3663 addresses; heads read in the 3732 exe).
constexpr uintptr_t kVoiceProc = 0x800A10, kSoundProc = 0x7FC3A0, kPlayEvent = 0x3D358F0;
constexpr uint8_t kVoiceHead[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20};
constexpr uint8_t kSoundHead[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0x44, 0x24, 0x60, 0x48, 0x8B, 0xDA};
constexpr uint8_t kPlayHead[] = {0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x48, 0x20, 0x4C, 0x89, 0x40, 0x18, 0x89, 0x50, 0x10};
constexpr uint32_t kHostCharacter = 0xA0100001;
constexpr uint32_t kActorKey = 0x60;

uintptr_t g_base = 0;
bool g_guest = false;
SafetyHookInline g_voice_hook, g_sound_hook, g_play_hook;
// [who: 0 the other player, 1 this game's own player][0 voice calls, 1 voice posts, 2 accepted, 3 sound calls,
// 4 sound posts, 5 accepted]
std::atomic<uint32_t> g_n[2][6]{};
std::atomic<uint32_t> g_other_key{0}, g_own_key{0};
// 2026-11-22 (cs98: every count 0 on the host, both keys 0, the swing sounds too): every call counted, whoever it is
// for, and the first calls' arguments logged, to see whether the processors run at all on 3732 and what p4 holds.
std::atomic<uint32_t> g_calls[2]{};  // [0 voice, 1 sound] all calls
std::atomic<int> g_logged{0};
thread_local int t_who = -1;
thread_local int t_kind = -1;  // 0 voice, 1 sound

// 2026-11-23 (cs98c: who -1 on every call; the 3576 decomp of ClientFrameEventVoiceSound::vfunction2 0x79A160 reads
// plVar1 = *param_4 = *p4). cs98d's key scan: *p4 is an ACTOR (+0x60 its key, +0x90 its controller user), every
// logged call an NPC's (0xB01... keys). Now each call's *p4 is compared with the two players' actor pointers (found
// by key every 2 s: 0xA0100001 the host's; user 2's from the server on the host, its own key on the guest), and the
// calls on any 0xA01 key are counted: if the players never appear, their voices go another way.
std::atomic<uintptr_t> g_actor_host{0}, g_actor_u2{0};
std::atomic<uint32_t> g_key_u2{0};
std::atomic<uint32_t> g_char_calls[2]{};  // calls whose *p4+0x60 is any character key (0xA01...) [voice, sound]
std::atomic<int> g_player_logged{0};
std::string keys_in(uintptr_t obj) {
    uint32_t buf[0x40]{};
    if (!obj || !game::fast_read(obj, buf, sizeof buf)) return " (unreadable)";
    std::string s;
    for (int i = 0; i < 0x40; ++i) {
        if ((buf[i] >> 20) == 0xA01 || (buf[i] >> 20) == 0xB01 || (buf[i] >> 20) == 0x901) {
            s += std::format(" +{:X}={:08X}", i * 4, buf[i]);
        }
    }
    return s.empty() ? std::string(" none") : s;
}
// A player's sound object id (the u32 the game posts its sounds with): [[[[actor+0x68]+0x60]+0x18]+0x70]+0x18 (the
// 3576 voice processor's path; see play_detour). 0 = not readable.
std::atomic<uint32_t> g_obj_host{0}, g_obj_u2{0};
uint32_t sound_obj_of(uintptr_t actor) noexcept {
    uintptr_t holder = 0, x = 0, s = 0, o = 0;
    uint32_t id = 0;
    if (!actor || !game::fast_read(actor + 0x68, &holder, 8) || !holder || !game::fast_read(holder + 0x60, &x, 8) ||
        !x || !game::fast_read(x + 0x18, &s, 8) || !s || !game::fast_read(s + 0x70, &o, 8) || !o ||
        !game::fast_read(o + 0x18, &id, 4))
        return 0;
    return id;
}
void refresh_players() noexcept {
    uint32_t u2 = 0;
    if (g_guest) {
        u2 = guest_seq_route_own_key();
        if (!u2) u2 = net::capture_own_player_key();
    } else {
        u2 = server_user2_player_key();
    }
    g_key_u2.store(u2, std::memory_order_relaxed);
    const auto h = game::find_actor(kHostCharacter);
    g_actor_host.store(h ? *h : 0, std::memory_order_relaxed);
    const auto g = u2 ? game::find_actor(u2) : std::nullopt;
    g_actor_u2.store(g ? *g : 0, std::memory_order_relaxed);
    g_obj_host.store(sound_obj_of(h ? *h : 0), std::memory_order_relaxed);
    g_obj_u2.store(sound_obj_of(g ? *g : 0), std::memory_order_relaxed);
}

// 0 the other player, 1 this game's own player, -1 anyone else.
int who_of(uintptr_t ctx, int kind) noexcept {
    uintptr_t actor = 0;
    uint32_t key = 0;
    if (!ctx || !game::fast_read(ctx, &actor, sizeof actor) || !actor) return -1;
    game::fast_read(actor + kActorKey, &key, sizeof key);
    if ((key >> 20) == 0xA01) g_char_calls[kind].fetch_add(1, std::memory_order_relaxed);
    const uintptr_t ha = g_actor_host.load(std::memory_order_relaxed), ua = g_actor_u2.load(std::memory_order_relaxed);
    const uint32_t uk = g_key_u2.load(std::memory_order_relaxed);
    int host_char = -1;  // 1 the host's character, 0 user 2's
    if ((ha && actor == ha) || key == kHostCharacter) host_char = 1;
    else if ((ua && actor == ua) || (uk && key == uk)) host_char = 0;
    if (host_char < 0) return -1;
    const bool other = g_guest ? host_char == 1 : host_char == 0;  // the guest's other is the host, and the reverse
    (other ? g_other_key : g_own_key).store(host_char ? kHostCharacter : uk, std::memory_order_relaxed);
    return other ? 0 : 1;
}

uintptr_t run(SafetyHookInline& hook, int kind, uintptr_t self, uintptr_t p1, uintptr_t p2, uintptr_t p3,
              uintptr_t p4) {
    g_calls[kind == 0 ? 0 : 1].fetch_add(1, std::memory_order_relaxed);
    const int w = who_of(p4, kind == 0 ? 0 : 1);
    const bool first_player_call = w >= 0 && g_player_logged.load(std::memory_order_relaxed) < 4 &&
                                   g_player_logged.fetch_add(1) < 4;
    if (first_player_call || (g_logged.load(std::memory_order_relaxed) < 4 && g_logged.fetch_add(1) < 4)) {
        uintptr_t q[4]{};
        for (int i = 0; i < 4; ++i) game::fast_read((i == 0 ? p1 : i == 1 ? p2 : i == 2 ? p3 : p4), &q[i], 8);
        uintptr_t holder = 0;
        if (q[3]) game::fast_read(q[3] + 0x68, &holder, sizeof holder);
        log::info("VoiceProbe: {}{} call: self {:X} p1 {:X} p2 {:X} p3 {:X} p4 {:X} | *p1 {:X} *p2 {:X} *p3 {:X} *p4 {:X} | "
                  "who {} | keys in *p4:{} | in [*p4+0x68] {:X}:{} | in p3:{}",
                  first_player_call ? "a PLAYER's " : "", kind == 0 ? "voice" : "sound", self, p1, p2, p3, p4, q[0], q[1],
                  q[2], q[3], w, keys_in(q[3]), holder, keys_in(holder), keys_in(p3));
    }
    if (w >= 0) g_n[w][kind == 0 ? 0 : 3].fetch_add(1, std::memory_order_relaxed);
    const int keep_who = t_who, keep_kind = t_kind;
    t_who = w;
    t_kind = kind;
    const uintptr_t r = hook.call<uintptr_t>(self, p1, p2, p3, p4);
    t_who = keep_who;
    t_kind = keep_kind;
    return r;
}
uintptr_t __fastcall voice_detour(uintptr_t self, uintptr_t p1, uintptr_t p2, uintptr_t p3, uintptr_t p4) {
    return run(g_voice_hook, 0, self, p1, p2, p3, p4);
}
uintptr_t __fastcall sound_detour(uintptr_t self, uintptr_t p1, uintptr_t p2, uintptr_t p3, uintptr_t p4) {
    return run(g_sound_hook, 1, self, p1, p2, p3, p4);
}
// 2026-11-23 (cs98e: in ~820 voice and ~10000 sound processor calls none was on a player's actor): every post is
// counted by its sound object instead, whatever posted it. The 3576 decomp of the voice processor posts with
// FUN_143b577d0(manager, u32 [obj+0x18], obj+0x20, &event, ...), obj = [[[[actor+0x68]+0x60]+0x18]+0x70]: so each
// player's object id is read from its actor (refresh_players) and the posts for it are counted, with the first event
// ids, on both games. The friend's posts on this game vs its own posts on its game says whether its sounds play here.
std::atomic<uint32_t> g_posts[2]{}, g_posts_ok[2]{};  // [0 the host's character, 1 user 2's]
std::atomic<uint32_t> g_ev[2][8]{};                    // the first distinct event ids posted for each
// 2026-11-23 (cs98f: each game posts 5 event ids on its own player's object and only 3-4 on the other player's;
// 8AB4ADF9, which starts with the first swing, never on the other's, both ways): a count per event, and the call chain
// of each event's first post on each player's object (exe-relative return addresses), to find what posts 8AB4ADF9 for
// the own player and why that path skips the other one.
std::atomic<uint32_t> g_ev_n[2][8]{};
void log_chain(int who, uint32_t id) noexcept {
    void* frames[8]{};
    const USHORT n = RtlCaptureStackBackTrace(2, 8, frames, nullptr);  // past note_event and play_detour
    std::string s;
    for (USHORT i = 0; i < n; ++i) {
        const uintptr_t a = reinterpret_cast<uintptr_t>(frames[i]);
        s += a > g_base && a < g_base + 0x10000000 ? std::format(" exe+0x{:X}", a - g_base) : std::format(" {:X}", a);
    }
    log::info("VoiceProbe event {:08X}: first post on {}'s sound object on this {}; call chain:{}", id,
              who == 0 ? "the host's character" : "user 2's character", g_guest ? "guest" : "host", s);
}
void note_event(int who, uintptr_t ev) noexcept {
    uint32_t id = 0;
    if (!ev || !game::fast_read(ev, &id, 4) || !id) return;
    for (int i = 0; i < 8; ++i) {
        auto& slot = g_ev[who][i];
        uint32_t cur = slot.load(std::memory_order_relaxed);
        if (!cur && slot.compare_exchange_strong(cur, id, std::memory_order_relaxed)) {
            g_ev_n[who][i].fetch_add(1, std::memory_order_relaxed);
            log_chain(who, id);
            return;
        }
        if (cur == id) {
            g_ev_n[who][i].fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
}
// 2026-11-23 (cs98g: the other player's FE5BE4B4 / 22D10317 / 8AB4ADF9 are rare or absent on ITS character's object,
// while the own player's swing posts them on its own): those events counted by EVERY object they are posted on, to
// see whether the other player's swing sounds go to another object (its weapon, a scene emitter) or nowhere.
constexpr uint32_t kWatchEv[3] = {0xFE5BE4B4u, 0x22D10317u, 0x8AB4ADF9u};
std::atomic<uint32_t> g_wev_obj[3][8]{}, g_wev_n[3][8]{};
void note_any_object(uintptr_t ev, uint32_t obj) noexcept {
    uint32_t id = 0;
    if (!ev || !obj || !game::fast_read(ev, &id, 4)) return;
    for (int e = 0; e < 3; ++e) {
        if (id != kWatchEv[e]) continue;
        for (int i = 0; i < 8; ++i) {
            uint32_t cur = g_wev_obj[e][i].load(std::memory_order_relaxed);
            if (!cur && g_wev_obj[e][i].compare_exchange_strong(cur, obj, std::memory_order_relaxed)) cur = obj;
            if (cur == obj) {
                g_wev_n[e][i].fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        return;
    }
}
std::string watched_events() {
    std::string s;
    for (int e = 0; e < 3; ++e) {
        s += std::format(" | {:08X}:", kWatchEv[e]);
        bool any = false;
        for (int i = 0; i < 8; ++i)
            if (const uint32_t o = g_wev_obj[e][i].load(std::memory_order_relaxed)) {
                s += std::format(" obj 0x{:X} x{}", o, g_wev_n[e][i].load(std::memory_order_relaxed));
                any = true;
            }
        if (!any) s += " none";
    }
    return s;
}
// 2026-11-23 (cs98h: FE5BE4B4 spikes at each player's load-in, not at its swings; the swings post on no player's
// character object at all): every post counted by event id in 10-s windows (an open-addressed table, 512 slots), the
// top events of each window logged, to find the events that rise while one player swings and see whether they rise on
// the other game too.
struct EvSlot {
    std::atomic<uint32_t> id{0}, n{0};
};
EvSlot g_win[512];
std::atomic<uint32_t> g_win_dropped{0};
void note_window(uintptr_t ev) noexcept {
    uint32_t id = 0;
    if (!ev || !game::fast_read(ev, &id, 4) || !id) return;
    for (uint32_t i = 0, h = (id * 2654435761u) >> 23; i < 512; ++i, h = (h + 1) & 511) {
        uint32_t cur = g_win[h].id.load(std::memory_order_relaxed);
        if (!cur && g_win[h].id.compare_exchange_strong(cur, id, std::memory_order_relaxed)) cur = id;
        if (cur == id) {
            g_win[h].n.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    g_win_dropped.fetch_add(1, std::memory_order_relaxed);
}
std::string window_top_and_reset() {
    std::vector<std::pair<uint32_t, uint32_t>> v;
    for (auto& s : g_win) {
        const uint32_t n = s.n.exchange(0, std::memory_order_relaxed);
        const uint32_t id = s.id.load(std::memory_order_relaxed);
        if (id && n) v.emplace_back(n, id);
    }
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::string s = std::format(" {} kinds", v.size());
    for (size_t i = 0; i < v.size() && i < 16; ++i) s += std::format(" {:08X} x{}", v[i].second, v[i].first);
    return s;
}
uintptr_t __fastcall play_detour(uintptr_t mgr, uint32_t obj, uintptr_t emitter, uintptr_t ev, uintptr_t params) {
    const uintptr_t r = g_play_hook.call<uintptr_t>(mgr, obj, emitter, ev, params);
    note_any_object(ev, obj);
    note_window(ev);
    if (t_who >= 0 && t_kind >= 0) {
        const int at = t_kind == 0 ? 1 : 4;
        g_n[t_who][at].fetch_add(1, std::memory_order_relaxed);
        if (r & 0xFFFFFFFFull) g_n[t_who][at + 1].fetch_add(1, std::memory_order_relaxed);
    }
    if (obj) {
        const uint32_t oh = g_obj_host.load(std::memory_order_relaxed), ou = g_obj_u2.load(std::memory_order_relaxed);
        const int who = obj == oh ? 0 : obj == ou ? 1 : -1;
        if (who >= 0) {
            g_posts[who].fetch_add(1, std::memory_order_relaxed);
            if (r & 0xFFFFFFFFull) g_posts_ok[who].fetch_add(1, std::memory_order_relaxed);
            note_event(who, ev);
        }
    }
    return r;
}
std::string events(int who) {
    std::string s;
    for (int i = 0; i < 8; ++i) {
        if (const uint32_t id = g_ev[who][i].load(std::memory_order_relaxed))
            s += std::format(" {:08X} x{}", id, g_ev_n[who][i].load(std::memory_order_relaxed));
    }
    return s.empty() ? std::string(" none") : s;
}

std::string counts(int w) {
    auto n = [&](int i) { return g_n[w][i].load(std::memory_order_relaxed); };
    return std::format("voice {} (posts {}, accepted {}) | sounds {} (posts {}, accepted {})", n(0), n(1), n(2), n(3),
                       n(4), n(5));
}

DWORD WINAPI line_thread(LPVOID) {
    for (int i = 0; i < 300; ++i) {
        Sleep(2000);
        refresh_players();
        if (i % 5 != 4) continue;
        log::info("VoiceProbe: the other player 0x{:08X}: {} || own 0x{:08X}: {} || all calls: voice {} sound {} | on a "
                  "character key (0xA01...): voice {} sound {} | players: host's actor {:X}, user 2 0x{:08X} actor {:X}",
                  g_other_key.load(), counts(0), g_own_key.load(), counts(1), g_calls[0].load(), g_calls[1].load(),
                  g_char_calls[0].load(), g_char_calls[1].load(), g_actor_host.load(), g_key_u2.load(),
                  g_actor_u2.load());
        log::info("VoiceProbe posts by sound object: the host's character (object 0x{:X}) {} ({} accepted), events{} || "
                  "user 2's (object 0x{:X}) {} ({} accepted), events{}",
                  g_obj_host.load(), g_posts[0].load(), g_posts_ok[0].load(), events(0), g_obj_u2.load(),
                  g_posts[1].load(), g_posts_ok[1].load(), events(1));
        log::info("VoiceProbe attack events by object (every object):{}", watched_events());
        log::info("VoiceProbe all posts in the last 10 s by event:{}", window_top_and_reset());
    }
    return 0;
}

bool head_ok(uintptr_t rva, const uint8_t* head, size_t n) {
    uint8_t got[16]{};
    return n <= sizeof got && game::safe_read(g_base + rva, got, n) && std::memcmp(got, head, n) == 0;
}

}  // namespace

void voice_probe_start(bool guest) {
    static std::atomic<bool> started{false};
    if (started.exchange(true)) return;  // the host's install and the guest's start both call it
    log::info("VoiceProbe ({}): starting", guest ? "guest" : "host");
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_guest = guest;
    if (head_ok(kPlayEvent, kPlayHead, sizeof kPlayHead))
        g_play_hook = safetyhook::create_inline(reinterpret_cast<void*>(g_base + kPlayEvent),
                                                reinterpret_cast<void*>(&play_detour));
    if (head_ok(kVoiceProc, kVoiceHead, sizeof kVoiceHead))
        g_voice_hook = safetyhook::create_inline(reinterpret_cast<void*>(g_base + kVoiceProc),
                                                 reinterpret_cast<void*>(&voice_detour));
    if (head_ok(kSoundProc, kSoundHead, sizeof kSoundHead))
        g_sound_hook = safetyhook::create_inline(reinterpret_cast<void*>(g_base + kSoundProc),
                                                 reinterpret_cast<void*>(&sound_detour));
    log::info("VoiceProbe ({}): voice processor {}, sound processor {}, PlayEvent {} (research, counts only)",
              guest ? "guest" : "host", g_voice_hook ? "hooked" : "NOT hooked (bytes differ)",
              g_sound_hook ? "hooked" : "NOT hooked (bytes differ)", g_play_hook ? "hooked" : "NOT hooked (bytes differ)");
    if (g_voice_hook || g_sound_hook) CloseHandle(CreateThread(nullptr, 0, line_thread, nullptr, 0, nullptr));
}

}  // namespace cdmp::session2
