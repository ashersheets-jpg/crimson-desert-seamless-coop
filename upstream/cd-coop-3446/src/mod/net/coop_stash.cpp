#include "mod/net/coop_stash.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <optional>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/net/coop_activity.h"
#include "mod/net/coop_chat.h"
#include "mod/net/local_capture.h"
#include "mod/net/remote_puppet.h"
#include "mod/net/troc_sniffer.h"
#include "mod/game/info_row.h"

namespace cdmp::net {
namespace {

namespace off = game::build_3663;

constexpr uint8_t kStashType = 29;  // coop_activity's kStash
constexpr uint8_t kOpAsk = 1, kOpPage = 2;
constexpr size_t kPerPage = 100;
constexpr uint16_t kOpMoveItem = 3104;
constexpr uint32_t kWithdrawEntry = 0xC0DE0003;  // the native's stash withdraw (upstream/cdmp-native trade_probe)
constexpr uint32_t kHostCharacter = 0xA0100001;
constexpr uint32_t kAskEveryMs = 4000;

std::mutex g_mu;
// Host: the guest's last ask, not answered yet.
uint32_t g_ask_pending = 0, g_ask_answered = 0;
// Guest: the list, put together from the host's pages.
std::vector<StashItem> g_list, g_building;
uint32_t g_build_id = 0;
uint8_t g_build_seen = 0;
bool g_known = false;
uint64_t g_list_at = 0;
// Guest: when the page was last on screen, when we last asked.
std::atomic<uint64_t> g_page_seen{0};
std::atomic<uint64_t> g_page_role_seen{0};
std::atomic<bool> g_is_host{false};
uint64_t g_asked_at = 0;
bool g_force_ask = false;  // under g_mu
uint32_t g_ask_id = 0;

uint64_t now64() { return GetTickCount64(); }

// The static inventory manager (client side): the WareHouse index at +0xB2.
bool warehouse_index(uint16_t& wh) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    // The game's own chain (exe+0xEC03A0): *(*(*(*[exe+0x71C04A0]) + 8) + 0x240) + 0xB2.
    auto slot = game::read_ptr(base + 0x7245560);
    if (!slot || !*slot) return false;
    auto root = game::read_ptr(*slot);
    if (!root || !*root) return false;
    auto info = game::read_ptr(*root + 8);
    if (!info || !*info) return false;
    auto mgr = game::read_ptr(*info + 0x220);  // 3663 (3576 +0x240)
    if (!mgr || !*mgr) return false;
    return game::safe_read(*mgr + 0x8A, &wh, sizeof wh) && wh != 0xFFFF;  // 3663 (3576 +0xB2)
}

uint16_t inventory_key(uint16_t index) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        const uintptr_t row = game::info_row(base + 0x57B410, index);
        return row ? *reinterpret_cast<uint16_t*>(row + 8) : 0xFFFF;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xFFFF;
    }
}

// The character this game controls (the player reader's chain).
std::optional<uintptr_t> own_actor() {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto world = game::read_ptr(base + off::kWorldSystemSlot);
    if (!world || !*world) return std::nullopt;
    auto manager = game::read_ptr(*world + off::kWorldSystem_ActorManager);
    if (!manager || !*manager) return std::nullopt;
    auto actor = game::read_ptr(*manager + off::kActorManager_ControlledActor);
    if (!actor || !*actor) return std::nullopt;
    uintptr_t vt = 0;
    if (!game::safe_read(*actor, &vt, sizeof vt) || vt != base + off::kVtControlledActor) return std::nullopt;
    return actor;
}

