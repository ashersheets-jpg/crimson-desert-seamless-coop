#include "mod/net/coop_worlds.h"

#include <windows.h>

#include <atomic>
#include <cctype>
#include <format>
#include <mutex>
#include <thread>
#include <vector>

#include "mod/log.h"
#include "mod/save_io.h"
#include "mod/net/troc_sniffer.h"
#include "mod/net/story_sync.h"
#include "mod/net/prologue_hold.h"
#include "mod/net/xp_share.h"
#include "mod/net/coop_lobby.h"

namespace cdmp::net {
namespace {

constexpr uint64_t kJoinStableMs = 8000;      // connected this long before we switch into the world
constexpr uint64_t kLeaveStableMs = 45000;    // the friend gone this long before we switch back (reconnects stay)
constexpr uint64_t kCountdownMs = 10000;
constexpr ULONG_PTR kOurInput = 0xC0C0A7;     // dwExtraInfo of keys/clicks we send (coop_activity's marker)
// The pause menu at 1920x1080 (2026-11-04, 1.0.0.3527): the Others tab, then Title Screen in its Options page.
constexpr int kOthersX = 804, kOthersY = 50, kTitleX = 466, kTitleY = 724;

std::mutex g_m;
bool g_enabled = false;
std::string g_test_partner;
WorldsInput g_in;
std::atomic<bool> g_in_world{false}, g_menu_open{false}, g_host{false}, g_dev_host{false};
uint64_t g_connected_since = 0, g_gone_since = 0;
std::string g_partner_key, g_partner_name;  // the last partner seen (kept while they are briefly away)
std::wstring g_want;                        // the world we should be in
uint64_t g_countdown_until = 0;
std::atomic<bool> g_switching{false};
std::atomic<int> g_failures{0};
uint64_t g_retry_after = 0, g_out_of_world_since = 0;
std::string g_status, g_toast;
// Co-op+ (2026-11-05, user: "new save" or "start from host's current progress" on the first invite): how a world
// that does not exist yet starts. The host chooses on a card; the guest's game follows the host's choice (World msg).
using save_io::WorldStart;
std::atomic<bool> g_card{false};
std::string g_card_key, g_card_name;          // the partner the card asks about
std::string g_choice_key;                     // host: the partner the answer is for
WorldStart g_choice = WorldStart::Unknown;
std::string g_host_said_key;                  // guest: the host the message came from
WorldStart g_host_said = WorldStart::Unknown;
WorldStart g_sent = WorldStart::Unknown;      // host: what this connection was told
std::vector<std::vector<uint8_t>> g_out;
// Co-op+ (2026-11-05): a switch only when the player agrees (card); a banner while the mod drives the game.
enum class Ask { None, Pending, Go, Manual };
Ask g_ask = Ask::None;
std::wstring g_ask_for;                       // the world the answer is about
bool g_ask_into = false, g_ask_new = false;  // into a co-op save; it starts as a new game
std::string g_ask_name;
std::atomic<bool> g_switch_card{false};
std::string g_banner;
std::atomic<uint32_t> g_epoch{1};             // co-op world switches started (coop.cpp drops the old copy)
std::atomic<bool> g_has_pos{false};
std::atomic<float> g_pos_x{0}, g_pos_z{0};
// Where a new game puts Kliff (1.0.0.3549: "Hills of No Return", every new-game test at (-9910, -2807)). A save the
// player loads with Play instead of New Game starts elsewhere: then the new journey has not begun.
constexpr float kNewGameX = -9910.3f, kNewGameZ = -2807.1f, kNewGameNearM = 200.0f;
bool at_new_game_start() {
    if (!g_has_pos.load()) return false;
    const float dx = g_pos_x.load() - kNewGameX, dz = g_pos_z.load() - kNewGameZ;
    return dx * dx + dz * dz <= kNewGameNearM * kNewGameNearM;
}
std::wstring g_mark_when_in;                  // a new-game world switched at the title: marked once the game runs
constexpr uint8_t kMsgStart = 1;              // World message: u8 kMsgStart | u8 1 new game / 2 continue
constexpr uint8_t kMsgLobbyFirst = 2;         // 2.. : the title lobby's own messages (coop_lobby.cpp)

std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s += static_cast<char>(c < 128 ? c : '?');
    return s;
}

std::wstring world_for(const std::string& key) {
    std::wstring w = L"with-";
    for (char c : key) w += std::isalnum(static_cast<unsigned char>(c)) || c == '-' ? static_cast<wchar_t>(c) : L'_';
    return w;
}

bool game_in_front() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

void key(WORD vk, DWORD hold_ms) {
    const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    keybd_event(static_cast<BYTE>(vk), static_cast<BYTE>(scan), 0, kOurInput);
    Sleep(hold_ms);
    keybd_event(static_cast<BYTE>(vk), static_cast<BYTE>(scan), KEYEVENTF_KEYUP, kOurInput);
}

// A click at a 1920x1080 menu position, scaled to the game window (the UI scales with the height and stays centred).
bool click(int x, int y) {
    const HWND w = GetForegroundWindow();
    RECT rc{};
    if (!game_in_front() || !GetClientRect(w, &rc) || rc.bottom <= 0) return false;
    const double s = rc.bottom / 1080.0;
    POINT p{static_cast<LONG>(rc.right / 2.0 + (x - 960) * s), static_cast<LONG>(y * s)};
    ClientToScreen(w, &p);
    SetCursorPos(p.x, p.y);
    Sleep(80);
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, kOurInput);
    Sleep(60);
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, kOurInput);
    return true;
}

