#pragma once
// Co-op+ hunting contest (user 2026-11-02: "other competitive minigames vs friends: target shooting..."). D44.
//
// The game's own shooting contests (archery at Lioncrest Manor, the firearm contest) are client-run stages against
// an NPC and unlock through the story, so the two-player contest is the mod's, on the game's own animals: a challenge
// game ("Hunting contest" in the picker). After 3, 2, 1, each player has kRoundMs to bring down wild animals; each
// kill (a death notice, NotifyHandleDeadAck / NotifyHandleFatalAck, of a wild animal we hit in the last 8 s: animal_names.inc, generated
// from the game data) scores 1 (small game), 2 (deer, foxes...) or 3 (bears, wolves, boars...). Scores go to the other
// player as they change (Activity type 14); highest score when time runs out wins the round; then a new round.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace cdmp::net {

inline constexpr uint32_t kHuntStage = 0xD0E10002;  // the challenge "stage" of a hunting contest

// Our server's messages: our kills.
void hunt_observe_s2c(const uint8_t* packet, size_t size);

// Co-op+ (T36): the game animal's name for a variant ("Deer"), nullptr when it is not one of the contest's animals.
const char* hunt_animal_name(uint32_t variant);

// Activity type 14 from the other player.
void hunt_on_event(const uint8_t* data, size_t n);

struct HuntTick {
    std::string banner;
    std::string toast;
    int round_result = 0;  // this tick a round ended: 1 we won, 2 they did, 3 a draw
};
HuntTick hunt_tick(bool on, bool connected, const std::string& who);

// The big centred title (countdown, result); "" = none.
struct HuntHud {
    std::string title, subtitle;
    unsigned int color = 0;
    float alpha = 0;
};
HuntHud hunt_hud();

// [dev] hunt_test: one-PC test: any death we see counts as our kill (the KillCheatReq kills of the dev script name
// no killer), and the echo of our score is ignored.
void hunt_set_test(bool on);

// [dev] script step "killgame": KillCheatReq on up to 4 loaded wild animals (false if none is loaded).
bool hunt_dev_kill();
// [dev] script step "tpkill": the first animal the last "killgame" killed (0 = none yet).
uint32_t hunt_dev_last_kill();

}  // namespace cdmp::net
