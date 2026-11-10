#include "mod/net/coop_activity.h"
#include "mod/net/coop_travel.h"
#include "mod/net/coop_chat.h"
#include "mod/render/coop_page.h"

#include <windows.h>

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <format>
#include <functional>
#include <mutex>
#include <thread>

#include "mod/game/main_menu.h"
#include "mod/game/ui_state.h"
#include "mod/input/actions.h"
#include "mod/input/pad_input.h"
#include "mod/input/game_input_block.h"
#include "mod/log.h"
#include "mod/render/game_ui.h"
#include "mod/render/hud.h"
#include "mod/settings_menu.h"
#include "mod/net/coop_lobby.h"
#include "mod/net/direct_ip.h"
#include "mod/native_layer.h"
#include "mod/net/steam_join.h"
#include "mod/net/troc_sniffer.h"

namespace cdmp::render {
namespace {

// The game's look (game_ui.cpp): warm gold, light text, dim text, thin light rules.
constexpr ImU32 kGold = IM_COL32(232, 200, 130, 255);
constexpr ImU32 kText = IM_COL32(232, 228, 220, 255);
constexpr ImU32 kDim = IM_COL32(170, 164, 152, 255);
// The pause menu's tab row (1920x1080 positions): the five game tabs and ours after them.
constexpr float kTabX[] = {166, 324, 484, 644, 804, 980};
constexpr float kTabY = 50, kTabSize = 21;
constexpr ImU32 kRule = IM_COL32(205, 195, 175, 90);
constexpr ULONG_PTR kOurInput = 0xC0C0A7;  // input we send ourselves (coop_worlds / coop_activity mark it so)

const char* const kSections[] = {"Session", "Saves & Story", "Combat & Loot", "Reviving", "World",
                                 "Display", "Challenges", "Controls", "Status"};
constexpr int kSectionCount = static_cast<int>(std::size(kSections));

std::atomic<bool> g_open{false}, g_details{false};
std::atomic<bool> g_lobby_shown{false};  // the title lobby is on screen (its keys and clicks are ours)
int g_lobby_sel = 0;
// Co-op+ (2026-11-07, Plitos's handoff): the lobby's "Radmin VPN / Direct IP" page (Host, Join) is open; its last
// message; the invite already put on the clipboard (copied once per new invite).
bool g_direct_page = false;
std::string g_direct_note;
std::string g_direct_copied;
std::atomic<HWND> g_hwnd{nullptr};

// Input that the hook threads take for the page, read by the render thread.
std::mutex g_in_mutex;
std::deque<unsigned> g_keys;
std::deque<POINT> g_clicks;  // screen coordinates
int g_wheel = 0;
// The tab's rectangle on screen while it is drawn (a click on it opens the page).
std::atomic<long> g_tab_l{0}, g_tab_t{0}, g_tab_r{0}, g_tab_b{0};
std::atomic<bool> g_tab_shown{false};
std::atomic<bool> g_swallow_up{false};
bool g_steam_overlay = false;  // the Steam overlay is open this frame (draw thread only)

int g_section = 0, g_row = 0, g_top = 0;
std::atomic<bool> g_focus_rows{false};  // keyboard / pad: Up / Down move in the rows (true) or the sections (false)

// ---- the page as the pause menu's sixth tab (after "Others", before the row wraps round to "Map")
// The game's own tab keys (RB / LB, and the keyboard's) reach the game; its tab switch (game/main_menu.h) asks
// tab_filter first. Stepping past either end of the row lands on Co-op and the game stays on the page it was on;
// stepping on from Co-op goes to the far side (let through) or back (the game is already there: only we close).
std::atomic<bool> g_as_tab{false};       // the open page is the menu's tab (not the title's settings, not F8)
std::atomic<uint64_t> g_pad_step_ms{0};  // last frame the pad's LB or RB was down
std::atomic<uint64_t> g_key_ms{0};       // last key down
std::atomic<unsigned> g_key_vk{0};

void open_as_tab(const char* how) {
    g_focus_rows = false;
    {
        std::lock_guard lock(g_in_mutex);
        g_keys.clear();
        g_clicks.clear();
        g_wheel = 0;
    }
    g_as_tab = true;
    g_open = true;
    log::info("Co-op page opened as the menu's tab ({})", how);
}

void close_tab(const char* how) {
    if (!g_open.exchange(false)) return;
    g_as_tab = false;
    log::info("Co-op page closed ({})", how);
}

bool tab_filter(int from, int to) {
    if (from < 0) {  // the game opened a page by name (a shortcut key): never stopped
        if (g_open.load() && g_as_tab.load()) close_tab("the game opened a page");
        return false;
    }
    const bool wrap = (from == game::kMenuPageCount - 1 && to == 0) || (from == 0 && to == game::kMenuPageCount - 1);
    if (g_open.load()) {
        if (!g_as_tab.load()) return false;
        const bool back = std::abs(from - to) == 1;  // the page under us is where this step should end
        close_tab(wrap ? "on to the next tab" : back ? "back to the tab before" : "another menu page");
        return back;
    }
    const uint64_t now = GetTickCount64();
    const bool pad = now - g_pad_step_ms.load() < 400, key = now - g_key_ms.load() < 400;
    if (!wrap || !g_tab_shown.load()) return false;
    if (!pad && !key) return false;  // a click on the game's tab, or the game's own doing
    log::info("Menu tab step {} -> {} ({}, key 0x{:X}): Co-op", from, to, pad ? "pad" : "keyboard", g_key_vk.load());
    open_as_tab(pad ? "pad" : "keyboard");
    return true;
}

bool game_in_front() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

LRESULT CALLBACK mouse_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && game_in_front() && !net::join_overlay_active()) {
        const auto* m = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        if (m->dwExtraInfo != kOurInput) {
            const bool down = wp == WM_LBUTTONDOWN, up = wp == WM_LBUTTONUP;
            if (g_open.load() || g_lobby_shown.load()) {
                if (down || up || wp == WM_RBUTTONDOWN || wp == WM_RBUTTONUP || wp == WM_MOUSEWHEEL) {
                    std::lock_guard lock(g_in_mutex);
                    if (down && g_clicks.size() < 16) g_clicks.push_back(m->pt);
                    if (wp == WM_MOUSEWHEEL) g_wheel += static_cast<short>(HIWORD(m->mouseData)) / WHEEL_DELTA;
                    if (wp == WM_RBUTTONDOWN) g_keys.push_back(VK_ESCAPE);  // right click: back, as in the game
                    return 1;
                }
            } else if (down && g_tab_shown.load() && m->pt.x >= g_tab_l && m->pt.x <= g_tab_r && m->pt.y >= g_tab_t &&
                       m->pt.y <= g_tab_b) {
                std::lock_guard lock(g_in_mutex);
                g_clicks.push_back(m->pt);
                g_swallow_up = true;
                return 1;
            } else if (up && g_swallow_up.exchange(false)) {
                return 1;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void start_mouse_hook() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread([] {
            if (!SetWindowsHookExW(WH_MOUSE_LL, mouse_hook, GetModuleHandleW(nullptr), 0)) {
                log::warn("Co-op page: mouse hook not installed ({}); the page works with keys and pads", GetLastError());
                return;
            }
            MSG m;
            while (GetMessageW(&m, nullptr, 0, 0) > 0) {
            }
        }).detach();
    });
}

// ---- the page's rows
struct Row {
    enum Kind { Setting, Button, Info } kind = Info;
    std::string label, value, help;
    bool live = true;
    std::function<void(int)> act;  // Setting: change by delta; Button: press (delta ignored)
};

std::string connection_text(const HudData& d) {
    if (!d.connected) return "not connected";
    std::string s = std::format("{:.0f} ms", d.rtt_ms);
    if (d.loss_pct >= 0) s += std::format(", {:.1f} % loss", d.loss_pct);
    return s;
}

Row button(const std::string& label, input::Action a, const std::string& help) {
    // Shared world: no hotkeys, so no key after the row's name.
    Row r{Row::Button, label, kNativeLayer ? std::string() : "{" + std::string(input::binding(a).name) + "}", help,
          true, nullptr};
    r.act = [a](int) { input::trigger(a); };
    return r;
}

int g_quick_pick = 0;  // the quick message chosen in the Session rows (draw thread)

std::vector<Row> rows_for(int section, const HudData& d, const std::vector<std::string>& status) {
    std::vector<Row> rows;
    const std::string name = kSections[section];
    if (name == "Session") {
        rows.push_back({Row::Info, "Playing with", d.connected ? d.peer_name : "nobody yet", "", true, nullptr});
        rows.push_back({Row::Info, "Connection", connection_text(d), "", true, nullptr});
        // Shared world: friends are invited from the title screen's co-op menu only.
        if (!kNativeLayer) {
            rows.push_back(button("Invite a friend", input::Action::Invite,
                                  "Opens Steam's invite list: your friend accepts it and joins your world."));
        }
        if (kNativeLayer) {
            // What the friend asked for, first: the same answers as holding interact gives.
            const std::string who = d.peer_name.empty() ? std::string("your friend") : d.peer_name;
            const net::ChallengeView ch = net::activity_challenge_view();
            if (ch.state == 1) {
                rows.push_back(button("Accept the challenge: " + ch.game, input::Action::Challenge,
                                      who + " challenged you. It starts when you accept."));
                rows.push_back(button("Decline the challenge", input::Action::Decline, "Tells " + who + " no."));
            } else if (ch.state == 2) {
                rows.push_back(button("Withdraw my challenge: " + ch.game, input::Action::Challenge,
                                      "Takes the challenge back before " + who + " answers."));
            } else if (ch.state == 3) {
                rows.push_back(button("End the challenge: " + ch.game, input::Action::Challenge,
                                      "Stops the challenge for both of you."));
            }
            if (const int t = net::travel_request(); t == 1) {
                rows.push_back(button("Go to " + who + " (they called you)", input::Action::GoToFriend,
                                      "Takes you to their side."));
                rows.push_back(button("Stay here", input::Action::Decline, "Tells " + who + " you are staying."));
            } else if (t == 2) {
                rows.push_back(button("Follow " + who, input::Action::GoToFriend,
                                      "They travelled far: takes you to their side."));
            }
            rows.push_back(button("Go to my friend", input::Action::GoToFriend, "Takes you to your friend's side."));
            rows.push_back(button("Bring my friend to me", input::Action::Summon,
                                  "Asks your friend to come to you; they accept or decline."));
            if (d.connected) {
                const char* q = net::chat_quick_name(g_quick_pick);
                if (!q) q = net::chat_quick_name(g_quick_pick = 0);
                Row pick{Row::Setting, "Quick message", q, "Left / right choose the message; the row below sends it.",
                         true, nullptr};
                pick.act = [](int delta) {
                    int n = 0;
                    while (net::chat_quick_name(n)) ++n;
                    g_quick_pick = ((g_quick_pick + (delta == 0 ? 1 : delta)) % n + n) % n;
                };
                rows.push_back(pick);
                Row send{Row::Button, std::string("Send: ") + q, "",
                         "Your friend sees it as a notice. \"Go here\", \"Danger here!\" and \"Loot here\" also put "
                         "a pin on their map where you stand.",
                         true, nullptr};
                send.act = [](int) { net::chat_send_quick(g_quick_pick); };
                rows.push_back(send);
            }
            rows.push_back(button("Resynchronise", input::Action::Resync,
                                  "Joins again from scratch when things drift apart (both characters are sent again)."));
            rows.push_back(button("Leave the session", input::Action::Leave,
                                  "Ends the session: you go back to the title screen, nothing of your friend stays."));
            return rows;
        }
        rows.push_back(button("Leave the session", input::Action::Leave,
                              "Ends the session: you go back to your own world, nothing of your friend stays."));
        rows.push_back(button("Go to my friend", input::Action::GoToFriend, "Takes you to your friend's side."));
        rows.push_back(button("Bring my friend to me", input::Action::Summon,
                              "Asks your friend to come to you; they accept or decline."));
        rows.push_back(button("Resynchronise", input::Action::Resync,
                              "Joins again from scratch when things drift apart (both characters are sent again)."));
    } else if (name == "Controls" && kNativeLayer) {
        // Shared world: everything is on the friend (hold interact) or in this tab; no hotkeys to list.
        const std::string more = "Optional shortcuts: [keys] in coopplus.ini (README).";
        rows.push_back({Row::Info, "Interact with your friend (challenges)", "Hold {Interact} on them", more, true, nullptr});
        rows.push_back({Row::Info, "Answer a challenge or a call", "Hold {Interact}", more, true, nullptr});
        rows.push_back({Row::Info, "Revive a downed friend", "Hold {Interact} by them", more, true, nullptr});
        rows.push_back({Row::Info, "In the interaction menu",
                        "{MenuUp}{MenuDown} choose   {MenuSelect} select   {MenuBack} leave", more, true, nullptr});
        if (input::binding(input::Action::Chat).vk) {
            rows.push_back({Row::Info, "Type a chat line (keyboard)", "{Chat}", more, true, nullptr});
        }
        rows.push_back({Row::Info, "Go to your friend, call them, quick messages, leave", "Session, in this tab", more,
                        true, nullptr});
        rows.push_back(button("Show the controls card", input::Action::Help, "Shows the short co-op guide in the world."));
    } else if (name == "Controls") {
        static const std::pair<const char*, input::Action> kList[] = {
            {"Co-op page", input::Action::Panel},        {"Invite a friend", input::Action::Invite},
            {"Leave", input::Action::Leave},             {"Ping / quick messages", input::Action::Ping},
            {"Go to my friend", input::Action::GoToFriend}, {"Bring my friend to me", input::Action::Summon},
            {"Challenge / accept", input::Action::Challenge}, {"Decline", input::Action::Decline},
            {"Revive my friend (hold)", input::Action::ReviveFriend}, {"Chat", input::Action::Chat},
            {"Name tags", input::Action::Nameplates},    {"Resynchronise", input::Action::Resync},
            {"Controls card", input::Action::Help}};
        for (const auto& [label, a] : kList) {
            if (kNativeLayer && a == input::Action::Invite) continue;
            rows.push_back({Row::Info, label, "{" + std::string(input::binding(a).name) + "}",
                            "Change keys under [keys] in cdmp.ini (README).", true, nullptr});
        }
    } else if (name == "Status") {
        Row det{Row::Setting, "Status panel (top left)", g_details.load() ? "shown" : "hidden",
                "The detailed connection panel, for reporting a problem.", true, nullptr};
        det.act = [](int) { g_details = !g_details.load(); };
        rows.push_back(det);
        for (const auto& s : status) rows.push_back({Row::Info, s, "", "", true, nullptr});
    } else {
        const auto items = settings::page_rows(name);
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            Row r{Row::Setting, items[i].label, items[i].value, items[i].help, items[i].live, nullptr};
            r.act = [name, i](int delta) { settings::page_change(name, i, delta == 0 ? 1 : delta); };
            rows.push_back(std::move(r));
        }
    }
    return rows;
}