void set_status(const std::string& s) {
    std::lock_guard lock(g_m);
    g_status = s;
}

void banner(const std::string& s) {
    std::lock_guard lock(g_m);
    g_banner = s;
}

void toast(const std::string& s) {
    std::lock_guard lock(g_m);
    g_toast = "Co-op+\n" + s;
}

// Waits for `cond` up to timeout_ms (polling); the game window must be in front for keys, which the caller checks.
template <class F>
bool wait_for(F cond, uint64_t timeout_ms) {
    for (uint64_t t = 0; t < timeout_ms; t += 100) {
        if (cond()) return true;
        Sleep(100);
    }
    return cond();
}

bool wait_front(const std::string& what) {
    if (game_in_front()) return true;
    set_status(what + ": click the Crimson Desert window to continue.");
    return wait_for(game_in_front, 180000);
}

const char* start_text(WorldStart s) {
    return s == WorldStart::NewGame ? "a new game" : s == WorldStart::Continue ? "the host's progress" : "not chosen";
}

// The guest's co-op world follows the host's story (the host's quests go into the WORLD copy only). Not for a
// new-game world: the story a guest gets on connecting is the host's SOLO story.
void put_host_story(const std::wstring& target) {
    if (g_host || save_io::world_start(target) == WorldStart::NewGame || !save_io::world_has_saves(target)) return;
    std::string why;
    if (!story_put_into_world(save_io::world_folder(target), &why)) {
        log::warn("Co-op worlds: the host's story was not put into the world: {}", why);
        toast("Your co-op world keeps its own story this time (" + why + ").");
    }
}