// Our own character's WareHouse, read from this game's client (non-empty slots only).
bool read_own_warehouse(std::vector<StashItem>& out) {
    uint16_t wh = 0xFFFF;
    if (!warehouse_index(wh)) return false;
    auto actor = own_actor();
    if (!actor) return false;
    auto comps = game::read_ptr(*actor + 0x68);
    if (!comps || !*comps) return false;
    auto holder = game::read_ptr(*comps + 0xB8);
    if (!holder || !*holder) return false;
    auto list = game::read_ptr(*holder + 0x18);
    uint32_t n = 0;
    if (!list || !*list || !game::safe_read(*holder + 0x20, &n, sizeof n) || n > 64) return false;
    for (uint32_t i = 0; i < n; ++i) {
        auto inv = game::read_ptr(*list + i * 8ull);
        if (!inv || !*inv) continue;
        int16_t index = -1, slots = 0;
        if (!game::safe_read(*inv + 0x10, &index, 2) || index != static_cast<int16_t>(wh)) continue;
        auto first = game::read_ptr(*inv);
        if (!first || !*first || !game::safe_read(*inv + 8, &slots, 2) || slots < 0 || slots > 2000) return false;
        for (int16_t s = 0; s < slots; ++s) {
            uint8_t rec[0x18] = {};
            if (!game::safe_read(*first + static_cast<uintptr_t>(s) * 200, rec, sizeof rec)) break;
            uint16_t idx = 0;
            int64_t cnt = 0;
            std::memcpy(&idx, rec + 8, 2);
            std::memcpy(&cnt, rec + 0x10, 8);
            if (idx == 0xFFFF || cnt < 1) continue;
            out.push_back({static_cast<uint16_t>(s), idx, static_cast<uint32_t>(std::min<int64_t>(cnt, 0xFFFFFFFF))});
        }
        return true;
    }
    return false;
}

// The host's server reads its WareHouse (native entry 0xC0DE0004, a 3111 our own client sends it); the result comes
// back through cdmp.asi's cdmp_stash_list. Our client's own copy is the fallback (empty until a keeper was visited).
constexpr uint32_t kListEntry = 0xC0DE0004;
using StashListFn = int (*)(uint32_t*, uint16_t*, uint16_t*, uint32_t*, int);
using StashSlotsFn = int (*)();
int native_slots() {
    static StashSlotsFn fn = nullptr;
    if (!fn) {
        if (HMODULE m = GetModuleHandleW(L"cdmp.asi")) fn = reinterpret_cast<StashSlotsFn>(GetProcAddress(m, "cdmp_stash_slots"));
    }
    return fn ? fn() : -1;
}
std::atomic<int> g_guest_slots{-1};  // the guest: the host's slots as its last page said
StashListFn native_list() {
    static StashListFn fn = nullptr;
    if (!fn) {
        if (HMODULE m = GetModuleHandleW(L"cdmp.asi")) fn = reinterpret_cast<StashListFn>(GetProcAddress(m, "cdmp_stash_list"));
    }
    return fn;
}
uint32_t native_gen() {
    uint32_t gen = 0;
    if (auto fn = native_list()) fn(&gen, nullptr, nullptr, nullptr, 0);
    return gen;
}
bool ask_own_server() {
    std::vector<uint8_t> pkt(5 + 30);
    const uint16_t len = 30, none = 0xFFFF;
    const uint32_t item = 0, who = kHostCharacter, entry = kListEntry;
    const uint64_t n = 1;
    std::memcpy(&pkt[0], &kOpMoveItem, 2);
    std::memcpy(&pkt[3], &len, 2);
    uint8_t* p = &pkt[5];
    std::memcpy(p + 0, &item, 4);
    std::memcpy(p + 4, &none, 2);
    std::memcpy(p + 6, &n, 8);
    std::memcpy(p + 14, &who, 4);
    std::memcpy(p + 18, &who, 4);
    std::memcpy(p + 22, &none, 2);
    std::memcpy(p + 24, &none, 2);
    std::memcpy(p + 26, &entry, 4);
    return sniffer_inject_c2s(std::move(pkt));
}

