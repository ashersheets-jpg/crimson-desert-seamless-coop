#include "mod/native_layer.h"
#include "mod/net/coop_activity.h"
#include "mod/input/actions.h"
#include "mod/net/coop_worlds.h"
#include "mod/optin.h"
#include "mod/input/pad_input.h"

#include <windows.h>

#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <format>
#include <map>
#include <mutex>
#include <thread>

#include "mod/game/ui_state.h"
#include "mod/log.h"
#include "mod/net/session_summary.h"
#include "mod/net/boss_safe.h"
#include "mod/net/coop_duel.h"
#include "mod/net/coop_hunt.h"
#include "mod/net/coop_sync.h"
#include "mod/net/coop_revive.h"
#include "mod/net/coop_travel.h"
#include "mod/net/coop_chat.h"
#include "mod/net/kill_feed.h"
#include "mod/net/coop_race.h"
#include "mod/net/coop_hideseek.h"
#include "mod/net/coop_fishing.h"
#include "mod/net/scoreboard.h"
#include "mod/settings_menu.h"
#include "mod/render/coop_page.h"
#include "mod/net/aggro_test.h"
#include "mod/net/enemy_relay.h"
#include "mod/net/loot_twin.h"
#include "mod/net/party_quests.h"
#include "mod/net/local_capture.h"
#include "mod/net/puppet_visibility.h"
#include "mod/net/remote_puppet.h"
#include "mod/net/stage_cinema.h"

