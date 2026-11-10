#include "mod/net/coop_give.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <vector>

#include <safetyhook.hpp>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/net/local_capture.h"
#include "mod/net/troc_sniffer.h"

namespace cdmp::net {
namespace {

namespace off = game::build_3576;

constexpr uintptr_t kDiscardBag = 0xEF6560, kDiscardList = 0xEF68F0, kClientDiscard = 0xAC9210;
constexpr uintptr_t kInventoryVftable = 0x59FB218;
constexpr uint16_t kOpMoveItem = 3111;
constexpr uint32_t kGiveEntry = 0xC0DE0001;  // the native's give entry (upstream/cdmp-native trade_probe)
// The character keys of a two-player session (the host's game hands them out in this order).
constexpr uint32_t kHostCharacter = 0xA0100001, kGuestCharacter = 0xA0100066;
constexpr uint64_t kQuestionMs = 60000;

safetyhook::InlineHook g_bag_hook, g_list_hook, g_client_hook;
int g_state = 0;

std::atomic<bool> g_linked{false}, g_host{false}, g_near{false};
std::atomic<bool> g_question{false};
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

uint64_t on_discard(int which, int64_t* root, int64_t count, uint64_t a3, uint64_t a4) {
    auto& hook = which ? g_list_hook : g_bag_hook;
    if (g_pass || !g_linked.load() || !g_near.load()) return hook.call<uint64_t>(root, count, a3, a4);
    g_pending = {true, which, root, count, GetTickCount64()};
    g_answer = -1;
    g_question = true;
    log::info("Give: a discard of {} held back for the question ({})", count, which ? "list" : "bag");
    return 0;
}
uint64_t on_discard_bag(int64_t* root, int64_t count, uint64_t a3, uint64_t a4) { return on_discard(0, root, count, a3, a4); }
uint64_t on_discard_list(int64_t* root, int64_t count, uint64_t a3, uint64_t a4) { return on_discard(1, root, count, a3, a4); }

int* on_client_discard(int64_t holder, int* err, int64_t* actor, uint64_t inventory, int16_t slot, int64_t count) {
    if (!g_give) return g_client_hook.call<int*>(holder, err, actor, inventory, slot, count);
    g_give = false;
    const uint32_t me = capture_own_player_key();
    const uint32_t to = g_host.load() ? kGuestCharacter : kHostCharacter;
    const uint32_t item = 0, entry = kGiveEntry;
    const uint16_t none = 0xFFFF, inv = static_cast<uint16_t>(inventory), sl = static_cast<uint16_t>(slot);
    const uint64_t n = count > 0 ? static_cast<uint64_t>(count) : 1;
    std::vector<uint8_t> pkt(5 + 30);
    const uint16_t len = 30;
    std::memcpy(&pkt[0], &kOpMoveItem, 2);
    std::memcpy(&pkt[3], &len, 2);
    uint8_t* p = &pkt[5];
    std::memcpy(p + 0, &item, 4);
    std::memcpy(p + 4, &none, 2);
    std::memcpy(p + 6, &n, 8);
    std::memcpy(p + 14, &me, 4);
    std::memcpy(p + 18, &to, 4);
    std::memcpy(p + 22, &inv, 2);
    std::memcpy(p + 24, &sl, 2);
    std::memcpy(p + 26, &entry, 4);
    const bool ok = me && sniffer_inject_c2s(std::move(pkt));
    log::info("Give: x{} from inventory {} slot {} to 0x{:08X} (giver 0x{:08X}){}", n, inv, slot, to, me,
              ok ? "" : " NOT sent");
    if (err) *err = 0;
    return err;
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
void give_answer(int code) { g_answer = code; }

void give_ui_tick() {
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
    g_pass = true;
    g_give = answer == 1;
    const uint64_t r = hook.call<uint64_t>(p.root, p.count, uint64_t{0}, uint64_t{0});
    g_pass = false;
    if (g_give) {
        g_give = false;
        log::info("Give: the game did not reach its discard (nothing given)");
    }
    log::info("Give: {} done, the handler returned {}", answer == 1 ? "give" : "discard", r & 0xFF);
}

}  // namespace cdmp::net