// The switch itself (its own thread): title screen, change the saves, Play (or New Game for a new-game world).
void run_switch(std::wstring target, std::string partner_name, WorldStart start) {
    const bool into = !target.empty();
    const std::string label = into ? "your co-op world with " + partner_name : "your own saves";
    const auto fail = [&](const std::string& why) {
        banner("");
        log::warn("Co-op worlds: switch to {} stopped: {}", narrow(target.empty() ? L"own saves" : target), why);
        const int n = ++g_failures;
        set_status("Could not switch to " + label + ": " + why +
                   (n < 5 ? ". Trying again in 30 s." : ". Or do it yourself: Esc > Others > Title Screen, then Play: "
                                                        "Co-op+ switches the saves at the title."));
        toast("Could not switch to " + label + ": " + why);
        {
            std::lock_guard lock(g_m);
            g_retry_after = GetTickCount64() + 30000;
        }
        g_switching = false;
    };
    log::info("Co-op worlds: switching to {} ({})", narrow(target.empty() ? L"own saves" : target), label);
    const bool begins = into && (start == WorldStart::NewGame || save_io::world_new_game_pending(target));
    banner(begins  ? "A New Journey\nWith " + partner_name + ". Returning to the title screen to begin; please wait."
           : into ? "Continuing with " + partner_name + "\nReturning to the title screen to load your co-op save; please wait."
                  : "Returning to Your Save\nReturning to the title screen to load it; please wait.");
    ++g_epoch;
    set_status("Switching to " + label + "...");
    if (!wait_front("Switching to " + label)) return fail("the game window was not in front");
    key(VK_ESCAPE, 40);
    if (!wait_for([] { return g_menu_open.load(); }, 4000)) {
        key(VK_ESCAPE, 40);
        if (!wait_for([] { return g_menu_open.load(); }, 4000)) {
            return fail("the menu did not open (close any open window, like a tutorial)");
        }
    }
    Sleep(800);
    if (!click(kOthersX, kOthersY)) return fail("the game window lost focus");
    Sleep(1200);
    if (!click(kTitleX, kTitleY)) return fail("the game window lost focus");
    Sleep(1200);
    if (!game_in_front()) return fail("the game window lost focus");
    key(VK_SPACE, 1500);  // "Return to the title screen?" Confirm (hold)
    // At the title: our character is gone for a while.
    if (!wait_for([] { return !g_in_world.load(); }, 30000)) return fail("the game did not go back to the title");
    Sleep(8000);
    std::string why;
    bool new_game = false;
    if (!save_io::set_world(target, &why, start)) {
        log::warn("Co-op worlds: {}; staying with the saves we had", why);
        toast("Could not open " + label + " (" + why + "); loading the saves you had.");
    } else if (into) {
        new_game = save_io::world_new_game_pending(target);
        put_host_story(target);
    }
    if (new_game) {
        // A new-game world whose new game has not started yet. New Game is a held Z (a short press is ignored,
        // 2026-11-05); then the game's agreement and difficulty, which the player answers. Pressed twice, 12 s
        // apart (the title ignores keys for a while after the return); Z does nothing on those two screens.
        banner("");
        set_status("New game in " + label + ": accept the agreement and choose the difficulty.");
        toast("New game in " + label + ": accept the agreement and choose the difficulty when they appear. "
              "Your own saves are untouched.");
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (!wait_front("New game in " + label)) return fail("the game window was not in front (press New Game, Z)");
            key('Z', 1500);
            if (wait_for([] { return sniffer_world_loading() || g_in_world.load(); }, 12000)) break;
        }
        log::info("Co-op worlds: New Game pressed in {}; waiting for the player's agreement and difficulty",
                  narrow(target));
        if (!wait_for([] { return g_in_world.load(); }, 900000)) {
            return fail("the new game did not start (at the title: New Game, Z)");
        }
        wait_for([] { return g_has_pos.load(); }, 5000);
        if (!at_new_game_start()) {
            // The player backed out of New Game and loaded a save instead (a copy of their own in this co-op save).
            log::warn("Co-op worlds: in {} but not at the new game's start: the new journey has not begun", narrow(target));
            set_status("");
            toast("This is a copy of your own save, not your new journey with " + partner_name + ". To begin it: "
                  "Esc > Others > Title Screen, then New Game.");
            g_failures = 0;
            g_switching = false;
            return;
        }
        log::info("Co-op worlds: the new game started in {}", narrow(target));
        save_io::world_mark_new_game_started(target);  // from now on this world loads with Play
        set_status("You are in " + label + " (a new game). Your own saves are untouched.");
        toast("Your new journey with " + partner_name + " begins. Your own saves are untouched.");
        g_failures = 0;
        g_switching = false;
        return;
    }
    // Play (E): the newest save of the saves now in use. The title takes a while to accept keys after the return
    // (a press 8 s in was ignored, 2026-11-04): press, then again every 12 s until the game starts loading.
    bool loading = false;
    for (int attempt = 0; attempt < 8 && !loading; ++attempt) {
        if (!wait_front("Loading " + label)) return fail("the game window was not in front (you are at the title: press E)");
        key('E', 80);
        loading = wait_for([] { return sniffer_world_loading() || g_in_world.load(); }, 12000);
    }
    if (!loading) return fail("the title screen did not start the game (press E)");
    banner("");
    if (!wait_for([] { return g_in_world.load(); }, 240000)) return fail("the save did not finish loading");
    log::info("Co-op worlds: now in {}", narrow(save_io::world().empty() ? L"own saves" : save_io::world()));
    set_status(save_io::world().empty() ? std::string() : "You are in " + label + ". Your own saves are untouched.");
    toast(save_io::world().empty() ? "Back in your own world (your own saves)."
                                   : "You are in " + label + ". Your own saves are untouched.");
    g_failures = 0;
    g_switching = false;
}