// ---- drawing helpers (1920x1080 positions; the game's menu scales with the height and stays centred)
struct Space {
    float w, h, s;
    float x(float v) const { return w * 0.5f + (v - 960.0f) * s; }
    float y(float v) const { return v * s; }
};

float text(ImDrawList* dl, float x, float y, float size, const std::string& t, ImU32 c, bool bold = false) {
    return ui::rich_text(dl, x, y, size, t, c, bold ? ui::serif_bold() : ui::serif());
}
float text_w(float size, const std::string& t, bool bold = false) {
    return ui::rich_text(nullptr, 0, 0, size, t, 0, bold ? ui::serif_bold() : ui::serif());
}

// A light key cap with a caption after it ("Esc Close"), right to left from `right`; returns the new right edge.
float hint(ImDrawList* dl, float right, float y, float size, const std::string& key, const std::string& caption) {
    const float cw = text_w(size, caption);
    const float kw = text_w(size * 0.85f, key, true) + size * 0.7f;
    const float h = size * 1.2f;
    text(dl, right - cw, y, size, caption, kText);
    const float kx = right - cw - size * 0.4f - kw;
    dl->AddRectFilled(ImVec2(kx, y - size * 0.1f), ImVec2(kx + kw, y - size * 0.1f + h), IM_COL32(214, 210, 202, 235),
                      size * 0.18f);
    text(dl, kx + size * 0.35f, y + size * 0.05f, size * 0.85f, key, IM_COL32(28, 26, 24, 255), true);
    return kx - size * 1.1f;
}

void wrap(ImDrawList* dl, float x, float y, float width, float size, const std::string& t, ImU32 c) {
    std::string line, word;
    float yy = y;
    auto flush = [&] {
        if (!line.empty()) text(dl, x, yy, size, line, c);
        yy += size * 1.3f;
        line.clear();
    };
    for (size_t i = 0; i <= t.size(); ++i) {
        if (i == t.size() || t[i] == ' ') {
            const std::string cand = line.empty() ? word : line + " " + word;
            if (!line.empty() && text_w(size, cand) > width) {
                flush();
                line = word;
            } else {
                line = cand;
            }
            word.clear();
        } else {
            word += t[i];
        }
    }
    flush();
}

