#pragma once
// Co-op+ (roadmap T41): a race to the waypoint, a challenge game ("Race to the waypoint" in the picker).
//
// The finish is the host's map waypoint (right-click on the map), or the guest's when the host has none: both players
// know both waypoints (each one is sent as a pin, coop.cpp), so both pick the same point without a message of its
// own. After 3, 2, 1 the first to come within kFinishM of it (on foot, on a horse, gliding: anything goes) wins; the
// finish time goes to the other player (Activity type 24: u32 round | u32 ms). A new waypoint starts the next round;
// with no waypoint the banner asks for one.

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace cdmp::net {

inline constexpr uint32_t kRaceStage = 0xD0E10003;  // the challenge "stage" of a race

// Our own map waypoint (local_capture) and the other player's (coop.cpp), as they are set; nullopt = removed.
void race_set_own_waypoint(std::optional<std::array<float, 3>> pos);
void race_set_partner_waypoint(std::optional<std::array<float, 3>> pos);

// The finish race_tick would pick now: the host's waypoint, else the guest's; with no host flag at hand, ours if we
// have one, else the other player's (the same point while only one player has set one). The dev script's tpwp.
std::optional<std::array<float, 3>> race_any_waypoint();

// Activity type 24 from the other player.
void race_on_event(const uint8_t* data, size_t n);

struct RaceTick {
    std::string banner;
    std::string toast;
    int round_result = 0;  // this tick a round ended: 1 we won, 2 they did
};
RaceTick race_tick(bool on, bool connected, bool host, const std::string& who, std::optional<std::array<float, 3>> me,
                   std::optional<std::array<float, 3>> partner);

struct RaceHud {
    std::string title, subtitle;
    unsigned int color = 0;
    float alpha = 0;
};
RaceHud race_hud();

}  // namespace cdmp::net
