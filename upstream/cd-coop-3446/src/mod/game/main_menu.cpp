#include "mod/game/main_menu.h"

#include <windows.h>
#include <tlhelp32.h>

#include <atomic>
#include <cstring>

#include <safetyhook.hpp>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"

namespace cdmp::game {
namespace {
namespace off = build_3576;

constexpr uintptr_t kOpenPanel = 0xF19180;        // void (root, const char* panel, void* sub, uint64 flag)
constexpr uintptr_t kRootVftable = 0x5A033D0;     // UIGamePlayControl_Root_MainMenu
constexpr uintptr_t kRoot_PageIndex = 0x1C8;      // uint32
const char* const kPanels[kMenuPageCount] = {"WorldMapView", "InventoryEquipmentPanel", "QuestMenuPanel2",
                                             "SkillTreePanel", "LogoutView"};

constexpr uintptr_t kTabChanged = 0xF1CF40;      // void (root, list, flag): the tab row's selection changed
constexpr uintptr_t kListSelect = 0xA7866B0;      // void (list, page + 0x10, 0, flag): select the item with this key
constexpr uintptr_t kRoot_Pages = 0x158;          // stride 0x30
constexpr uintptr_t kRoot_PageCount = 0x160;      // uint32
constexpr uintptr_t kRoot_TabRow = 0x1A8;         // UIGamePlayControlCommon_CircularList*
constexpr uintptr_t kPageStride = 0x30;
constexpr uintptr_t kList_Index = 0x100;          // int32
constexpr uintptr_t kList_Silent = 0x128;         // byte the game sets before selecting from code

SafetyHookInline g_hook;
SafetyHookInline g_tab_hook;
MenuTabFilter g_filter = nullptr;
std::atomic<int> g_page{-1};
int g_state = 0;  // 1 hooked, -1 refused

int page_of(const char* name) {
    char buf[32] = {};
    if (!name || !safe_read(reinterpret_cast<uintptr_t>(name), buf, sizeof buf - 1)) {
        // the name may end right before an unreadable page: read it byte by byte
        for (size_t i = 0; i + 1 < sizeof buf; ++i) {
            if (!name || !safe_read(reinterpret_cast<uintptr_t>(name) + i, &buf[i], 1) || !buf[i]) break;
        }
    }
    for (int i = 0; i < kMenuPageCount; ++i) {
        if (std::strcmp(buf, kPanels[i]) == 0) return i;
    }
    return -1;
}

// "Open the panel with this name" (shortcut keys, the game's own jumps): never stopped, only reported (from -1).
void open_panel(uintptr_t self, const char* name, void* sub, uint64_t flag) {
    const int to = page_of(name);
    if (to >= 0) {
        if (g_filter) g_filter(-1, to);
        g_page.store(to, std::memory_order_relaxed);
    }
    g_hook.call<void>(self, name, sub, flag);
}

// The tab row's "selection changed" callback (LB / RB, Q / E, a click on a name): the row (root+0x1A8, a
// CircularList, index +0x100) has already moved; this function copies its index to root+0x1C8 and opens the page.
// To stop a step we put the row back on the page the menu is on (the game's own "select by key", silent) and skip
// the function.
bool g_reverting = false;
void tab_changed(uintptr_t root, uintptr_t list, uint64_t flag) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uintptr_t vft = 0, row = 0, pages = 0;
    uint32_t cur = 0xFFFFFFFF, count = 0;
    int32_t to = -1;
    if (!g_reverting && g_filter && root && safe_read(root, &vft, sizeof vft) && vft == base + kRootVftable &&
        safe_read(root + kRoot_PageIndex, &cur, sizeof cur) && cur < kMenuPageCount &&
        safe_read(root + kRoot_PageCount, &count, sizeof count) && count == kMenuPageCount &&
        safe_read(root + kRoot_TabRow, &row, sizeof row) && row && safe_read(row + kList_Index, &to, sizeof to) &&
        to >= 0 && to < kMenuPageCount && static_cast<int>(cur) != to && safe_read(root + kRoot_Pages, &pages, sizeof pages) &&
        pages) {
        if (g_filter(static_cast<int>(cur), to)) {
            g_reverting = true;
            *reinterpret_cast<uint8_t*>(row + kList_Silent) = 1;
            reinterpret_cast<void (*)(uintptr_t, uintptr_t, uint64_t, uint64_t)>(base + kListSelect)(
                row, pages + static_cast<uintptr_t>(cur) * kPageStride + 0x10, 0, 0);
            g_reverting = false;
            return;
        }
        g_page.store(to, std::memory_order_relaxed);
    }
    g_tab_hook.call<void>(root, list, flag);
}

}  // namespace