// At the title screen (out of the world, not loading): the saves can change right away, no keys needed. Covers a
// player who goes to the title by hand, and a game started while a co-op world is wanted.
void switch_at_title(const std::wstring& target, bool host, WorldStart start) {
    std::string why;
    g_ask = Ask::None;      // at the title the player went there: nothing to ask, no keys
    g_switch_card = false;
    ++g_epoch;
    if (!save_io::set_world(target, &why, start)) {
        g_status = "Could not open the saves at the title: " + why;
        return;
    }
    if (!target.empty() && !host && save_io::world_start(target) != WorldStart::NewGame &&
        save_io::world_has_saves(target) && !story_put_into_world(save_io::world_folder(target), &why)) {
        log::warn("Co-op worlds: the host's story was not put into the world: {}", why);
    }
    if (!target.empty() && save_io::world_new_game_pending(target)) g_mark_when_in = target;
    g_failures = 0;
    g_status.clear();
    g_toast = target.empty() ? "Co-op+\nYour own saves are back: press Play (E)."
              : save_io::world_new_game_pending(target)
                  ? "Co-op+\nYour new co-op game with " + g_partner_name +
                        " is ready: press New Game (hold Z), then accept and choose the difficulty."
                  : "Co-op+\nYour co-op world with " + g_partner_name + " is ready: press Play (E).";
    log::info("Co-op worlds: saves switched at the title to {}", narrow(target.empty() ? L"own saves" : target));
}

}  // namespace

void worlds_configure(bool enabled, const std::string& test_partner) {
    std::lock_guard lock(g_m);
    g_enabled = enabled;
    // The opt-in turns this on again with no partner (optin.cpp): a [dev] test partner set before stays (2026-11-05:
    // losing it made a one-PC test switch "back" to the player's own saves).
    if (!test_partner.empty()) g_test_partner = test_partner;
    log::info("Co-op worlds: {}{}", enabled ? "on (a save per friend; your own saves only when you play alone)" : "off",
              test_partner.empty() ? "" : " | [dev] partner " + test_partner);
}

