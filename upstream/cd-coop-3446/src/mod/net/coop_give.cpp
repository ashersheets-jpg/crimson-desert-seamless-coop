#include "mod/net/coop_give.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <vector>

#include <safetyhook.hpp>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/net/coop_stash.h"
#include "mod/net/local_capture.h"
#include "mod/net/troc_sniffer.h"

namespace cdmp::net {
namespace {

namespace off = game::build_3576;

constexpr uintptr_t kDiscardBag = 0xEF6560, kDiscardList = 0xEF68F0, kClientDiscard = 0xAC9210;
constexpr uintptr_t kInventoryVftable = 0x59FB218;
constexpr uint16_t kOpMoveItem = 3111;
constexpr uint32_t kGiveEntry = 0xC0DE0001;  // the native's give entry (upstream/cdmp-native trade_probe)
constexpr uint32_t kStashDepositEntry = 0xC0DE0002;  // B12: guest bag -> the host character's WareHouse (native)
// The character keys of a two-player session (the host's game hands them out in this order).
constexpr uint32_t kHostCharacter = 0xA0100001, kGuestCharacter = 0xA0100066;
constexpr uint64_t kQuestionMs = 60000;

safetyhook::InlineHook g_bag_hook, g_list_hook, g_client_hook;
int g_state = 0;

std::atomic<bool> g_linked{false}, g_host{false}, g_near{false};
std::atomic<bool> g_question{false};
std::atomic<bool> g_offer_give{false}, g_offer_stash{false};
std::atomic<int> g_answer{-1};

// UI thread only.
struct Pending {
    bool on = false;
    int which = 0;  // 0 bag, 1 list
    int64_t* root = nullptr;
    int64_t count = 0;
    uint64_t since = 0;
} g_pending;
bool g_pass = false;  // our own call of a handler: no question
bool g_give = false;  // ... and ClientDiscard becomes the give
bool g_stash = false;  // ... or the deposit into the shared stash (B12)

uint16_t inventory_key(uint32_t index) noexcept {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        const uintptr_t row = reinterpret_cast<uintptr_t (__fastcall*)(uint64_t, uint32_t)>(base + 0x5560E0)(0, index);
        if (row) return *reinterpret_cast<uint16_t*>(row + 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return static_cast<uint16_t>(index);
}

uint16_t slot_item(int64_t holder, int16_t inventory, int16_t slot) noexcept {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        const uintptr_t e = reinterpret_cast<uintptr_t (__fastcall*)(int64_t, int16_t, int16_t)>(base + 0x22F7FE0)(
            holder, inventory, slot);
        if (e && *reinterpret_cast<int64_t*>(e + 0x10) > 0) return *reinterpret_cast<uint16_t*>(e + 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return 0xFFFF;
}

uint64_t on_discard(int which, int64_t* root, int64_t count, uint64_t a3, uint64_t a4) {
    auto& hook = which ? g_list_hook : g_bag_hook;
    // The question when the friend is near (Give) or, as the guest, anywhere (the shared stash).
    const bool give = g_near.load(), stash = g_linked.load() && !g_host.load();
    if (g_pass || !g_linked.load() || (!give && !stash)) return hook.call<uint64_t>(root, count, a3, a4);
    g_offer_give = give;
    g_offer_stash = stash;
    g_pending = {true, which, root, count, GetTickCount64()};
    g_answer = -1;
    g_question = true;
    log::info("Give: a discard of {} held back for the question ({})", count, which ? "list" : "bag");
    return 0;
}
uint64_t on_discard_bag(int64_t* root, int64_t count, uint64_t a3, uint64_t a4) { return on_discard(0, root, count, a3, a4); }
uint64_t on_discard_list(int64_t* root, int64_t count, uint64_t a3, uint64_t a4) { return on_discard(1, root, count, a3, a4); }

int* on_client_discard(int64_t holder, int* err, int64_t* actor, uint64_t inventory, int16_t slot, int64_t count) {
    if (!g_give && !g_stash) return g_client_hook.call<int*>(holder, err, actor, inventory, slot, count);
    const bool stash = g_stash;
    g_give = g_stash = false;
    const uint32_t me = capture_own_player_key();
    const uint32_t to = g_host.load() ? kGuestCharacter : kHostCharacter;  // the stash is the host character's
    const uint32_t item = 0, entry = stash ? kStashDepositEntry : kGiveEntry;
    // ClientDiscard's inventory is an index into the game's inventory table; the request carries that row's key
    // (+8), as the game's own request builder does (exe+0xAC9210 -> exe+0x5560E0 -> exe+0xCDCEF0).
    const uint16_t sl = static_cast<uint16_t>(slot);
    const uint16_t inv = inventory_key(static_cast<uint32_t>(inventory & 0xFFFF));
    // The item the client sees in that slot: the server finds it by this when its slot numbers differ.
    const uint16_t hint = slot_item(holder, static_cast<int16_t>(inventory & 0xFFFF), slot);
    const uint64_t n = count > 0 ? static_cast<uint64_t>(count) : 1;
    std::vector<uint8_t> pkt(5 + 30);
    const uint16_t len = 30;
    std::memcpy(&pkt[0], &kOpMoveItem, 2);
    std::memcpy(&pkt[3], &len, 2);
    uint8_t* p = &pkt[5];
    std::memcpy(p + 0, &item, 4);
    std::memcpy(p + 4, &hint, 2);
    std::memcpy(p + 6, &n, 8);
    std::memcpy(p + 14, &me, 4);
    std::memcpy(p + 18, &to, 4);
    std::memcpy(p + 22, &inv, 2);
    std::memcpy(p + 24, &sl, 2);
    std::memcpy(p + 26, &entry, 4);
    const bool ok = me && inv != 0xFFFF && sniffer_inject_c2s(std::move(pkt));
    log::info("Give: {}x{} from inventory {} (key {}) slot {} (item index 0x{:X}) to 0x{:08X} (giver 0x{:08X}){}",
              stash ? "into the shared stash " : "", n,
              inventory & 0xFFFF, inv, slot, hint, to, me,
              ok ? "" : " NOT sent");
    if (err) *err = 0;
    return err;
}

// The discard handler leaves the inventory waiting for the server's answer ("Processing the previous request" on
// the next action). A give never gets one: the handler's own error clean-up (no pending action, waiting state hidden).
bool clear_wait(int64_t* root, uintptr_t base) noexcept {
    __try {
        root[0x27] = 0;
        *reinterpret_cast<int64_t*>(reinterpret_cast<uintptr_t>(root) + 0x138) = 0;
        const auto owner = reinterpret_cast<int64_t (__fastcall*)(int64_t*)>(
            *reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(root) + 0x118))(root);
        if (owner) reinterpret_cast<void (__fastcall*)(int64_t)>(base + 0xF1A000)(owner);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

bool give_install() {
    if (g_state) return g_state > 0;
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    static const uint8_t kHandlerHead[16] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
                                             0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57};
    static const uint8_t kClientHead[16] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x4C,
                                            0x89, 0x40, 0x18, 0x48, 0x89, 0x48, 0x08, 0x55};
    uint8_t a[16] = {}, b[16] = {}, c[16] = {};
    if (base != off::kImageBase || !game::safe_read(base + kDiscardBag, a, 16) ||
        !game::safe_read(base + kDiscardList, b, 16) || !game::safe_read(base + kClientDiscard, c, 16) ||
        std::memcmp(a, kHandlerHead, 16) != 0 || std::memcmp(b, kHandlerHead, 16) != 0 ||
        std::memcmp(c, kClientHead, 16) != 0) {
        log::warn("Give: the inventory's discard functions differ on this build; giving from the inventory is off");
        g_state = -1;
        return false;
    }
    g_client_hook = safetyhook::create_inline(reinterpret_cast<void*>(base + kClientDiscard),
                                              reinterpret_cast<void*>(on_client_discard));
    g_bag_hook = safetyhook::create_inline(reinterpret_cast<void*>(base + kDiscardBag),
                                           reinterpret_cast<void*>(on_discard_bag));
    g_list_hook = safetyhook::create_inline(reinterpret_cast<void*>(base + kDiscardList),
                                            reinterpret_cast<void*>(on_discard_list));
    const bool ok = g_client_hook && g_bag_hook && g_list_hook;
    if (!ok) {
        g_client_hook = {};
        g_bag_hook = {};
        g_list_hook = {};
    }
    log::info("Give: inventory hooks {}", ok ? "installed" : "FAILED");
    g_state = ok ? 1 : -1;
    return ok;
}

void give_set_state(bool linked, bool host, bool friend_near) {
    g_linked = linked;
    g_host = host;
    g_near = linked && friend_near;
}

bool give_take_question() { return g_question.exchange(false); }
bool give_offer_give() { return g_offer_give.load(); }
bool give_offer_stash() { return g_offer_stash.load(); }
void give_answer(int code) { g_answer = code; }

void give_ui_tick() {
    stash_ui_tick();  // B12: item names for the stash page (needs this thread)
    if (!g_pending.on) return;
    const int answer = g_answer.exchange(-1);
    if (answer < 0) {
        if (GetTickCount64() - g_pending.since > kQuestionMs + 5000) g_pending.on = false;
        return;
    }
    const Pending p = g_pending;
    g_pending.on = false;
    if (answer == 0) {
        log::info("Give: cancelled");
        return;
    }
    // The inventory must still be that screen (the question outlived a closed menu otherwise).
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uintptr_t vft = 0;
    if (!game::safe_read(reinterpret_cast<uintptr_t>(p.root), &vft, sizeof vft) || vft != base + kInventoryVftable) {
        log::info("Give: the inventory is gone, nothing done");
        return;
    }
    auto& hook = p.which ? g_list_hook : g_bag_hook;
    if (answer == 3 && (g_host.load() || !g_linked.load())) {
        log::info("Give: the shared stash is the guest's only; nothing done");
        return;
    }
    g_pass = true;
    g_give = answer == 1;
    g_stash = answer == 3;
    bool unlocked = true;
    const uint64_t r = hook.call<uint64_t>(p.root, p.count, uint64_t{0}, uint64_t{0});
    g_pass = false;
    if (g_give || g_stash) {
        g_give = g_stash = false;
        log::info("Give: the game did not reach its discard (nothing given)");
    } else if (answer == 1 || answer == 3) {
        // The handler now waits for the server's answer to a discard that was never sent ("Processing the previous
        // request" on the next action). Its own error path's clean-up: no pending action, the waiting state hidden.
        unlocked = clear_wait(p.root, base);
    }
    log::info("Give: {} done, the handler returned {}{}", answer == 1 ? "give" : answer == 3 ? "stash" : "discard", r & 0xFF,
              unlocked ? "" : " (the inventory's wait could not be cleared)");
}

}  // namespace cdmp::net
