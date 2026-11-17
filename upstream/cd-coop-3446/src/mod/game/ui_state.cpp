#include "mod/game/ui_state.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"

namespace cdmp::game {
namespace {
namespace off = build_3663;

// 1 = checked and usable, -1 = refused, 0 = not checked yet.
int g_state = 0;
uintptr_t g_flag = 0;

bool check() {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint8_t site[7] = {};
    int32_t rel = 0;
    if (base == off::kImageBase && safe_read(base + off::kUiModePush, site, sizeof site)) {
        std::memcpy(&rel, site + 3, 4);
    }
    const bool ok = site[0] == 0x0F && site[1] == 0xB6 && site[2] == 0x05 &&
                    off::kUiModePush + 7 + rel == off::kGameplayInput;
    if (ok) {
        g_flag = base + off::kGameplayInput;
    } else {
        log::warn("UI state: menu flag not recognised on this build; name tags may show over menus");
    }
    return ok;
}

}  // namespace

bool ui_menu_open() {
    if (g_state == 0) g_state = check() ? 1 : -1;
    if (g_state < 0) return false;
    uint8_t v = 1;
    return safe_read(g_flag, &v, 1) && v == 0;
}

uintptr_t minigame_gauge_address() {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (base != off::kImageBase) return 0;
    const auto root = read_ptr(base + off::kMiniGameRoot);
    if (!root) return 0;
    const auto holder = read_ptr(*root + off::kMiniGameRoot_Holder);
    if (!holder) return 0;
    MEMORY_BASIC_INFORMATION mbi{};
    const uintptr_t a = *holder + off::kMiniGameHolder_Gauge;
    if (!VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) {
        return 0;
    }
    return a;
}

bool photo_mode_active() {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (base != off::kImageBase) return false;
    // The raw bit, held for 4 reads in a row before it counts either way (one frame's blip does not flip it).
    static bool shown = false;
    static int same = 0;
    uint8_t st = 0, chk = 0;
    if (!safe_read(base + off::kPhotoModeState, &st, 1) || !safe_read(base + off::kPhotoModeCheck, &chk, 1)) return false;
    const bool raw = st == 0x20 && !(chk & 0x80);
    if (raw == shown) {
        same = 0;
    } else if (++same >= 4) {
        shown = raw;
        same = 0;
    }
    return shown;
}

namespace {
struct Watched {
    uintptr_t off = 0;
    int last = -1;
};
std::vector<Watched> g_watch;
int g_watch_lines = 0;
}  // namespace

void dev_watch_bytes_set(const std::string& list) {
    g_watch.clear();
    size_t i = 0;
    while (i < list.size() && g_watch.size() < 64) {
        const size_t j = list.find('/', i);
        const std::string part = list.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (!part.empty()) {
            try {
                g_watch.push_back({static_cast<uintptr_t>(std::stoull(part, nullptr, 16)), -1});
            } catch (...) {
            }
        }
        if (j == std::string::npos) break;
        i = j + 1;
    }
    if (!g_watch.empty()) log::info("Dev watch_bytes: {} byte(s) watched", g_watch.size());
}

void dev_watch_bytes_tick() {
    if (g_watch.empty() || g_watch_lines > 4000) return;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    for (auto& w : g_watch) {
        uint8_t v = 0;
        if (!safe_read(base + w.off, &v, 1)) continue;
        if (w.last != v) {
            if (w.last >= 0 && ++g_watch_lines <= 4000)
                log::info("Dev watch: exe+0x{:X} {:02X} -> {:02X}", w.off, w.last, v);
            w.last = v;
        }
    }
}

std::optional<bool> cutscene_playing() {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (base != off::kImageBase) return std::nullopt;
    uint8_t v = 0;
    if (!safe_read(base + off::kCutscenePlaying, &v, 1) || v > 1) return std::nullopt;
    return v == 1;
}

}  // namespace cdmp::game