bool main_menu_install(MenuTabFilter filter) {
    if (g_state) return g_state > 0;
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint8_t head[16] = {};
    if (base != off::kImageBase || !safe_read(base + kOpenPanel, head, sizeof head)) {
        log::warn("Main menu: tab function not recognised on this build; the Co-op tab opens with a click only");
        g_state = -1;
        return false;
    }
    char hex[3 * sizeof head + 1] = {};
    for (size_t i = 0; i < sizeof head; ++i) std::snprintf(hex + 3 * i, 4, "%02X ", head[i]);
    g_filter = filter;
    uint8_t head2[16] = {};
    safe_read(base + kTabChanged, head2, sizeof head2);
    // 1.0.0.3576: both functions start with these bytes; anything else is another build, and nothing is hooked.
    static const uint8_t kHead1[16] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41};
    static const uint8_t kHead2[16] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x55, 0x57, 0x41, 0x54, 0x41, 0x56};
    if (std::memcmp(head, kHead1, 16) != 0 || std::memcmp(head2, kHead2, 16) != 0) {
        log::warn("Main menu: tab functions differ on this build; the Co-op tab opens with a click only");
        g_state = -1;
        return false;
    }
    char hex2[3 * sizeof head2 + 1] = {};
    for (size_t i = 0; i < sizeof head2; ++i) std::snprintf(hex2 + 3 * i, 4, "%02X ", head2[i]);
    g_hook = safetyhook::create_inline(reinterpret_cast<void*>(base + kOpenPanel), reinterpret_cast<void*>(open_panel));
    g_tab_hook = safetyhook::create_inline(reinterpret_cast<void*>(base + kTabChanged), reinterpret_cast<void*>(tab_changed));
    log::info("Main menu: tab hooks {} / {} (heads {}/ {})", g_hook ? "installed" : "FAILED", g_tab_hook ? "installed" : "FAILED",
              hex, hex2);
    g_state = g_tab_hook ? 1 : -1;
    return g_state > 0;
}

// ---- [dev] write watch on the page index -------------------------------------------------------------------------
namespace {
constexpr uintptr_t kListVftable = 0x5A70548;  // UIGamePlayControlCommon_CircularList
bool g_watch_on = false;
int g_watch_state = 0;  // 1 armed, -1 failed
uintptr_t g_watch_addr = 0;
struct Hit {
    uintptr_t rip;
    uintptr_t ret[6];
    uint32_t value;
};
Hit g_hits[64];
std::atomic<unsigned> g_hit_n{0};
unsigned g_hit_logged = 0;

LONG CALLBACK watch_veh(EXCEPTION_POINTERS* e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(e->ContextRecord->Dr6 & 1)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    e->ContextRecord->Dr6 = 0;
    const unsigned n = g_hit_n.fetch_add(1);
    if (n < std::size(g_hits)) {
        Hit& h = g_hits[n];
        h.rip = e->ContextRecord->Rip;
        h.value = *reinterpret_cast<uint32_t*>(g_watch_addr);
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* sp = reinterpret_cast<const uintptr_t*>(e->ContextRecord->Rsp);
        int k = 0;
        for (int i = 0; i < 200 && k < 6; ++i) {  // values on the stack that point into the game's code
            if (sp[i] > base + 0x1000 && sp[i] < base + 0x4A00000) h.ret[k++] = sp[i];
        }
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

uintptr_t find_root() {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const uintptr_t want = base + kListVftable;
    MEMORY_BASIC_INFORMATION mbi{};
    for (uintptr_t a = 0x10000; a < 0x7FFFFFFF0000ull && VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof mbi);
         a = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || mbi.Protect != PAGE_READWRITE) continue;
        const uintptr_t lo = reinterpret_cast<uintptr_t>(mbi.BaseAddress), hi = lo + mbi.RegionSize;
        uintptr_t buf[512];
        for (uintptr_t p = lo; p + sizeof buf <= hi; p += sizeof buf) {
            if (!safe_read(p, buf, sizeof buf)) break;
            for (size_t i = 0; i < std::size(buf); ++i) {
                if (buf[i] != want) continue;
                const uintptr_t list = p + i * 8;
                int32_t count = 0;
                uintptr_t owner = 0, vft = 0;
                if (safe_read(list + 0xD0, &count, 4) && count == kMenuPageCount && safe_read(list + 0x28, &owner, 8) &&
                    owner && safe_read(owner, &vft, 8) && vft == base + kRootVftable) {
                    return owner;
                }
            }
        }
    }
    return 0;
}
}  // namespace

void main_menu_dev_watch(bool on) { g_watch_on = on; }

void main_menu_dev_watch_tick() {
    if (!g_watch_on) return;
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!g_watch_state) {
        const uintptr_t root = find_root();
        if (!root) return;  // not built yet: try again next frame
        g_watch_addr = root + kRoot_PageIndex;
        AddVectoredExceptionHandler(1, watch_veh);
        int set = 0;
        const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 te{sizeof te};
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
            const HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
            if (!th) continue;
            if (SuspendThread(th) != static_cast<DWORD>(-1)) {
                CONTEXT c{};
                c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (GetThreadContext(th, &c)) {
                    c.Dr0 = g_watch_addr;
                    c.Dr7 = (c.Dr7 & ~0xF0003ull) | 1 | (1ull << 16) | (3ull << 18);  // L0, write, 4 bytes
                    if (SetThreadContext(th, &c)) ++set;
                }
                ResumeThread(th);
            }
            CloseHandle(th);
        }
        CloseHandle(snap);
        g_watch_state = 1;
        log::info("Main menu: [dev] write watch on root 0x{:X} +0x1C8, {} threads armed", root, set);
    }
    const unsigned n = std::min<unsigned>(g_hit_n.load(), static_cast<unsigned>(std::size(g_hits)));
    for (; g_hit_logged < n; ++g_hit_logged) {
        const Hit& h = g_hits[g_hit_logged];
        log::info("Main menu: [dev] page index written ({}) after exe+0x{:X}; stack: exe+0x{:X} 0x{:X} 0x{:X} 0x{:X} 0x{:X} 0x{:X}",
                  h.value, h.rip - base, h.ret[0] ? h.ret[0] - base : 0, h.ret[1] ? h.ret[1] - base : 0,
                  h.ret[2] ? h.ret[2] - base : 0, h.ret[3] ? h.ret[3] - base : 0, h.ret[4] ? h.ret[4] - base : 0,
                  h.ret[5] ? h.ret[5] - base : 0);
    }
}

int main_menu_page() { return g_page.load(std::memory_order_relaxed); }

}  // namespace cdmp::game