namespace cdmp::net {
namespace {

constexpr uint16_t kOpActivateMiniGameReq = 2507;  // u16 minigame id | u32 our player
constexpr uint16_t kOpLeaveMiniGameReq = 2379;     // u8 4 | u32 actor | 14 x 0 | u32 stage key
constexpr uint16_t kOpSpawnMiniGameCharacterReq = 3124;  // every match: the NPC opponent is spawned
constexpr uint16_t kOpJoinMiniGameReq = 2151;     // u8 4 | u16 minigame id | u32 actor (us, then the NPC opponent)
constexpr uint16_t kOpStartActionReq = 2202;      // our client also sends the NPC opponent's actions
constexpr float kNearM = 100.0f;                   // the user's rule: cutscenes are shared within 100 m
constexpr uint32_t kSkipTestDelayMs = 6000;

enum Type : uint8_t { kCutsceneOn = 1, kCutsceneOff = 2, kSkip = 3, kGameOn = 4, kGameOff = 5, kVersus = 6, kRps = 10, kChallenge = 11, kDuel = 12, kQuest = 13, kHunt = 14, kMenu = 16, kLooted = 17, kReviveYou = 18, kWanted = 19, kTravel = 20, kEvent = 21, kChat = 22, kKill = 23, kRace = 24, kTitle = 25, kHide = 26, kFish = 27, kBye = 28 };
std::atomic<bool> g_bye{false};  // the host is ending the session (tcB291)

// Co-op+ (T22): world events (reinforcements, bounty hunters, camp and outpost activity) are stages our server starts
// (PrepareSequencerAck); their fighters are enemies, which the guest already gets as copies near the host. What the
// other player lacks is knowing it is happening: kEvent (u32 stage key) -> "<name>: <event>" notice.
struct EventName {
    uint32_t key;
    const char* name;
};
constexpr EventName kEventNames[] = {
#include "mod/net/event_names.inc"
};
const char* event_name(uint32_t key) {
    const auto it = std::lower_bound(std::begin(kEventNames), std::end(kEventNames), key,
                                     [](const EventName& e, uint32_t k) { return e.key < k; });
    return it != std::end(kEventNames) && it->key == key ? it->name : nullptr;
}
std::atomic<uint32_t> g_event_test{0};  // [dev] event_test: a stage key announced 20 s after connecting

// Co-op+ (T09): a player in a menu (map, inventory, pause...) has their own world paused, while the other player's
// world goes on with their character standing in it. So: each side tells the other when it opens / closes a menu
// (kMenu: u8 open, again every kMenuResendMs); the other side's enemies leave that character alone meanwhile
// (aggro_set_partner_in_menu) and its name tag says "in a menu"; and blows that reach us while we are in a menu
// are not applied (player_hits.cpp).
constexpr uint32_t kMenuResendMs = 3000;
std::atomic<bool> g_partner_menu{false};

// Versus arm wrestling (D35): while both players are in a match, the host owns one gauge,
// 50 + kPushGain x (host's E presses - guest's E presses), clamped to 0..100, and writes it into its game every tick;
// the guest writes 100 - that into its own. 100 = a win, 0 = a loss: each game then ends its match by itself.
constexpr float kPushGain = 2.0f;
constexpr uint32_t kVersusSendMs = 100;
std::atomic<uint32_t> g_e_presses{0};     // E key-downs (the hook counts them; not ours)
std::atomic<bool> g_match_on{false};      // between SpawnMiniGameCharacterReq and LeaveMiniGameReq
std::atomic<uint32_t> g_match_stage{0};   // the match's minigame stage key (0 = unknown)

// The other player in the NPC opponent's seat (F9, user 2026-11-03: "we also need to see my friend sitting in the
// NPC's seat during arm wrestling"). Our client runs the match against an NPC it spawns at the table; the other
// player sits in the player's seat of their own table and plays the same actions as that NPC does (capture
// 2026-11-02: both seats play 837487D4 / 35DD392F..., 1.07 m apart, facing each other). So while both are in a
// versus match, their copy is drawn at the NPC's transform (its own actions from our client) and the NPC is hidden.
struct Opponent {
    uint32_t key = 0;     // the NPC opponent of our match (its JoinMiniGameReq)
    bool posed = false;   // pos / rot from one of its StartActionReq
    proto::Vec3 pos;
    proto::Quat rot;
};
std::mutex g_opp_mutex;
Opponent g_opp;
bool g_partner_game = false;  // between their kGameOn and kGameOff (under g_mutex)
bool g_seated = false;        // the copy is in the NPC's seat (activity tick)

// The minigames that use the strength gauge the versus shares (arm wrestling's stages, hand wrestling). Any other
// minigame keeps its own rules: the versus never writes into it.
bool is_gauge_game(uint32_t stage) {
    switch (stage) {
        case 16005100: case 1001987: case 1001988: case 1003873: case 1003875: case 1018693: return true;
        default: return false;
    }
}
std::atomic<bool> g_seat_test{false};     // [dev] seat_test (coop_activity.h)
std::atomic<int> g_versus_test_rate{0};   // [dev] versus_test: a simulated other player pushing this many times/s
struct Versus {
    bool live = false;          // our match is running (gauge readable, no cutscene)
    uint32_t base = 0;          // our presses when the versus started
    bool started = false;
    float gauge = 50.0f;        // the shared gauge, as the host sees it
    uint32_t last_send = 0;
    uint32_t live_since = 0;
    bool partner_live = false;  // from their last kVersus
    uint32_t partner_count = 0; // their presses since their match started
    float partner_gauge = 50.0f;
    uint32_t partner_seen = 0;
    bool announced = false;
    float last_ours = 50.0f;    // our side of the gauge at the last versus tick
} g_vs;

// Rock, paper, scissors duel (D39). The town RPS minigame cannot be turned into player against player: the child's
// hand comes from the stage's server-side random roll (AddStageRandomRateAck), and the server refuses an outcome that
// does not match it (2026-11-02: a rewritten pick transition got no answer). So an RPS challenge is a duel the mod runs
// itself, anywhere: each player presses U (scissors) / F (paper) / E (rock), the keyboard hook keeps those keys from
// the game meanwhile, both hands are shown at once when both have picked, best of three, then the next match.
constexpr uint32_t kRpsTestDelayMs = 4000;   // [dev] rps_test: the simulated player picks this long after us
constexpr uint32_t kRpsRevealMs = 3500;      // both hands on screen before the next round
constexpr uint32_t kRpsNameStage = 1001949;  // Hernand_MiniGame_RockPaperSci: names the game in messages
constexpr int kRpsKeys[3] = {'U', 'F', 'E'};  // scissors, paper, rock
const char* const kHands[3] = {"scissors", "paper", "rock"};
std::atomic<bool> g_rps_hold{false};  // the hook keeps U/F/E from the game (a duel is waiting for our hand)
std::atomic<int> g_rps_pressed{-1};   // the hand the hook caught (-1 none)
std::atomic<int> g_rps_test{-1};      // [dev] rps_test: a simulated other player always showing this hand
struct Rps {
    int ours = -1, theirs = -1;  // this round's hands (-1 not yet)
    uint32_t picked_at = 0;
    uint32_t reveal_until = 0;   // both hands shown until then
    std::string reveal;          // what is shown
    int shown_ours = -1, shown_theirs = -1, winner = 0;  // the hands of the reveal
} g_rps;

// Challenges (D40, user 2026-11-02: "give me the option to challenge a friend in the mini games"). Versus play only
// happens inside an accepted challenge: Shift+F5 invites the other player, to arm / hand wrestling when standing at
// such a table, otherwise to a rock-paper-scissors duel (anywhere). They accept with Shift+F5 (Shift+F6
// declines; an invite lapses after a minute). Wrestling is played at the same table in each world, the shared gauge
// above making it player against player; the duel runs in the mod. A running series score is kept until either player
// ends the challenge (Shift+F5 again).
// Event kChallenge: u8 action | u32 the game's stage key.
enum Game : uint8_t { kNoGame = 0, kGameWrestle = 1, kGameRps = 2, kGameDuel = 3, kGameHunt = 4, kGameRace = 5, kGameHide = 6, kGameFish = 7 };
enum ChallengeAction : uint8_t { kInvite = 1, kAccept = 2, kDecline = 3, kEnd = 4 };
enum ChallengeState : uint8_t { kChNone, kChPick, kChSent, kChReceived, kChOn };
constexpr uint32_t kWrestleStages[] = {16005100, 1001987, 1001988, 1003873, 1003875, 1018693};
constexpr float kChallengeNearM = 30.0f;  // how close to the table Shift+F5 finds it
constexpr uint32_t kInviteMs = 60000;
constexpr uint32_t kPickMs = 60000;  // the menu closes by itself (B16b: 60 s, was 20 s)
struct Challenge {
    ChallengeState state = kChNone;
    uint32_t stage = 0;          // the game's stage key
    uint32_t since = 0;
    int ours = 0, theirs = 0;    // series: matches won
    int round_ours = 0, round_theirs = 0;  // RPS: rounds won in the current match (first to 2)
    bool inviter = false;        // we sent the invite (a duel's ground is ours to place)
    std::vector<uint32_t> options;  // kChPick: the games to choose from, and the chosen one
    size_t pick = 0;
} g_ch;
// B16b (2026-11-09): the player interaction menu's two non-game entries (options), and its keys caught by the hook.
constexpr uint32_t kMenuTrade = 1, kMenuLeave = 2;
std::atomic<bool> g_menu_select{false}, g_menu_cancel{false};
std::atomic<bool> g_menu_hold{false};  // the picker is open: the keyboard hook keeps the arrow keys
std::atomic<int> g_menu_step{0};       // arrow presses it caught (-1 left, +1 right, summed)
// Shared world (2026-11-10): the interaction menu while a challenge is sent, received or on -- its answers instead of
// the game list ("Accept", "Decline", "End the challenge", "Leave"). Under g_mutex.
enum CtxCode : int { kCtxLeave = 0, kCtxGo = 1, kCtxDecline = 2 };
struct CtxMenu {
    bool open = false;
    uint32_t since = 0;
    size_t pick = 0;
    std::vector<std::pair<std::string, int>> items;
} g_ctx;
// [dev] challenge_test: 1 = a simulated other player accepts our invites; 2 = they also challenge us to a duel once.
std::atomic<int> g_challenge_test{0};

Game game_of(uint32_t stage);

// A text that goes through std::vformat later (g_toasts): its literal braces (button tokens) doubled.
std::string escape_braces(const std::string& t) {
    std::string r;
    for (char ch : t) {
        r += ch;
        if (ch == '{' || ch == '}') r += ch;
    }
    return r;
}

// What to do once a challenge is on.
std::string how_to_play(uint32_t stage) {
    switch (game_of(stage)) {
        case kGameRps: return "Pick your hand:  {RpsScissors} scissors  {RpsPaper} paper  {RpsRock} rock  (first to 2)";
        case kGameDuel: return "Bring your opponent to their knees. Stay within the duel ground!";
        case kGameHunt: return "Bring down the most game in 3 minutes: big game counts more!";
        case kGameRace: return "The host's map waypoint is the finish: first within 12 m wins!";
        case kGameHide: return "You count first, your friend hides. Then swap: first to 2 rounds.";
        case kGameFish: return "Six minutes to catch the most fish: river 1, sea 2, legendary 5.";
        default: return "Both sit at the table and start a match, then mash {Mash}!";
    }
}

uint32_t g_last_game = 0;  // Co-op+ (T42): the last game we challenged to (the picker opens on it)
// Co-op+ (2026-11-05): the last match result, shown as the big title card for a few seconds (activity_match_hud).
std::string g_match_title, g_match_subtitle;
bool g_match_won = false;
uint32_t g_match_at = 0;

Game game_of(uint32_t stage) {
    if (std::find(std::begin(kWrestleStages), std::end(kWrestleStages), stage) != std::end(kWrestleStages)) return kGameWrestle;
    if (stage == kRpsNameStage) return kGameRps;
    if (stage == kDuelStage) return kGameDuel;
    if (stage == kHuntStage) return kGameHunt;
    if (stage == kRaceStage) return kGameRace;
    if (stage == kHideStage) return kGameHide;
    if (stage == kFishStage) return kGameFish;
    return kNoGame;
}

std::mutex g_mutex;
uint32_t g_seq_out = 1;
uint32_t g_seq_in = 0;               // newest sequence seen from the other player
bool g_partner_cutscene = false;
std::array<float, 3> g_partner_cut_pos{};
std::vector<std::string> g_toasts;   // filled by events, drained by the tick
bool g_partner_skip = false;         // the other player pressed skip in their current cutscene
std::array<float, 3> g_skip_from{};
bool g_our_skip = false;             // we pressed skip in our current cutscene
bool g_released = false;             // both pressed: our Esc went to the game
bool g_last_cutscene = false;
uint32_t g_cut_since = 0;
std::atomic<bool> g_hold_esc{false};  // the keyboard hook holds back the player's Esc
std::atomic<uint64_t> g_hold_seen{0};  // GetTickCount64 of the last tick that wanted it (stale = let Esc through)
std::atomic<bool> g_esc_held{false};  // it did (a press to count as our skip)
constexpr ULONG_PTR kOurKey = 0xC0C0A7;  // dwExtraInfo of the Esc we press ourselves (the hook lets it through)
bool g_skip_test_done = false;
std::atomic<bool> g_skip_test{false};

void send(Type t, const void* data, size_t n) {
    std::vector<uint8_t> b(5 + n);
    uint32_t seq;
    {
        std::lock_guard lock(g_mutex);
        seq = g_seq_out++;
    }
    std::memcpy(b.data(), &seq, 4);
    b[4] = t;
    if (n) std::memcpy(b.data() + 5, data, n);
    capture_emit_activity(std::move(b));
}

float dist_xz(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return std::hypot(a[0] - b[0], a[2] - b[2]);
}

void press_escape() {
    const UINT scan = MapVirtualKeyW(VK_ESCAPE, MAPVK_VK_TO_VSC);
    keybd_event(VK_ESCAPE, static_cast<BYTE>(scan), 0, kOurKey);
    Sleep(40);
    keybd_event(VK_ESCAPE, static_cast<BYTE>(scan), KEYEVENTF_KEYUP, kOurKey);
}

bool game_in_front() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// Low-level keyboard hook on its own thread (it needs a message loop): swallows Esc while g_hold_esc is set and the
// game is in front, except the Esc we press ourselves. Every other key passes untouched.
LRESULT CALLBACK keyboard_hook(int code, WPARAM wp, LPARAM lp) {
    // Co-op+ (T24): the open chat line takes every key; the quick list takes the arrows.
    if (code == HC_ACTION && game_in_front()) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
        if (k->dwExtraInfo != kOurKey && chat_hook_key(k->vkCode, k->scanCode, down)) return 1;
        if (k->dwExtraInfo != kOurKey && render::coop_page_hook_key(k->vkCode, down)) return 1;  // the Co-op page
        if (k->dwExtraInfo != kOurKey && settings::hook_key(k->vkCode, down)) return 1;  // Co-op+ (T30)
        if (chat_quick_open() && (k->vkCode == VK_LEFT || k->vkCode == VK_RIGHT)) {
            if (down) chat_quick_step(k->vkCode == VK_LEFT ? -1 : 1);
            return 1;
        }
    }
    if (code == HC_ACTION && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        if (k->vkCode == 'E' && k->dwExtraInfo != kOurKey && !(k->flags & LLKHF_UP)) g_e_presses.fetch_add(1);
    }
    if (code == HC_ACTION && g_rps_hold.load() && GetTickCount64() - g_hold_seen.load() < 500) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        for (int hand = 0; hand < 3; ++hand) {
            if (k->vkCode == static_cast<DWORD>(kRpsKeys[hand]) && game_in_front()) {
                if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) g_rps_pressed = hand;
                return 1;
            }
        }
    }
    if (code == HC_ACTION && g_menu_hold.load() && GetTickCount64() - g_hold_seen.load() < 500) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        if ((k->vkCode == VK_LEFT || k->vkCode == VK_RIGHT) && game_in_front()) {
            if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) g_menu_step.fetch_add(k->vkCode == VK_LEFT ? -1 : 1);
            return 1;
        }
        // B16b: the menu as the game's own menus: up / down (W / S) choose, Space / Enter select, Esc leaves.
        const DWORD vk = k->vkCode;
        if ((vk == VK_UP || vk == VK_DOWN || vk == 'W' || vk == 'S') && game_in_front()) {
            if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) g_menu_step.fetch_add(vk == VK_UP || vk == 'W' ? -1 : 1);
            return 1;
        }
        if ((vk == VK_SPACE || vk == VK_RETURN) && game_in_front()) {
            if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) g_menu_select = true;
            return 1;
        }
        if (vk == VK_ESCAPE && k->dwExtraInfo != kOurKey && game_in_front()) {
            if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) g_menu_cancel = true;
            return 1;
        }
    }
    if (code == HC_ACTION && g_hold_esc.load() && GetTickCount64() - g_hold_seen.load() < 500) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        if (k->vkCode == VK_ESCAPE && k->dwExtraInfo != kOurKey && game_in_front()) {
            if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) g_esc_held = true;
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void start_hook() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread([] {
            if (!SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_hook, GetModuleHandleW(nullptr), 0)) {
                log::warn("Activity: keyboard hook not installed ({}); skips will not wait for everyone",
                          GetLastError());
                return;
            }
            MSG m;
            while (GetMessageW(&m, nullptr, 0, 0) > 0) {
            }
        }).detach();
    });
}

}  // namespace