void worlds_tick(const WorldsInput& in_raw) {
    WorldsInput in = in_raw;
    if (g_dev_host) in.host = true;
    g_in_world = in.in_world;
    g_has_pos = in.has_pos;
    g_pos_x = in.pos_x;
    g_pos_z = in.pos_z;
    g_menu_open = in.menu_open;
    g_host = in.host;
    std::lock_guard lock(g_m);
    if (!g_enabled) return;
    if (!g_test_partner.empty() && in.connected) in.partner_key = g_test_partner;
    if (in.connected && !in.partner_key.empty()) {
        if (!g_connected_since) {
            g_connected_since = in.now_ms ? in.now_ms : 1;
            if (!in.host) story_auto_request();  // the host's story, for our co-op world with them
        }
        g_gone_since = 0;
        g_partner_key = in.partner_key;
        g_partner_name = in.partner_name.empty() ? "your friend" : in.partner_name;
        if (in.now_ms - g_connected_since >= kJoinStableMs) g_want = world_for(g_partner_key);
        if (!in.host && g_host_said != WorldStart::Unknown && g_host_said_key.empty()) g_host_said_key = g_partner_key;
        // The host tells the guest how their world starts, once per connection (as soon as it is known).
        if (in.host && g_sent == WorldStart::Unknown) {
            const std::wstring w = world_for(g_partner_key);
            // An earlier version's co-op save waits for the host's answer (the card) before anything is said.
            const WorldStart s = save_io::world_exists(w) && !save_io::world_is_legacy(w) ? save_io::world_start(w)
                                 : g_choice_key == g_partner_key                          ? g_choice
                                                                                          : WorldStart::Unknown;
            if (s != WorldStart::Unknown) {
                g_out.push_back({kMsgStart, static_cast<uint8_t>(s == WorldStart::NewGame ? 1 : 2)});
                g_sent = s;
                log::info("Co-op worlds: told {} that our co-op world starts from {}", g_partner_name, start_text(s));
            }
        }
    } else {
        g_connected_since = 0;
        g_sent = WorldStart::Unknown;
        g_card = false;
        if (!g_gone_since) g_gone_since = in.now_ms ? in.now_ms : 1;
        if (in.now_ms - g_gone_since >= kLeaveStableMs) g_want.clear();
    }
    if (in.in_world || sniffer_world_loading()) {
        g_out_of_world_since = 0;
    } else if (!g_out_of_world_since) {
        g_out_of_world_since = in.now_ms ? in.now_ms : 1;
    }
    if (g_switching) return;
    const std::wstring active = save_io::world();
    if (!g_mark_when_in.empty() && in.in_world && in.has_pos) {
        if (g_mark_when_in == active) {
            if (at_new_game_start()) {
                save_io::world_mark_new_game_started(active);
                g_toast = "Co-op+\nYour new journey with " + g_partner_name + " begins. Your own saves are untouched.";
            } else {
                log::warn("Co-op worlds: {} was loaded with Play, not New Game: the new journey has not begun",
                          narrow(active));
                g_toast = "Co-op+\nThis is a copy of your own save, not your new journey with " + g_partner_name +
                          ". To begin it: Esc > Others > Title Screen, then New Game.";
            }
        }
        g_mark_when_in.clear();
    }
    if (g_want == active) {
        g_countdown_until = 0;
        g_failures = 0;
        g_ask = Ask::None;
        g_switch_card = false;
        return;
    }
    // A world with this friend that does not exist yet. Co-op+ (user, 2026-11-05: "start both members on a fresh
    // save before launching their co-op mode for the first time"): a pair's first co-op save is always a NEW GAME for
    // both. The host decides it (and tells the guest, World message); the guest waits for that word, so both games
    // agree even if one of them already had a world with the other. Nothing switches until it is known.
    // Co-op+ (2026-11-05): a co-op save made by an earlier version (a copy of the player's own progress). The host is
    // asked once: a new journey (the old save is kept as <name>-old-<date>, worlds_answer) or continue it; the guest's
    // game follows the host's word the same way.
    if (!g_want.empty() && g_want != active) {
        if (in.host && save_io::world_is_legacy(g_want) && g_choice_key != g_partner_key) {
            if (!g_card) log::info("Co-op worlds: {}'s co-op save is from an earlier version: asking", g_partner_name);
            g_card_key = g_partner_key;
            g_card_name = g_partner_name;
            g_card = true;
            g_countdown_until = 0;
            g_status = "A new journey with " + g_partner_name + "? (the card on screen)";
            return;
        }
        if (!in.host && save_io::world_exists(g_want) && g_host_said_key == g_partner_key) {
            std::string why;
            if (g_host_said == WorldStart::NewGame && save_io::world_start(g_want) != WorldStart::NewGame) {
                if (!save_io::world_archive(g_want, &why)) log::warn("Co-op worlds: old co-op save not set aside: {}", why);
            } else if (g_host_said == WorldStart::Continue && save_io::world_is_legacy(g_want)) {
                save_io::world_mark_continue(g_want);
            }
        }
    }
    WorldStart start = WorldStart::Continue;
    // The guest never enters a co-op save before the host's game has said how it starts (sent at once on every
    // connection): an earlier version's save at the title was entered before a "new journey" word could set it aside.
    if (!in.host && !g_want.empty() && g_host_said_key != g_partner_key) {
        g_countdown_until = 0;
        g_status = "Joining " + g_partner_name + ": waiting for their game.";
        return;
    }
    if (!g_want.empty() && !save_io::world_exists(g_want)) {
        if (in.host && g_choice_key != g_partner_key) {
            g_choice_key = g_partner_key;
            g_choice = WorldStart::NewGame;
            log::info("Co-op worlds: first co-op session with {}: our co-op save starts as a new game", g_partner_name);
        }
        start = in.host ? (g_choice_key == g_partner_key ? g_choice : WorldStart::Unknown)
                        : (g_host_said_key == g_partner_key ? g_host_said : WorldStart::Unknown);
        if (start == WorldStart::Unknown) {
            g_countdown_until = 0;
            if (in.host) {
                g_status.clear();
            } else {
                g_status = "Your first co-op session with " + g_partner_name + ": waiting for their game.";
            }
            return;
        }
    }
    if (g_out_of_world_since && in.now_ms - g_out_of_world_since > 10000) {  // sitting at the title
        switch_at_title(g_want, in.host, start);
        return;
    }
    if (g_failures >= 5 || in.now_ms < g_retry_after) return;
    // Only from the world itself (not a menu, not a loading screen), and only when the player says so.
    // Co-op+ (2026-11-05 playtest): the game going to the title by itself (keys, the mouse moving and clicking) made
    // the host think his PC had a virus. Now a card asks: "go now" (the mod does it, with a banner saying so) or
    // "I'll do it" (the player goes to the title; the saves change there with no keys at all, switch_at_title).
    if (!in.in_world || in.menu_open) return;
    const std::string label = g_want.empty() ? "your own saves" : "your co-op save with " + g_partner_name;
    if (g_ask_for != g_want || g_ask == Ask::None) {
        g_ask_for = g_want;
        g_ask = Ask::Pending;
        g_ask_into = !g_want.empty();
        g_ask_new = start == WorldStart::NewGame || (!g_want.empty() && save_io::world_new_game_pending(g_want));
        g_ask_name = g_partner_name;
        g_switch_card = true;
        log::info("Co-op worlds: asking whether to switch to {} now", narrow(g_want.empty() ? L"own saves" : g_want));
    }
    if (g_ask == Ask::Pending) {
        g_status = g_want.empty() ? "Your friend left: back to your own saves? (the card on screen)"
                                  : "Load your co-op save with " + g_partner_name + "? (the card on screen)";
        return;
    }
    if (g_ask == Ask::Manual) {
        g_status = "When you are ready for " + label + ": Esc > Others > Title Screen. Co-op+ changes the saves at the "
                   "title (no keys pressed); then press Play.";
        return;
    }
    g_ask = Ask::None;
    g_countdown_until = 0;
    g_switching = true;
    std::thread(run_switch, g_want, g_partner_name, start).detach();
}