bool inside(const ImVec2& p, float l, float t, float r, float b) { return p.x >= l && p.x <= r && p.y >= t && p.y <= b; }

// ---- the title lobby (net/coop_lobby.h): the co-op main menu before anyone plays, in the look of the game's own
// dialogs (its difficulty choice): a centred band with light rules, a header line, option cards with an emblem on the
// left, the chosen one framed in gold.
enum class Emblem { Solo, Invite, Coop, Settings, NewSave, FromSolo, Continue, Direct };

void draw_emblem(ImDrawList* dl, float cx, float cy, float r, Emblem e, ImU32 c) {
    // a diamond plate with a symbol, like the difficulty cards' crests
    dl->AddQuadFilled(ImVec2(cx, cy - r), ImVec2(cx + r, cy), ImVec2(cx, cy + r), ImVec2(cx - r, cy), IM_COL32(58, 48, 36, 255));
    dl->AddQuad(ImVec2(cx, cy - r), ImVec2(cx + r, cy), ImVec2(cx, cy + r), ImVec2(cx - r, cy), IM_COL32(150, 120, 80, 200), 1.2f);
    const float s = r * 0.55f, w = std::max(1.5f, r * 0.09f);
    auto sword = [&](float x, float y, float ang) {
        const float dx = std::sin(ang), dy = -std::cos(ang);
        dl->AddLine(ImVec2(x - dx * s, y - dy * s), ImVec2(x + dx * s, y + dy * s), c, w);
        const float gx = x - dx * s * 0.45f, gy = y - dy * s * 0.45f;
        dl->AddLine(ImVec2(gx - dy * s * 0.35f, gy + dx * s * 0.35f), ImVec2(gx + dy * s * 0.35f, gy - dx * s * 0.35f), c, w);
    };
    switch (e) {
        case Emblem::Solo: sword(cx, cy, 0.0f); break;
        case Emblem::Coop: sword(cx, cy, 0.62f); sword(cx, cy, -0.62f); break;
        case Emblem::Invite:
            dl->AddLine(ImVec2(cx - s, cy), ImVec2(cx + s, cy), c, w);
            dl->AddLine(ImVec2(cx, cy - s), ImVec2(cx, cy + s), c, w);
            break;
        case Emblem::Settings:
            dl->AddCircle(ImVec2(cx, cy), s * 0.75f, c, 12, w);
            for (int i = 0; i < 6; ++i) {
                const float a = i * 1.0472f;
                dl->AddLine(ImVec2(cx + std::cos(a) * s * 0.75f, cy + std::sin(a) * s * 0.75f),
                            ImVec2(cx + std::cos(a) * s * 1.05f, cy + std::sin(a) * s * 1.05f), c, w);
            }
            break;
        case Emblem::NewSave:
            dl->AddRect(ImVec2(cx - s * 0.6f, cy - s * 0.8f), ImVec2(cx + s * 0.6f, cy + s * 0.8f), c, 0.0f, w);
            dl->AddLine(ImVec2(cx - s * 0.3f, cy), ImVec2(cx + s * 0.3f, cy), c, w);
            dl->AddLine(ImVec2(cx, cy - s * 0.3f), ImVec2(cx, cy + s * 0.3f), c, w);
            break;
        case Emblem::FromSolo:
            dl->AddCircle(ImVec2(cx, cy - s * 0.35f), s * 0.3f, c, 10, w);
            dl->AddBezierQuadratic(ImVec2(cx - s * 0.6f, cy + s * 0.7f), ImVec2(cx, cy - s * 0.1f), ImVec2(cx + s * 0.6f, cy + s * 0.7f), c, w);
            break;
        case Emblem::Continue:
            dl->AddTriangleFilled(ImVec2(cx - s * 0.45f, cy - s * 0.6f), ImVec2(cx + s * 0.6f, cy), ImVec2(cx - s * 0.45f, cy + s * 0.6f), c);
            break;
        case Emblem::Direct:  // two linked nodes: a straight line between two PCs
            dl->AddCircle(ImVec2(cx - s * 0.55f, cy + s * 0.35f), s * 0.28f, c, 10, w);
            dl->AddCircle(ImVec2(cx + s * 0.55f, cy - s * 0.35f), s * 0.28f, c, 10, w);
            dl->AddLine(ImVec2(cx - s * 0.33f, cy + s * 0.2f), ImVec2(cx + s * 0.33f, cy - s * 0.2f), c, w);
            break;
    }
}

void draw_crown(ImDrawList* dl, float x, float y, float h, ImU32 c) {
    // three points on a band: the host
    const float w = h * 1.3f;
    const ImVec2 pts[] = {ImVec2(x, y + h), ImVec2(x, y + h * 0.3f), ImVec2(x + w * 0.25f, y + h * 0.62f), ImVec2(x + w * 0.5f, y),
                          ImVec2(x + w * 0.75f, y + h * 0.62f), ImVec2(x + w, y + h * 0.3f), ImVec2(x + w, y + h)};
    dl->AddConvexPolyFilled(pts, 3, c);
    dl->AddConvexPolyFilled(pts + 2, 3, c);
    dl->AddConvexPolyFilled(pts + 4, 3, c);
    dl->AddRectFilled(ImVec2(x, y + h * 0.62f), ImVec2(x + w, y + h), c);
}

struct Card {
    Emblem emblem;
    std::string title, line;
    bool enabled = true;
    std::function<void()> act;
};

// The dialog: a header over the cards; returns the card under the mouse (-1). `sel` is the chosen one.
int draw_cards(ImDrawList* dl, float width, float height, const std::string& header, const std::vector<Card>& cards, int sel,
               ImVec2 mouse, float top_y) {
    const float s = height / 1080.0f, cx = width * 0.5f;
    const float card_w = 640 * s, card_h = 92 * s, gap = 14 * s;
    const float band_t = top_y, band_b = top_y + 96 * s + cards.size() * (card_h + gap) + 30 * s;
    // the band: dark in the middle, fading to the sides, a light rule above and below (the game's dialogs)
    dl->AddRectFilledMultiColor(ImVec2(0, band_t), ImVec2(cx, band_b), IM_COL32(16, 14, 12, 0), IM_COL32(16, 14, 12, 235),
                                IM_COL32(16, 14, 12, 235), IM_COL32(16, 14, 12, 0));
    dl->AddRectFilledMultiColor(ImVec2(cx, band_t), ImVec2(width, band_b), IM_COL32(16, 14, 12, 235), IM_COL32(16, 14, 12, 0),
                                IM_COL32(16, 14, 12, 0), IM_COL32(16, 14, 12, 235));
    for (float y : {band_t, band_b}) {
        dl->AddRectFilledMultiColor(ImVec2(cx - 560 * s, y), ImVec2(cx, y + 1.2f * s), IM_COL32(205, 195, 175, 0),
                                    IM_COL32(205, 195, 175, 150), IM_COL32(205, 195, 175, 150), IM_COL32(205, 195, 175, 0));
        dl->AddRectFilledMultiColor(ImVec2(cx, y), ImVec2(cx + 560 * s, y + 1.2f * s), IM_COL32(205, 195, 175, 150),
                                    IM_COL32(205, 195, 175, 0), IM_COL32(205, 195, 175, 0), IM_COL32(205, 195, 175, 150));
    }
    const float hs = 25 * s;
    text(dl, cx - text_w(hs, header) * 0.5f, band_t + 30 * s, hs, header, kText);
    int over = -1;
    for (size_t i = 0; i < cards.size(); ++i) {
        const Card& c = cards[i];
        const float l = cx - card_w * 0.5f, t = band_t + 86 * s + i * (card_h + gap);
        const bool chosen = static_cast<int>(i) == sel && c.enabled;
        if (inside(mouse, l, t, l + card_w, t + card_h)) over = static_cast<int>(i);
        dl->AddRectFilled(ImVec2(l, t), ImVec2(l + card_w, t + card_h),
                          chosen ? IM_COL32(110, 96, 74, 235) : IM_COL32(40, 37, 33, c.enabled ? 235 : 170));
        dl->AddRect(ImVec2(l, t), ImVec2(l + card_w, t + card_h),
                    chosen ? IM_COL32(232, 200, 130, 255) : IM_COL32(120, 112, 100, c.enabled ? 160 : 80), 0.0f, chosen ? 2.0f : 1.0f);
        const float ex = l + card_h * 0.55f, ey = t + card_h * 0.5f;
        draw_emblem(dl, ex, ey, card_h * 0.34f, c.emblem, c.enabled ? (chosen ? kGold : IM_COL32(220, 210, 190, 255))
                                                                       : IM_COL32(120, 112, 100, 200));
        dl->AddLine(ImVec2(l + card_h * 1.08f, t + 16 * s), ImVec2(l + card_h * 1.08f, t + card_h - 16 * s),
                    IM_COL32(150, 140, 125, 110), 1.0f);
        const ImU32 tc = !c.enabled ? IM_COL32(140, 134, 124, 200) : kText;
        const float tx = l + card_h * 1.3f;
        text(dl, tx, t + (c.line.empty() ? card_h * 0.32f : 16 * s), 30 * s, c.title, tc, true);
        if (!c.line.empty()) text(dl, tx, t + 54 * s, 20 * s, c.line, c.enabled ? kDim : IM_COL32(120, 114, 106, 200));
    }
    return over;
}