std::string activity_minigame_name(uint32_t stage) {
    switch (stage) {
        case 16005100: case 1001987: case 1001988: case 1003873: case 1003875: return "arm wrestling";
        case 1018693: return "hand wrestling";
        case 16000200: case 1000804: return "the archery contest";
        case 1016639: return "the firearm contest";
        case 1000750: case 1012872: return "an unarmed duel";
        case 1018347: return "a wrestling match";
        case 1000302: return "a spear duel";
        case 1018695: return "a sword and shield duel";
        case 1001120: case 1002460: case 1023356: return "a horse race";
        case 1001949: case 1001963: case 1001964: case 1001969: case 16000600: case 1001970: case 1001974:
            return "rock, paper, scissors";
        case 1000219: case 1001269: return "Duo";
        case 1001524: case 1018696: return "Five-Card";
        case kDuelStage: return "a duel";
        case kHuntStage: return "a hunting contest";
        case kRaceStage: return "a race to the waypoint";
        case kHideStage: return "hide and seek";
        case kFishStage: return "a fishing contest";
        default: return "a minigame";
    }
}

bool activity_take_bye() { return g_bye.exchange(false); }
void activity_send_bye() { send(kBye, nullptr, 0); }
void activity_send(uint8_t type, const std::vector<uint8_t>& data) { send(static_cast<Type>(type), data.data(), data.size()); }

void activity_set_skip_test(bool on) { g_skip_test = on; }
void activity_set_versus_test(int presses_per_s) { g_versus_test_rate = presses_per_s; }
bool activity_partner_in_menu() { return g_partner_menu.load(); }
void activity_set_seat_test(bool on) { g_seat_test = on; }
void activity_set_event_test(uint32_t key) { g_event_test = key; }

namespace {

void write_gauge(uintptr_t a, float v) {
    if (a) *reinterpret_cast<volatile float*>(a) = v;  // a writable float (game::minigame_gauge_address checked it)
}

// The other player's copy takes the NPC opponent's seat while `want` (both in a versus match) and the NPC's place is
// known; the NPC is hidden meanwhile.
void seat_tick(bool want) {
    Opponent o;
    {
        std::lock_guard lock(g_opp_mutex);
        o = g_opp;
    }
    const bool seat = want && o.key && o.posed;
    if (seat) {
        puppet_set_seat(o.pos, o.rot);  // also follows the NPC if the game moves it
        puppet_hide_tick();
    }
    if (seat == g_seated) return;
    g_seated = seat;
    if (!seat) puppet_set_seat(std::nullopt);
    puppet_hide_actor(seat ? o.key : 0);
    log::info("Activity: the other player's copy {} the opponent's seat (NPC 0x{:08X})", seat ? "takes" : "leaves", o.key);
}

// Versus arm wrestling (see kPushGain): every tick of the network loop.
void versus_tick(ActivityTick& out, bool connected, bool host, bool cut, const std::string& who, uint32_t now) {
    const uintptr_t ga = game::minigame_gauge_address();
    float g = 0.0f;
    if (ga) g = *reinterpret_cast<volatile float*>(ga);
    const bool live = g_match_on.load() && is_gauge_game(g_match_stage.load()) && !cut && ga && g > 0.0f && g <= 100.0f;
    const std::string game = activity_minigame_name(g_match_stage.load());
    const int test_rate = g_versus_test_rate.load();
    std::lock_guard lock(g_mutex);
    // Only inside an accepted wrestling challenge (or the one-PC test): otherwise a match against the NPC, as normal.
    const bool challenged = (g_ch.state == kChOn && game_of(g_ch.stage) == kGameWrestle) || test_rate > 0;
    seat_tick(g_match_on.load() && is_gauge_game(g_match_stage.load()) &&
              ((challenged && (connected || test_rate > 0) && (g_partner_game || test_rate > 0)) || g_seat_test.load()));
    Versus& v = g_vs;
    if (live && !v.live) {
        v.base = g_e_presses.load();
        v.live_since = now;
        v.started = false;
        v.announced = false;
    }
    v.live = live;
    const uint32_t mine = live ? g_e_presses.load() - v.base : 0;
    if (!challenged) {
        v.partner_live = false;
    } else if (test_rate > 0 && live) {  // one-PC test: a simulated other player in the same match
        v.partner_live = true;
        v.partner_count = static_cast<uint32_t>(static_cast<uint64_t>(now - v.live_since) * test_rate / 1000);
        v.partner_seen = now;
        host = true;
    } else if (!connected || GetTickCount() - v.partner_seen > 1500) {
        v.partner_live = false;
    }
    if ((connected || test_rate > 0) && now - v.last_send >= kVersusSendMs && (live || v.started)) {
        v.last_send = now;
        uint8_t b[9];
        b[0] = live ? 1 : 0;
        std::memcpy(b + 1, &mine, 4);
        std::memcpy(b + 5, &v.gauge, 4);
        if (connected) {
            std::vector<uint8_t> pkt(5 + 9);
            const uint32_t seq = g_seq_out++;
            std::memcpy(pkt.data(), &seq, 4);
            pkt[4] = kVersus;
            std::memcpy(pkt.data() + 5, b, 9);
            capture_emit_activity(std::move(pkt));
        }
    }
    if (!(live && v.partner_live)) {
        if (!live && v.started && !v.announced) {
            // The game ends the match itself near the ends of the gauge (~96 / ~4): the last value tells who won.
            v.announced = true;
            (v.last_ours > 50.0f ? g_ch.ours : g_ch.theirs) += 1;
            out.toast = std::format("{}   Series: you {} - {} {}",
                                    v.last_ours > 50.0f ? std::format("You beat {} at {}!", who, game)
                                                        : std::format("{} won the {}", who, game),
                                    g_ch.ours, g_ch.theirs, who);
            log::info("Activity: versus {} over: {} ({:.0f})", game, v.last_ours > 50.0f ? "we won" : "they won",
                      v.last_ours);
        }
        if (!live) v.started = false;
        return;  // a match against the game's NPC, as normal
    }
    if (!v.started) {
        v.started = true;
        log::info("Activity: versus {} with {} ({})", game, who, host ? "we hold the gauge" : "they hold the gauge");
    }
    if (host) {
        v.gauge = std::clamp(50.0f + kPushGain * (static_cast<float>(mine) - static_cast<float>(v.partner_count)), 0.0f,
                             100.0f);
        write_gauge(ga, v.gauge);
    } else {
        v.gauge = 100.0f - v.partner_gauge;
        write_gauge(ga, std::clamp(v.gauge, 0.0f, 100.0f));
    }
    const float ours = host ? v.gauge : 100.0f - v.partner_gauge;
    v.last_ours = ours;
    std::string title = game;
    if (!title.empty()) title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(title[0])));
    out.banner = std::format("{} vs {}: mash {{Mash}}!   You {:.0f} - {:.0f} {}", title, who, ours, 100.0f - ours, who);
}

