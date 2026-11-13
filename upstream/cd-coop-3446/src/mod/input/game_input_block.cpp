#include "mod/input/game_input_block.h"

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <mutex>

#include <safetyhook.hpp>

#include "mod/log.h"

namespace cdmp::input {
namespace {

SafetyHookInline g_fg, g_active;
std::atomic<bool> g_blocked{false}, g_downed{false}, g_pads{false};
std::atomic<unsigned> g_mode{7};
uintptr_t g_exe_lo = 0, g_exe_hi = 0;

bool from_game(void* ret) {
    const auto a = reinterpret_cast<uintptr_t>(ret);
    return a >= g_exe_lo && a < g_exe_hi;
}

bool held() { return g_blocked.load(std::memory_order_relaxed) || g_downed.load(std::memory_order_relaxed); }

HWND WINAPI hooked_foreground() {
    if (held() && (g_mode.load(std::memory_order_relaxed) & 1) && from_game(_ReturnAddress())) return nullptr;
    return g_fg.stdcall<HWND>();
}

HWND WINAPI hooked_active() {
    if (held() && (g_mode.load(std::memory_order_relaxed) & 1) && from_game(_ReturnAddress())) return nullptr;
    return g_active.stdcall<HWND>();
}

}  // namespace

bool game_input_block_install() {
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] {
        const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(exe + dos->e_lfanew);
        g_exe_lo = exe;
        g_exe_hi = exe + nt->OptionalHeader.SizeOfImage;
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        auto* fg = user32 ? reinterpret_cast<void*>(GetProcAddress(user32, "GetForegroundWindow")) : nullptr;
        auto* active = user32 ? reinterpret_cast<void*>(GetProcAddress(user32, "GetActiveWindow")) : nullptr;
        if (fg) g_fg = safetyhook::create_inline(fg, reinterpret_cast<void*>(hooked_foreground));
        if (active) g_active = safetyhook::create_inline(active, reinterpret_cast<void*>(hooked_active));
        ok = static_cast<bool>(g_fg) && static_cast<bool>(g_active);
        log::info("Input: the game's input can be held while a Co-op+ screen stands in for its own ({})",
                  ok ? "hooked" : "NOT hooked: the title may react under our menu");
    });
    return ok;
}

void set_game_input_blocked(bool on) {
    if (g_blocked.exchange(on) != on) log::info("Input: the game's own input {}", on ? "held (a Co-op+ screen)" : "back");
}

bool game_input_blocked() { return g_blocked.load(std::memory_order_relaxed); }

void set_game_input_downed(bool on) {
    if (g_downed.exchange(on) != on) log::info("Input: the game's own input {}", on ? "held (we are down)" : "back (up again)");
}

bool hold_no_button_mask() { return (g_mode.load(std::memory_order_relaxed) & 8) != 0; }

bool pads_held() { return (held() || g_pads.load(std::memory_order_relaxed)) && (g_mode.load(std::memory_order_relaxed) & 4); }

void set_game_pads_blocked(bool on) { g_pads.store(on, std::memory_order_relaxed); }

void set_hold_mode(unsigned bits) {
    g_mode = bits & 15;
    if ((bits & 15) != 7) log::info("Input: [dev] title_hold_mode {} (1 focus answers, 2 inactive messages, 4 idle XInput pad, 8 no button mask)", bits & 15);
}

void game_input_block_tick(void* game_window) {
    static bool told_inactive = false;
    static uint64_t told_at = 0;
    static uint32_t sent = 0;
    const HWND w = static_cast<HWND>(game_window);
    if (!w || !IsWindow(w)) return;
    const bool want = held() && (g_mode.load(std::memory_order_relaxed) & 2);
    const uint64_t now = GetTickCount64();
    if (want) {
        if (!told_inactive || now - told_at >= 400) {
            PostMessageW(w, WM_ACTIVATE, WA_INACTIVE, 0);
            PostMessageW(w, WM_KILLFOCUS, 0, 0);
            if (!told_inactive) log::info("Input: the game is told its window is inactive while its input is held");
            told_inactive = true;
            told_at = now;
            ++sent;
        }
    } else if (told_inactive) {
        told_inactive = false;
        // Only a game that really is in front is told so: one in the background hears it from Windows when it comes back.
        const bool front = g_fg ? g_fg.stdcall<HWND>() == w : GetForegroundWindow() == w;
        if (front) {
            PostMessageW(w, WM_ACTIVATE, WA_ACTIVE, 0);
            PostMessageW(w, WM_SETFOCUS, 0, 0);
        }
        log::info("Input: the game is told its window is active again ({} inactive notices sent{})", sent,
                  front ? "" : "; it is in the background, Windows tells it");
        sent = 0;
    }
}

}  // namespace cdmp::input