// The party in the top right: our name and the friend's, a crown by the host's.
void draw_party(ImDrawList* dl, float width, float height, const net::LobbyView& v) {
    const float s = height / 1080.0f, right = width - 70 * s, top = 92 * s, size = 26 * s;
    text(dl, right - text_w(19 * s, "PARTY", true), top, 19 * s, "PARTY", kGold, true);
    dl->AddRectFilledMultiColor(ImVec2(right - 330 * s, top + 32 * s), ImVec2(right, top + 33.2f * s), IM_COL32(205, 195, 175, 0),
                                IM_COL32(205, 195, 175, 150), IM_COL32(205, 195, 175, 150), IM_COL32(205, 195, 175, 0));
    struct Member {
        std::string name;
        bool host;
        ImU32 colour;
    };
    std::vector<Member> m;
    m.push_back({v.own_name, v.host, kText});
    if (v.connected) m.push_back({v.partner_name, !v.host, ui::partner_colour()});
    std::sort(m.begin(), m.end(), [](const Member& a, const Member& b) { return a.host > b.host; });
    float y = top + 48 * s;
    for (const Member& p : m) {
        const float tw = text_w(size, p.name);
        text(dl, right - tw, y, size, p.name, p.colour);
        if (p.host) draw_crown(dl, right - tw - 40 * s, y + 6 * s, 18 * s, kGold);
        y += size + 22 * s;
    }
    if (!v.connected) {
        const std::string w = v.host ? "Invite a friend to join" : "Looking for the host";
        text(dl, right - text_w(20 * s, w), y, 20 * s, w, kDim);
    }
}