// The pad's side of what the keyboard hook catches (input/actions.h): mash presses, RPS hands, the cutscene skip.
// XInput pads' buttons are kept from the game while a Co-op+ box wants them.
void pad_tick() {
    // Co-op+ (T30): the F8 settings take the D-pad while the panel is open.
    if (settings::panel_open() && !input::chord_held()) {
        static input::Edge up, down, left, right;
        const uint16_t b = input::pad_buttons();
        input::mask_from_game(input::kPadDpad, 150);
        const int dy = up(b & input::kPadUp) ? -1 : down(b & input::kPadDown) ? 1 : 0;
        const int dx = left(b & input::kPadLeft) ? -1 : right(b & input::kPadRight) ? 1 : 0;
        if (dx || dy) settings::step(dx, dy);
        return;
    }
    static input::Edge mash, scissors, paper, rock, skip;
    if (mash(input::action_down(input::Action::Mash, false)) && g_match_on.load()) g_e_presses.fetch_add(1);
    const bool rps = g_rps_hold.load() && GetTickCount64() - g_hold_seen.load() < 500;
    if (rps) input::mask_from_game(input::kPadX | input::kPadY | input::kPadB, 150);
    const bool s = scissors(input::action_down(input::Action::RpsScissors, false));
    const bool p = paper(input::action_down(input::Action::RpsPaper, false));
    const bool r = rock(input::action_down(input::Action::RpsRock, false));
    if (rps && (s || p || r)) g_rps_pressed = s ? 0 : p ? 1 : 2;
    const bool hold = g_hold_esc.load() && GetTickCount64() - g_hold_seen.load() < 500;
    if (hold) input::mask_from_game(input::kPadStart | input::kPadB, 150);
    if (skip(input::action_down(input::Action::Skip, false)) && hold) g_esc_held = true;
    static input::Edge prev, next;
    const bool menu = g_menu_hold.load() && GetTickCount64() - g_hold_seen.load() < 500;
    const bool quick = chat_quick_open();  // Co-op+ (T24)
    if (menu || quick) input::mask_from_game(input::kPadLeft | input::kPadRight, 150);
    // The interaction menu on a pad, as the game's own lists: D-pad up / down choose, A selects, B leaves.
    static input::Edge m_up, m_down, m_sel, m_back;
    const uint16_t pb = input::chord_held() ? uint16_t{0} : input::pad_buttons();
    const bool u_down = m_up((pb & input::kPadUp) != 0), d_down = m_down((pb & input::kPadDown) != 0);
    const bool a_down = m_sel((pb & input::kPadA) != 0), b_down = m_back((pb & input::kPadB) != 0);
    if (menu) {
        input::mask_from_game(input::kPadUp | input::kPadDown | input::kPadA | input::kPadB, 250);
        if (u_down) g_menu_step.fetch_add(-1);
        if (d_down) g_menu_step.fetch_add(1);
        if (a_down) g_menu_select = true;
        if (b_down) g_menu_cancel = true;
    }
    const bool p_down = prev(input::action_down(input::Action::MenuPrev, false) && !input::chord_held());
    const bool n_down = next(input::action_down(input::Action::MenuNext, false) && !input::chord_held());
    if (p_down && menu) g_menu_step.fetch_add(-1);
    if (n_down && menu) g_menu_step.fetch_add(1);
    if (p_down && quick) chat_quick_step(-1);
    if (n_down && quick) chat_quick_step(1);
}

// The rock-paper-scissors duel (see Rps): every tick of the network loop.
void rps_tick(ActivityTick& out, bool connected, const std::string& who) {
    const uint32_t now = GetTickCount();
    const int test = g_rps_test.load();
    int send_hand = -1;
    {
        std::lock_guard lock(g_mutex);
        Rps& r = g_rps;
        Challenge& c = g_ch;
        const bool on = c.state == kChOn && game_of(c.stage) == kGameRps;
        if (!on) {
            g_rps_hold = false;
            g_rps_pressed = -1;
            r = Rps{};
            return;
        }
        const bool picking = static_cast<int32_t>(now - r.reveal_until) >= 0;
        g_rps_hold = picking;
        const int pressed = g_rps_pressed.exchange(-1);
        if (picking && pressed >= 0 && r.ours < 0) {
            r.ours = pressed;
            r.picked_at = now;
            send_hand = pressed;
            log::info("Activity: rock, paper, scissors: we picked {}", kHands[pressed]);
        }
        if (test >= 0 && r.ours >= 0 && r.theirs < 0 && now - r.picked_at > kRpsTestDelayMs) r.theirs = test;
        if (picking && r.ours >= 0 && r.theirs >= 0) {
            // Scissors 0 beats paper 1, paper 1 beats rock 2, rock 2 beats scissors 0: a beats b when a == (b + 2) % 3.
            const bool win = r.ours == (r.theirs + 2) % 3, lose = r.theirs == (r.ours + 2) % 3;
            if (win) ++c.round_ours;
            if (lose) ++c.round_theirs;
            r.reveal = std::format("You: {}   {}: {}   -   {}", kHands[r.ours], who, kHands[r.theirs],
                                   win ? std::string("you win the round") : lose ? who + " wins the round" : "a draw");
            log::info("Activity: rock, paper, scissors: {} vs {} ({})", kHands[r.ours], kHands[r.theirs],
                      win ? "won" : lose ? "lost" : "draw");
            if (c.round_ours == 2 || c.round_theirs == 2) {
                const bool match = c.round_ours == 2;
                (match ? c.ours : c.theirs) += 1;
                out.toast = std::format("{} rock, paper, scissors {} - {}!   Series: you {} - {} {}",
                                        match ? std::string("You win") : who + " wins", std::max(c.round_ours, c.round_theirs),
                                        std::min(c.round_ours, c.round_theirs), c.ours, c.theirs, who);
                log::info("Activity: rock, paper, scissors match: {} ({} - {})", match ? "we won" : "they won",
                          c.round_ours, c.round_theirs);
                c.round_ours = c.round_theirs = 0;
            }
            r.shown_ours = r.ours, r.shown_theirs = r.theirs, r.winner = win ? 1 : lose ? 2 : 0;
            r.ours = r.theirs = -1;
            r.reveal_until = now + kRpsRevealMs;
            g_rps_hold = false;
        }
        const std::string score = std::format("you {} - {} {}", c.round_ours, c.round_theirs, who);
        if (!picking) {
            out.banner = r.reveal + "   (" + score + ")";
        } else if (r.ours < 0) {
            out.banner = std::format("Rock, paper, scissors vs {} ({}, first to 2):   {{RpsScissors}} scissors   {{RpsPaper}} paper   {{RpsRock}} rock{}", who,
                                     score, r.theirs >= 0 ? std::format("   ({} has picked)", who) : "");
        } else {
            out.banner = std::format("Rock, paper, scissors: you picked {}; waiting for {}   ({})", kHands[r.ours], who, score);
        }
    }
    if (send_hand >= 0 && connected) {
        const uint8_t h = static_cast<uint8_t>(send_hand);
        send(kRps, &h, 1);
    }
}

std::atomic<bool> g_menu_request{false};  // B16b: activity_request_menu
std::atomic<bool> g_hold_prompt{false};   // B16b: activity_set_hold_prompt

// Shared world (2026-11-10): takes the old hotkey hints out of a line: a bracket that names {Challenge} / {Decline}
// goes ("(first to 2;  {Challenge} stops)" -> "(first to 2)"), so does a trailing "{Challenge} to accept".
void strip_key_hints(std::string& s) { input::strip_unbound_hints(s); }

