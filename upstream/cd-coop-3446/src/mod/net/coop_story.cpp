#include "mod/net/coop_story.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <format>
#include <vector>

#include "mod/log.h"
#include "mod/net/coop_activity.h"

namespace cdmp::net {
namespace {

constexpr uint8_t kStoryType = 30;  // Activity event type: u8 op | data
constexpr uint8_t kOpShown = 1, kOpPress = 2;
constexpr uint32_t kPollMs = 250, kResendMs = 2000, kStaleMs = 6000;

using StateFn = int (*)(uint8_t*, int);
using PressFn = int (*)(int);
StateFn g_state = nullptr;
PressFn g_press = nullptr;
bool g_looked = false;

std::atomic<int> g_shown{0};          // ours
std::atomic<int> g_from_host{0};      // the guest: what the host said is ours
std::atomic<uint32_t> g_from_host_at{0};
std::atomic<bool> g_answer{false};    // the HUD's hold fired
std::atomic<bool> g_friend_pressed{false};  // the host: the guest pressed

bool resolve() {
    if (!g_looked) {
        g_looked = true;
        if (HMODULE m = GetModuleHandleW(L"cdmp.asi")) {
            g_state = reinterpret_cast<StateFn>(GetProcAddress(m, "cdmp_story_gate_state"));
            g_press = reinterpret_cast<PressFn>(GetProcAddress(m, "cdmp_story_gate_press"));
        }
    }
    return g_state && g_press;
}

const char* shown_name(int s) {
    switch (s) {
        case 1: return "we wait alone";
        case 2: return "both here";
        case 3: return "we are ready";
        case 4: return "the friend waits for us";
        default: return "none";
    }
}

}  // namespace

int story_shown() { return g_shown.load(); }
void story_answer() { g_answer.store(true); }

void story_on_event(const uint8_t* d, size_t n) {
    if (n < 1) return;
    if (d[0] == kOpShown && n >= 2) {
        g_from_host.store(d[1]);
        g_from_host_at.store(GetTickCount() | 1);
    } else if (d[0] == kOpPress) {
        g_friend_pressed.store(true);
    }
}

StoryTick story_tick(bool connected, bool host, const std::string& name, uint32_t now) {
    StoryTick out;
    const std::string who = name.empty() ? std::string("Your friend") : name;
    int shown = 0;
    if (!connected) {
        g_from_host.store(0);
        g_answer.store(false);
        g_friend_pressed.store(false);
    } else if (host) {
        static uint32_t polled_at = 0, sent_at = 0;
        static int sent = -1;
        static int ours = 0, theirs = 0;
        if (resolve() && now - polled_at >= kPollMs) {
            polled_at = now;
            uint8_t st[52] = {};
            if (g_state(st, sizeof st) >= 52) {
                ours = st[0];
                theirs = st[1];
            } else {
                ours = theirs = 0;
            }
        }
        if (g_press) {
            if (g_answer.exchange(false)) log::info("Story: we pressed (released: {})", g_press(0));
            if (g_friend_pressed.exchange(false)) log::info("Story: the friend pressed (released: {})", g_press(1));
        }
        if (theirs != sent || (theirs && now - sent_at >= kResendMs)) {
            if (theirs != sent) log::info("Story: the friend's screen now: {}", shown_name(theirs));
            sent = theirs;
            sent_at = now;
            activity_send(kStoryType, {kOpShown, static_cast<uint8_t>(theirs)});
        }
        shown = ours;
    } else {
        if (g_answer.exchange(false)) {
            activity_send(kStoryType, {kOpPress});
            log::info("Story: we pressed (sent to the host)");
        }
        const uint32_t at = g_from_host_at.load();
        shown = at && GetTickCount() - at < kStaleMs ? g_from_host.load() : 0;
    }
    if (shown != g_shown.load()) log::info("Story: our screen now: {}", shown_name(shown));
    g_shown.store(shown);
    switch (shown) {
        case 1: out.line = std::format("Story: {} is not here yet. The scene waits for both of you", who); break;
        case 2: out.line = std::format("Story: {} is here. Start the scene together", who); break;
        case 3: out.line = std::format("Story: ready. Waiting for {}", who); break;
        case 4: out.line = std::format("Story: {} is waiting for you to start a scene together", who); break;
        default: break;
    }
    return out;
}

}  // namespace cdmp::net
