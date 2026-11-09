#pragma once
// Co-op+ (roadmap T15): reviving each other.
//
// A player who goes down while the other is alive and connected is "downed": for kBleedOutMs the other player can
// revive them -- within kReachM of them, hold E (the interact key) or the ReviveFriend action (Shift+F7, pad View +
// LB) for kHoldMs. The
// downed player then gets up where they fell with full health (revive_prompt.h: revive_in_place) and both get a
// notice naming who revived whom. The game's own choices (Q Instant Revive, U Revive at Checkpoint) stay available
// the whole time; after the bleed-out only they remain. Not during a boss fight's wait (boss_death_wait has its own
// rule). [coop] teammate_revive=0 turns it off.

#include <cstdint>
#include <optional>
#include <string>

#include "common/protocol.h"

namespace cdmp::net {

struct ReviveRemote {
    float hp = 0, hp_max = 0;
    proto::Vec3 pos;
};

struct ReviveTick {
    std::string line;   // the reviver's hint / progress, or the downed player's bleed-out line ("" = none)
    std::string toast;  // "You revived X" / "X revived you" ("" = none)
};

// Network loop, every pass. Our health and position; the other player's (nullopt: not connected).
ReviveTick revive_partner_tick(bool connected, float hp, float hp_max, const proto::Vec3& pos,
                               const std::optional<ReviveRemote>& partner, const std::string& partner_name,
                               bool focused, uint32_t now_ms);

// Activity type 18 from the other player: they revived us.
void revive_partner_on_event();

// A friend is connected and their state reaches us (as of the last revive_partner_tick). On the native base, where no
// copy of them is ever spawned by us, this is what "a friend is with us" means (revive_filter_s2c).
bool revive_partner_present();

// [coop] teammate_revive; [dev] revive_test (one PC: a dead player may revive the echo copy of themselves, so the
// whole loop runs: hold -> message -> echoed back -> revived in place).
void revive_partner_set(bool on, bool test);

}  // namespace cdmp::net
