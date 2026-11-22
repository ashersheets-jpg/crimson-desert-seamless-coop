#include "mod/session2/voice_probe.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>

#include <safetyhook.hpp>

#include "mod/game/safe_memory.h"
#include "mod/log.h"

namespace cdmp::session2 {
namespace {

// 1.0.0.3732 (build/port3732 fmap from the 3576 / 3663 addresses; heads read in the 3732 exe).
constexpr uintptr_t kVoiceProc = 0x7C72A0, kSoundProc = 0x7C2C60, kPlayEvent = 0x3BD5820;
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
thread_local int t_who = -1;
thread_local int t_kind = -1;  // 0 voice, 1 sound

int who_of(uintptr_t ctx) noexcept {
    uintptr_t actor = 0;
    uint32_t key = 0;
    if (!ctx || !game::fast_read(ctx, &actor, sizeof actor) || !actor ||
        !game::fast_read(actor + kActorKey, &key, sizeof key) || (key >> 28) != 0xA) {
        return -1;
    }
    const bool other = g_guest ? key == kHostCharacter : key != kHostCharacter;
    (other ? g_other_key : g_own_key).store(key, std::memory_order_relaxed);
    return other ? 0 : 1;
}

uintptr_t run(SafetyHookInline& hook, int kind, uintptr_t self, uintptr_t p1, uintptr_t p2, uintptr_t p3,
              uintptr_t p4) {
    const int w = who_of(p4);
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
uintptr_t __fastcall play_detour(uintptr_t mgr, uint32_t obj, uintptr_t emitter, uintptr_t ev, uintptr_t params) {
    const uintptr_t r = g_play_hook.call<uintptr_t>(mgr, obj, emitter, ev, params);
    if (t_who >= 0 && t_kind >= 0) {
        const int at = t_kind == 0 ? 1 : 4;
        g_n[t_who][at].fetch_add(1, std::memory_order_relaxed);
        if (r & 0xFFFFFFFFull) g_n[t_who][at + 1].fetch_add(1, std::memory_order_relaxed);
    }
    return r;
}

std::string counts(int w) {
    auto n = [&](int i) { return g_n[w][i].load(std::memory_order_relaxed); };
    return std::format("voice {} (posts {}, accepted {}) | sounds {} (posts {}, accepted {})", n(0), n(1), n(2), n(3),
                       n(4), n(5));
}

DWORD WINAPI line_thread(LPVOID) {
    for (int i = 0; i < 60; ++i) {
        Sleep(10000);
        log::info("VoiceProbe: the other player 0x{:08X}: {} || own 0x{:08X}: {}", g_other_key.load(), counts(0),
                  g_own_key.load(), counts(1));
    }
    return 0;
}

bool head_ok(uintptr_t rva, const uint8_t* head, size_t n) {
    uint8_t got[16]{};
    return n <= sizeof got && game::safe_read(g_base + rva, got, n) && std::memcmp(got, head, n) == 0;
}

}  // namespace

void voice_probe_start(bool guest) {
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