bool worlds_switch_card_visible() { return g_switch_card.load(); }

SwitchCard worlds_switch_card() {
    std::lock_guard lock(g_m);
    return {g_ask_into, g_ask_new, g_ask_name};
}

void worlds_switch_answer(bool go_now) {
    std::lock_guard lock(g_m);
    if (!g_switch_card) return;
    g_switch_card = false;
    g_ask = go_now ? Ask::Go : Ask::Manual;
    log::info("Co-op worlds: switch {}", go_now ? "now (the player said go)" : "left to the player (at the title)");
}

std::string worlds_banner() {
    std::lock_guard lock(g_m);
    return g_banner;
}

uint32_t worlds_switch_epoch() { return g_epoch.load(); }

void worlds_dev_as_host(bool on) { g_dev_host = on; }

bool worlds_card_visible() { return g_card.load(); }

std::string worlds_card_partner() {
    std::lock_guard lock(g_m);
    return g_card_name;
}

void worlds_answer(bool new_game) {
    std::lock_guard lock(g_m);
    if (!g_card) return;
    g_card = false;
    g_choice_key = g_card_key;
    g_choice = new_game ? WorldStart::NewGame : WorldStart::Continue;
    g_status.clear();
    // The earlier version's co-op save: set aside for the new journey (kept, never deleted), or kept going.
    const std::wstring w = world_for(g_card_key);
    std::string why;
    if (new_game && save_io::world_exists(w) && !save_io::world_archive(w, &why)) {
        log::warn("Co-op worlds: old co-op save not set aside: {}", why);
    } else if (!new_game && save_io::world_is_legacy(w)) {
        save_io::world_mark_continue(w);
    }
    log::info("Co-op worlds: our co-op world with {} starts from {}", g_card_name, start_text(g_choice));
}

std::vector<std::vector<uint8_t>> worlds_take_outgoing() {
    std::lock_guard lock(g_m);
    std::vector<std::vector<uint8_t>> out;
    out.swap(g_out);
    return out;
}