// Challenges (see Challenge): Shift+F5 / Shift+F6, invites lapsing, the RPS child made willing, banners.
void challenge_tick(ActivityTick& out, bool connected, bool focused, std::optional<std::array<float, 3>> me,
                    const std::string& who) {
    const uint32_t now = GetTickCount();
    // Shift, not Ctrl: Ctrl is a game key (it moves the interaction focus off the table host, and crouches).
    // Shift+F5 / Shift+F6, or on a pad the co-op chord + D-pad Up / Down (input/actions.h).
    static input::Edge challenge_edge, decline_edge;
    bool pressed = challenge_edge(input::action_down(input::Action::Challenge, focused));
    // B16b: with the hold prompt up and nothing pending, the pad's chord press fills the prompt's ring (hud.cpp) and
    // the menu opens from there (a press here would open it at once). Keyboard Shift+F5 still opens it directly.
    if (pressed && g_hold_prompt.load() && input::method() != input::Method::Keyboard) {
        std::lock_guard lock(g_mutex);
        if (g_ch.state == kChNone) pressed = false;
    }
    // B16b: the hold prompt on the other player. With nothing going on it opens the game list (as a press does);
    // with a challenge sent, received or on it opens that challenge's answers (g_ctx) -- a hold never ends a duel.
    if (g_menu_request.exchange(false)) {
        std::lock_guard lock(g_mutex);
        const std::string game = activity_minigame_name(g_ch.stage);
        g_ctx = CtxMenu{};
        if (g_ch.state == kChNone) {
            pressed = true;
        } else if (g_ch.state == kChReceived) {
            g_ctx.items = {{"Accept: " + game, kCtxGo}, {"Decline", kCtxDecline}, {"Leave", kCtxLeave}};
        } else if (g_ch.state == kChSent) {
            g_ctx.items = {{"Withdraw the challenge", kCtxGo}, {"Leave", kCtxLeave}};
        } else if (g_ch.state == kChOn) {
            g_ctx.items = {{"End the challenge", kCtxGo}, {"Leave", kCtxLeave}};
        }
        if (!g_ctx.items.empty()) {
            g_ctx.open = true;
            g_ctx.since = now;
            g_ctx.pick = g_ch.state == kChReceived ? 0 : g_ctx.items.size() - 1;  // never "End" by a stray press
            g_menu_step = 0;
            g_menu_select = g_menu_cancel = false;
        }
    }
    bool declined = decline_edge(input::action_down(input::Action::Decline, focused));
    // The first-launch card's answer, or the first co-op session's (render/hud.cpp).
    // A card's answer (render/hud.cpp) is not also a challenge: while a card is up, and until both keys are let go
    // after it (2026-11-05: the card's Shift+F5 opened the challenge picker as the card closed; the HUD and this run
    // on different threads).
    static bool card_keys_held = false;
    if (optin::card_visible() || worlds_card_visible() || worlds_switch_card_visible()) card_keys_held = true;
    if (card_keys_held) {
        if (!input::action_down(input::Action::Challenge, focused) && !input::action_down(input::Action::Decline, focused)) {
            card_keys_held = false;
        }
        pressed = declined = false;
    }
    const int test_mode = g_challenge_test.load();
    const bool test = test_mode != 0;
    std::optional<std::pair<uint8_t, uint32_t>> send_it;
    std::string toast;
    {
        std::lock_guard lock(g_mutex);
        Challenge& c = g_ch;
        // Co-op+ (T08): a lost link ends the challenge in the one-PC tests too (their duel / hunt went on after the
        // session, state audit); without a link at all a test still challenges its simulated friend.
        static bool was_linked = false;
        const bool lost = was_linked && !connected;
        was_linked = connected;
        if (c.state != kChNone && ((!connected && !test) || lost)) {
            c = Challenge{};
            toast = "Challenge ended: the other player left";
        }
        if ((c.state == kChSent || c.state == kChReceived) && now - c.since > kInviteMs) {
            toast = c.state == kChSent ? std::format("{} did not answer the challenge", who) : "The challenge lapsed";
            c.state = kChNone;
        }
        if (test_mode == 2 && declined && c.state == kChNone) {  // [dev] Shift+F6: the simulated player invites us
            c.state = kChReceived;
            c.stage = kRpsNameStage;
            c.since = now;
            toast = std::format("{} challenges you to rock, paper, scissors! {{Challenge}} to accept", who);
            log::info("Activity: [dev] challenge_test: the other player challenges us");
            declined = false;  // that press was the trigger
        }
        if (test && c.state == kChSent && now - c.since > 2000) {  // [dev] the simulated player accepts
            c.state = kChOn;
            toast = std::format("{} accepted: {}! {}", who, activity_minigame_name(c.stage), how_to_play(c.stage));
            log::info("Activity: [dev] challenge_test: the other player accepted");
        }
        if (c.state == kChPick && now - c.since > kPickMs) c.state = kChNone;
        if (c.state == kChPick && !c.options.empty()) {
            const int step = g_menu_step.exchange(0);
            const auto n = static_cast<int>(c.options.size());
            c.pick = static_cast<size_t>(((static_cast<int>(c.pick) + step) % n + n) % n);
            if (step) c.since = now;
        }
        if (g_ctx.open) {  // a running challenge's answers
            const int step = g_menu_step.exchange(0);
            const auto n = static_cast<int>(g_ctx.items.size());
            g_ctx.pick = static_cast<size_t>(((static_cast<int>(g_ctx.pick) + step) % n + n) % n);
            if (step) g_ctx.since = now;
            const bool gone = c.state != kChSent && c.state != kChReceived && c.state != kChOn;
            if (g_menu_cancel.exchange(false) || gone || now - g_ctx.since > kPickMs) {
                g_ctx.open = false;
            } else if (g_menu_select.exchange(false)) {
                const int code = g_ctx.items[g_ctx.pick].second;
                if (code == kCtxGo) pressed = true;
                if (code == kCtxDecline) declined = true;
                g_ctx.open = false;
            }
            g_menu_select = false;
        } else if (c.state == kChPick) {  // B16b: the menu's own keys
            if (g_menu_cancel.exchange(false)) declined = true;
            if (g_menu_select.exchange(false)) {
                const uint32_t sel = c.options.empty() ? 0 : c.options[std::min(c.pick, c.options.size() - 1)];
                if (sel == kMenuLeave) {
                    declined = true;
                } else if (sel == kMenuTrade) {
                    toast = "Trading with your friend comes in a later update";
                } else {
                    pressed = true;
                }
            }
        } else {
            g_menu_select = false;
            g_menu_cancel = false;
        }
        if (declined && c.state == kChPick) {
            c.state = kChNone;
            declined = false;
        }
        if (declined && c.state == kChReceived) {
            send_it = std::make_pair(static_cast<uint8_t>(kDecline), c.stage);
            c.state = kChNone;
            toast = "Challenge declined";
        } else if (pressed) {
            switch (c.state) {
                case kChReceived:
                    c.state = kChOn;
                    c.ours = c.theirs = c.round_ours = c.round_theirs = 0;
                    send_it = std::make_pair(static_cast<uint8_t>(kAccept), c.stage);
                    toast = std::format("Challenge accepted: {} vs {}. {}", activity_minigame_name(c.stage), who,
                                        how_to_play(c.stage));
                    break;
                case kChSent:
                case kChOn:
                    send_it = std::make_pair(static_cast<uint8_t>(kEnd), c.stage);
                    toast = c.state == kChOn
                                ? std::format("Challenge over. Final series: you {} - {} {}", c.ours, c.theirs, who)
                                : std::string("Challenge withdrawn");
                    c = Challenge{};
                    break;
                case kChNone: {
                    // The picker: a duel, rock-paper-scissors, and the wrestling table we stand at.
                    if (!connected && !test) {
                        toast = kNativeLayer ? "No one to challenge: invite a friend from the title screen first"
                                             : "No one to challenge: invite a friend first ({Invite})";
                        break;
                    }
                    c = Challenge{};
                    c.state = kChPick;
                    c.since = now;
                    if (const uint32_t table = me ? cinema_minigame_near(*me, kChallengeNearM, kWrestleStages,
                                                                         std::size(kWrestleStages))
                                                  : 0) {
                        c.options.push_back(table);
                    }
                    c.options.push_back(kDuelStage);
                    c.options.push_back(kRpsNameStage);
                    c.options.push_back(kHuntStage);
                    c.options.push_back(kRaceStage);  // Co-op+ (T41)
                    c.options.push_back(kHideStage);  // Co-op+ (2026-11-05)
                    c.options.push_back(kFishStage);
                    c.options.push_back(kMenuTrade);  // B16b: shown, not yet working
                    c.options.push_back(kMenuLeave);
                    // Co-op+ (T42): rematch -- the picker opens on the game played last ({Challenge} twice).
                    for (size_t i = 0; i < c.options.size(); ++i) {
                        if (c.options[i] == g_last_game) c.pick = i;
                    }
                    g_menu_step = 0;
                    break;
                }
                case kChPick: {
                    const uint32_t chosen = c.options.empty() ? kDuelStage : c.options[std::min(c.pick, c.options.size() - 1)];
                    g_last_game = chosen;
                    c = Challenge{};
                    c.state = kChSent;
                    c.stage = chosen;
                    c.since = now;
                    c.inviter = true;
                    send_it = std::make_pair(static_cast<uint8_t>(kInvite), chosen);
                    toast = std::format("You challenged {} to {}", who, activity_minigame_name(chosen));
                    break;
                }
            }
        }
        g_menu_hold = c.state == kChPick || g_ctx.open;
        if (g_ctx.open) {
            out.banner = "{MenuUp}{MenuDown} choose   {MenuSelect} select   {MenuBack} leave";
        } else if (c.state == kChPick) {
            // "Challenge Ana to:   Duel   Rock, paper, scissors" with the chosen one in gold.
            std::string items;
            for (size_t i = 0; i < c.options.size(); ++i) {
                std::string name = activity_minigame_name(c.options[i]);
                if (name.rfind("a ", 0) == 0) name.erase(0, 2);
                if (name.rfind("an ", 0) == 0) name.erase(0, 3);
                if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
                items += i == c.pick ? "   {*}" + name + "{*}" : "   " + name;
            }
            (void)items;  // B16b: the menu card is drawn by the HUD (activity_menu_snapshot); the hint only
            out.banner = "{MenuUp}{MenuDown} choose   {MenuSelect} select   {MenuBack} leave";
        } else if (out.banner.empty()) {
            const std::string game = activity_minigame_name(c.stage);
            if (c.state == kChReceived) {
                out.banner = std::format("{} challenges you to {}!   {{Challenge}} accept   {{Decline}} decline", who, game);
            } else if (c.state == kChSent) {
                out.banner = std::format("Challenge sent: {} vs {}. Waiting for an answer ({{Challenge}} withdraws)", game, who);
            } else if (c.state == kChOn && game_of(c.stage) == kGameHunt) {
                out.banner = std::format("Hunting contest vs {}   you {} - {} {}   (first to 2;  {{Challenge}} stops)", who,
                                         c.ours, c.theirs, who);
            } else if (c.state == kChOn && game_of(c.stage) == kGameRace) {
                out.banner = std::format("Race vs {}   you {} - {} {}   (first to 2;  {{Challenge}} stops)", who, c.ours,
                                         c.theirs, who);
            } else if (c.state == kChOn && game_of(c.stage) == kGameFish) {
                out.banner = std::format("Fishing contest vs {}: get ready   ({{Challenge}} stops)", who);
            } else if (c.state == kChOn && game_of(c.stage) == kGameHide) {
                out.banner = std::format("Hide and seek vs {}   you {} - {} {}   (first to 2;  {{Challenge}} stops)", who,
                                         c.ours, c.theirs, who);
            } else if (c.state == kChOn && game_of(c.stage) == kGameDuel) {
                out.banner = std::format("Duel vs {}   you {} - {} {}   (first to 2;  {{Challenge}} stops)", who,
                                         c.ours, c.theirs, who);
            } else if (c.state == kChOn) {
                out.banner = std::format("Challenge: {} vs {}   series you {} - {} {}   (start a match at the table; "
                                         "{{Challenge}} ends)", game, who, c.ours, c.theirs, who);
            }
        }
    }
    if (send_it) {
        std::vector<uint8_t> b(5);
        b[0] = send_it->first;
        std::memcpy(b.data() + 1, &send_it->second, 4);
        send(kChallenge, b.data(), b.size());
        log::info("Activity: challenge action {} sent ({})", static_cast<int>(send_it->first),
                  activity_minigame_name(send_it->second));
    }
    if (!toast.empty() && out.toast.empty()) out.toast = std::move(toast);
}

}  // namespace