// Draws the lobby (or the guest's waiting screen) and takes its input; false when neither is on screen.
bool draw_lobby(float width, float height, ImDrawList* dl, ImVec2 mouse, const std::vector<ImVec2>& clicks,
                const std::vector<unsigned>& keys) {
    const net::LobbyView v = net::lobby_view();
    const float s = height / 1080.0f;
    if (v.guest_wait && !g_open.load()) {
        // The guest's game is driven behind this (New Game, the difficulty): it covers everything.
        g_lobby_shown = true;
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), IM_COL32(6, 5, 4, 255));
        const float cx = width * 0.5f, cy = height * 0.45f;
        const double t = ImGui::GetTime();
        for (int i = 0; i < 12; ++i) {
            const float a = static_cast<float>(t * 2.0 + i * 0.5236);
            const float alpha = 0.25f + 0.75f * static_cast<float>((i + static_cast<int>(t * 8)) % 12) / 11.0f;
            dl->AddCircleFilled(ImVec2(cx + std::cos(a) * 26 * s, cy - 120 * s + std::sin(a) * 26 * s), 3.2f * s,
                                IM_COL32(232, 200, 130, static_cast<int>(alpha * 255)));
        }
        const std::string title = "Waiting for " + v.partner_name;
        text(dl, cx - text_w(42 * s, title, true) * 0.5f, cy - 40 * s, 42 * s, title, kGold, true);
        text(dl, cx - text_w(24 * s, v.wait_line) * 0.5f, cy + 30 * s, 24 * s, v.wait_line, kText);
        const std::string note = "Please wait. Your game follows the host's on its own.";
        text(dl, cx - text_w(19 * s, note) * 0.5f, cy + 80 * s, 19 * s, note, kDim);
        draw_party(dl, width, height, v);
        return true;
    }
    g_lobby_shown = v.show && !g_open.load();
    if (!v.show || g_open.load()) return false;
    static bool choosing_before = false, direct_before = false;
    if (v.choosing != choosing_before || g_direct_page != direct_before) g_lobby_sel = 0;
    choosing_before = v.choosing;
    direct_before = g_direct_page;
    std::vector<Card> cards;
    std::string header;
    const bool counting = v.countdown >= 0;
    // Radmin VPN / Direct IP: a new invite goes to the clipboard once, as soon as the network has it.
    const net::DirectState ds = net::direct_state();
    if (ds.active && ds.host && !ds.invite.empty() && ds.invite != g_direct_copied) {
        g_direct_copied = ds.invite;
        g_direct_note = net::direct_clipboard_set(ds.invite)
            ? "Invite copied. Paste it to your friend (Discord, chat): they copy it and press Join."
            : "Your invite: " + ds.invite + " (copying it failed: write it down for your friend)";
        log::info("Co-op lobby: invite {} on the clipboard", ds.invite.substr(0, ds.invite.rfind('/')) + "/...");
    }
    if (!ds.error.empty() && g_direct_note != ds.error && !ds.active) g_direct_note = ds.error;
    if (g_direct_page && v.choosing) g_direct_page = false;
    if (g_direct_page) {
        header = "Radmin VPN / Direct IP";
        cards.push_back({Emblem::Coop, "Host", "Your friend joins you with an invite (copied for you).", !counting, [] {
                             net::DirectRequest r;
                             r.kind = net::DirectRequest::Kind::Host;
                             r.target.port = 37015;
                             r.target.token = net::direct_new_token();
                             // A bare address on the clipboard (Radmin's Copy IP of the guest) is let in too.
                             if (const auto t = net::direct_invite_parse(net::direct_clipboard_get()); t && !t->token) {
                                 r.peer_ip = t->ip;
                             }
                             g_direct_copied.clear();
                             g_direct_note = "Hosting on Radmin VPN / Direct IP...";
                             net::direct_request(r);
                         }});
        cards.push_back({Emblem::Direct, "Join", "Copy your friend's invite (or address), then press Join.", !counting, [] {
                             const std::string clip = net::direct_clipboard_get();
                             const auto t = net::direct_invite_parse(clip);
                             if (!t) {
                                 g_direct_note = "Nothing to join on the clipboard: copy your friend's invite first.";
                                 return;
                             }
                             net::DirectRequest r;
                             r.kind = net::DirectRequest::Kind::Join;
                             r.target = *t;
                             g_direct_note = "Joining " + net::direct_dotted(t->ip) +
                                             (t->token ? " with the invite..." : " (the host must have your address)...");
                             net::direct_request(r);
                         }});
        if (ds.active) {
            cards.push_back({Emblem::Invite, "Back to Steam Invite", "Leave the direct connection.", !counting, [] {
                                 net::DirectRequest r;
                                 r.kind = net::DirectRequest::Kind::Steam;
                                 g_direct_note.clear();
                                 g_direct_copied.clear();
                                 net::direct_request(r);
                             }});
        }
        cards.push_back({Emblem::Solo, "Back", "", true, [] { g_direct_page = false; }});
    } else if (v.choosing) {
        header = "How does your co-op journey begin?";
        if (v.worlds_on) {
            // user 2026-11-06: continuing first (greyed until there is a co-op save), then solo progress, then a new save
            cards.push_back({Emblem::Continue, "Continue Our Co-op Save",
                             v.can_continue ? "Where the two of you left off." : "Not yet: you have no co-op save together.",
                             v.can_continue, [] { net::lobby_choose(net::LobbyPlan::ContinueCoop); }});
            cards.push_back({Emblem::FromSolo, "Start From My Solo Progress", v.partner_name + " joins your story where you are.",
                             true, [] { net::lobby_choose(net::LobbyPlan::FromSolo); }});
            cards.push_back({Emblem::NewSave, "New Co-op Save", "Both of you begin the story. You choose the difficulty.", true,
                             [] { net::lobby_choose(net::LobbyPlan::NewSave); }});
        } else {
            cards.push_back({Emblem::Continue, "Play Together", "Each of you in your own save (co-op saves are off).", true,
                             [] { net::lobby_choose(net::LobbyPlan::ContinueCoop); }});
        }
    } else if (v.host) {
        header = counting ? std::format("Starting together in {}...", std::max(1, v.countdown)) : "Select how to play.";
        cards.push_back({Emblem::Solo, "Start Solo", "Your own saves, as without the mod.", !counting, [] { net::lobby_dismiss(); }});
        if (ds.active && ds.host && !ds.invite.empty()) {
            cards.push_back({Emblem::Invite, "Copy Invite", "Radmin VPN / Direct IP: send it to your friend.", !counting, [] {
                                 const net::DirectState d = net::direct_state();
                                 g_direct_note = net::direct_clipboard_set(d.invite) ? "Invite copied again." : "Your invite: " + d.invite;
                             }});
        } else {
            cards.push_back({Emblem::Invite, "Invite Friends", "Steam's invite list: your friend joins your party.", !counting,
                             [] { input::trigger(input::Action::Invite); }});
        }
        cards.push_back({Emblem::Direct, "Radmin VPN / Direct IP",
                         ds.active ? "On: " + ds.address + (ds.host ? " (hosting)" : " (joined)")
                                   : "No Steam invite: Host or Join over Radmin VPN or a LAN.",
                         !counting, [] { g_direct_page = true; }});
        cards.push_back({Emblem::Coop, "Start Co-op",
                         v.connected ? "Begin the journey with " + v.partner_name + "." : "Invite a friend first.",
                         v.connected && !counting && v.plan == net::LobbyPlan::None, [] { net::lobby_start_coop(); }});
        cards.push_back({Emblem::Settings, "Co-op Settings", "", !counting, [] { coop_page_toggle(); }});
    } else {
        header = v.connected ? "You are in " + v.partner_name + "'s party." : "Select how to play.";
        cards.push_back({Emblem::Solo, "Start Solo", "Your own saves, as without the mod.", true, [] { net::lobby_dismiss(); }});
        cards.push_back({Emblem::Coop, v.connected ? "Waiting for " + v.partner_name : "Join a Friend",
                         v.connected ? v.partner_name + " starts the co-op journey." : "Accept their Steam invite.", false, nullptr});
        cards.push_back({Emblem::Invite, v.connected || !ds.active ? "Leave the Party" : "Stop Joining", "",
                         v.connected || ds.active, [] { input::trigger(input::Action::Leave); }});
        if (!v.connected) {
            cards.push_back({Emblem::Direct, "Radmin VPN / Direct IP", "No Steam invite: Host or Join over Radmin VPN or a LAN.",
                             true, [] { g_direct_page = true; }});
        }
        cards.push_back({Emblem::Settings, "Co-op Settings", "", true, [] { coop_page_toggle(); }});
    }
    const int n = static_cast<int>(cards.size());
    if (g_lobby_sel >= n || !cards[g_lobby_sel].enabled) {
        for (int i = 0; i < n; ++i) {
            if (cards[i].enabled) {
                g_lobby_sel = i;
                break;
            }
        }
    }
    // input
    static uint16_t pad_before = 0;
    const uint16_t pad = input::pad_buttons();
    const uint16_t pressed = g_steam_overlay ? 0 : pad & ~pad_before;
    pad_before = pad;
    input::mask_from_game(input::kPadDpad | input::kPadA | input::kPadB | input::kPadStart, 150);
    auto move = [&](int d) {
        for (int i = 0; i < n; ++i) {
            g_lobby_sel = (g_lobby_sel + d + n) % n;
            if (cards[g_lobby_sel].enabled) break;
        }
    };
    auto act = [&](int i) {
        if (i >= 0 && i < n && cards[i].enabled && cards[i].act) {
            log::info("Co-op lobby: {}", cards[i].title);
            cards[i].act();
        }
    };
    auto back = [&] {
        if (g_direct_page) g_direct_page = false;
        else if (v.choosing) net::lobby_choose(net::LobbyPlan::None);
        else net::lobby_dismiss();
    };
    for (unsigned k : keys) {
        if (k == VK_UP || k == 'W') move(-1);
        else if (k == VK_DOWN || k == 'S') move(1);
        else if (k == VK_RETURN || k == VK_SPACE) act(g_lobby_sel);
        else if (k == VK_ESCAPE || k == VK_BACK) back();
    }
    if (pressed & input::kPadUp) move(-1);
    if (pressed & input::kPadDown) move(1);
    if (pressed & input::kPadA) act(g_lobby_sel);
    if (pressed & input::kPadB) back();
    // drawing: our own screen in place of the game's title (opaque), the wordmark, the dialog, the party, key hints
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), IM_COL32(7, 6, 5, 255));
    dl->AddRectFilledMultiColor(ImVec2(0, height * 0.18f), ImVec2(width, height * 0.82f), IM_COL32(20, 16, 12, 255),
                                IM_COL32(20, 16, 12, 255), IM_COL32(7, 6, 5, 255), IM_COL32(7, 6, 5, 255));
    {
        // letter-spaced capitals, the way the game sets its own name
        const auto spaced = [](const std::string& w) {
            std::string out;
            for (size_t i = 0; i < w.size(); ++i) out += (i ? " " : "") + std::string(1, w[i]);
            return out;
        };
        const std::string name = spaced("CRIMSON") + "      " + spaced("DESERT");
        const float ns = 46 * s;
        text(dl, width * 0.5f - text_w(ns, name) * 0.5f, 150 * s, ns, name, IM_COL32(214, 190, 142, 255));
        const std::string mark = "SEAMLESS  CO-OP";
        const float ms = 20 * s;
        const float mw = text_w(ms, mark, true);
        text(dl, width * 0.5f - mw * 0.5f, 226 * s, ms, mark, kGold, true);
        for (const float side : {-1.0f, 1.0f}) {
            const float x0 = width * 0.5f + side * (mw * 0.5f + 24 * s), x1 = x0 + side * 150 * s;
            dl->AddLine(ImVec2(x0, 238 * s), ImVec2(x1, 238 * s), IM_COL32(205, 195, 175, 120), 1.0f);
        }
    }
    const int over = draw_cards(dl, width, height, header, cards, g_lobby_sel, mouse, 300 * s);
    // The mouse picks the card under it only when it moves (a resting mouse over a card snapped the highlight back
    // every frame, so W / S did nothing: found in the two-copy test of the Radmin page, 2026-11-07).
    static ImVec2 mouse_before(-1, -1);
    const bool mouse_moved = std::abs(mouse.x - mouse_before.x) + std::abs(mouse.y - mouse_before.y) > 2.0f;
    mouse_before = mouse;
    if (over >= 0 && mouse_moved && cards[over].enabled && input::method() == input::Method::Keyboard) g_lobby_sel = over;
    for (const ImVec2& c : clicks) {
        (void)c;
        if (over >= 0) act(over);
    }
    {
        const float y = 300 * s + 96 * s + n * (92 * s + 14 * s) + 50 * s;
        std::string line = v.status;
        if (g_direct_page) {
            std::string name, why;
            const auto ours = net::direct_host_address(name, why);
            line = ds.active ? "On " + ds.address + (ds.host ? ", hosting" : ", joined to " + ds.friend_address)
                 : ours      ? "Your address: " + net::direct_dotted(*ours) + " on " + name
                             : why;
        }
        if ((g_direct_page || ds.active) && !g_direct_note.empty()) line = g_direct_note;
        text(dl, width * 0.5f - text_w(21 * s, line) * 0.5f, y, 21 * s, line, kDim);
    }
    draw_party(dl, width, height, v);
    // the title's own prompts (Play, New Game, Load, Exit) are not in play while the lobby is up: hidden
    dl->AddRectFilledMultiColor(ImVec2(width * 0.30f, height - 120 * s), ImVec2(width * 0.55f, height), IM_COL32(6, 5, 4, 0),
                                IM_COL32(6, 5, 4, 255), IM_COL32(6, 5, 4, 255), IM_COL32(6, 5, 4, 0));
    dl->AddRectFilled(ImVec2(width * 0.55f, height - 120 * s), ImVec2(width, height), IM_COL32(6, 5, 4, 255));
    dl->AddRectFilledMultiColor(ImVec2(0, height - 120 * s), ImVec2(width * 0.2f, height), IM_COL32(6, 5, 4, 255),
                                IM_COL32(6, 5, 4, 0), IM_COL32(6, 5, 4, 0), IM_COL32(6, 5, 4, 255));
    {
        float right = width - 60 * s;
        const float y = height - 64 * s, size = 20 * s;
        if (input::method() == input::Method::Keyboard) {
            right = hint(dl, right, y, size, "Esc", v.choosing || g_direct_page ? "Back" : "Start Solo");
            right = hint(dl, right, y, size, "Space", "Confirm");
            hint(dl, right, y, size, "W S", "Move");
        } else {
            right = hint(dl, right, y, size, "B", v.choosing || g_direct_page ? "Back" : "Start Solo");
            right = hint(dl, right, y, size, "A", "Confirm");
            hint(dl, right, y, size, "D-pad", "Move");
        }
    }
    if (counting) ui::title_card(width, height, std::to_string(std::max(1, v.countdown)), "Both games start together", kGold);
    return true;
}

}  // namespace

