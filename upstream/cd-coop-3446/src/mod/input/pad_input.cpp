#include "mod/input/pad_input.h"
#include "mod/input/game_input_block.h"

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <xinput.h>

#include <safetyhook.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mod/log.h"

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace cdmp::input {
namespace {

constexpr uint32_t kChordTapMs = 300;  // View released this fast without a chord: it was meant for the game
constexpr uint32_t kTapInjectMs = 90;
constexpr int kStickMove = 12000;      // a stick pushed this far counts as using the pad

std::atomic<Method> g_method{Method::Keyboard};
std::atomic<uint16_t> g_xinput{0};     // real XInput buttons (any pad)
std::atomic<uint16_t> g_sony{0};       // PlayStation pads' buttons
std::atomic<bool> g_xinput_present{false};  // an XInput pad answers (connected_pad)
std::atomic<int> g_sony_present{0};         // PlayStation pad readers running
std::atomic<uint16_t> g_sim{0};        // [dev] simulated presses
std::atomic<uint64_t> g_sim_until{0};
std::atomic<int> g_sim_family{0};
std::atomic<uint16_t> g_mask{0};
std::atomic<uint64_t> g_mask_until{0};
std::atomic<uint64_t> g_back_tap_until{0};  // re-give a View tap to the game

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
XInputGetStateFn g_real_get_state = nullptr;
SafetyHookInline g_hook_get_state;
SafetyHookInline g_hook_get_state_ex;

uint64_t now64() { return GetTickCount64(); }
std::atomic<bool> g_chord_modifier{true};  // set_chord_modifier

// What the game gets from XInput: our masked buttons removed; a View tap that was really a tap given back.
void filter(XINPUT_STATE* st) {
    if (!st) return;
    const uint64_t t = now64();
    WORD b = st->Gamepad.wButtons;
    if (t < g_mask_until.load()) b &= static_cast<WORD>(~g_mask.load());
    if (g_chord_modifier.load()) {
        if ((b & XINPUT_GAMEPAD_BACK) && t >= g_back_tap_until.load()) b &= static_cast<WORD>(~XINPUT_GAMEPAD_BACK);
        if (t < g_back_tap_until.load()) b |= XINPUT_GAMEPAD_BACK;
    }
    if (b != st->Gamepad.wButtons) {
        st->Gamepad.wButtons = b;
        ++st->dwPacketNumber;
    }
}

// [dev] pad_sim=1: the game gets a pad too (user index 0), with the dev script's presses, so a one-PC test without a
// controller sees the game's own pad screens (its LB/RB tab switching, its button icons).
DWORD sim_feed(DWORD i, XINPUT_STATE* st, DWORD r) {
    if (g_sim_family.load() != 1 || i != 0 || !st) return r;
    static std::atomic<uint32_t> packet{1};
    static std::atomic<uint16_t> last{0};
    if (r != ERROR_SUCCESS) *st = XINPUT_STATE{};
    const uint16_t sim = now64() < g_sim_until.load() ? static_cast<uint16_t>(g_sim.load() & 0xF3FF) : uint16_t{0};
    if (last.exchange(sim) != sim) ++packet;
    st->Gamepad.wButtons |= sim;
    if (r != ERROR_SUCCESS) st->dwPacketNumber = packet.load();
    else st->dwPacketNumber += packet.load();
    return ERROR_SUCCESS;
}

DWORD WINAPI hooked_get_state(DWORD i, XINPUT_STATE* st) {
    const DWORD r = sim_feed(i, st, g_hook_get_state.stdcall<DWORD>(i, st));
    if (r == ERROR_SUCCESS) filter(st);
    return r;
}

DWORD WINAPI hooked_get_state_ex(DWORD i, XINPUT_STATE* st) {
    const DWORD r = sim_feed(i, st, g_hook_get_state_ex.stdcall<DWORD>(i, st));
    if (r == ERROR_SUCCESS) filter(st);
    return r;
}

void install_xinput_hook() {
    HMODULE x = GetModuleHandleW(L"xinput1_4.dll");
    if (!x) x = LoadLibraryW(L"xinput1_4.dll");
    if (!x) {
        log::warn("Input: xinput1_4.dll not available; no Xbox pad support");
        return;
    }
    auto get_state = reinterpret_cast<void*>(GetProcAddress(x, "XInputGetState"));
    auto get_state_ex = reinterpret_cast<void*>(GetProcAddress(x, reinterpret_cast<LPCSTR>(100)));  // with Guide
    if (get_state) {
        g_hook_get_state = safetyhook::create_inline(get_state, reinterpret_cast<void*>(hooked_get_state));
        g_real_get_state = g_hook_get_state ? g_hook_get_state.original<XInputGetStateFn>()
                                            : reinterpret_cast<XInputGetStateFn>(get_state);
    }
    if (get_state_ex) {
        g_hook_get_state_ex = safetyhook::create_inline(get_state_ex, reinterpret_cast<void*>(hooked_get_state_ex));
    }
    log::info("Input: XInput {} (the game's View button is ours while held as the co-op chord)",
              g_hook_get_state ? "hooked" : "read only");
}

// ---------------------------------------------------------------- PlayStation pads (HID)

struct SonyPad {
    HANDLE file = INVALID_HANDLE_VALUE;
    bool dualsense = false;
    std::wstring path;
};

uint16_t hat_bits(uint8_t hat) {
    switch (hat & 0x0F) {
        case 0: return kPadUp;
        case 1: return kPadUp | kPadRight;
        case 2: return kPadRight;
        case 3: return kPadDown | kPadRight;
        case 4: return kPadDown;
        case 5: return kPadDown | kPadLeft;
        case 6: return kPadLeft;
        case 7: return kPadUp | kPadLeft;
        default: return 0;
    }
}

// b0: hat (low nibble) | square 0x10 | cross 0x20 | circle 0x40 | triangle 0x80; b1: L1 R1 L2 R2 Create/Share
// Options L3 R3; b2: PS 0x01 | touchpad 0x02 (DualShock 4 and DualSense share this layout).
uint16_t sony_buttons(uint8_t b0, uint8_t b1, uint8_t b2) {
    uint16_t r = hat_bits(b0);
    if (b0 & 0x10) r |= kPadX;
    if (b0 & 0x20) r |= kPadA;
    if (b0 & 0x40) r |= kPadB;
    if (b0 & 0x80) r |= kPadY;
    if (b1 & 0x01) r |= kPadLB;
    if (b1 & 0x02) r |= kPadRB;
    if (b1 & 0x08) r |= kPadRT;
    if (b1 & 0x10) r |= kPadBack;
    if (b1 & 0x20) r |= kPadStart;
    if (b1 & 0x40) r |= kPadL3;
    if (b1 & 0x80) r |= kPadR3;
    if (b2 & 0x02) r |= kPadTouchpad;
    return r;
}

// One input report -> buttons (nullopt-like 0xFFFF when not a layout we know). Offsets of the first button byte:
// DualSense USB 0x01 = 8, DualSense Bluetooth 0x31 = 9, DualShock 4 USB 0x01 = 5, DualShock 4 Bluetooth 0x11 = 7,
// both pads' basic Bluetooth report 0x01 (10 bytes) = 5.
uint32_t parse_report(const uint8_t* r, size_t n, bool dualsense, int& sticks) {
    size_t at = 0, sx = 0;
    if (r[0] == 0x01 && dualsense && n >= 11) at = 8, sx = 1;
    else if (r[0] == 0x31 && n >= 12) at = 9, sx = 2;
    else if (r[0] == 0x01 && n >= 8) at = 5, sx = 1;
    else if (r[0] == 0x11 && n >= 10) at = 7, sx = 3;
    else return 0xFFFFFFFF;
    sticks = 0;
    for (size_t k = 0; k < 4; ++k) sticks = std::max(sticks, std::abs(static_cast<int>(r[sx + k]) - 128));
    return sony_buttons(r[at], r[at + 1], at + 2 < n ? r[at + 2] : 0);
}

std::vector<SonyPad> find_sony_pads() {
    std::vector<SonyPad> out;
    GUID hid;
    HidD_GetHidGuid(&hid);
    HDEVINFO set = SetupDiGetClassDevsW(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return out;
    SP_DEVICE_INTERFACE_DATA ifd{sizeof ifd};
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hid, i, &ifd); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
        std::vector<uint8_t> buf(need);
        auto* det = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, nullptr, nullptr)) continue;
        HANDLE f = CreateFileW(det->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (f == INVALID_HANDLE_VALUE) {
            f = CreateFileW(det->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED, nullptr);
        }
        if (f == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES a{sizeof a};
        const bool sony = HidD_GetAttributes(f, &a) && a.VendorID == 0x054C &&
                          (a.ProductID == 0x05C4 || a.ProductID == 0x09CC || a.ProductID == 0x0BA0 ||
                           a.ProductID == 0x0CE6 || a.ProductID == 0x0DF2);
        if (!sony) {
            CloseHandle(f);
            continue;
        }
        SonyPad p;
        p.file = f;
        p.dualsense = a.ProductID == 0x0CE6 || a.ProductID == 0x0DF2;
        p.path = det->DevicePath;
        out.push_back(std::move(p));
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

// One reader per pad: overlapped reads with a timeout; a pad that goes away ends its reader.
void sony_reader(SonyPad pad) {
    log::info("Input: PlayStation pad ({}) found", pad.dualsense ? "DualSense" : "DualShock 4");
    std::vector<uint8_t> buf(128);
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    uint16_t last = 0;
    for (;;) {
        ResetEvent(ov.hEvent);
        DWORD got = 0;
        if (!ReadFile(pad.file, buf.data(), static_cast<DWORD>(buf.size()), nullptr, &ov) &&
            GetLastError() != ERROR_IO_PENDING) {
            break;
        }
        g_sony_present = 1;
        if (WaitForSingleObject(ov.hEvent, 2000) != WAIT_OBJECT_0) {
            CancelIo(pad.file);
            continue;
        }
        if (!GetOverlappedResult(pad.file, &ov, &got, FALSE) || got == 0) break;
        int sticks = 0;
        const uint32_t b = parse_report(buf.data(), got, pad.dualsense, sticks);
        if (b == 0xFFFFFFFF) continue;
        const uint16_t bt = static_cast<uint16_t>(b);
        g_sony = bt;
        if (bt != last || sticks > 40) g_method = Method::PlayStation;
        last = bt;
    }
    g_sony = 0;
    g_sony_present = 0;
    CloseHandle(ov.hEvent);
    CloseHandle(pad.file);
    log::info("Input: PlayStation pad gone");
}

// ---------------------------------------------------------------- polling (XInput, keyboard, mouse)

void poll_loop() {
    std::vector<std::wstring> sony_paths;
    uint64_t last_scan = 0;
    uint16_t last_x = 0;
    POINT last_cursor{};
    GetCursorPos(&last_cursor);
    bool chord_used = false, back_was = false;
    uint64_t back_down_at = 0;
    for (;;) {
        Sleep(15);
        const uint64_t t = now64();
        // XInput, all four slots (through the real function: our filter must not hide anything from us).
        uint16_t xb = 0;
        int stick = 0;
        bool present = false;
        if (g_real_get_state) {
            for (DWORD i = 0; i < 4; ++i) {
                XINPUT_STATE st{};
                if (g_real_get_state(i, &st) != ERROR_SUCCESS) continue;
                present = true;
                xb |= st.Gamepad.wButtons;
                if (st.Gamepad.bRightTrigger > 100) xb |= kPadRT;
                stick = std::max({stick, std::abs(static_cast<int>(st.Gamepad.sThumbLX)),
                                  std::abs(static_cast<int>(st.Gamepad.sThumbLY)),
                                  std::abs(static_cast<int>(st.Gamepad.sThumbRX)),
                                  std::abs(static_cast<int>(st.Gamepad.sThumbRY))});
            }
        }
        g_xinput = xb;
        g_xinput_present = present;
        // A Sony pad seen through Steam Input shows up here too: its HID reports decide the icons then.
        if (((xb != last_x && xb) || stick > kStickMove) && g_sony.load() == 0) g_method = Method::Xbox;
        last_x = xb;
        // Keyboard and mouse.
        POINT c{};
        GetCursorPos(&c);
        bool kb = std::abs(c.x - last_cursor.x) + std::abs(c.y - last_cursor.y) > 6;
        last_cursor = c;
        for (int vk = 0x01; vk <= 0xFE && !kb; ++vk) {
            if (vk == VK_PACKET) continue;
            if (GetAsyncKeyState(vk) & 0x8000) kb = true;
        }
        if (kb && GetForegroundWindow() && g_sim_family.load() == 0) g_method = Method::Keyboard;
        // PlayStation pads: look for new ones every 3 s.
        if (t - last_scan > 3000) {
            last_scan = t;
            for (auto& p : find_sony_pads()) {
                bool known = false;
                for (const auto& s : sony_paths) known |= s == p.path;
                if (known) {
                    CloseHandle(p.file);
                    continue;
                }
                sony_paths.push_back(p.path);
                std::thread([p] { sony_reader(p); }).detach();
            }
            if (g_sony.load() == 0 && sony_paths.size() > 4) sony_paths.clear();  // stale entries
        }
        // The chord: View held = ours; a quick tap without a chord is given back to the game.
        const uint16_t all = pad_buttons();
        const bool back = g_chord_modifier.load() && (all & (kPadBack | kPadTouchpad)) != 0;
        if (back && !back_was) {
            back_down_at = t;
            chord_used = false;
        }
        if (back && (all & (kPadFace | kPadDpad | kPadLB | kPadRB | kPadR3 | kPadL3))) chord_used = true;
        if (!back && back_was && !chord_used && t - back_down_at < kChordTapMs) g_back_tap_until = t + kTapInjectMs;
        if (back) mask_from_game(kPadFace | kPadDpad | kPadLB | kPadRB | kPadR3 | kPadL3, 120);
        back_was = back;
        if (int f = g_sim_family.load()) g_method = f == 2 ? Method::PlayStation : Method::Xbox;
    }
}

}  // namespace

void input_start() {
    static std::once_flag once;
    std::call_once(once, [] {
        install_xinput_hook();
        game_input_block_install();  // Co-op+: our screens that stand in for the game's (render/coop_page.cpp)
        std::thread(poll_loop).detach();
    });
}

Method method() { return g_method.load(); }

Method connected_pad() {
    if (const int f = g_sim_family.load()) return f == 2 ? Method::PlayStation : Method::Xbox;
    if (g_sony_present.load()) return Method::PlayStation;
    return g_xinput_present.load() ? Method::Xbox : Method::Keyboard;
}

uint16_t pad_buttons() {
    uint16_t b = g_xinput.load() | g_sony.load();
    if (now64() < g_sim_until.load()) b |= g_sim.load();
    return b;
}

bool chord_held() { return g_chord_modifier.load() && (pad_buttons() & (kPadBack | kPadTouchpad)) != 0; }
void set_chord_modifier(bool on) { g_chord_modifier = on; }

void mask_from_game(uint16_t buttons, uint32_t ms) {
    const uint64_t until = now64() + ms;
    if (until > g_mask_until.load() || (g_mask.load() | buttons) != g_mask.load()) {
        g_mask = (now64() < g_mask_until.load() ? g_mask.load() : 0) | buttons;
        g_mask_until = until;
    }
}

void set_sim(int family) {
    g_sim_family = family;
    if (family) log::info("Input: [dev] pad_sim: acting as a {} pad", family == 2 ? "PlayStation" : "Xbox");
}

void dev_pad_press(uint16_t buttons, uint32_t ms) {
    g_sim = buttons;
    g_sim_until = now64() + ms;
}

}  // namespace cdmp::input