void send_list() {
    std::vector<StashItem> items;
    bool ok = false;
    if (auto fn = native_list()) {
        static uint16_t sl[512], ix[512];
        static uint32_t ct[512];
        uint32_t gen = 0;
        const int n = fn(&gen, sl, ix, ct, 512);
        if (n >= 0) {
            ok = true;
            for (int i = 0; i < n; ++i) items.push_back({sl[i], ix[i], ct[i]});
        }
    }
    if (!ok) ok = read_own_warehouse(items);
    static uint32_t list_id = 0;
    ++list_id;
    const size_t pages = std::max<size_t>(1, (items.size() + kPerPage - 1) / kPerPage);
    for (size_t pg = 0; pg < pages && pg < 255; ++pg) {
        const size_t from = pg * kPerPage, to = std::min(items.size(), from + kPerPage);
        std::vector<uint8_t> b{kOpPage};
        b.resize(1 + 4 + 3 + 2);
        std::memcpy(&b[1], &list_id, 4);
        b[5] = static_cast<uint8_t>(pg);
        b[6] = static_cast<uint8_t>(pages);
        b[7] = static_cast<uint8_t>(to - from);
        const int16_t slots = static_cast<int16_t>(native_slots());
        std::memcpy(&b[8], &slots, 2);
        for (size_t i = from; i < to; ++i) {
            uint8_t e[8];
            std::memcpy(e, &items[i].slot, 2);
            std::memcpy(e + 2, &items[i].idx, 2);
            std::memcpy(e + 4, &items[i].count, 4);
            b.insert(b.end(), e, e + 8);
        }
        activity_send(kStashType, b);
    }
    static uint32_t logged = 0;
    if (logged++ < 8)
        log::info("Stash: our warehouse to the guest: {} stack(s){}", items.size(), ok ? "" : " (not readable now)");
}

}  // namespace

void stash_on_event(const uint8_t* p, size_t n) {
    if (n < 1) return;
    std::lock_guard lk(g_mu);
    if (p[0] == kOpAsk && n >= 5) {
        std::memcpy(&g_ask_pending, p + 1, 4);
        if (!g_ask_pending) g_ask_pending = 1;
        return;
    }
    if (p[0] != kOpPage || n < 10) return;
    uint32_t id = 0;
    std::memcpy(&id, p + 1, 4);
    const uint8_t page = p[5], pages = p[6], count = p[7];
    int16_t slots = -1;
    std::memcpy(&slots, p + 8, 2);
    g_guest_slots = slots;
    p += 2;  // the entries follow the slots
    n -= 2;
    if (n < 8 + count * 8ull || pages == 0 || page >= pages) return;
    if (id != g_build_id) {
        g_build_id = id;
        g_building.clear();
        g_build_seen = 0;
    }
    for (uint8_t i = 0; i < count; ++i) {
        StashItem it;
        std::memcpy(&it.slot, p + 8 + i * 8, 2);
        std::memcpy(&it.idx, p + 8 + i * 8 + 2, 2);
        std::memcpy(&it.count, p + 8 + i * 8 + 4, 4);
        g_building.push_back(it);
    }
    if (++g_build_seen >= pages) {
        g_list = g_building;
        std::sort(g_list.begin(), g_list.end(), [](const StashItem& a, const StashItem& b) { return a.slot < b.slot; });
        g_list.erase(std::unique(g_list.begin(), g_list.end(),
                                 [](const StashItem& a, const StashItem& b) { return a.slot == b.slot; }),
                     g_list.end());
        g_known = true;
        g_list_at = now64();
        g_build_id = 0;
    }
}

// Lootr chests (native lootr.h): a share that waits for a player who was not at the chest. The host's native queues
// the event (export cdmp_chest_events); the player it waits for gets a notice and a pin on the map.
void chest_notices() {
    using EventsFn = int (*)(uint8_t*, int);
    static EventsFn fn = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        if (HMODULE m = GetModuleHandleW(L"cdmp.asi")) fn = reinterpret_cast<EventsFn>(GetProcAddress(m, "cdmp_chest_events"));
    }
    if (!fn) return;
    uint8_t ev[20 * 8] = {};
    const int n = fn(ev, 8);
    const uint32_t own = capture_own_player_key();
    for (int i = 0; i < n; ++i) {
        const uint8_t* e = ev + 20 * i;
        uint32_t player = 0;
        proto::Vec3 at{};
        std::memcpy(&player, e + 4, 4);
        std::memcpy(&at.x, e + 8, 4);
        std::memcpy(&at.y, e + 12, 4);
        std::memcpy(&at.z, e + 16, 4);
        if (e[0] != 1) continue;  // 2 = fetched: the game's own "acquired" lines say it
        const bool ours = player == own && own != 0;
        log::info("Chest: a share waits for {} at ({:.0f}, {:.0f}, {:.0f})", ours ? "us" : "the friend", at.x, at.y, at.z);
        if (ours) {
            chat_notice_local("Your friend opened a chest. Your share waits at the chest (pinned on your map)");
            puppet_waypoint_pin(own, at);
        } else {
            chat_send_notice("Your friend opened a chest. Your share waits at the chest (pinned on your map)");
            capture_emit_ping(at.x, at.y, at.z);
        }
    }
}