void activity_set_challenge_test(int mode) { g_challenge_test = mode; }

RpsView activity_rps_view() {
    RpsView v;
    std::lock_guard lock(g_mutex);
    if (g_ch.state != kChOn || game_of(g_ch.stage) != kGameRps) return v;
    v.on = true;
    const uint32_t now = GetTickCount();
    if (static_cast<int32_t>(now - g_rps.reveal_until) < 0) {
        v.ours = g_rps.shown_ours, v.theirs = g_rps.shown_theirs, v.winner = g_rps.winner;
        v.reveal_t = static_cast<float>(kRpsRevealMs - (g_rps.reveal_until - now)) / 1000.0f;
    } else {
        v.ours_picked = g_rps.ours >= 0;
        v.theirs_picked = g_rps.theirs >= 0;
    }
    return v;
}
void activity_set_rps_test(int hand) { g_rps_test = hand; }

// Co-op+ (T16): ChangeWantedStateAck 2324, IncreaseWantedPriceAck 2249, WantedAddCrimeRecordAck 2569 from our server.
std::atomic<uint32_t> g_wanted_sent_at{0};
void activity_observe_s2c(const uint8_t* packet, size_t size) {
    if (size < 5) return;
    uint16_t op = 0;
    std::memcpy(&op, packet, 2);
    if (op == 2380 && size >= 5 + 23) {  // PrepareSequencerAck: u32 stage at +19 (weather_sync.cpp kPrepKeyAt)
        uint32_t key = 0;
        std::memcpy(&key, packet + 5 + 19, 4);
        if (const char* n = event_name(key)) {
            static std::map<uint32_t, uint32_t> told;  // sniffer thread only
            const uint32_t t = GetTickCount();
            if (auto it = told.find(key); it != told.end() && t - it->second < 300000) return;
            told[key] = t ? t : 1;
            log::info("Activity: a world event started here: {} (stage {})", n, key);
            send(kEvent, &key, 4);
        }
        return;
    }
    if (op != 2324 && op != 2249 && op != 2569) return;
    const uint32_t now = GetTickCount();
    const uint32_t last = g_wanted_sent_at.load();
    if (last && now - last < 60000) return;  // one notice a minute is plenty
    g_wanted_sent_at = now ? now : 1;
    log::info("Activity: we are wanted now (message {}): telling the other player", op);
    send(kWanted, nullptr, 0);
}

void activity_observe_c2s(const uint8_t* packet, size_t size) {
    if (size < 5) return;
    uint16_t op = 0;
    std::memcpy(&op, packet, 2);
    if (op == kOpActivateMiniGameReq && size >= 5 + 2) {
        uint16_t id = 0;
        std::memcpy(&id, packet + 5, 2);
        log::info("Activity: we started a minigame (id 0x{:04X})", id);
        send(kGameOn, &id, 2);
    } else if (op == kOpSpawnMiniGameCharacterReq && size >= 5 + 9) {
        // u8 4 | u64 the minigame's stage (sequencer) | u16 id | u32 opponent | ...
        uint64_t seq = 0;
        std::memcpy(&seq, packet + 6, 8);
        g_match_stage = cinema_stage_key(seq);
        g_match_on = true;
        {
            std::lock_guard lock(g_opp_mutex);
            g_opp = Opponent{};
        }
        log::info("Activity: a match of {} (stage {})", activity_minigame_name(g_match_stage.load()), g_match_stage.load());
    } else if (op == kOpJoinMiniGameReq && size >= 5 + 7 && g_match_on.load()) {
        uint32_t key = 0;
        std::memcpy(&key, packet + 5 + 3, 4);
        if (key && key != capture_own_player_key()) {
            std::lock_guard lock(g_opp_mutex);
            g_opp.key = key;
        }
    } else if (op == kOpStartActionReq && size >= 5 + 0x15A && g_match_on.load()) {
        uint32_t key = 0;
        std::memcpy(&key, packet + 5, 4);
        std::lock_guard lock(g_opp_mutex);
        if (key && key == g_opp.key) {
            std::memcpy(&g_opp.rot, packet + 5 + 0x13E, 16);  // wire::kActionRot / kActionPos
            std::memcpy(&g_opp.pos, packet + 5 + 0x14E, 12);
            g_opp.posed = std::isfinite(g_opp.pos.x) && std::isfinite(g_opp.pos.z) && std::isfinite(g_opp.rot.w);
        }
    } else if (op == kOpLeaveMiniGameReq && size >= 5 + 23) {
        g_match_on = false;
        uint32_t stage = 0;
        std::memcpy(&stage, packet + 5 + 19, 4);
        log::info("Activity: we finished {} (stage {})", activity_minigame_name(stage), stage);
        send(kGameOff, &stage, 4);
    }
}

void activity_on_event(const std::vector<uint8_t>& p) {
    if (p.size() < 5) return;
    uint32_t seq = 0;
    std::memcpy(&seq, p.data(), 4);
    std::lock_guard lock(g_mutex);
    if (seq <= g_seq_in && g_seq_in - seq < 0x10000) return;  // a repeat (each event is sent three times)
    g_seq_in = seq;
    std::array<float, 3> pos{};
    if (p.size() >= 5 + 12) std::memcpy(pos.data(), p.data() + 5, 12);
    switch (p[4]) {
        case kCutsceneOn: g_partner_cutscene = true; g_partner_cut_pos = pos; g_partner_skip = false; break;
        case kCutsceneOff: g_partner_cutscene = false; g_partner_skip = false; break;
        case kSkip: g_partner_skip = true; g_skip_from = pos; break;
        case kGameOn: g_toasts.push_back("{} is playing a minigame"); g_partner_game = true; break;
        case kVersus:  // u8 live | u32 presses since their match started | f32 gauge (the host's)
            if (p.size() >= 5 + 9) {
                g_vs.partner_live = p[5] != 0;
                std::memcpy(&g_vs.partner_count, p.data() + 6, 4);
                std::memcpy(&g_vs.partner_gauge, p.data() + 10, 4);
                g_vs.partner_seen = GetTickCount();
            }
            break;
        case kGameOff: {
            uint32_t stage = 0;
            if (p.size() >= 9) std::memcpy(&stage, p.data() + 5, 4);
            g_toasts.push_back("{} finished " + activity_minigame_name(stage));
            g_partner_game = false;
            break;
        }
        case 7: case 8: case 9:  // stage_cinema.h (it takes its own lock)
        case kDuel:              // coop_duel.h, below
        case kQuest:             // party_quests.h, below
            break;
        // Co-op+ (T39 found): kMenu sat right under the list above with no break since T09, so cutscene (7-9), duel
        // and quest events ran this too and set "the partner is in a menu" from their first data byte.
        case kMenu:
            if (p.size() >= 6) {
                const bool open = p[5] != 0;
                if (g_partner_menu.exchange(open) != open) {
                    aggro_set_partner_in_menu(open);
                    log::info("Activity: the other player {} a menu", open ? "is in" : "left");
                }
            }
            break;
        case kLooted:  // Co-op+ (T10) first_come: u8 1 = the host looted (data: our copy key) / 0 = the guest did
            if (p.size() >= 10) {
                uint32_t copy = 0;
                std::memcpy(&copy, p.data() + 6, 4);
                if (p[5] == 1) loot_partner_looted(copy);
                else enemy_partner_looted_copy(copy);
            }
            break;
        case kWanted:
            g_toasts.push_back("{} is now wanted by the guards (in their own world)");
            break;
        case kEvent:
            if (p.size() >= 9) {
                uint32_t key = 0;
                std::memcpy(&key, p.data() + 5, 4);
                if (const char* n = event_name(key)) g_toasts.push_back(std::string("World event near {}\n") + n);
            }
            break;
        case kBye:  // the host leaves: we go back to the title first, so its server unloads us the native way
            log::info("Activity: the host is ending the session");
            g_toasts.push_back("{} ended the session.");
            g_bye = true;
            break;
        case kTitle:             // scoreboard.h, below
        case kRace:              // coop_race.h, below
        case kKill:              // kill_feed.h, below
        case kChat:              // coop_chat.h, below
        case kTravel:            // coop_travel.h, below
        case kReviveYou:         // coop_revive.h, below
        case kHunt:              // coop_hunt.h, below
        case 15:                 // coop_sync.h, below
            break;
        case kChallenge:  // u8 action | u32 stage key
            if (p.size() >= 10) {
                uint32_t stage = 0;
                std::memcpy(&stage, p.data() + 6, 4);
                const std::string game = activity_minigame_name(stage);
                switch (p[5]) {
                    case kInvite:
                        // [dev] challenge_test with the echo peer: our own invite comes back; it is not theirs.
                        if (g_challenge_test.load() && g_ch.state == kChSent && g_ch.stage == stage) break;
                        g_ch = Challenge{};
                        g_ch.state = kChReceived;
                        g_ch.stage = stage;
                        g_ch.since = GetTickCount();
                        g_toasts.push_back("{} challenges you to " + game + "! {{Challenge}} to accept");
                        break;
                    case kAccept:
                        if (g_ch.state == kChSent) {
                            g_ch.state = kChOn;
                            g_toasts.push_back("{} accepted: " + game + "! " + escape_braces(how_to_play(stage)));
                        }
                        break;
                    case kDecline:
                        if (g_ch.state == kChSent) g_ch.state = kChNone;
                        g_toasts.push_back("{} declined the challenge");
                        break;
                    case kEnd:
                        if (g_ch.state != kChNone) {
                            g_toasts.push_back(std::format("{{}} ended the challenge. Final series: you {} - {}", g_ch.ours,
                                                           g_ch.theirs));
                        }
                        g_ch = Challenge{};
                        break;
                    default: break;
                }
            }
            break;
        case kRps:  // u8 hand (0 scissors, 1 paper, 2 rock): their pick in the duel
            if (p.size() >= 6 && p[5] < 3) g_rps.theirs = p[5];
            break;
        default: break;
    }
    if (p[4] >= 7 && p[4] <= 9) cinema_on_event(p[4], p.data() + 5, p.size() - 5);
    if (p[4] == kDuel) duel_on_event(p.data() + 5, p.size() - 5);  // coop_duel.h (its own lock)
    if (p[4] == kQuest) party_quests_on_event(p.data() + 5, p.size() - 5);  // party_quests.h (its own lock)
    if (p[4] == kHunt) hunt_on_event(p.data() + 5, p.size() - 5);  // coop_hunt.h (its own lock)
    if (p[4] == 15) sync_on_event(p.data() + 5, p.size() - 5);      // coop_sync.h (its own lock)
    if (p[4] == kReviveYou) revive_partner_on_event();              // coop_revive.h
    if (p[4] == kTravel) travel_on_event(p.data() + 5, p.size() - 5);  // coop_travel.h
    if (p[4] == kChat) chat_on_event(p.data() + 5, p.size() - 5);      // coop_chat.h
    if (p[4] == kKill) kill_feed_on_event(p.data() + 5, p.size() - 5); // kill_feed.h
    if (p[4] == kRace) race_on_event(p.data() + 5, p.size() - 5);      // coop_race.h
    if (p[4] == kHide) hideseek_on_event(p.data() + 5, p.size() - 5);  // coop_hideseek.h
    if (p[4] == kFish) fishing_on_event(p.data() + 5, p.size() - 5);   // coop_fishing.h
    if (p[4] == kTitle) scoreboard_on_event(p.data() + 5, p.size() - 5);  // scoreboard.h
}

