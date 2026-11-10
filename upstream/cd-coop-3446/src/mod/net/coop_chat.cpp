#include "mod/net/coop_chat.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <deque>
#include <format>
#include <mutex>

#include "mod/input/actions.h"
#include "mod/log.h"
#include "mod/net/coop_activity.h"

namespace cdmp::net {
namespace {

constexpr uint8_t kChatType = 22;
constexpr size_t kMaxText = 120;
constexpr uint32_t kQuickMs = 8000;
constexpr uint32_t kLogMs = 15000;
constexpr size_t kLogLines = 4;
constexpr std::array<const char*, 9> kQuick = {"Go here", "Danger here!", "Loot here", "Follow me",
                                               "Wait for me",  "Help!",        "Thanks!",   "Ready?",
                                               "Hold still: photo!"};  // Co-op+ (T44): the game's photo mode
constexpr int kHereCount = 3;  // the first three drop a map pin

std::mutex g_mutex;
std::atomic<bool> g_open{false};  // the text line is open (the hook reads it)
std::string g_text;
bool g_send = false, g_cancel = false;
std::atomic<bool> g_quick{false};
std::atomic<int> g_quick_step{0};
int g_quick_index = 0;
uint32_t g_quick_since = 0;
struct Line {
    uint32_t at;
    std::string text;
};
std::deque<Line> g_log;
std::deque<std::string> g_incoming;  // from the event, shown by the tick

std::string sanitize(std::string s) {
    for (char& c : s) {
        if (static_cast<unsigned char>(c) < 0x20) c = ' ';
    }
    if (s.size() > kMaxText) s.resize(kMaxText);
    return s;
}

void add_log(const std::string& text, uint32_t now) {
    g_log.push_back({now, text});
    while (g_log.size() > kLogLines) g_log.pop_front();
}

}  // namespace

bool chat_quick_open() { return g_quick.load(); }
void chat_quick_step(int delta) { g_quick_step.fetch_add(delta); }

namespace {
std::atomic<int> g_quick_send{0};  // index + 1 asked for by the Co-op tab
}
const char* chat_quick_name(int index) {
    return index >= 0 && index < static_cast<int>(kQuick.size()) ? kQuick[index] : nullptr;
}
void chat_send_quick(int index) {
    if (chat_quick_name(index)) g_quick_send = index + 1;
}

std::atomic<uint64_t> g_taken[4];  // key-downs we took (by vk bit): only their key-ups are ours too
std::atomic<bool> g_open_request{false};  // Shift+F2 seen by the hook (not GetAsyncKeyState: unreliable here, chat2-4)
std::atomic<bool> g_shift{false};         // Shift as the hook saw it

bool chat_hook_key(uint32_t vk, uint32_t scan, bool down) {
    const uint64_t bit = 1ull << (vk & 63);
    std::atomic<uint64_t>& taken = g_taken[(vk >> 6) & 3];
    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) g_shift = down;
    if (!down) {
        // Only the release of a key whose press we took.
        return (taken.fetch_and(~bit) & bit) != 0;
    }
    if (!g_open.load()) {
        // The Chat action's own key (Shift+F2): only noted, never swallowed. Swallowing half of a press leaves the
        // key "held" for GetAsyncKeyState, and a held F2 + Ctrl is the Leave action (chat6: Ctrl+F3 left the session).
        const auto& b = input::binding(input::Action::Chat);
        if (vk == static_cast<uint32_t>(b.vk) && g_shift.load() == b.shift && !(GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            g_open_request = true;
        }
        return false;
    }
    if (vk == VK_F2) return false;  // the same rule while the line is open
    taken.fetch_or(bit);
    std::lock_guard lock(g_mutex);
    if (vk == VK_RETURN) {
        g_send = true;
    } else if (vk == VK_ESCAPE) {
        g_cancel = true;
    } else if (vk == VK_BACK) {
        if (!g_text.empty()) g_text.pop_back();
    } else if (g_text.size() < kMaxText) {
        BYTE state[256] = {};
        if (GetKeyState(VK_SHIFT) & 0x8000) state[VK_SHIFT] = 0x80;
        if (GetKeyState(VK_CAPITAL) & 1) state[VK_CAPITAL] = 1;
        wchar_t w[4] = {};
        const int n = ToUnicode(vk, scan, state, w, 4, 0);
        if (n == 1 && w[0] >= 0x20 && w[0] < 0x7F) g_text += static_cast<char>(w[0]);
    }
    return true;
}

void chat_on_event(const uint8_t* d, size_t n) {
    if (n < 1) return;
    std::string text;
    if (d[0] == 0) {
        text = sanitize(std::string(reinterpret_cast<const char*>(d + 1), n - 1));
    } else if (d[0] <= kQuick.size()) {
        text = kQuick[d[0] - 1];
    }
    if (text.empty()) return;
    std::lock_guard lock(g_mutex);
    if (g_incoming.size() < 16) g_incoming.push_back(std::move(text));
}

ChatView chat_tick(bool connected, const std::string& name, bool focused, uint32_t now) {
    ChatView v;
    const std::string who = name.empty() ? std::string("Your friend") : name;
    static input::Edge ping_key;
    const bool chat_pressed = g_open_request.exchange(false) && focused;
    const bool ping_down = input::action_down(input::Action::Ping, focused);
    const bool ping_pressed = ping_key(ping_down);

    std::vector<uint8_t> out;
    {
        std::lock_guard lock(g_mutex);
        // Incoming.
        while (!g_incoming.empty()) {
            const std::string line = who + ": " + g_incoming.front();
            add_log(line, now);
            v.toast = "Co-op\n" + line;
            g_incoming.pop_front();
        }
        if (!connected) {
            g_open = false;
            g_quick = false;
            g_text.clear();
        }
        // The text line.
        if (connected && chat_pressed && !g_open.load() && !g_quick.load()) {
            log::info("Chat: line opened");
            g_open = true;
            g_text.clear();
            g_send = g_cancel = false;
        }
        if (g_open.load()) {
            if (g_cancel || (g_send && g_text.empty())) {
                g_open = false;
            } else if (g_send) {
                out.push_back(0);
                out.insert(out.end(), g_text.begin(), g_text.end());
                add_log("You: " + g_text, now);
                log::info("Chat: we said \"{}\"", g_text);
                g_open = false;
            } else {
                v.input = "Say: " + g_text + "_      Enter send   Esc cancel";
            }
            g_send = g_cancel = false;
        }
        // A quick message chosen in the menu's Co-op tab.
        if (const int q = g_quick_send.exchange(0); q && connected) {
            out.push_back(static_cast<uint8_t>(q));
            add_log(std::string("You: ") + kQuick[q - 1], now);
            v.ping = q - 1 < kHereCount;
            if (v.toast.empty()) v.toast = std::string("Co-op\nYou: ") + kQuick[q - 1];
            log::info("Chat: quick message \"{}\" (Co-op tab)", kQuick[q - 1]);
        } else if (connected && ping_pressed && !g_open.load()) {
            // The quick list.
            if (!g_quick.load()) {
                g_quick = true;
                g_quick_index = 0;
                g_quick_since = now;
                g_quick_step = 0;
            } else {
                const int i = g_quick_index;
                out.push_back(static_cast<uint8_t>(i + 1));
                add_log(std::string("You: ") + kQuick[i], now);
                v.ping = i < kHereCount;
                g_quick = false;
                log::info("Chat: quick message \"{}\"", kQuick[i]);
            }
        }
        if (g_quick.load()) {
            const int step = g_quick_step.exchange(0);
            if (step) {
                const int n = static_cast<int>(kQuick.size());
                g_quick_index = (g_quick_index + step % n + n) % n;
                g_quick_since = now;
            }
            if (now - g_quick_since > kQuickMs) {
                g_quick = false;
            } else {
                std::string list;
                for (int i = 0; i < static_cast<int>(kQuick.size()); ++i) {
                    list += i == g_quick_index ? std::format("  {{*}}{}{{*}}", kQuick[i]) : std::format("  {}", kQuick[i]);
                }
                v.quick = "Message:" + list + "      {MenuPrev}{MenuNext} choose   {Ping} send";
            }
        }
        // The log.
        while (!g_log.empty() && now - g_log.front().at > kLogMs) g_log.pop_front();
        for (const auto& l : g_log) v.log.push_back(l.text);
    }
    if (!out.empty()) activity_send(kChatType, out);
    return v;
}

}  // namespace cdmp::net