void stash_tick(bool connected, bool host, uint32_t) {
    if (!connected) {
        std::lock_guard lk(g_mu);
        g_known = false;
        g_list.clear();
        g_ask_pending = g_ask_answered = 0;
        return;
    }
    g_is_host = host;
    if (host) {
        static uint64_t chest_at = 0;
        const uint64_t t0 = now64();
        if (t0 - chest_at >= 1000) {
            chest_at = t0;
            chest_notices();
        }
    }
    // Early: whether the host's warehouse is unlocked (the card offers the stash only then), asked every 10 s until known.
    {
        static uint64_t probe_at = 0;
        const uint64_t t0 = now64();
        if (t0 - probe_at >= 10000 && (host ? native_slots() : g_guest_slots.load()) < 0) {
            probe_at = t0;
            if (host) {
                ask_own_server();
            } else {
                std::lock_guard lk(g_mu);
                g_asked_at = 0;  // the guest's next tick asks (below), page or not
                g_force_ask = true;
            }
        }
    }
    if (host) {
        // Our own page: our server reads the stash every few seconds while it is shown (the list comes back through
        // cdmp_stash_list; stash_list reads it there).
        {
            static uint64_t own_at = 0;
            const uint64_t t0 = now64();
            if (t0 - g_page_seen.load() <= 1500 && t0 - own_at >= kAskEveryMs) {
                own_at = t0;
                ask_own_server();
            }
        }
        // An ask: our server reads the warehouse first (a new cdmp_stash_list generation), then the list goes out;
        // after 1.5 s without one, what we have.
        static uint32_t asked_gen = 0;
        static uint64_t asked_at = 0;
        bool ask = false;
        {
            std::lock_guard lk(g_mu);
            if (g_ask_pending && g_ask_pending != g_ask_answered) {
                g_ask_answered = g_ask_pending;
                ask = true;
            }
        }
        const uint64_t t = now64();
        if (ask && !asked_at) {
            asked_gen = native_gen();
            asked_at = t;
            if (!ask_own_server()) asked_at = t - 1500;  // no server read: answer at once
        }
        if (asked_at && (native_gen() != asked_gen || t - asked_at >= 1500)) {
            asked_at = 0;
            send_list();
        }
        return;
    }
    // Guest: ask while the page is on screen (and right after a withdraw), and once early (above).
    const uint64_t t = now64();
    bool forced = false;
    {
        std::lock_guard lk(g_mu);
        forced = g_force_ask;
        g_force_ask = false;
    }
    if (!forced && t - g_page_seen.load() > 1500) return;
    bool ask = false;
    {
        std::lock_guard lk(g_mu);
        if (t - g_asked_at >= kAskEveryMs) {
            g_asked_at = t;
            ++g_ask_id;
            ask = true;
        }
    }
    if (ask) {
        std::vector<uint8_t> b{kOpAsk, 0, 0, 0, 0};
        std::memcpy(&b[1], &g_ask_id, 4);
        activity_send(kStashType, b);
    }
}

void stash_page_shown() {
    const uint64_t t = now64();
    g_page_role_seen = t;
    if (t - g_page_seen.load() > 1500) {  // just opened: ask now
        std::lock_guard lk(g_mu);
        g_asked_at = 0;
    }
    g_page_seen = t;
}

std::vector<StashItem> stash_list(bool& known, uint32_t& age_ms) {
    if (g_is_host.load()) {  // the host: what its server read last
        std::vector<StashItem> out;
        known = false;
        age_ms = 0;
        if (auto fn = native_list()) {
            static uint16_t sl[512], ix[512];
            static uint32_t ct[512];
            uint32_t gen = 0;
            const int n = fn(&gen, sl, ix, ct, 512);
            if (n >= 0) {
                known = true;
                for (int i = 0; i < n; ++i) out.push_back({sl[i], ix[i], ct[i]});
            }
        }
        return out;
    }
    std::lock_guard lk(g_mu);
    known = g_known;
    age_ms = g_known ? static_cast<uint32_t>(std::min<uint64_t>(now64() - g_list_at, 0xFFFFFFFF)) : 0;
    return g_list;
}