ActivityTick activity_tick(bool connected, bool host, bool focused, std::optional<std::array<float, 3>> me,
                           std::optional<std::array<float, 3>> partner, const std::string& partner_name,
                           uint32_t now) {
    // [dev] event_test (T22): announce a world event 20 s after connecting.
    {
        static uint32_t linked_at = 0;
        static bool done = false;
        if (!connected) linked_at = 0, done = false;
        else if (!linked_at) linked_at = now ? now : 1;
        if (const uint32_t key = g_event_test.load(); key && connected && !done && now - linked_at >= 20000) {
            done = true;
            log::info("Activity: [dev] event_test: announcing stage {} ({})", key, event_name(key) ? event_name(key) : "?");
            send(kEvent, &key, 4);
        }
    }
    // Co-op+ (T10) first_come: bodies we looted, to the other player.
    if (connected) {
        for (uint32_t copy : enemy_take_looted_originals()) {
            uint8_t b[5] = {1};
            std::memcpy(b + 1, &copy, 4);
            send(kLooted, b, 5);
        }
        for (uint32_t copy : loot_take_looted_copies()) {
            uint8_t b[5] = {0};
            std::memcpy(b + 1, &copy, 4);
            send(kLooted, b, 5);
        }
    }
    // Co-op+ (T09): our menu state to the other player; theirs forgotten with the link.
    {
        static bool sent_open = false, was_linked = false;
        static uint32_t sent_at = 0;
        const bool open = game::ui_menu_open();
        if (connected && (open != sent_open || !was_linked || now - sent_at >= kMenuResendMs)) {
            const uint8_t b = open ? 1 : 0;
            send(kMenu, &b, 1);
            sent_open = open;
            sent_at = now;
        }
        if (!connected && g_partner_menu.exchange(false)) aggro_set_partner_in_menu(false);
        was_linked = connected;
    }

    ActivityTick out;
    const bool cut = game::cutscene_playing().value_or(false);
    boss_note_cutscene(cut);
    // Our last known position (none while a minigame holds the player): what we send and measure from.
    static std::array<float, 3> last_here{};
    static bool have_here = false;
    if (me) {
        last_here = *me;
        have_here = true;
    }
    const std::array<float, 3> here = last_here;
    if (!me && have_here) me = last_here;
    if (cut != g_last_cutscene) {
        g_last_cutscene = cut;
        g_cut_since = now;
        g_skip_test_done = false;
        {
            std::lock_guard lock(g_mutex);
            g_our_skip = false;
            g_released = false;
            if (g_skip_test) g_partner_skip = false;
        }
        g_hold_esc = false;
        log::info("Activity: our cutscene {}", cut ? "started" : "ended");
        if (connected) {
            if (cut) send(kCutsceneOn, here.data(), 12);
            else send(kCutsceneOff, nullptr, 0);
        }
    }
    pad_tick();
    cinema_tick(connected, host, cut, me, partner, now);  // the host's stage cinematics for the guest (D38)
    const std::string who = partner_name.empty() ? std::string("Your friend") : partner_name;
    start_hook();
    bool release = false, send_skip = false;
    {
        std::lock_guard lock(g_mutex);
        if (!connected) {
            g_partner_cutscene = false;
            g_partner_skip = false;
            g_toasts.clear();
        }
        if (g_skip_test && cut) {
            // One-PC test: the "other player" is in the same cutscene and presses skip kSkipTestDelayMs in.
            g_partner_cutscene = true;
            g_partner_cut_pos = here;
            if (!g_skip_test_done && now - g_cut_since > kSkipTestDelayMs) {
                g_skip_test_done = true;
                g_partner_skip = true;
                g_skip_from = here;
                log::info("Activity: [dev] skip_test: the other player pressed skip");
            }
        }
        // Everyone in the cutscene must press skip (D32 as the user specified): while the other player is in a
        // cutscene within kNearM, our own Esc is held back (keyboard hook) and only sent to them.
        const bool partner_near = !me || dist_xz(here, g_partner_cut_pos) <= kNearM;
        const bool shared = cut && (connected || g_skip_test) && g_partner_cutscene && partner_near;
        g_hold_esc = shared && !g_released;
        g_hold_seen = GetTickCount64();
        if (g_esc_held.exchange(false) && shared && !g_our_skip) {
            g_our_skip = true;
            send_skip = true;  // sent below, outside g_mutex (send() takes it)
        }
        if (shared && g_our_skip && g_partner_skip && !g_released && focused) {
            g_released = true;
            g_hold_esc = false;
            release = true;
        }
        if (!g_toasts.empty()) {
            out.toast = std::vformat(g_toasts.front(), std::make_format_args(who));
            g_toasts.erase(g_toasts.begin());
        }
        const bool is_near = !partner || !me || dist_xz(*partner, here) <= kNearM;
        if (g_partner_cutscene && is_near && !cut) {
            out.banner = std::format("{} is watching a cutscene", who);
        } else if (shared && g_our_skip && !g_partner_skip) {
            out.banner = std::format("Skip: waiting for {}", who);
        } else if (shared && g_partner_skip && !g_our_skip) {
            out.banner = std::format("{} wants to skip: {{Skip}} to skip together", who);
        } else if (shared && !g_released) {
            out.banner = std::format("{} is in this cutscene too: it skips when you both press {{Skip}}", who);
        }
    }
    if (send_skip) {
        log::info("Activity: we pressed skip; waiting for the other player");
        send(kSkip, here.data(), 12);
    }
    if (release) {
        press_escape();
        log::info("Activity: both players pressed skip; skipping");
    }
    versus_tick(out, connected, host, cut, who, now);
    rps_tick(out, connected, who);
    challenge_tick(out, connected, focused, me, who);
    bool duel_on = false, inviter = false;
    {
        std::lock_guard lock(g_mutex);
        duel_on = g_ch.state == kChOn && game_of(g_ch.stage) == kGameDuel;
        inviter = g_ch.inviter;
    }
    DuelTick d = duel_tick(duel_on, inviter, connected, me, partner, who, now);
    if (d.round_result) {
        std::lock_guard lock(g_mutex);
        (d.round_result == 1 ? g_ch.ours : g_ch.theirs) += 1;
        out.toast = std::format("Duel\n{}   Series: you {} - {} {}", d.round_result == 1 ? "The round is yours."
                                                                                         : "The round goes to " + who + ".",
                                g_ch.ours, g_ch.theirs, who);
        log::info("Activity: duel round to {}; series {} - {}", d.round_result == 1 ? "us" : "them", g_ch.ours,
                  g_ch.theirs);
    }
    if (!d.banner.empty()) out.banner = std::move(d.banner);
    if (!d.toast.empty() && out.toast.empty()) out.toast = std::move(d.toast);
    bool hunt_on = false;
    {
        std::lock_guard lock(g_mutex);
        hunt_on = g_ch.state == kChOn && game_of(g_ch.stage) == kGameHunt;
    }
    HuntTick h = hunt_tick(hunt_on, connected, who);
    if (h.round_result) {
        std::lock_guard lock(g_mutex);
        if (h.round_result == 1) ++g_ch.ours;
        if (h.round_result == 2) ++g_ch.theirs;
        out.toast = std::format("Hunting contest\n{}   Series: you {} - {} {}",
                                h.round_result == 1 ? std::string("The hunt is yours.")
                                : h.round_result == 2 ? who + " takes the hunt." : std::string("An even hunt."),
                                g_ch.ours, g_ch.theirs, who);
    }
    if (!h.banner.empty()) out.banner = std::move(h.banner);
    if (!h.toast.empty() && out.toast.empty()) out.toast = std::move(h.toast);
    // Co-op+ (T41): the race.
    bool race_on = false;
    {
        std::lock_guard lock(g_mutex);
        race_on = g_ch.state == kChOn && game_of(g_ch.stage) == kGameRace;
    }
    RaceTick r = race_tick(race_on, connected, host, who, me, partner);
    if (r.round_result) {
        std::lock_guard lock(g_mutex);
        if (r.round_result == 1) ++g_ch.ours;
        if (r.round_result == 2) ++g_ch.theirs;
        out.toast = std::format("Race\n{}   Series: you {} - {} {}",
                                r.round_result == 1 ? std::string("You win the race.") : who + " wins the race.",
                                g_ch.ours, g_ch.theirs, who);
    }
    if (!r.banner.empty()) out.banner = std::move(r.banner);
    // Co-op+ (2026-11-05): hide and seek.
    bool hide_on = false, hide_inviter = false;
    {
        std::lock_guard lock(g_mutex);
        hide_on = g_ch.state == kChOn && game_of(g_ch.stage) == kGameHide;
        hide_inviter = g_ch.inviter;
    }
    HideSeekTick hs = hideseek_tick(hide_on, connected, hide_inviter, who, me, partner);
    if (hs.round_result) {
        std::lock_guard lock(g_mutex);
        if (hs.round_result == 1) ++g_ch.ours;
        if (hs.round_result == 2) ++g_ch.theirs;
        out.toast = std::format("Hide and seek\n{}   Series: you {} - {} {}",
                                hs.round_result == 1 ? std::string("The round is yours.") : "The round goes to " + who + ".",
                                g_ch.ours, g_ch.theirs, who);
    }
    if (!hs.banner.empty()) out.banner = std::move(hs.banner);
    // Co-op+ (2026-11-05): the fishing contest (one round; a draw plays another).
    bool fish_on = false;
    {
        std::lock_guard lock(g_mutex);
        fish_on = g_ch.state == kChOn && game_of(g_ch.stage) == kGameFish;
    }
    FishingTick ft = fishing_tick(fish_on, connected, who);
    if (ft.round_result == 1 || ft.round_result == 2) {
        std::lock_guard lock(g_mutex);
        (ft.round_result == 1 ? g_ch.ours : g_ch.theirs) += 1;
    } else if (ft.round_result == 3) {
        out.toast = "Fishing contest\nAn even catch: one more round to settle it.";
    }
    if (!ft.banner.empty()) out.banner = std::move(ft.banner);
    if (!ft.toast.empty() && out.toast.empty()) out.toast = std::move(ft.toast);
    // Co-op+ (T43): every round won or lost goes on the lasting scoreboard (the series counters move by one).
    {
        static uint32_t stage = 0;
        static int ours = 0, theirs = 0;
        std::unique_lock lock(g_mutex);
        const bool on = g_ch.state == kChOn;
        if (!on || g_ch.stage != stage) {
            stage = on ? g_ch.stage : 0, ours = g_ch.ours, theirs = g_ch.theirs;
        } else if (g_ch.ours != ours || g_ch.theirs != theirs) {
            const bool won = g_ch.ours > ours;
            const Game game = game_of(g_ch.stage);
            ours = g_ch.ours, theirs = g_ch.theirs;
            lock.unlock();
            scoreboard_record(who, game == kGameDuel ? "duel" : game == kGameRps ? "rps" : game == kGameHunt ? "hunt"
                                   : game == kGameRace ? "race" : game == kGameHide ? "hide"
                                   : game == kGameFish ? "fish" : "wrestle", won);
        }
    }
    // Co-op+ (2026-11-05 playtest: challenges "just keep going forever unless you press View + Up"): a challenge is
    // one match. Duel, race and hunt: first to two rounds; rock-paper-scissors and the wrestling table: one match
    // (rock-paper-scissors is itself first to two hands). Each game sees the same rounds, so both end it here on
    // their own; the result shows as the game's big title card. The challenge key still stops it early.
    {
        std::lock_guard lock(g_mutex);
        if (g_ch.state == kChOn) {
            const Game game = game_of(g_ch.stage);
            const int need = game == kGameDuel || game == kGameRace || game == kGameHunt || game == kGameHide ? 2 : 1;
            if (g_ch.ours >= need || g_ch.theirs >= need) {
                const bool won = g_ch.ours > g_ch.theirs;
                std::string name = activity_minigame_name(g_ch.stage);
                if (name.rfind("a ", 0) == 0) name.erase(0, 2);
                if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
                g_match_title = won ? "Victory" : "Defeat";
                g_match_subtitle = std::format("{} vs {}   {} - {}", name, who, g_ch.ours, g_ch.theirs);
                g_match_won = won;
                g_match_at = GetTickCount();
                summary_note(won ? SummaryStat::MatchWon : SummaryStat::MatchLost);
                log::info("Activity: the {} is over ({} {} - {})", name, won ? "we won" : "they won", g_ch.ours,
                          g_ch.theirs);
                g_last_game = g_ch.stage;
                g_ch = Challenge{};
                out.banner.clear();
            }
        }
    }
    // The party journal (party_quests.h): the other player's completed quests. Held back while another notice shows.
    static uint32_t journal_after = 0;
    if (out.toast.empty() && static_cast<int32_t>(now - journal_after) >= 0) {
        if (std::string j = party_quests_tick(connected, who); !j.empty()) {
            out.toast = std::move(j);
            journal_after = now + 6500;
        }
    } else if (!out.toast.empty()) {
        journal_after = now + 6500;
    }
    // Shared world: no key shortcuts in the lines (hold interact on the friend, or the menu's Co-op tab).
    if (kNativeLayer) {
        strip_key_hints(out.banner);
        strip_key_hints(out.toast);
    }
    return out;
}

