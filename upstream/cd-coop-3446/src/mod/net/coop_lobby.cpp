#include "mod/net/coop_lobby.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <format>
#include <mutex>

#include "mod/log.h"
#include "mod/native_layer.h"
#include "mod/net/coop_worlds.h"
#include "mod/net/native_mode.h"
#include "mod/net/troc_sniffer.h"

namespace cdmp::net {
namespace {

// RelKind::World message types (coop_worlds.cpp keeps 1 for its own; 2.. come here).
constexpr uint8_t kMsgGo = 3;          // u8 | u8 new game | u16 ms to start      host -> guest
constexpr uint8_t kMsgFresh = 4;       // u8                                       host -> guest: a fresh co-op save
constexpr uint8_t kMsgDifficulty = 5;  // u8 | u32 difficulty                      either way
constexpr uint8_t kMsgPlan = 6;        // u8 | u8 LobbyPlan                        host -> guest
// Native base (lobby_native_init): u8 | u8 LobbyPlan | u8 stage (1 = the host restarts now, 2 = your turn: restart as
// the guest)                                                                     host -> guest
constexpr uint8_t kMsgNative = 20;  // (7 is prologue_hold.h, 8 xp_share.h: coop_worlds.cpp routes those first)
constexpr uint8_t kMsgNativeRefused = 22;  // u8: co-op was refused on the sender's PC (B22a)   either way
constexpr uint8_t kMsgNativeReady = 21;  // u8: the guest's game is in guest mode (after its restart)   guest -> host
constexpr uint64_t kRestartDelayMs = 1500;  // the message goes out before this game closes
constexpr uint64_t kCountdownMs = 3000;
constexpr uint64_t kDifficultyWaitMs = 25000;  // the guest's new game waits this long for the host's difficulty
constexpr uint32_t kUnknown = UINT32_MAX;
constexpr uint32_t kNormal = 1;  // 0 Easy, 1 Normal, 2 Hard (the game's dialog, top to bottom)

enum class Phase { Menu, HostPrepare, HostSetup, Starting, GuestWait, NativeRestart };

std::mutex g_m;
LobbyInput g_in;
bool g_dismissed = false, g_was_at_title = false, g_choosing = false;
Phase g_phase = Phase::Menu;
LobbyPlan g_plan = LobbyPlan::None;
uint64_t g_go_at = 0;            // countdown end (both games press Play)
bool g_go_sent = false;          // host: the guest was told the run started
uint64_t g_new_go_since = 0;     // guest: the host's run started at (new save): our New Game follows
bool g_new_started = false;      // guest: our New Game was pressed
uint32_t g_gen_mark = 0;         // the run generation when the phase began: a later one = our run entered the world
std::vector<std::vector<uint8_t>> g_out;
std::atomic<uint32_t> g_own_diff{kUnknown};
uint32_t g_partner_diff = kUnknown, g_sent_diff = kUnknown;
std::atomic<uint16_t> g_diff_op{0};
// Native base.
bool g_native = false;           // lobby_native_init ran (coopplus.asi)
std::wstring g_native_ini, g_native_bin;
NativeResume g_resume;           // this start's restart state (mode Solo = none)
NativeMode g_native_now = NativeMode::Solo;  // the native cdmp.ini's mode at this start
uint64_t g_restart_at = 0;       // NativeRestart: when this game restarts
const char* g_restart_why = "";
bool g_switch_sent = false;      // host after its restart: the guest was told to switch
bool g_ready_sent = false;       // guest after its restart: the host was told
bool g_guest_ready = false;      // host: the guest is in guest mode
bool g_auto_started = false;     // host: the plan went on after the guest came back
bool g_resume_cleared = false;   // in the world: the next launch was set back to Solo
uint64_t g_resume_since = 0;     // a resumed start waiting for its partner since
std::string g_native_note;       // a line for the menu (e.g. the friend did not come back)
bool g_refused_seen = false;     // B22a: the native base refused co-op for this launch (its save redirect failed)

// B22a (2026-11-09): the native base's reason when it refused co-op for this launch (cdmp_coop_refused_reason in
// cdmp.asi), null when co-op is fine or the export is not there.
const char* native_refused_reason() {
    using Fn = const char* (*)();
    static Fn fn = nullptr;
    if (!fn) {
        if (HMODULE m = GetModuleHandleW(L"cdmp.asi")) fn = reinterpret_cast<Fn>(GetProcAddress(m, "cdmp_coop_refused_reason"));
    }
    return fn ? fn() : nullptr;
}

bool native_write_ok(const NativeModeSpec& spec);

// Co-op stops for this launch (ours refused, or the partner's): the next launch is solo, nothing more is sent.
void native_back_to_solo(const std::string& note) {
    NativeModeSpec solo;
    native_write_ok(solo);
    native_resume_clear(g_native_ini);
    g_resume = NativeResume{};
    g_native_note = note;
}

uint64_t now_ms() { return GetTickCount64(); }
void send(std::vector<uint8_t> m) { g_out.push_back(std::move(m)); }

void reset_phase() {
    g_phase = Phase::Menu;
    g_plan = LobbyPlan::None;
    g_go_at = 0;
    g_go_sent = false;
    g_new_go_since = 0;
    g_new_started = false;
    g_choosing = false;
}

const char* plan_text(LobbyPlan p) {
    switch (p) {
        case LobbyPlan::NewSave: return "a new co-op save";
        case LobbyPlan::ContinueCoop: return "your co-op save";
        case LobbyPlan::FromSolo: return "the host's solo progress";
        default: return "nothing yet";
    }
}

bool native_write_ok(const NativeModeSpec& spec) { return native_mode_write(g_native_bin, spec); }

// The host's co-op save with the partner of g_in is there already (under g_m).
bool native_coop_exists() {
    if (!g_in.connected) return false;
    wchar_t local[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (!n || n >= MAX_PATH) return false;
    const std::string rel = native_coop_dir(NativeMode::Host, g_in.steam ? g_in.partner_steam_id : 0, g_in.partner_address);
    const std::wstring path = std::wstring(local) + L"\\Pearl Abyss\\CD\\" + std::wstring(rel.begin(), rel.end());
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

const char* seed_for(LobbyPlan p) {
    switch (p) {
        case LobbyPlan::NewSave: return "new";
        case LobbyPlan::FromSolo: return "copy";
        default: return "keep";
    }
}

std::atomic<bool> g_shared_quest_credit{false};  // B10 option 2 ([coop] shared_quest_credit)
std::string g_native_extra;                      // dev: [dev] native_extra (set once at start)

// The native ini for this mode with this partner (under g_m).
bool native_write(NativeMode mode, LobbyPlan plan) {
    NativeModeSpec spec;
    spec.mode = mode;
    spec.steam = g_in.steam;
    spec.partner_steam_id = g_in.steam ? g_in.partner_steam_id : 0;
    spec.partner_address = g_in.steam ? std::string() : g_in.partner_address;
    spec.player_name = g_in.own_name.empty() ? "Player" : g_in.own_name;
    spec.shared_quest_credit = g_shared_quest_credit.load();
    if (mode == NativeMode::Host) spec.extra = g_native_extra;
    // Over udp the Co-op+ link already has its port (Radmin VPN / Direct IP: 37015): the native tunnel takes the next.
    if (!spec.steam) spec.port = static_cast<uint16_t>((g_in.direct_port ? g_in.direct_port : 37015) + 1);
    if (mode != NativeMode::Solo && !(mode == NativeMode::Guest && test_role() == L"guest")) {
        // (Two copies on one PC: the guest keeps save_cdmp_guest, the harness's test save.)
        spec.coop_save_dir = native_coop_dir(mode, spec.partner_steam_id, g_in.partner_address);
        // The guest keeps its co-op character with this host (a first time: a copy of its own progress); the host's
        // world follows the plan.
        spec.coop_save_seed = mode == NativeMode::Host ? seed_for(plan) : "keep";
    }
    if (!native_write_ok(spec)) return false;
    NativeResume r;
    r.mode = mode;
    r.steam = spec.steam;
    r.partner_steam_id = spec.partner_steam_id;
    r.partner_address = spec.partner_address;
    r.plan = static_cast<uint8_t>(plan);
    r.direct = g_in.direct;
    r.direct_token = g_in.direct_token;
    r.direct_port = g_in.direct_port;
    if (mode == NativeMode::Solo) {
        native_resume_clear(g_native_ini);
    } else {
        native_resume_write(g_native_ini, r);
    }
    return true;
}

}  // namespace

void lobby_native_init(const std::wstring& coopplus_ini, const std::wstring& bin_dir) {
    std::lock_guard lock(g_m);
    g_native = true;
    g_native_ini = coopplus_ini;
    g_native_bin = bin_dir;
    g_native_now = native_mode_current(bin_dir);
    const NativeResume r = native_resume_read(coopplus_ini);
    if (native_resume_fresh(r) && r.mode == g_native_now) {
        g_resume = r;
        g_resume_since = GetTickCount64();
        log::info("Co-op lobby: started in co-op as the {} (restart {} s ago): reconnecting to the partner",
                  native_mode_name(r.mode), r.written_unix ? "a few" : "?");
        return;
    }
    if (g_native_now != NativeMode::Solo || r.mode != NativeMode::Solo) {
        // A co-op launch nobody came back to (or a hand-set mode without its restart): the next launch is Solo again.
        NativeModeSpec solo;
        native_write_ok(solo);
        native_resume_clear(coopplus_ini);
        if (g_native_now != NativeMode::Solo) {
            g_native_note = "This game started in co-op mode from an earlier session. Restart it to play solo.";
            log::warn("Co-op lobby: the native base started as the {} with no fresh restart: the next launch is Solo",
                      native_mode_name(g_native_now));
        }
    }
}


void lobby_tick(const LobbyInput& in) {
    const WorldsLobbyInfo w = worlds_lobby_info();
    const bool save_ready = !w.enabled || (w.active && !w.switching);
    enum { None, PressNew, PressPlay, GuestNew } todo = None;
    bool todo_restart = false;
    uint32_t guest_diff = kNormal;
    {
        std::lock_guard lock(g_m);
        if (in.at_title && !g_was_at_title) g_dismissed = false;  // back at the title: the menu again
        if (g_native && !g_refused_seen && g_resume.mode != NativeMode::Solo) {
            if (const char* why = native_refused_reason()) {
                g_refused_seen = true;
                log::warn("Co-op lobby: the native base refused co-op for this launch ({}): playing solo, the next "
                          "launch is solo, the partner is told", why);
                native_back_to_solo(std::string("Co-op could not start on this PC (") + why +
                                    "). You are playing solo and your saves are safe. Restart the game to try again.");
                send({kMsgNativeRefused});
                reset_phase();
            }
        }
        g_was_at_title = in.at_title;
        if (!in.connected) {
            if (g_phase != Phase::Menu && g_phase != Phase::HostSetup) log::info("Co-op lobby: the party broke up");
            if (g_phase != Phase::HostSetup) reset_phase();
            g_partner_diff = g_sent_diff = kUnknown;
        }
        // Our run entered the world (a new character capture since the phase began): the lobby's work is done. The host
        // in its setup first tells the guest (HostSetup below).
        if (g_phase != Phase::Menu && g_phase != Phase::HostSetup && in.run_generation > g_gen_mark) reset_phase();
        g_in = in;
        if (g_native) {
            if (g_phase == Phase::NativeRestart && g_restart_at && in.now_ms >= g_restart_at) {
                g_restart_at = 0;
                todo_restart = true;
            }
            if (g_resume.mode == NativeMode::Host && in.connected && !g_switch_sent) {
                // Back after the host's restart, the guest linked again: its turn.
                g_switch_sent = true;
                send({kMsgNative, g_resume.plan, 2});
                g_plan = static_cast<LobbyPlan>(g_resume.plan);
                log::info("Co-op lobby: back in co-op as the host; {}'s game restarts into co-op now", in.partner_name);
            }
            if (g_resume.mode == NativeMode::Guest && in.connected && !g_ready_sent) {
                g_ready_sent = true;
                send({kMsgNativeReady});
                g_plan = static_cast<LobbyPlan>(g_resume.plan);
                g_phase = Phase::GuestWait;
                g_gen_mark = in.run_generation;
                log::info("Co-op lobby: back in co-op as the guest; the host starts the journey");
            }
            if (g_resume.mode != NativeMode::Solo && !in.connected && g_resume_since &&
                in.now_ms - g_resume_since > 5 * 60 * 1000 && g_native_note.empty()) {
                g_native_note = "Your friend did not come back. Restart the game to play solo.";
                NativeModeSpec solo;
                native_write_ok(solo);
                native_resume_clear(g_native_ini);
                log::warn("Co-op lobby: the partner did not come back in 5 minutes: the next launch is Solo");
            }
            if (g_resume.mode == NativeMode::Host && g_guest_ready && !g_auto_started && in.connected &&
                in.at_title && g_phase == Phase::Menu) {
                g_auto_started = true;
                g_phase = Phase::HostPrepare;
                g_plan = static_cast<LobbyPlan>(g_resume.plan);
                g_gen_mark = in.run_generation;
                log::info("Co-op lobby: both games are in co-op: starting {}", plan_text(g_plan));
            }
            // In the world: this launch's co-op is under way; the next launch is the player's own game again (the
            // native base read its ini at this start).
            if (g_resume.mode != NativeMode::Solo && in.in_world && !g_resume_cleared) {
                g_resume_cleared = true;
                NativeModeSpec solo;
                native_write_ok(solo);
                native_resume_clear(g_native_ini);
                log::info("Co-op lobby: in the shared world; the next launch is solo again");
            }
        }
        const uint32_t own = g_own_diff.load();
        if (in.connected && own != kUnknown && own != g_sent_diff) {
            std::vector<uint8_t> m{kMsgDifficulty, 0, 0, 0, 0};
            std::memcpy(m.data() + 1, &own, 4);
            send(std::move(m));
            g_sent_diff = own;
        }
        switch (g_phase) {
            case Phase::HostPrepare:
                // Ready when the co-op save the host chose is the one in use (a new one: its new game not begun yet).
                if (save_ready && in.at_title && (g_plan != LobbyPlan::NewSave || w.new_game_pending || g_native)) {
                    if (g_plan == LobbyPlan::NewSave || w.new_game_pending) {
                        g_phase = Phase::HostSetup;
                        g_gen_mark = in.run_generation;
                        todo = PressNew;
                        log::info("Co-op lobby: our co-op save is ready: New Game (the host picks the difficulty)");
                    } else if (g_native) {
                        // Shared world (2026-11-09, tcB174-B183): the host's game loads first and the guest's follows
                        // once the host is in the world, as in a normal join. Both pressing Play together let the
                        // guest's character enter while the host's client still loaded, and the host's load never
                        // finished (the NPC hold releasing the host's NPCs to user 2 meanwhile).
                        g_phase = Phase::HostSetup;
                        g_gen_mark = in.run_generation;
                        todo = PressPlay;
                        log::info("Co-op lobby: both games are ready: our game loads the shared world first; {}'s "
                                  "follows once we are in", in.partner_name);
                    } else {
                        g_phase = Phase::Starting;
                        g_go_at = in.now_ms + kCountdownMs;
                        std::vector<uint8_t> m{kMsgGo, 0, 0, 0};
                        const uint16_t ms = static_cast<uint16_t>(kCountdownMs);
                        std::memcpy(m.data() + 2, &ms, 2);
                        send(std::move(m));
                        log::info("Co-op lobby: both games start in {} ms", ms);
                    }
                }
                break;
            case Phase::HostSetup:
                // The host went through the game's own setup (agreement, difficulty): when its run enters the world, the
                // guest's game follows.
                if (in.run_generation > g_gen_mark && (!g_native || in.in_world)) {
                    send({kMsgGo, 1, 0, 0});
                    log::info("Co-op lobby: our run entered the world: the guest's game follows");
                    reset_phase();
                }
                break;
            case Phase::Starting:
                if (g_go_at && in.now_ms >= g_go_at) {
                    g_go_at = 0;
                    g_phase = Phase::Menu;
                    todo = PressPlay;
                }
                break;
            case Phase::GuestWait:
                if (g_go_at && in.now_ms >= g_go_at) {
                    g_go_at = 0;
                    todo = PressPlay;
                } else if (g_native && g_new_go_since && !g_new_started && in.at_title) {
                    g_new_started = true;
                    todo = PressPlay;
                    log::info("Co-op lobby: the host's world is up: Play (our character joins it)");
                } else if (g_new_go_since && !g_new_started && save_ready && in.at_title &&
                           (g_partner_diff != kUnknown || in.now_ms - g_new_go_since > kDifficultyWaitMs)) {
                    g_new_started = true;
                    guest_diff = g_partner_diff != kUnknown && g_partner_diff <= 2 ? g_partner_diff : kNormal;
                    todo = GuestNew;
                    log::info("Co-op lobby: the host's run started: our New Game on difficulty {}", guest_diff);
                }
                break;
            case Phase::Menu:
            case Phase::NativeRestart:
                break;
        }
    }
    if (todo_restart) {
        if (!native_restart_game(g_restart_why)) {
            std::lock_guard lock(g_m);
            g_native_note = "The game could not restart by itself: close it and start it again from Steam.";
            reset_phase();
        }
    }
    if (todo == PressNew) worlds_lobby_press(true);
    if (todo == PressPlay) worlds_lobby_press(false);
    if (todo == GuestNew) worlds_lobby_guest_new(static_cast<int>(guest_diff));
}

LobbyView lobby_view() {
    const WorldsLobbyInfo w = worlds_lobby_info();
    std::lock_guard lock(g_m);
    LobbyView v;
    v.at_title = g_in.at_title;
    v.host = g_in.host;
    v.connected = g_in.connected;
    v.worlds_on = w.enabled;
    v.own_name = g_in.own_name.empty() ? "You" : g_in.own_name;
    v.partner_name = g_in.partner_name.empty() ? "your friend" : g_in.partner_name;
    v.can_continue = w.exists && w.has_saves && !w.new_game_pending;
    if (g_native) {
        // The native base keeps the co-op save (native_mode.h): this friend's folder exists = Continue.
        v.worlds_on = true;
        v.can_continue = native_coop_exists();
    }
    v.plan = g_plan;
    v.choosing = g_choosing;
    v.countdown = g_go_at ? static_cast<int>((g_go_at - std::min(g_go_at, now_ms()) + 999) / 1000) : -1;
    v.guest_wait = !g_in.host && g_phase == Phase::GuestWait;
    v.driving = w.switching;
    v.show = g_in.at_title && !g_dismissed && g_phase != Phase::HostSetup && !v.guest_wait;
    const std::string& f = v.partner_name;
    if (v.guest_wait) {
        v.wait_line = g_new_started ? "Joining the start of the story..."
                      : g_plan == LobbyPlan::NewSave
                          ? f + " is setting up the game. You join at the start of the story."
                          : g_native ? f + " is loading the shared world. You join right after."
                                     : f + " is getting ready. You both start together.";
    }
    if (g_phase == Phase::NativeRestart) v.status = "Restarting into co-op...";
    if (v.status.empty() && g_native && g_resume.mode == NativeMode::Host && !g_guest_ready && !g_auto_started) {
        v.status = v.connected ? f + "'s game is restarting into co-op..." : "In co-op mode: waiting for " + f + "...";
    }
    if (!v.status.empty()) {
    } else if (!g_native_note.empty()) {
        v.status = g_native_note;
    } else if (v.countdown >= 0) {
        v.status = "Both games start together...";
    } else if (g_phase == Phase::HostPrepare) {
        v.status = "Preparing your co-op save with " + f + "...";
    } else if (v.host) {
        v.status = v.connected ? f + " is in your party. Start Co-op when you are both ready."
                               : "Invite a friend to begin a co-op journey, or start solo.";
    } else {
        v.status = v.connected ? "You are in " + f + "'s party. " + f + " starts the journey."
                               : "Waiting for the host: accept their Steam invite, or use Join Game on them in Steam.";
    }
    return v;
}

void lobby_start_coop() {
    std::lock_guard lock(g_m);
    if (!g_in.host || !g_in.connected || g_phase != Phase::Menu) return;
    g_choosing = true;
}

void lobby_choose(LobbyPlan plan) {
    {
        std::lock_guard lock(g_m);
        g_choosing = false;
        if (plan == LobbyPlan::None || !g_in.host || !g_in.connected) return;
    }
    if (g_native) {
        std::lock_guard lock(g_m);
        if (g_resume.mode == NativeMode::Host && !g_guest_ready) {
            log::info("Co-op lobby: Start Co-op waits: {}'s game is not in co-op yet", g_in.partner_name);
            return;
        }
        if (g_resume.mode != NativeMode::Host) {
            if (!native_write(NativeMode::Host, plan)) {
                g_native_note = "Co-op could not be set up (see coopplus.log).";
                return;
            }
            g_plan = plan;
            g_phase = Phase::NativeRestart;
            g_restart_at = g_in.now_ms + kRestartDelayMs;
            g_restart_why = "the host starts co-op";
            send({kMsgNative, static_cast<uint8_t>(plan), 1});
            log::info("Co-op lobby: the host chose {}: this game restarts into co-op first", plan_text(plan));
            return;
        }
    }
    // The co-op save first (the network loop must not see the old one as ready), then the phase.
    std::string why;
    bool ok = true;
    if (plan == LobbyPlan::NewSave) ok = worlds_lobby_fresh(&why);
    else if (plan == LobbyPlan::FromSolo) ok = worlds_lobby_from_solo(&why);
    else worlds_lobby_continue();
    if (!ok) log::warn("Co-op lobby: the co-op save could not be prepared: {}", why);
    std::lock_guard lock(g_m);
    g_plan = plan;
    g_phase = Phase::HostPrepare;
    g_gen_mark = g_in.run_generation;
    send({kMsgPlan, static_cast<uint8_t>(plan)});
    if (plan == LobbyPlan::NewSave) send({kMsgFresh});
    log::info("Co-op lobby: the host chose {}", plan_text(plan));
}

void lobby_dismiss() {
    std::lock_guard lock(g_m);
    g_dismissed = true;
    g_choosing = false;
    log::info("Co-op lobby: closed (the game's own title)");
}

void lobby_session_ended() {
    std::lock_guard lock(g_m);
    if (!g_native) return;
    g_resume_since = 0;
    native_back_to_solo("The co-op session has ended. Restart the game to play again.");
    log::info("Co-op lobby: the session ended by a Leave: the next launch is Solo");
}

void lobby_reopen() {
    std::lock_guard lock(g_m);
    g_dismissed = false;
}

std::vector<std::vector<uint8_t>> lobby_take_outgoing() {
    std::lock_guard lock(g_m);
    std::vector<std::vector<uint8_t>> out;
    out.swap(g_out);
    return out;
}

void lobby_on_message(const std::vector<uint8_t>& b) {
    if (b.empty()) return;
    bool fresh = false;
    {
        std::lock_guard lock(g_m);
        switch (b[0]) {
            case kMsgPlan:
                if (b.size() >= 2 && !g_in.host) {
                    reset_phase();
                    g_plan = static_cast<LobbyPlan>(b[1]);
                    g_phase = Phase::GuestWait;
                    g_gen_mark = g_in.run_generation;
                    log::info("Co-op lobby: the host chose {}: waiting for the host", plan_text(g_plan));
                }
                break;
            case kMsgGo:
                if (b.size() >= 4 && !g_in.host) {
                    if (b[1]) {
                        g_new_go_since = now_ms();
                        log::info("Co-op lobby: the host's run started");
                    } else {
                        uint16_t ms = 0;
                        std::memcpy(&ms, b.data() + 2, 2);
                        g_go_at = now_ms() + std::min<uint16_t>(ms, 10000);
                        log::info("Co-op lobby: the host starts in {} ms", ms);
                    }
                    if (g_phase != Phase::GuestWait) g_gen_mark = g_in.run_generation;
                    g_phase = Phase::GuestWait;
                }
                break;
            case kMsgNativeRefused:
                if (g_native) {  // also before our own restart (the host refused right after its restart)
                    log::warn("Co-op lobby: {}'s game could not start co-op (its save protection did not install): "
                              "the next launch is solo", g_in.partner_name);
                    native_back_to_solo(g_in.partner_name.empty() ? std::string("Your friend's game could not start co-op. Restart the game to play solo.")
                                        : g_in.partner_name + "'s game could not start co-op. Restart the game to play solo.");
                    reset_phase();
                }
                break;
            case kMsgFresh:
                fresh = !g_in.host && !g_native;
                break;
            case kMsgNative:
                if (b.size() >= 3 && !g_in.host && g_native) {
                    g_plan = static_cast<LobbyPlan>(b[1]);
                    if (b[2] == 1) {
                        reset_phase();
                        g_plan = static_cast<LobbyPlan>(b[1]);
                        g_phase = Phase::GuestWait;
                        g_gen_mark = g_in.run_generation;
                        log::info("Co-op lobby: the host restarts into co-op; we follow when it is back");
                    } else if (b[2] == 2 && g_resume.mode != NativeMode::Guest) {
                        if (native_write(NativeMode::Guest, g_plan)) {
                            g_phase = Phase::NativeRestart;
                            g_restart_at = now_ms() + kRestartDelayMs;
                            g_restart_why = "joining the host's co-op world";
                            log::info("Co-op lobby: the host is back in co-op: this game restarts as its guest");
                        } else {
                            g_native_note = "Co-op could not be set up (see coopplus.log).";
                        }
                    }
                }
                break;
            case kMsgNativeReady:
                if (g_in.host && g_native) {
                    g_guest_ready = true;
                    log::info("Co-op lobby: {}'s game is in co-op as our guest", g_in.partner_name);
                }
                break;
            case kMsgDifficulty:
                if (b.size() >= 5) {
                    std::memcpy(&g_partner_diff, b.data() + 1, 4);
                    log::info("Co-op lobby: {}'s difficulty is {}", g_in.partner_name, g_partner_diff);
                }
                break;
        }
    }
    if (fresh) {
        std::string why;
        if (!worlds_lobby_fresh(&why)) log::warn("Co-op lobby: the host's fresh save could not start here: {}", why);
    }
}

void lobby_observe_c2s(const uint8_t* packet, size_t size) {
    uint16_t op = g_diff_op.load(std::memory_order_relaxed);
    if (!op) {
        op = opcode_for("ChangeGameDifficultyReq", 0xFFFF);
        g_diff_op = op;
    }
    if (size < 9 || op == 0xFFFF || static_cast<uint16_t>(packet[0] | (packet[1] << 8)) != op) return;
    uint32_t d = 0;
    std::memcpy(&d, packet + 5, 4);
    if (d != g_own_diff.exchange(d)) log::info("Co-op lobby: our difficulty is {}", d);
}

std::string lobby_difficulty_card() {
    const uint32_t own = g_own_diff.load();
    std::lock_guard lock(g_m);
    if (!g_in.connected || !g_in.in_world || own == kUnknown || g_partner_diff == kUnknown || own == g_partner_diff) return {};
    static const char* const kNames[] = {"Easy", "Normal", "Hard"};
    const auto name = [](uint32_t d) { return d <= 2 ? std::string(kNames[d]) : std::format("difficulty {}", d + 1); };
    return std::format("Choose the same difficulty\nYou play on {} and {} on {}. Pick the same one in the game's "
                       "Settings so the fights are as hard for both of you.",
                       name(own), g_in.partner_name.empty() ? "your friend" : g_in.partner_name, name(g_partner_diff));
}

void lobby_set_shared_quest_credit(bool on) { g_shared_quest_credit = on; }
void lobby_set_native_extra(const std::string& kv) {
    g_native_extra = kv;
    if (!kv.empty()) log::info("Lobby: [dev] native_extra '{}' is added to the host's native ini", kv);
}

}  // namespace cdmp::net
