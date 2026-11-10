#pragma once
// Co-op+: shared activities (cutscenes and minigames), EventKind::Activity.
//
// Cutscenes run inside each player's own client: the server only sees side effects, there is no "start" or "skip"
// message (captures of the Hernand pub arm wrestling intro, 2026-11-02). We read the game's "cutscene playing" flag
// (game::cutscene_playing) and tell the other player when ours starts and ends. Skipping needs everyone: while the
// other player is in a cutscene too, within kNearM of us, a keyboard hook holds back our Esc and only tells them we
// pressed it; when both have pressed, each mod gives its own game the Esc (D32 as revised by the user 2026-11-02).
// If the other player is not in a cutscene near us, Esc works as in the game.
//
// Minigames: the client asks the server to start one (ActivateMiniGameReq / JoinMiniGameReq), the server spawns the
// NPC opponent (SpawnMiniGameCharacterReq/Ack), the match itself is simulated in the client, and the end is
// LeaveMiniGameReq (names the minigame's stage) + MiniGameResultReq. We tell the other player "started" and
// "finished <game>"; the result bytes did not differ between a win and a loss, so no winner is claimed.
//
// Payload: u32 sequence | u8 type | data. Sent three times on the lossy channel; the receiver drops repeats.
//   type 1 cutscene started | f32 x, y, z     type 2 cutscene ended     type 3 skip pressed | f32 x, y, z
//   type 4 minigame started | u16 minigame id  type 5 minigame finished | u32 stage key
//   type 6 versus (10 Hz in a match) | u8 in a match | u32 E presses this match | f32 gauge (the host's)
// Versus arm wrestling (D35): when both players are in a match (each at their own table), the host owns one gauge
// that both players' E presses push; each mod writes it into its game (game::minigame_gauge_address), so the one who
// reaches 100 wins on both screens.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cdmp::net {

// Client requests we watch (minigame start/end).
void activity_observe_c2s(const uint8_t* packet, size_t size);
// Co-op+ (T16): our server's messages: we became wanted (ChangeWantedStateAck / IncreaseWantedPriceAck / a new crime
// record): the other player gets a notice. Crimes and bounties stay with the player who committed them.
void activity_observe_s2c(const uint8_t* packet, size_t size);

// An Activity event from the other player.
void activity_on_event(const std::vector<uint8_t>& payload);
// B16b (user 2026-11-09): the player interaction menu asked for (the hold-R prompt on the other player, render/hud.cpp):
// the next challenge tick acts as Shift+F5 does (opens the challenge list, or accepts a received challenge).
void activity_request_menu();
// Shared world (2026-11-10): the challenge between the two players now, for the HUD's accept prompt and the Co-op
// tab's rows. state: 0 none, 1 they challenged us (waiting for our answer), 2 we challenged them, 3 on.
struct ChallengeView {
    int state = 0;
    std::string game;  // "a duel"
};
ChallengeView activity_challenge_view();
// B16b: the hold prompt on the other player is up now (render/hud.cpp): on a pad the challenge chord then FILLS its
// ring (the menu opens when full) instead of opening the menu at the press.
void activity_set_hold_prompt(bool shown);
// B16b: the open player interaction menu (false: none): its entries (challenges, "Trade (coming soon)", "Leave") and
// the chosen one, for the HUD's shop-style card.
bool activity_menu_snapshot(std::string& title, std::vector<std::string>& items, size_t& pick);

struct ActivityTick {
    std::string banner;  // persistent line while the other player is in a cutscene ("" = none)
    std::string toast;   // one-off message ("" = none)
};

// Network loop, every frame: reads our cutscene flag, our Esc key, and applies the other player's skips.
// `me` / `partner`: positions (partner unset = not connected). `focused`: the game window is in front.
ActivityTick activity_tick(bool connected, bool host, bool focused, std::optional<std::array<float, 3>> me,
                           std::optional<std::array<float, 3>> partner, const std::string& partner_name,
                           uint32_t now_ms);

// [dev] skip_test: one-PC test of the shared skip: during each of our cutscenes act as if the other player were in it
// too and pressed skip 6 s in (the echo peer cannot press keys).
void activity_set_skip_test(bool on);

// [dev] versus_test: one-PC test of versus arm wrestling: a simulated other player in every match of ours, pushing
// this many times a second (0 = off).
void activity_set_versus_test(int presses_per_s);
// Co-op+ (T09): the other player is in a menu now (their name tag says so).
bool activity_partner_in_menu();
// [dev] seat_test: in any arm / hand wrestling match against the NPC (no versus, no challenge), the other player's
// copy takes the NPC's seat and the NPC is hidden (F9 one-PC test: the echo copy plays our own actions there).
void activity_set_seat_test(bool on);
// [dev] event_test (T22): this stage key is announced as a world event 20 s after connecting.
void activity_set_event_test(uint32_t key);

// [dev] challenge_test: one-PC test of challenges: 1 = a simulated other player accepts our Shift+F5 invite after
// 2 s; 2 = also Shift+F6 (with nothing pending) makes them challenge us to a rock-paper-scissors duel.
void activity_set_challenge_test(int mode);

// [dev] rps_test: one-PC test of the RPS duel: a simulated other player who always shows this hand (0 scissors,
// 1 paper, 2 rock; -1 = off), 4 s after our pick.
void activity_set_rps_test(int hand);

// Another Co-op+ module's Activity event (stage_cinema: types 7..9), on the same sequence as ours.
void activity_send(uint8_t type, const std::vector<uint8_t>& data);
// tcB291: the host's "Leave the session". The host says goodbye first; the guest goes back to its title (the host's
// server then unloads the guest the game's own way) and only after that the host returns to its title. Returning
// with the guest still attached crashed the host in whichever request of the guest came in during the unload.
void activity_send_bye();   // host
bool activity_take_bye();   // guest: true once after the host's goodbye arrived

// The rock-paper-scissors duel as the overlay shows it over the two characters (render/hud.cpp): while a duel is on,
// "choosing" / "picked" over each, and at the reveal both hands (0 scissors, 1 paper, 2 rock) with the winner.
struct RpsView {
    bool on = false;
    int ours = -1, theirs = -1;     // shown hands (-1: none yet / hidden)
    bool ours_picked = false, theirs_picked = false;
    int winner = 0;                 // at the reveal: 1 us, 2 them, 0 a draw
    float reveal_t = -1;            // seconds into the reveal (-1 not revealing)
};
RpsView activity_rps_view();

// Human name of a minigame stage key ("arm wrestling"...), "a minigame" when unknown.
std::string activity_minigame_name(uint32_t stage_key);

// Co-op+ (2026-11-05): a finished challenge's result ("Victory" / "Defeat", game and score) as the big title card,
// for a few seconds after the match; title "" = none.
struct MatchHud {
    std::string title, subtitle;
    unsigned int color = 0;
    float alpha = 0;
};
MatchHud activity_match_hud();

}  // namespace cdmp::net