void worlds_on_message(const std::vector<uint8_t>& bytes) {
    if (!bytes.empty() && bytes[0] == kMsgSharedXp) {  // Co-op+ (2026-11-07): the other player's XP gain
        xp_share_on_message(bytes);
        return;
    }
    if (!bytes.empty() && bytes[0] == kMsgPrologue) {  // Co-op+ (2026-11-07): the other player's prologue state
        prologue_on_message(bytes);
        return;
    }
    if (!bytes.empty() && bytes[0] >= kMsgLobbyFirst) {  // Co-op+ (2026-11-06): the title lobby's (coop_lobby.h)
        lobby_on_message(bytes);
        return;
    }
    if (bytes.size() < 2 || bytes[0] != kMsgStart || g_host) return;
    const WorldStart s = bytes[1] == 1 ? WorldStart::NewGame : bytes[1] == 2 ? WorldStart::Continue : WorldStart::Unknown;
    if (s == WorldStart::Unknown) return;
    std::lock_guard lock(g_m);
    g_host_said = s;
    g_host_said_key = g_partner_key;  // "" until the partner is known: worlds_tick fills it in
    log::info("Co-op worlds: the host says our co-op world with them starts from {}", start_text(s));
    if (!save_io::world_exists(world_for(g_partner_key)) || g_partner_key.empty()) {
        g_toast = s == WorldStart::NewGame
                      ? "Co-op+\nYour first journey with " + g_partner_name + ": you both begin at the start of the "
                        "story, in a co-op save of its own. Your own saves are untouched."
                      : "Co-op+\n" + g_partner_name + " chose to start your co-op world from their progress: you "
                        "keep your character and join their story. Your own saves are untouched.";
    }
}

std::string worlds_hud_text() {
    std::lock_guard lock(g_m);
    return g_status;
}

std::string worlds_take_toast() {
    std::lock_guard lock(g_m);
    std::string t;
    t.swap(g_toast);
    return t;
}

std::wstring worlds_active() { return save_io::world(); }

// ---- Co-op+ (2026-11-06): the title lobby (coop_lobby.h)
WorldsLobbyInfo worlds_lobby_info() {
    std::lock_guard lock(g_m);
    WorldsLobbyInfo i;
    i.enabled = g_enabled;
    i.partner_key = g_partner_key;
    if (g_partner_key.empty()) return i;
    const std::wstring w = world_for(g_partner_key);
    i.exists = save_io::world_exists(w) && !save_io::world_is_legacy(w);
    i.has_saves = i.exists && save_io::world_has_saves(w);
    i.new_game_pending = i.exists && save_io::world_new_game_pending(w);
    i.active = !save_io::world().empty() && save_io::world() == w;
    i.switching = g_switching.load();
    i.choice = g_host ? (g_choice_key == g_partner_key ? g_choice : WorldStart::Unknown)
                      : (g_host_said_key == g_partner_key ? g_host_said : WorldStart::Unknown);
    return i;
}

bool worlds_lobby_fresh(std::string* why) {
    std::lock_guard lock(g_m);
    if (g_partner_key.empty() || g_switching) {
        if (why) *why = g_partner_key.empty() ? "no friend yet" : "a switch is under way";
        return false;
    }
    const std::wstring w = world_for(g_partner_key);
    // Out of use first (at the title nothing is being saved), then set aside: kept as <name>-old-<date>, never deleted.
    if (save_io::world() == w) save_io::set_world(L"", nullptr);
    if (save_io::world_exists(w) && !save_io::world_archive(w, why)) return false;
    if (g_host) {
        g_choice_key = g_partner_key;
        g_choice = WorldStart::NewGame;
        g_sent = WorldStart::Unknown;  // told again on the next tick
    } else {
        g_host_said_key = g_partner_key;
        g_host_said = WorldStart::NewGame;
    }
    g_out_of_world_since = 1;  // at the title: the next tick makes the new world at once
    log::info("Co-op worlds: a fresh co-op save with {} (the old one, if any, kept aside)", g_partner_name);
    return true;
}

bool worlds_lobby_from_solo(std::string* why) {
    std::lock_guard lock(g_m);
    if (!g_host || g_partner_key.empty() || g_switching) {
        if (why) *why = !g_host ? "only the host" : g_partner_key.empty() ? "no friend yet" : "a switch is under way";
        return false;
    }
    // A new co-op save made from our own progress (a copy of our slots; ours are only read). An old one: set aside.
    const std::wstring w = world_for(g_partner_key);
    if (save_io::world() == w) save_io::set_world(L"", nullptr);
    if (save_io::world_exists(w) && !save_io::world_archive(w, why)) return false;
    g_choice_key = g_partner_key;
    g_choice = WorldStart::Continue;
    g_sent = WorldStart::Unknown;
    g_out_of_world_since = 1;
    log::info("Co-op worlds: a co-op save with {} from our solo progress", g_partner_name);
    return true;
}

