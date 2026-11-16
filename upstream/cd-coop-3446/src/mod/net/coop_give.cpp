#include "mod/net/coop_give.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#include <safetyhook.hpp>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/net/coop_stash.h"
#include "mod/net/local_capture.h"
#include "mod/net/troc_sniffer.h"
#include "mod/game/info_row.h"

namespace cdmp::net {
namespace {

namespace off = game::build_3663;

constexpr uintptr_t kDiscardBag = 0xF31570, kDiscardList = 0xF318E0, kClientDiscard = 0xAFEE20;
constexpr uintptr_t kInventoryVftable = 0x5A77B88;
constexpr uint16_t kOpMoveItem = 3104;
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
bool g_probe = false;  // ... and ClientDiscard only records what it would give (the trade's offer)
struct Offer {
    bool on = false;
    uint16_t inv = 0, slot = 0, hint = 0xFFFF;  // the request's inventory key, slot and item index
    uint64_t n = 0;
    std::string name;
};
Offer g_probed;  // UI thread: the dry run's result
bool g_give = false;  // ... and ClientDiscard becomes the give
bool g_stash = false;  // ... or the deposit into the shared stash (B12)

uint16_t inventory_key(uint32_t index) noexcept {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        const uintptr_t row = game::info_row(base + 0x57B410, index);
        if (row) return *reinterpret_cast<uint16_t*>(row + 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return static_cast<uint16_t>(index);
}

uint16_t slot_item(int64_t holder, int16_t inventory, int16_t slot) noexcept {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    __try {
        const uintptr_t e = reinterpret_cast<uintptr_t (__fastcall*)(int64_t, int16_t, int16_t)>(base + 0x23687F0)(
            holder, inventory, slot);
        if (e && *reinterpret_cast<int64_t*>(e + 0x10) > 0) return *reinterpret_cast<uint16_t*>(e + 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return 0xFFFF;
}

uint64_t on_discard(int which, int64_t* root, int64_t count, uint64_t a3, uint64_t a4) {
    auto& hook = which ? g_list_hook : g_bag_hook;
    // The question when the friend is near (Give) or, as the guest, anywhere (the shared stash).
    // The shared stash: both players, anywhere, once the host's warehouse has room (0 = not unlocked yet).
    const bool give = g_near.load(), stash = g_linked.load() && stash_slots() != 0;
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

// The give request (MoveItemInventoryToOtherInventoryReq with the native's give / stash entry), injected.
bool send_move(uint32_t me, uint32_t to, uint16_t inv, uint16_t sl, uint16_t hint, uint64_t n, uint32_t entry) {
    const uint32_t item = 0;
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
    return me && inv != 0xFFFF && sniffer_inject_c2s(std::move(pkt));
}

uint32_t friend_key() {
    // The guest's character key varies with the world (2026-11-15: 0xA0100066 only in the test world).
    const uint32_t guest = partner_actor_key() ? partner_actor_key() : kGuestCharacter;
    return g_host.load() ? guest : kHostCharacter;
}

int* on_client_discard(int64_t holder, int* err, int64_t* actor, uint64_t inventory, int16_t slot, int64_t count) {
    if (!g_give && !g_stash && !g_probe) return g_client_hook.call<int*>(holder, err, actor, inventory, slot, count);
    const bool stash = g_stash, probe = g_probe;
    g_give = g_stash = g_probe = false;
    const uint32_t me = capture_own_player_key();
    // The give goes to the friend; the stash is on the host's character (the host's own deposit: itself).
    const uint32_t to = stash ? kHostCharacter : friend_key();
    const uint32_t entry = stash ? kStashDepositEntry : kGiveEntry;
    // ClientDiscard's inventory is an index into the game's inventory table; the request carries that row's key
    // (+8), as the game's own request builder does (exe+0xAC9210 -> exe+0x5560E0 -> exe+0xCDCEF0).
    const uint16_t sl = static_cast<uint16_t>(slot);
    const uint16_t inv = inventory_key(static_cast<uint32_t>(inventory & 0xFFFF));
    // The item the client sees in that slot: the server finds it by this when its slot numbers differ.
    const uint16_t hint = slot_item(holder, static_cast<int16_t>(inventory & 0xFFFF), slot);
    const uint64_t n = count > 0 ? static_cast<uint64_t>(count) : 1;
    if (probe) {  // the trade's offer: recorded, nothing sent yet
        g_probed = {inv != 0xFFFF, inv, sl, hint, n, {}};
        log::info("Trade: our offer x{} from inventory {} (key {}) slot {} (item index 0x{:X})", n, inventory & 0xFFFF,
                  inv, slot, hint);
        if (err) *err = 0;
        return err;
    }
    const bool ok = send_move(me, to, inv, sl, hint, n, entry);
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
        if (owner) reinterpret_cast<void (__fastcall*)(int64_t)>(base + 0xF54850)(owner);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ---------------------------------------------------------------- B11 trade (2026-11-16)
constexpr uint64_t kTradeMs = 120000;  // an offer lapses after this
std::mutex g_tmu;  // leaf lock
struct Wager {
    Offer mine, theirs;
};
Wager g_wager;  // guarded by g_tmu
std::atomic<bool> g_challenge{false};
struct Trade {
    Offer mine, theirs;
    bool my_ok = false, their_ok = false, card = false;
    uint64_t since = 0;
    std::vector<std::vector<uint8_t>> out;
    std::vector<std::string> toasts;
} g_trade;  // guarded by g_tmu

std::string braces(const std::string& s) {  // a name inside a toast that goes through std::vformat
    std::string o;
    for (const char c : s) {
        o += c;
        if (c == '{' || c == '}') o += c;
    }
    return o;
}
std::string label(const Offer& o) {
    const std::string n = o.name.empty() ? std::format("item 0x{:X}", o.hint) : o.name;
    return o.n > 1 ? std::format("{} x{}", n, o.n) : n;
}
void trade_reset_locked() {
    g_trade.mine = g_trade.theirs = Offer{};
    g_trade.my_ok = g_trade.their_ok = g_trade.card = false;
}

}  // namespace

std::vector<uint8_t> offer_bytes(uint8_t op, const Offer& o) {
    std::vector<uint8_t> b{op, static_cast<uint8_t>(o.hint), static_cast<uint8_t>(o.hint >> 8)};
    for (int i = 0; i < 8; ++i) b.push_back(static_cast<uint8_t>(o.n >> (8 * i)));
    const std::string nm = o.name.substr(0, 60);
    b.push_back(static_cast<uint8_t>(nm.size()));
    b.insert(b.end(), nm.begin(), nm.end());
    return b;
}
bool offer_from(const uint8_t* d, size_t n, Offer& o) {
    if (n < 12) return false;
    o = Offer{};
    o.on = true;
    o.hint = static_cast<uint16_t>(d[1] | d[2] << 8);
    for (int i = 0; i < 8; ++i) o.n |= static_cast<uint64_t>(d[3 + i]) << (8 * i);
    const size_t len = d[11];
    if (n >= 12 + len) o.name.assign(reinterpret_cast<const char*>(d + 12), len);
    return true;
}

void wager_stake_made(const Offer& o) {
    std::lock_guard lk(g_tmu);
    g_wager.mine = o;
    g_trade.out.push_back(offer_bytes(11, o));
    g_trade.toasts.push_back(g_wager.theirs.on
                                 ? "Wager\nYour " + braces(label(o)) + " against their " + braces(label(g_wager.theirs)) +
                                       ": the loser's stake goes to the winner"
                                 : "Wager\nYou stake " + braces(label(o)) + ". It counts once {} stakes something too");
    log::info("Wager: we stake {} (item index 0x{:X})", label(o), o.hint);
}

void trade_offer_made(uint16_t inv, uint16_t slot, uint16_t hint, uint64_t n, const std::string& name) {
    std::lock_guard lk(g_tmu);
    g_trade.mine = {true, inv, slot, hint, n, name};
    g_trade.my_ok = g_trade.their_ok = g_trade.card = false;
    g_trade.since = GetTickCount64();
    std::vector<uint8_t> b{1, static_cast<uint8_t>(hint), static_cast<uint8_t>(hint >> 8)};
    for (int i = 0; i < 8; ++i) b.push_back(static_cast<uint8_t>(n >> (8 * i)));
    const std::string nm = name.substr(0, 60);
    b.push_back(static_cast<uint8_t>(nm.size()));
    b.insert(b.end(), nm.begin(), nm.end());
    g_trade.out.push_back(std::move(b));
    g_trade.toasts.push_back(g_trade.theirs.on ? std::string("Trade\nBoth offers are in: accept or decline")
                                               : "Trade\nYou offer " + braces(label(g_trade.mine)) +
                                                     ". Waiting for {} to offer something back");
    log::info("Trade: we offer {} (item index 0x{:X})", label(g_trade.mine), hint);
}

void trade_on_event(const uint8_t* d, size_t n) {
    if (n < 1) return;
    std::lock_guard lk(g_tmu);
    if (d[0] == 11) {  // B15: the friend's stake on the running challenge
        Offer o;
        if (!offer_from(d, n, o)) return;
        g_wager.theirs = o;
        g_trade.toasts.push_back(g_wager.mine.on
                                     ? "Wager\nYour " + braces(label(g_wager.mine)) + " against their " +
                                           braces(label(o)) + ": the loser's stake goes to the winner"
                                     : "Wager\n{} stakes " + braces(label(o)) +
                                           " on this challenge. Stake something back: Discard on an item, Stake on the challenge");
        log::info("Wager: the friend stakes {} (item index 0x{:X})", label(o), o.hint);
        return;
    }
    if (d[0] == 13) {
        g_wager = Wager{};
        log::info("Wager: the friend's wager is off");
        return;
    }
    if (d[0] == 1 && n >= 12) {
        Offer o;
        o.on = true;
        o.hint = static_cast<uint16_t>(d[1] | d[2] << 8);
        for (int i = 0; i < 8; ++i) o.n |= static_cast<uint64_t>(d[3 + i]) << (8 * i);
        const size_t len = d[11];
        if (n >= 12 + len) o.name.assign(reinterpret_cast<const char*>(d + 12), len);
        g_trade.theirs = o;
        g_trade.my_ok = g_trade.their_ok = g_trade.card = false;  // a new offer asks both again
        g_trade.since = GetTickCount64();
        g_trade.toasts.push_back(
            g_trade.mine.on ? "Trade\n{} offers " + braces(label(o)) + ": accept or decline"
                            : "Trade\n{} offers " + braces(label(o)) +
                                  ". To trade, use Discard on an item in your inventory and pick Offer in a trade");
        log::info("Trade: the friend offers {} (item index 0x{:X})", label(o), o.hint);
    } else if (d[0] == 2) {
        if (!g_trade.theirs.on) return;
        g_trade.their_ok = true;
        if (!g_trade.my_ok) g_trade.toasts.push_back("Trade\n{} accepted the trade");
        log::info("Trade: the friend accepted");
    } else if (d[0] == 3) {
        if (g_trade.mine.on || g_trade.theirs.on) g_trade.toasts.push_back("Trade\n{} cancelled the trade");
        trade_reset_locked();
        log::info("Trade: the friend cancelled");
    }
}

void trade_answer(bool accept) {
    std::lock_guard lk(g_tmu);
    if (!g_trade.mine.on || !g_trade.theirs.on) return;
    if (accept) {
        g_trade.my_ok = true;
        g_trade.out.push_back({2});
        if (!g_trade.their_ok) g_trade.toasts.push_back("Trade\nAccepted. Waiting for {}");
        log::info("Trade: we accepted");
    } else {
        g_trade.out.push_back({3});
        g_trade.toasts.push_back("Trade\nYou declined the trade");
        trade_reset_locked();
        log::info("Trade: we declined");
    }
}

void give_set_challenge(bool on) {
    const bool was = g_challenge.exchange(on);
    if (was && !on) {  // the challenge ended (a result settles the wager first, wager_match_end)
        std::lock_guard lk(g_tmu);
        if (g_wager.mine.on || g_wager.theirs.on) {
            g_trade.toasts.push_back("Wager\nThe challenge ended without a result: nothing changes hands");
            log::info("Wager: off (the challenge ended without a result)");
        }
        g_wager = Wager{};
    }
}
bool give_offer_stake() { return g_challenge.load() && g_near.load(); }

void wager_match_end(bool won) {
    std::lock_guard lk(g_tmu);
    const Wager w = g_wager;
    g_wager = Wager{};
    if (!w.mine.on && !w.theirs.on) return;
    if (!w.mine.on || !w.theirs.on) {
        g_trade.toasts.push_back("Wager\nOnly one of you staked: nothing changes hands");
        log::info("Wager: one-sided, nothing given");
        return;
    }
    if (won) {
        g_trade.toasts.push_back("Wager\nYou won the wager: {} gives you " + braces(label(w.theirs)));
        log::info("Wager: we won; the friend gives {}", label(w.theirs));
        return;
    }
    const uint32_t me = capture_own_player_key(), to = friend_key();
    const bool ok = send_move(me, to, w.mine.inv, w.mine.slot, w.mine.hint, w.mine.n, kGiveEntry);
    g_trade.toasts.push_back(ok ? "Wager\nYou lost the wager: your " + braces(label(w.mine)) + " goes to {}"
                                : std::string("Wager\nYour stake could not be sent"));
    log::info("Wager: we lost; our {} (inventory key {} slot {} item 0x{:X}) to 0x{:08X} (giver 0x{:08X}){}",
              label(w.mine), w.mine.inv, w.mine.slot, w.mine.hint, to, me, ok ? "" : " NOT sent");
}

TradeTick trade_tick(bool connected) {
    TradeTick t;
    std::lock_guard lk(g_tmu);
    if (!connected) g_wager = Wager{};
    const bool any = g_trade.mine.on || g_trade.theirs.on;
    if (any && !connected) {
        g_trade.toasts.push_back("Trade\nThe trade is off: {} left");
        trade_reset_locked();
    } else if (any && GetTickCount64() - g_trade.since > kTradeMs) {
        g_trade.out.push_back({3});
        g_trade.toasts.push_back("Trade\nThe trade lapsed");
        trade_reset_locked();
        log::info("Trade: lapsed");
    }
    if (g_trade.mine.on && g_trade.theirs.on && !g_trade.card && !g_trade.my_ok) {
        g_trade.card = true;
        t.card = true;
        t.mine = label(g_trade.mine);
        t.theirs = label(g_trade.theirs);
    }
    if (g_trade.mine.on && g_trade.theirs.on && g_trade.my_ok && g_trade.their_ok) {
        // Both accepted: each game gives its own offer to the other (the other side does the same now).
        const Offer m = g_trade.mine;
        const uint32_t me = capture_own_player_key(), to = friend_key();
        const bool ok = send_move(me, to, m.inv, m.slot, m.hint, m.n, kGiveEntry);
        log::info("Trade: both accepted: our {} (inventory key {} slot {} item 0x{:X}) to 0x{:08X} (giver 0x{:08X}){}",
                  label(m), m.inv, m.slot, m.hint, to, me, ok ? "" : " NOT sent");
        g_trade.toasts.push_back(ok ? "Trade\nDone: you gave " + braces(label(m)) + " for " + braces(label(g_trade.theirs))
                                    : std::string("Trade\nYour side could not be sent: nothing given"));
        trade_reset_locked();
    }
    t.send.swap(g_trade.out);
    t.toasts.swap(g_trade.toasts);
    return t;
}

std::atomic<bool> g_enabled{false};
void give_set_enabled(bool on) { g_enabled = on; }
bool give_enabled() { return g_enabled.load(); }

bool give_install() {
    if (!g_enabled.load()) return false;
    if (g_state) return g_state > 0;
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    // 3663: the two handlers no longer share a prologue (the bag one keeps a frame pointer now: the inventory-row
    // getter takes its index by address there). Same arguments, same fields (fdiff.py EF6560 F31570).
    static const uint8_t kBagHead[16] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
                                         0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55};
    static const uint8_t kListHead[16] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
                                          0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57};
    static const uint8_t kClientHead[16] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x66,
                                            0x44, 0x89, 0x48, 0x20, 0x4C, 0x89, 0x40, 0x18};
    uint8_t a[16] = {}, b[16] = {}, c[16] = {};
    if (base != off::kImageBase || !game::safe_read(base + kDiscardBag, a, 16) ||
        !game::safe_read(base + kDiscardList, b, 16) || !game::safe_read(base + kClientDiscard, c, 16) ||
        std::memcmp(a, kBagHead, 16) != 0 || std::memcmp(b, kListHead, 16) != 0 ||
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
    if (answer == 3 && !g_linked.load()) {
        log::info("Give: the shared stash needs the session; nothing done");
        return;
    }
    g_pass = true;
    g_give = answer == 1;
    g_stash = answer == 3;
    g_probe = answer == 4 || answer == 5;
    g_probed = Offer{};
    bool unlocked = true;
    const uint64_t r = hook.call<uint64_t>(p.root, p.count, uint64_t{0}, uint64_t{0});
    g_pass = false;
    if (g_give || g_stash || g_probe) {
        g_give = g_stash = g_probe = false;
        log::info("Give: the game did not reach its discard (nothing given)");
    } else if (answer == 1 || answer == 3 || answer == 4 || answer == 5) {
        // The handler now waits for the server's answer to a discard that was never sent ("Processing the previous
        // request" on the next action). Its own error path's clean-up: no pending action, the waiting state hidden.
        unlocked = clear_wait(p.root, base);
    }
    log::info("Give: {} done, the handler returned {}{}",
              answer == 1   ? "give"
              : answer == 3 ? "stash"
              : answer == 4 ? "trade offer"
              : answer == 5 ? "stake"
                            : "discard",
              r & 0xFF,
              unlocked ? "" : " (the inventory's wait could not be cleared)");
    if ((answer == 4 || answer == 5) && g_probed.on) {
        Offer o = g_probed;
        o.name = stash_item_name(o.hint);
        if (o.name.empty()) {  // asked now: resolved by the stash's name tick, which runs on this (the UI) thread
            stash_ui_tick();
            o.name = stash_item_name(o.hint);
        }
        if (answer == 5) {
            wager_stake_made(o);
        } else {
            trade_offer_made(o.inv, o.slot, o.hint, o.n, o.name);
        }
    }
}

}  // namespace cdmp::net