MatchHud activity_match_hud() {
    constexpr uint32_t kShowMs = 6000;
    MatchHud h;
    std::lock_guard lock(g_mutex);
    const uint32_t t = GetTickCount() - g_match_at;
    if (g_match_title.empty() || t >= kShowMs) return h;
    h.title = g_match_title;
    h.subtitle = g_match_subtitle;
    h.color = g_match_won ? 0xFF82C8E8 : 0xFF2830C4;  // gold / the death screen's red (ABGR)
    h.alpha = t < kShowMs - 1000 ? 1.0f : static_cast<float>(kShowMs - t) / 1000.0f;
    return h;
}

void activity_request_menu() { g_menu_request = true; }

ChallengeView activity_challenge_view() {
    std::lock_guard lock(g_mutex);
    ChallengeView v;
    v.state = g_ch.state == kChReceived ? 1 : g_ch.state == kChSent ? 2 : g_ch.state == kChOn ? 3 : 0;
    if (v.state) v.game = activity_minigame_name(g_ch.stage);
    return v;
}
void activity_set_hold_prompt(bool shown) { g_hold_prompt = shown; }

bool activity_menu_snapshot(std::string& title, std::vector<std::string>& items, size_t& pick) {
    std::lock_guard lock(g_mutex);
    if (g_ctx.open) {
        items.clear();
        for (const auto& it : g_ctx.items) items.push_back(it.first);
        pick = std::min(g_ctx.pick, items.size() - 1);
        title = "Co-op";
        return true;
    }
    if (g_ch.state != kChPick) return false;
    items.clear();
    for (const uint32_t o : g_ch.options) {
        if (o == kMenuTrade) {
            items.push_back("Trade (coming soon)");
            continue;
        }
        if (o == kMenuLeave) {
            items.push_back("Leave");
            continue;
        }
        std::string name = activity_minigame_name(o);
        if (name.rfind("a ", 0) == 0) name.erase(0, 2);
        if (name.rfind("an ", 0) == 0) name.erase(0, 3);
        if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        items.push_back(name);
    }
    pick = std::min(g_ch.pick, items.empty() ? size_t{0} : items.size() - 1);
    title = "Co-op";
    return true;
}

}  // namespace cdmp::net