void worlds_lobby_guest_new(int difficulty) {
    if (g_switching.exchange(true)) return;
    std::thread([difficulty] {
        // The guest's New Game behind the lobby's "waiting for the host" screen (user 2026-11-06: the guest never sees
        // setup): New Game (held Z), the agreement if the game shows one (scrolled to its end, accepted; the user's
        // decision for the guest, FLAGS F25), then the host's difficulty (the dialog lists Easy, Normal, Hard: W to the
        // top, S down to it) and Confirm.
        const auto loading = [] { return sniffer_world_loading() || g_in_world.load(); };
        const auto pick = [difficulty] {
            for (int i = 0; i < 3; ++i) key('W', 60), Sleep(150);
            for (int i = 0; i < difficulty; ++i) key('S', 60), Sleep(150);
            Sleep(200);
            key(VK_SPACE, 400);
        };
        const auto done = [](const std::string& s) {
            if (!s.empty()) set_status(s);
            g_switching = false;
        };
        if (!wait_front("Joining the host's new game")) return done("Click the game window to join the host's new game.");
        {
            std::lock_guard lock(g_m);
            if (!save_io::world().empty()) g_mark_when_in = save_io::world();
        }
        key('Z', 1500);
        Sleep(7000);
        if (!loading()) pick();
        if (!wait_for(loading, 5000)) {
            // The agreement: scroll it to its end with the mouse wheel over it, then hold Accept; then the difficulty.
            const HWND wnd = GetForegroundWindow();
            RECT rc{};
            if (GetClientRect(wnd, &rc)) {
                POINT c{rc.right / 2, rc.bottom / 2};
                ClientToScreen(wnd, &c);
                SetCursorPos(c.x, c.y);
            }
            for (int i = 0; i < 200; ++i) mouse_event(MOUSEEVENTF_WHEEL, 0, 0, static_cast<DWORD>(-WHEEL_DELTA), kOurInput), Sleep(5);
            Sleep(800);
            key(VK_SPACE, 1600);
            Sleep(5000);
            if (!loading()) pick();
        }
        const bool started = wait_for(loading, 15000);
        log::info("Co-op lobby: the guest's New Game {}", started ? "started" : "did not start (the player can finish it)");
        done(started ? "" : "Finish the new game's setup (choose the same difficulty as the host).");
    }).detach();
}

void worlds_lobby_continue() {
    std::lock_guard lock(g_m);
    if (!g_host || g_partner_key.empty()) return;
    g_choice_key = g_partner_key;
    g_choice = WorldStart::Continue;
    g_sent = WorldStart::Unknown;
}

void worlds_lobby_press(bool new_game) {
    if (g_switching.exchange(true)) return;
    std::thread([new_game] {
        const std::string label = save_io::world().empty() ? "your own save" : "your co-op save with " + g_partner_name;
        const auto done = [](const std::string& s) {
            if (!s.empty()) set_status(s);
            g_switching = false;
        };
        if (new_game) {
            for (int attempt = 0; attempt < 3; ++attempt) {
                if (!wait_front("New game in " + label)) return done("Press New Game (hold Z) to begin.");
                key('Z', 1500);
                if (wait_for([] { return sniffer_world_loading() || g_in_world.load(); }, 10000)) break;
            }
            log::info("Co-op lobby: New Game pressed in {}", narrow(save_io::world()));
            {
                std::lock_guard lock(g_m);
                if (!save_io::world().empty()) g_mark_when_in = save_io::world();
            }
            return done("");
        }
        for (int attempt = 0; attempt < 6; ++attempt) {
            if (!wait_front("Loading " + label)) return done("Press Play (E) to load.");
            // Already loading or in the world (the player pressed Play, or an earlier press took): no stray E (it is
            // "interact" in the world; tcB184 pressed it 2 min after the host was in).
            if (sniffer_world_loading() || g_in_world.load()) break;
            key('E', 80);
            if (wait_for([] { return sniffer_world_loading() || g_in_world.load(); }, 8000)) break;
        }
        log::info("Co-op lobby: Play pressed in {}", narrow(save_io::world().empty() ? L"own saves" : save_io::world()));
        done("");
    }).detach();
}

}  // namespace cdmp::net