bool coop_page_open() { return g_open.load(); }
bool coop_page_details() { return g_details.load(); }

void coop_page_toggle() {
    const bool now = !g_open.load();
    g_open = now;
    g_as_tab = false;
    if (now) {
        g_focus_rows = false;
        std::lock_guard lock(g_in_mutex);
        g_keys.clear();
        g_clicks.clear();
        g_wheel = 0;
    }
    log::info("Co-op page {}", now ? "opened" : "closed");
}

bool coop_page_hook_key(unsigned vk, bool down) {
    if (!game_in_front() || net::join_overlay_active()) return false;
    if (down) {
        g_key_vk = vk;
        g_key_ms = GetTickCount64();
    }
    if (!g_open.load() && g_lobby_shown.load()) {
        // The title lobby: its keys, and the title's own (Play, New Game, Load, Settings, Exit) kept from the game.
        switch (vk) {
            case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT: case VK_RETURN: case VK_SPACE: case VK_ESCAPE:
            case VK_BACK: case 'E': case 'Z': case 'F': case 'O': case 'W': case 'S':
                if (down) {
                    std::lock_guard lock(g_in_mutex);
                    if (g_keys.size() < 32) g_keys.push_back(vk);
                }
                return true;
            default:
                return false;
        }
    }
    if (!g_open.load()) return false;
    if (g_as_tab.load()) {
        // The menu's tab: the game's tab keys go to the game (its switch closes us), and Esc with nothing chosen
        // closes the whole menu as it does on the game's own tabs.
        if (vk == 'Q' || vk == 'E' || vk == VK_PRIOR || vk == VK_NEXT) return false;
        if (vk == VK_ESCAPE && !g_focus_rows.load()) return false;
    }
    // W A S D move as the arrows do (the game's menus take both)
    vk = vk == 'W' ? VK_UP : vk == 'S' ? VK_DOWN : vk == 'A' ? VK_LEFT : vk == 'D' ? VK_RIGHT : vk;
    switch (vk) {
        case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT: case VK_TAB: case VK_RETURN: case VK_SPACE:
        case VK_ESCAPE: case VK_BACK: case 'Q': case 'E': case VK_PRIOR: case VK_NEXT:
            if (down) {
                std::lock_guard lock(g_in_mutex);
                if (g_keys.size() < 32) g_keys.push_back(vk);
            }
            return true;
        default:
            return false;
    }
}

