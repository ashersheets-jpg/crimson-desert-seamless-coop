#pragma once
// Co-op+ (roadmap T19): summoning and party travel, both with consent.
//
// Summon: the Summon action (Shift+F4, pad View + right stick click) asks the other player to come; they get a line
// "<name> asks you to come to them: {GoToFriend} go" for kRequestMs and answer with their own go-to (Ctrl+F4, the
// hop teleport that lands beside the other player: never under ground, D31/D34), or let it lapse. kCooldownMs
// between summons. Party travel: when we cover more than kTravelM within a few seconds (fast travel, a long trip),
// the other player gets "<name> travelled far: {GoToFriend} follow them". Activity type 20: u8 kind (1 summon,
// 2 travelled).

#include <cstdint>
#include <optional>
#include <string>

#include "common/protocol.h"

namespace cdmp::net {

struct TravelTick {
    std::string line;   // the request on our screen ("" = none)
    std::string toast;  // "Summon sent" / "wait N s" ...
};
// Network loop. `me`: our position (nullopt: not in the world). `partner`: theirs (nullopt: not connected).
TravelTick travel_tick(bool connected, const std::optional<proto::Vec3>& me,
                       const std::optional<proto::Vec3>& partner, const std::string& partner_name, bool focused,
                       uint32_t now_ms);

// Their request waiting for our answer now: 0 none, 1 "come to me" (a summon), 2 they travelled far (follow?).
// Answered by our own go-to (input::Action::GoToFriend); the shared-world HUD offers it as a hold of interact.
int travel_request();

// Activity type 20 from the other player.
void travel_on_event(const uint8_t* data, size_t n);

}  // namespace cdmp::net
