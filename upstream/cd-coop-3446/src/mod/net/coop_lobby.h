#pragma once
// Co-op+ (user 2026-11-06): the co-op main menu at the title screen, before anyone plays. "Start Solo", "Invite
// Friends", "Start Co-op" (only with a friend in the party); the party's Steam names in the top right, a crown by the
// host's. Start Co-op asks the host how the journey begins: a new co-op save (both from the start of the story; the
// host alone picks the difficulty), from the host's solo progress (the friend joins the host's story), or the co-op save
// the two already have. The guest never sees a setup screen: a "waiting for the host" screen covers its game while the
// mod takes it through New Game and the host's difficulty once the host's game starts the run (user: "the game should
// start for both players at the beginning of the first cutscene"). Continuing: both games load together after a 3 s
// countdown. It keeps the co-op saves' rules (coop_worlds.h): one co-op save per friend, never your own; a fresh one
// sets the old one aside (kept). Different difficulties in a session bring up a card on both: choose the same.
//
// The network loop ticks it and sends its messages (inside RelKind::World, types 2..); the render thread draws it
// (render/coop_page.cpp) and calls the actions.

#include <cstdint>
#include <string>
#include <vector>

namespace cdmp::net {

struct LobbyInput {
    uint64_t now_ms = 0;
    bool at_title = false;   // the title screen (no character, not loading, the game window up)
    bool in_world = false;   // our character is loaded
    bool loading = false;    // the game is loading a save
    // Our character's data was captured this many times (capture_template_generation): it moves on when a run really
    // enters the world. New Game makes the character already at the difficulty dialog, so "in the world" cannot say it.
    uint32_t run_generation = 0;
    bool connected = false;
    bool host = false;
    std::string own_name, partner_name;
    // The partner, for the native base's restart into co-op (native_mode.h): over Steam its SteamID, direct its IPv4.
    bool steam = true;
    uint64_t partner_steam_id = 0;
    std::string partner_address;
    // Radmin VPN / Direct IP (direct_ip.h): the session's knock token and port, kept across the restart.
    bool direct = false;
    uint64_t direct_token = 0;
    uint16_t direct_port = 0;
};
void lobby_tick(const LobbyInput& in);

// Co-op+ on the native base (coopplus.asi; docs/design/native-join-and-saves.md): Start Co-op restarts both games
// into the native shared world (host first, then the guest), then both start together. `coopplus_ini` keeps the
// restart's state ([native_resume]); `bin_dir` holds the native cdmp.ini. Call once at start, before the first tick.
void lobby_native_init(const std::wstring& coopplus_ini, const std::wstring& bin_dir);

// How the host's co-op journey begins (Start Co-op's question).
enum class LobbyPlan : uint8_t { None = 0, NewSave = 1, ContinueCoop = 2, FromSolo = 3 };

struct LobbyView {
    bool solo_restarts = false;   // still in co-op mode with no session: Start Solo restarts the game
    bool show = false;            // the menu is drawn now (at the title, not dismissed)
    bool at_title = false;        // at the title (shown or dismissed)
    bool choosing = false;        // host: Start Co-op's question is open
    bool guest_wait = false;      // guest: the "waiting for the host" screen covers the game
    bool driving = false;         // the mod is pressing the title's keys itself (the game must take them)
    std::string wait_line;        // what that screen says under its title
    bool host = true;
    bool connected = false;
    bool worlds_on = false;       // co-op saves are on (else co-op uses each player's own save)
    std::string own_name, partner_name;
    bool can_continue = false;    // a co-op save with this friend that has been played
    LobbyPlan plan = LobbyPlan::None;
    int countdown = -1;           // seconds left before both games start (-1: none)
    std::string status;           // one line under the menu
};
LobbyView lobby_view();

// Render thread: the menu's actions.
// B10 option 2: [coop] shared_quest_credit, written into the host's native ini at the next co-op launch.
void lobby_set_shared_quest_credit(bool on);
// dev: [dev] native_extra (host only; research runs).
void lobby_set_native_extra(const std::string& kv);
void lobby_start_coop();          // host: opens the question (with a friend in the party)
void lobby_choose(LobbyPlan plan);  // host: the answer (None = cancel)
void lobby_start_solo();          // the Start Solo card (restarts when the game is still in co-op mode)
void lobby_dismiss();             // Start Solo / Esc: the game's own title until the next time we are at the title
void lobby_session_ended();    // a deliberate Leave: the next launch is solo, the title says so (tcB293)
void lobby_reopen();              // F8 at the title

// Network loop.
std::vector<std::vector<uint8_t>> lobby_take_outgoing();  // RelKind::World messages
void lobby_on_message(const std::vector<uint8_t>& bytes);
// Capture writer: our client's requests (the difficulty the player chose).
void lobby_observe_c2s(const uint8_t* packet, size_t size);
// The difficulty card (both players chose different ones): "" = none.
std::string lobby_difficulty_card();

}  // namespace cdmp::net