int stash_slots() { return g_is_host.load() ? native_slots() : g_guest_slots.load(); }

bool stash_withdraw(const StashItem& it) {
    const uint32_t me = capture_own_player_key();
    const uint16_t key = 0xFFFF;  // the host's server knows the stash inventory
    if (!me || it.idx == 0xFFFF || it.count < 1) return false;
    std::vector<uint8_t> pkt(5 + 30);
    const uint16_t len = 30, hint = it.idx, slot = it.slot;
    const uint32_t item = 0, giver = kHostCharacter, entry = kWithdrawEntry;
    const uint64_t n = it.count;
    std::memcpy(&pkt[0], &kOpMoveItem, 2);
    std::memcpy(&pkt[3], &len, 2);
    uint8_t* p = &pkt[5];
    std::memcpy(p + 0, &item, 4);
    std::memcpy(p + 4, &hint, 2);
    std::memcpy(p + 6, &n, 8);
    std::memcpy(p + 14, &giver, 4);
    std::memcpy(p + 18, &me, 4);
    std::memcpy(p + 22, &key, 2);
    std::memcpy(p + 24, &slot, 2);
    std::memcpy(p + 26, &entry, 4);
    const bool ok = sniffer_inject_c2s(std::move(pkt));
    log::info("Stash: take x{} of item index 0x{:X} from the stash slot {}{}", n, hint, slot, ok ? "" : " NOT sent");
    if (ok) {
        std::lock_guard lk(g_mu);
        g_list.erase(std::remove_if(g_list.begin(), g_list.end(), [&](const StashItem& x) { return x.slot == it.slot; }),
                     g_list.end());
        g_asked_at = now64() - kAskEveryMs + 1000;  // a fresh list in a second
    }
    return ok;
}

// Item names (build/decomp/notes_item_names.md, local): row = exe+0x3DD8A0(0, idx) (loaded on demand, freed 30 s
// later: only on the game's UI thread, never kept); its localized name object at row+0x90; the object's vftable slot 1
// returns the UTF-8 text (const char*, from the localization buffer). Resolved in stash_ui_tick, cached here.
namespace {
std::mutex g_names_mu;
std::unordered_map<uint16_t, std::string> g_names;
std::vector<uint16_t> g_names_wanted;

bool read_name(uint16_t idx, char* out, size_t cap) noexcept {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    out[0] = 0;
    __try {
        const uintptr_t row = game::info_row(base + 0x3DAF10, idx);
        if (!row) return false;
        const uintptr_t obj = *reinterpret_cast<uintptr_t*>(row + 0x90);
        if (!obj) return false;
        const uintptr_t vt = *reinterpret_cast<uintptr_t*>(obj);
        const char* t = reinterpret_cast<const char* (__fastcall*)(uintptr_t, uint64_t)>(
            *reinterpret_cast<uintptr_t*>(vt + 8))(obj, 0);
        if (!t) return false;
        size_t i = 0;
        for (; i + 1 < cap && t[i]; ++i) out[i] = t[i];
        out[i] = 0;
        return i > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = 0;
        return false;
    }
}
}  // namespace

std::string stash_item_name(uint16_t idx) {
    std::lock_guard lk(g_names_mu);
    if (auto it = g_names.find(idx); it != g_names.end()) return it->second;
    if (std::find(g_names_wanted.begin(), g_names_wanted.end(), idx) == g_names_wanted.end() &&
        g_names_wanted.size() < 512)
        g_names_wanted.push_back(idx);
    return {};
}

void stash_ui_tick() {
    std::vector<uint16_t> want;
    {
        std::lock_guard lk(g_names_mu);
        want.swap(g_names_wanted);
    }
    for (const uint16_t idx : want) {
        char buf[128];
        const bool ok = read_name(idx, buf, sizeof buf);
        std::lock_guard lk(g_names_mu);
        g_names[idx] = ok ? std::string(buf) : std::string();  // "" = the fallback, not asked again
    }
}

}  // namespace cdmp::net
