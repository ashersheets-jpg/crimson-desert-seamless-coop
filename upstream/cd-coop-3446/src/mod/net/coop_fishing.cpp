#include "mod/net/coop_fishing.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <atomic>
#include <cstring>
#include <format>
#include <iterator>
#include <mutex>
#include <utility>
#include <vector>

#include "mod/log.h"
#include "mod/net/coop_activity.h"

namespace cdmp::net {
namespace {

struct Fish {
    uint32_t item;
    int points;
    const char* name;
};
// The game's fish items (Food_<kind>, live item table 2026-11-05: build/port3479/fishkeys.txt), by key.
constexpr Fish kFish[] = {
    {29501, 5, "Paddlefish"},       {29503, 5, "Giant Red Sea Bream"}, {29504, 5, "Giant Red Snapper"},
    {29505, 5, "Giant Mudfish"},    {29506, 5, "Sailfish"},            {29507, 5, "Golden Tench"},
    {29508, 5, "Coelacanth"},       {29509, 5, "Golden Carp"},         {29513, 5, "Lightning Eel"},
    {29603, 2, "Salmon"},           {29604, 2, "Sturgeon"},            {29605, 2, "Sockeye Salmon"},
    {29607, 2, "Red Snapper"},      {29608, 2, "Rock Bream"},          {29609, 2, "Mackerel"},
    {29801, 1, "Blackbrow Bleak"},  {29802, 1, "Chub"},                {29806, 1, "Pike"},
    {29807, 1, "Trout"},            {29808, 1, "Carp"},                {29809, 1, "Tench"},
    {29810, 1, "Mudfish"},          {29811, 1, "Catfish"},             {29814, 1, "Eel"},
    {29815, 1, "Perch"},            {29816, 1, "Bass"},                {29817, 1, "Yellow Bass"},
    {29819, 1, "Crucian Carp"},     {29821, 1, "Crayfish"},            {1001241, 1, "Ricefish"},
    {1001293, 5, "Golden Coelacanth"},
};

const Fish* fish(uint32_t item) {
    const auto it = std::lower_bound(std::begin(kFish), std::end(kFish), item,
                                     [](const Fish& f, uint32_t k) { return f.item < k; });
    return it != std::end(kFish) && it->item == item ? it : nullptr;
}

constexpr uint16_t kOpAddItemToInventoryAck = 2210;
constexpr uint16_t kReasonCatch = 0x0D0F;  // AddItemToInventoryAck +10: a fish kept after the catch card
constexpr uint8_t kFishType = 27;          // Activity event type: u32 round | u16 score | u16 fish | u32 item
constexpr uint32_t kCountdownMs = 3000;
constexpr uint32_t kRoundMs = 360000;      // six minutes: a catch takes about half a minute to a minute
constexpr uint32_t kTestRoundMs = 150000;  // [dev] fishing_test
constexpr uint32_t kSettleMs = 1500;
constexpr uint32_t kOverMs = 8000;
enum Phase : uint8_t { kIdle, kCountdown, kFishing, kSettle, kOver };
std::atomic<bool> g_test{false};
uint32_t round_ms() { return g_test.load() ? kTestRoundMs : kRoundMs; }

struct State {
    Phase phase = kIdle;
    uint32_t round = 0, since = 0;
    int ours = 0, theirs = 0, caught = 0, their_caught = 0;
    uint32_t last_item = 0;      // our last catch (sent with the score)
    int result = 0;
    bool reported = false, send = false;
    std::string toast;           // "You caught a Bass  +1" / "Ana caught a Trout  +1"
} g;
std::mutex g_mutex;
std::string g_who;

std::string a_or_an(const char* name) {
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
    return (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' ? "an " : "a ") + std::string(name);
}

}  // namespace

const char* fishing_fish_name(uint32_t item_key) {
    const Fish* f = fish(item_key);
    return f ? f->name : nullptr;
}

void fishing_set_test(bool on) { g_test = on; }

void fishing_observe_s2c(const uint8_t* p, size_t size) {
    if (size < 5 + 26) return;
    uint16_t op, reason, count;
    std::memcpy(&op, p, 2);
    if (op != kOpAddItemToInventoryAck || p[5] != 4) return;  // 4: in game (1: the login lists)
    std::memcpy(&reason, p + 10, 2);
    std::memcpy(&count, p + 14, 2);
    uint32_t item;
    std::memcpy(&item, p + 25, 4);
    if (reason != kReasonCatch || count < 1) return;
    const Fish* f = fish(item);
    if (!f) {
        log::info("Fishing: caught item {} (not a fish of ours)", item);
        return;
    }
    log::info("Fishing: we kept {} (item {})", f->name, item);
    std::lock_guard lock(g_mutex);
    if (g.phase != kFishing) return;
    g.ours += f->points;
    g.caught += 1;
    g.last_item = item;
    g.send = true;
    g.toast = std::format("Fishing contest\nYou caught {}  +{}", a_or_an(f->name), f->points);
}

void fishing_on_event(const uint8_t* d, size_t n) {
    if (n < 12 || g_test) return;
    uint32_t round, item;
    uint16_t score, caught;
    std::memcpy(&round, d, 4);
    std::memcpy(&score, d + 4, 2);
    std::memcpy(&caught, d + 6, 2);
    std::memcpy(&item, d + 8, 4);
    std::lock_guard lock(g_mutex);
    if (round != g.round || (g.phase != kFishing && g.phase != kSettle)) return;
    if (caught > g.their_caught) {
        const Fish* f = fish(item);
        const std::string who = g_who.empty() ? std::string("Your friend") : g_who;
        g.toast = f ? std::format("Fishing contest\n{} caught {}  +{}", who, a_or_an(f->name), f->points)
                    : std::format("Fishing contest\n{} caught a fish", who);
    }
    g.theirs = score;
    g.their_caught = caught;
}

FishingTick fishing_tick(bool on, bool connected, const std::string& who) {
    FishingTick out;
    const uint32_t now = GetTickCount();
    std::vector<uint8_t> msg;
    {
        std::lock_guard lock(g_mutex);
        g_who = who;
        if (!on || (!connected && !g_test)) {
            if (g.phase != kIdle) log::info("Fishing: over");
            g = State{};
            return out;
        }
        switch (g.phase) {
            case kIdle:
                g.phase = kCountdown, g.since = now, g.round = 1;
                break;
            case kCountdown:
                if (now - g.since >= kCountdownMs) {
                    g.phase = kFishing, g.since = now;
                    log::info("Fishing: round {} on", g.round);
                }
                break;
            case kFishing:
                if (now - g.since >= round_ms()) g.phase = kSettle, g.since = now;
                break;
            case kSettle:
                if (now - g.since >= kSettleMs) {
                    g.phase = kOver, g.since = now;
                    g.result = g.ours > g.theirs ? 1 : g.ours < g.theirs ? 2 : 3;
                    log::info("Fishing: round {} ends {} - {} ({} / {} fish)", g.round, g.ours, g.theirs, g.caught,
                              g.their_caught);
                }
                break;
            case kOver:
                if (now - g.since >= kOverMs) {
                    const uint32_t next = g.round + 1;
                    g = State{};
                    g.phase = kCountdown, g.since = now, g.round = next;
                }
                break;
        }
        if (g.send) {
            g.send = false;
            msg.resize(12);
            const auto score = static_cast<uint16_t>(g.ours), caught = static_cast<uint16_t>(g.caught);
            std::memcpy(msg.data(), &g.round, 4);
            std::memcpy(msg.data() + 4, &score, 2);
            std::memcpy(msg.data() + 6, &caught, 2);
            std::memcpy(msg.data() + 8, &g.last_item, 4);
        }
        if (!g.toast.empty()) out.toast = std::exchange(g.toast, {});
        if (g.phase == kOver && !g.reported) {
            g.reported = true;
            out.round_result = g.result;
        }
        if (g.phase == kFishing || g.phase == kSettle) {
            const uint32_t left = g.phase == kFishing ? (round_ms() - std::min(round_ms(), now - g.since)) / 1000 : 0;
            out.banner = std::format("Fishing contest vs {}   {}:{:02}   You {} ({} fish)  -  {} {} ({} fish)", who,
                                     left / 60, left % 60, g.ours, g.caught, who, g.theirs, g.their_caught);
        }
    }
    if (!msg.empty() && connected) activity_send(kFishType, msg);
    return out;
}

FishingHud fishing_hud() {
    FishingHud h;
    std::lock_guard lock(g_mutex);
    const uint32_t t = GetTickCount() - g.since;
    if (g.phase == kCountdown) {
        h.title = std::to_string(std::max<uint32_t>(1, (kCountdownMs - std::min(kCountdownMs, t) + 999) / 1000));
        h.subtitle = "Fishing contest: 6 minutes, keep what you catch. River fish 1, sea fish 2, legendary fish 5";
        h.color = 0xFF82C8E8;
        h.alpha = 1.0f - static_cast<float>(t % 1000) / 1600.0f;
    } else if (g.phase == kFishing && t < 1500) {
        h.title = "Cast Your Lines!";
        h.color = 0xFF82C8E8;
        h.alpha = 1.0f - static_cast<float>(t) / 1500.0f;
    } else if (g.phase == kOver) {
        h.title = g.result == 1 ? "Victory" : g.result == 2 ? "Defeat" : "An Even Catch";
        h.subtitle = g.result == 3 ? std::string("Another round to settle it")
                                   : std::format("{} points ({} fish) to {} ({} fish)", g.ours, g.caught, g.theirs,
                                                 g.their_caught);
        h.color = g.result == 2 ? 0xFF2830C4 : 0xFF82C8E8;
        h.alpha = t < kOverMs - 1000 ? 1.0f : std::max(0.0f, (kOverMs - t) / 1000.0f);
    }
    return h;
}

}  // namespace cdmp::net