void coop_page_draw(float width, float height, const HudData& d, const std::vector<std::string>& status) {
    start_mouse_hook();
    const HWND fg = GetForegroundWindow();
    if (game_in_front()) g_hwnd = fg;
    const Space sp{width, height, height / 1080.0f};
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    // The mouse, in the overlay's (client) coordinates.
    ImVec2 mouse(-1, -1);
    {
        POINT p{};
        if (GetCursorPos(&p) && g_hwnd.load() && ScreenToClient(g_hwnd.load(), &p)) mouse = ImVec2(float(p.x), float(p.y));
    }
    std::vector<ImVec2> clicks;
    int wheel = 0;
    std::vector<unsigned> keys;
    {
        std::lock_guard lock(g_in_mutex);
        for (POINT p : g_clicks) {
            if (g_hwnd.load() && ScreenToClient(g_hwnd.load(), &p)) clicks.push_back(ImVec2(float(p.x), float(p.y)));
        }
        g_clicks.clear();
        wheel = g_wheel;
        g_wheel = 0;
        keys.assign(g_keys.begin(), g_keys.end());
        g_keys.clear();
    }
    // The Steam overlay is open (Shift+Tab, the invite dialog): the mouse, the keys and the pad are Steam's.
    const bool steam_overlay = net::join_overlay_active();
    g_steam_overlay = steam_overlay;
    if (steam_overlay) {
        mouse = ImVec2(-1, -1);
        clicks.clear();
        keys.clear();
        wheel = 0;
    }

    // ---- the title lobby (before anyone plays). It stands in for the game's title: while it (or the guest's waiting
    // screen) is up the game takes no input of its own (game_input_block.h), except while the mod presses its keys.
    {
        const net::LobbyView lv = net::lobby_view();
        // Not while the Steam overlay is open: it takes its clicks from the input the game reads, and a game that
        // believes it is in the background reads none (tcB260: the dialog's X lit up under the mouse but no click
        // reached it). The overlay keeps the game's own screens from reacting meanwhile.
        input::set_game_input_blocked((lv.show || lv.guest_wait) && !lv.driving && !steam_overlay);
    }
    if (!g_open.load() && draw_lobby(width, height, dl, mouse, clicks, keys)) return;
    // ---- a different difficulty from the friend's (net/coop_lobby.h)
    if (!g_open.load()) {
        const std::string card = net::lobby_difficulty_card();
        if (!card.empty()) {
            const auto nl = card.find('\n');
            ui::band(width, height * 0.16f, card.substr(0, nl), nl == std::string::npos ? card : card.substr(nl + 1));
        }
    }

    // ---- the tab after the game's "Others", while its menu is open
    const bool menu = game::ui_menu_open() && d.local.has_value() && !net::sniffer_world_loading() &&
                      !net::lobby_view().at_title;
    // The game's tabs: 160 apart (Map 166 ... Others 804, ours next at 964), their names centred on y 50.
    const float tab_size = kTabSize * sp.s;
    const std::string tab = "Co-op";
    const float tab_w = text_w(tab_size, tab);
    const float tab_x = sp.x(kTabX[5]) - tab_w * 0.5f, tab_y = sp.y(kTabY) - tab_size * 0.5f;
    // A shoulder-button glyph the way the game draws its own (dark plate, pale rim).
    auto glyph = [&](float cx, const char* name) {
        const float gw = 38 * sp.s, gh = 28 * sp.s, cy = sp.y(kTabY + 2), gs = 17 * sp.s;
        dl->AddRectFilled(ImVec2(cx - gw * 0.5f, cy - gh * 0.5f), ImVec2(cx + gw * 0.5f, cy + gh * 0.5f),
                          IM_COL32(20, 18, 16, 235), 6 * sp.s);
        dl->AddRect(ImVec2(cx - gw * 0.5f, cy - gh * 0.5f), ImVec2(cx + gw * 0.5f, cy + gh * 0.5f),
                    IM_COL32(214, 210, 202, 235), 6 * sp.s, 1.6f * sp.s);
        text(dl, cx - text_w(gs, name, true) * 0.5f, cy - gs * 0.58f, gs, name, IM_COL32(230, 226, 218, 255), true);
    };
    if (menu) {
        game::main_menu_install(tab_filter);
        game::main_menu_dev_watch_tick();
        if (input::pad_buttons() & (input::kPadLB | input::kPadRB)) g_pad_step_ms = GetTickCount64();
    }
    if (menu || g_open.load()) {
        const bool hover = inside(mouse, tab_x - 20 * sp.s, tab_y - 10 * sp.s, tab_x + tab_w + 20 * sp.s, tab_y + 60 * sp.s);
        if (!g_open.load()) {
            text(dl, tab_x, tab_y, tab_size, tab, hover && input::method() == input::Method::Keyboard ? kGold : kText);
            // The game's own RB glyph (after Others) is hidden (main_menu.cpp); ours stands after Co-op.
            if (menu && input::method() != input::Method::Keyboard) glyph(sp.x(912 + game::kMenuRbShift), "RB");
            // On screen (for the mouse hook): the label.
            POINT a{static_cast<LONG>(tab_x - 20 * sp.s), static_cast<LONG>(tab_y - 10 * sp.s)};
            POINT b{static_cast<LONG>(tab_x + tab_w + 20 * sp.s), static_cast<LONG>(sp.y(96))};
            if (g_hwnd.load()) {
                ClientToScreen(g_hwnd.load(), &a);
                ClientToScreen(g_hwnd.load(), &b);
            }
            g_tab_l = a.x, g_tab_t = a.y, g_tab_r = b.x, g_tab_b = b.y;
            g_tab_shown = menu;
            for (const ImVec2& c : clicks) {
                if (inside(c, tab_x - 20 * sp.s, tab_y - 10 * sp.s, tab_x + tab_w + 20 * sp.s, sp.y(96))) open_as_tab("click");
            }
        }
    } else {
        g_tab_shown = false;
    }
    if (!g_open.load()) return;
    g_tab_shown = false;

    // ---- input: keys, pad, mouse
    static uint16_t pad_before = 0;
    const uint16_t pad = input::pad_buttons();
    const uint16_t pressed = g_steam_overlay ? 0 : pad & ~pad_before;
    pad_before = pad;
    const bool as_tab = g_as_tab.load();
    if (as_tab && !menu) {  // the menu went away under us (Start, B, Esc)
        close_tab("the menu closed");
        return;
    }
    // A click on one of the game's tabs: the page closes and the click goes on to the game's row under it.
    if (as_tab) {
        for (const ImVec2& c : clicks) {
            if (c.y > sp.y(96) || c.x < sp.x(kTabX[0] - 80) || c.x > sp.x(kTabX[4] + 80)) continue;
            close_tab("a click on a game tab");
            INPUT in[2] = {};
            in[0].type = in[1].type = INPUT_MOUSE;
            in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
            in[0].mi.dwExtraInfo = in[1].mi.dwExtraInfo = kOurInput;
            SendInput(2, in, sizeof(INPUT));
            return;
        }
    }
    // As the menu's tab LB / RB stay the game's (tab_filter), and so does B until a row is chosen.
    input::mask_from_game(input::kPadDpad | input::kPadA |
                              (as_tab ? (g_focus_rows ? input::kPadB : 0) : input::kPadB | input::kPadLB | input::kPadRB),
                          150);
    // The sections with something in them (the shared world decides loot and scaling: that section is not shown).
    std::vector<int> vis;
    for (int i = 0; i < kSectionCount; ++i) {
        if (!rows_for(i, d, status).empty()) vis.push_back(i);
    }
    if (vis.empty()) vis.push_back(0);
    if (std::find(vis.begin(), vis.end(), g_section) == vis.end()) g_section = vis.front();
    const int vn = static_cast<int>(vis.size());
    std::vector<Row> rows = rows_for(g_section, d, status);
    const int n = static_cast<int>(rows.size());
    auto first_selectable = [&] {
        for (int i = 0; i < n; ++i) {
            if (rows[i].kind != Row::Info) return i;
        }
        return 0;
    };
    auto change_section = [&](int delta) {
        const int at = static_cast<int>(std::find(vis.begin(), vis.end(), g_section) - vis.begin());
        g_section = vis[((at + delta) % vn + vn) % vn];
        rows = rows_for(g_section, d, status);
        g_row = 0;
        g_top = 0;
    };
    auto move_row = [&](int delta) {
        const int m = static_cast<int>(rows.size());
        if (!m) return;
        for (int step = 0; step < m; ++step) {
            g_row = (g_row + delta + m) % m;
            if (rows[g_row].kind != Row::Info || g_section == 7 || g_section == 8) break;
        }
    };
    auto activate = [&](int delta) {
        if (g_row < 0 || g_row >= static_cast<int>(rows.size())) return;
        Row& r = rows[g_row];
        if (r.act && (r.kind == Row::Button ? delta == 0 : true)) r.act(delta);
        if (r.kind == Row::Button && delta == 0) {
            log::info("Co-op page: {}", r.label);
            // As the menu's tab an action closes the menu, as choosing a place on the game's map does: the player
            // sees what it did (the trip, the notice) in the world. A pad's Start for pad players, Esc otherwise.
            if (as_tab) {
                if (input::method() == input::Method::Xbox) {
                    input::press_for_game(input::kPadStart, 120);
                } else {
                    INPUT in[2] = {};
                    in[0].type = in[1].type = INPUT_KEYBOARD;
                    in[0].ki.wVk = in[1].ki.wVk = VK_ESCAPE;
                    in[0].ki.wScan = in[1].ki.wScan = static_cast<WORD>(MapVirtualKeyW(VK_ESCAPE, MAPVK_VK_TO_VSC));
                    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    in[0].ki.dwExtraInfo = in[1].ki.dwExtraInfo = kOurInput;
                    SendInput(2, in, sizeof(INPUT));
                }
                g_focus_rows = false;
            }
        }
    };
    bool close = false;
    for (unsigned k : keys) {
        switch (k) {
            case VK_ESCAPE: case VK_BACK:
                if (g_focus_rows) g_focus_rows = false;
                else close = true;
                break;
            case VK_TAB: case 'E': case VK_NEXT: change_section(1); break;
            case 'Q': case VK_PRIOR: change_section(-1); break;
            case VK_UP:
                if (g_focus_rows) move_row(-1);
                else change_section(-1);
                break;
            case VK_DOWN:
                if (g_focus_rows) move_row(1);
                else change_section(1);
                break;
            case VK_RIGHT:
                if (!g_focus_rows) {
                    g_focus_rows = true;
                    g_row = first_selectable();
                } else if (!rows.empty() && rows[g_row].kind == Row::Setting) {
                    activate(1);
                }
                break;
            case VK_LEFT:
                if (g_focus_rows && !rows.empty() && rows[g_row].kind == Row::Setting) activate(-1);
                else g_focus_rows = false;
                break;
            case VK_RETURN: case VK_SPACE:
                if (!g_focus_rows) {
                    g_focus_rows = true;
                    g_row = first_selectable();
                } else {
                    activate(0);
                }
                break;
        }
    }
    if (pressed & input::kPadB) {
        if (g_focus_rows) g_focus_rows = false;
        else if (!as_tab) close = true;
    }
    if (!as_tab) {
        if (pressed & input::kPadRB) change_section(1);
        if (pressed & input::kPadLB) change_section(-1);
    }
    if (pressed & input::kPadUp) g_focus_rows ? move_row(-1) : change_section(-1);
    if (pressed & input::kPadDown) g_focus_rows ? move_row(1) : change_section(1);
    if (pressed & input::kPadRight) {
        if (!g_focus_rows) {
            g_focus_rows = true;
            g_row = first_selectable();
        } else if (!rows.empty() && rows[g_row].kind == Row::Setting) {
            activate(1);
        }
    }
    if (pressed & input::kPadLeft) {
        if (g_focus_rows && !rows.empty() && rows[g_row].kind == Row::Setting) activate(-1);
        else g_focus_rows = false;
    }
    if (pressed & input::kPadA) {
        if (!g_focus_rows) {
            g_focus_rows = true;
            g_row = first_selectable();
        } else {
            activate(0);
        }
    }

    // ---- layout
    const float L = sp.x(230), R = sp.x(1690), T = sp.y(150), B = sp.y(910);
    const float sec_x = sp.x(270), sec_y = sp.y(285), sec_h = 54 * sp.s, sec_size = 27 * sp.s;
    const float div_x = sp.x(610);
    const float row_l = sp.x(640), row_r = sp.x(1650), row_y = sp.y(285), row_h = 52 * sp.s, row_size = 25 * sp.s;
    const int visible = std::max(1, static_cast<int>((sp.y(790) - row_y) / row_h));

    // mouse: hover picks, click acts, wheel scrolls the rows
    const bool use_mouse = input::method() == input::Method::Keyboard;
    for (int k = 0; k < vn; ++k) {
        const int i = vis[k];
        const float y = sec_y + k * sec_h;
        const bool over = inside(mouse, sec_x - 10 * sp.s, y - 6 * sp.s, div_x - 10 * sp.s, y + sec_h - 6 * sp.s);
        for (const ImVec2& c : clicks) {
            if (inside(c, sec_x - 10 * sp.s, y - 6 * sp.s, div_x - 10 * sp.s, y + sec_h - 6 * sp.s) && i != g_section) {
                g_section = i;
                change_section(0);
                g_focus_rows = false;
            }
        }
        (void)over;
    }
    rows = rows_for(g_section, d, status);
    const int rn = static_cast<int>(rows.size());
    if (g_row >= rn) g_row = std::max(0, rn - 1);
    if (wheel) g_top = std::clamp(g_top - wheel, 0, std::max(0, rn - visible));
    if (g_focus_rows) {
        if (g_row < g_top) g_top = g_row;
        if (g_row >= g_top + visible) g_top = g_row - visible + 1;
    }
    for (int i = g_top; i < std::min(rn, g_top + visible); ++i) {
        const float y = row_y + (i - g_top) * row_h;
        const bool over = inside(mouse, row_l - 10 * sp.s, y - 8 * sp.s, row_r + 10 * sp.s, y + row_h - 8 * sp.s);
        if (over && use_mouse && rows[i].kind != Row::Info) {
            g_row = i;
            g_focus_rows = true;
        }
        for (const ImVec2& c : clicks) {
            if (!inside(c, row_l - 10 * sp.s, y - 8 * sp.s, row_r + 10 * sp.s, y + row_h - 8 * sp.s)) continue;
            g_row = i;
            g_focus_rows = true;
            if (rows[i].kind == Row::Button) {
                activate(0);
            } else if (rows[i].kind == Row::Setting) {
                // on the value: its "<" goes back, the rest forward; on the label: forward
                const std::string v = "<   " + rows[i].value + "   >";
                const float vl = row_r - text_w(row_size, v);
                activate(c.x >= vl && c.x < vl + text_w(row_size, "<   ") ? -1 : 1);
            }
            rows = rows_for(g_section, d, status);
        }
    }

    // ---- drawing
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), IM_COL32(8, 7, 6, 205));
    // The menu's tab row, ours chosen: the game's own row is under the page with "Others" (or "Map") still lit, so
    // the whole row is drawn again here, the way the game draws it (gold name, a rule under it with a dot).
    if (as_tab) {
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(width, sp.y(96)), IM_COL32(8, 7, 6, 255));
        dl->AddRectFilledMultiColor(ImVec2(0, sp.y(96)), ImVec2(width, sp.y(136)), IM_COL32(8, 7, 6, 255),
                                    IM_COL32(8, 7, 6, 255), IM_COL32(8, 7, 6, 0), IM_COL32(8, 7, 6, 0));
        static const char* const kNames[] = {"Map", "Inventory", "Journal", "Skills", "Others"};
        for (int i = 0; i < 5; ++i) {
            const float w = text_w(tab_size, kNames[i]);
            const bool over = use_mouse && inside(mouse, sp.x(kTabX[i]) - 70 * sp.s, sp.y(20), sp.x(kTabX[i]) + 70 * sp.s, sp.y(80));
            text(dl, sp.x(kTabX[i]) - w * 0.5f, tab_y, tab_size, kNames[i], over ? kGold : kText);
        }
        // The game's two shoulder glyphs, where it draws them (before Map, after Others), on a controller only.
        if (!use_mouse) {
            glyph(sp.x(78), "LB");
            glyph(sp.x(912 + game::kMenuRbShift), "RB");
        }
    }
    text(dl, tab_x, tab_y, tab_size, tab, kGold);
    {
        const float cx = sp.x(kTabX[5]), y = sp.y(kTabY + 17), half = 80 * sp.s;
        const ImU32 on = IM_COL32(232, 200, 130, 230), off = IM_COL32(232, 200, 130, 0);
        dl->AddRectFilledMultiColor(ImVec2(cx - half, y), ImVec2(cx, y + 1.5f * sp.s), off, on, on, off);
        dl->AddRectFilledMultiColor(ImVec2(cx, y), ImVec2(cx + half, y + 1.5f * sp.s), on, off, off, on);
        dl->AddCircleFilled(ImVec2(cx, y + 0.75f * sp.s), 3.5f * sp.s, IM_COL32(236, 160, 70, 255));
    }
    // the frame
    dl->AddRectFilled(ImVec2(L, T), ImVec2(R, B), IM_COL32(22, 20, 18, 238));
    dl->AddRect(ImVec2(L, T), ImVec2(R, B), kRule, 0.0f, 1.0f);
    text(dl, sp.x(270), sp.y(180), 40 * sp.s, "Co-op", kGold, true);
    {
        const std::string who = d.connected ? "Playing with " + d.peer_name + "   " + connection_text(d) : "Not in a session";
        const float w = text_w(23 * sp.s, who);
        text(dl, sp.x(1650) - w, sp.y(196), 23 * sp.s, who, d.connected ? kText : kDim);
    }
    dl->AddLine(ImVec2(sp.x(270), sp.y(250)), ImVec2(sp.x(1650), sp.y(250)), kRule, 1.0f);
    // sections
    for (int k = 0; k < vn; ++k) {
        const int i = vis[k];
        const float y = sec_y + k * sec_h;
        const bool sel = i == g_section;
        const bool over = inside(mouse, sec_x - 10 * sp.s, y - 6 * sp.s, div_x - 10 * sp.s, y + sec_h - 6 * sp.s);
        if (sel) {
            dl->AddRectFilled(ImVec2(sec_x - 14 * sp.s, y - 6 * sp.s), ImVec2(div_x - 14 * sp.s, y + sec_h - 10 * sp.s),
                              IM_COL32(232, 200, 130, g_focus_rows ? 14 : 30));
            dl->AddRectFilled(ImVec2(sec_x - 14 * sp.s, y - 6 * sp.s), ImVec2(sec_x - 11 * sp.s, y + sec_h - 10 * sp.s),
                              kGold);
        }
        text(dl, sec_x, y + 4 * sp.s, sec_size, kSections[i], sel ? kGold : over ? kText : kDim);
    }
    dl->AddLine(ImVec2(div_x, sp.y(280)), ImVec2(div_x, sp.y(800)), kRule, 1.0f);
    // rows
    for (int i = g_top; i < std::min(rn, g_top + visible); ++i) {
        const Row& r = rows[i];
        const float y = row_y + (i - g_top) * row_h;
        const bool sel = g_focus_rows && i == g_row && r.kind != Row::Info;
        if (sel) {
            dl->AddRectFilled(ImVec2(row_l - 10 * sp.s, y - 6 * sp.s), ImVec2(row_r + 10 * sp.s, y + row_h - 10 * sp.s),
                              IM_COL32(232, 200, 130, 26));
            dl->AddLine(ImVec2(row_l - 10 * sp.s, y - 6 * sp.s), ImVec2(row_r + 10 * sp.s, y - 6 * sp.s),
                        IM_COL32(232, 200, 130, 150), 1.0f);
            dl->AddLine(ImVec2(row_l - 10 * sp.s, y + row_h - 10 * sp.s), ImVec2(row_r + 10 * sp.s, y + row_h - 10 * sp.s),
                        IM_COL32(232, 200, 130, 150), 1.0f);
        }
        const ImU32 lc = r.kind == Row::Info && g_section != 7 ? kDim : sel ? kGold : kText;
        text(dl, row_l, y + 4 * sp.s, row_size, r.label, lc);
        if (!r.value.empty()) {
            std::string v = r.value;
            if (r.kind == Row::Setting) v = "<   " + v + "   >";
            const float vw = text_w(row_size, v);
            text(dl, row_r - vw, y + 4 * sp.s, row_size, v, r.kind == Row::Setting ? (sel ? kGold : kText) : kDim);
        }
    }
    if (rn > visible) {
        const std::string more = std::format("{} - {} of {}", g_top + 1, std::min(rn, g_top + visible), rn);
        text(dl, row_r - text_w(18 * sp.s, more), sp.y(790), 18 * sp.s, more, kDim);
    }
    // the chosen row's explanation
    dl->AddLine(ImVec2(sp.x(270), sp.y(815)), ImVec2(sp.x(1650), sp.y(815)), kRule, 1.0f);
    if (g_focus_rows && g_row < rn) {
        std::string help = rows[g_row].help;
        if (!rows[g_row].live) help += help.empty() ? "Applies at the next start." : " Applies at the next start.";
        if (!help.empty()) wrap(dl, sp.x(270), sp.y(830), sp.x(1650) - sp.x(270), 21 * sp.s, help, kDim);
    } else {
        wrap(dl, sp.x(270), sp.y(830), sp.x(1650) - sp.x(270), 21 * sp.s,
             "Everything Co-op+ does, in one place. Changes are saved at once.", kDim);
    }
    // key hints, bottom right (the game's own place for them), over a band that hides the game's own hints
    dl->AddRectFilledMultiColor(ImVec2(sp.x(900), sp.y(985)), ImVec2(width, height), IM_COL32(8, 7, 6, 0),
                                IM_COL32(8, 7, 6, 245), IM_COL32(8, 7, 6, 245), IM_COL32(8, 7, 6, 0));
    {
        float right = sp.x(1880);
        const float y = sp.y(1032), size = 19 * sp.s;
        if (input::method() == input::Method::Keyboard) {
            right = hint(dl, right, y, size, "Esc", "Close");
            right = hint(dl, right, y, size, as_tab ? "W S" : "Tab", "Section");
            right = hint(dl, right, y, size, "Enter", "Select");
            hint(dl, right, y, size, "<  >", "Change");
        } else {
            right = hint(dl, right, y, size, "B", g_focus_rows || !as_tab ? "Back" : "Close");
            if (!as_tab) right = hint(dl, right, y, size, "LB RB", "Section");
            right = hint(dl, right, y, size, "A", "Select");
            hint(dl, right, y, size, "D-pad", as_tab ? "Move" : "Change");
        }
    }
    if (close) {
        if (as_tab) close_tab("closed");
        else coop_page_toggle();
    }
}

}  // namespace cdmp::render
