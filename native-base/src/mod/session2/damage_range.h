#pragma once
// Co-op+ (2026-11-11, build 1.0.0.3576): damage counts near ANY player, not only near the host.
//
// The game's damage routine (exe+0x225C8E0, research notes local) sets the amount of every hit to 0 when the victim
// is 80 m or more (30 m vertically; 500 m with the GlobalDetectTarget buff) from the character of the user who owns
// the victim's FIELD. In the shared world both players live in the host's field, so nothing more than 80 m from the
// host could be hurt: a guest fighting there took no damage (ff3 / ff5: 300 / 300 through 42 blows) and did none.
//
// A mid-function hook at exe+0x225D011, where the two positions and the two limits are in registers (victim x / z / y
// in xmm6 / xmm9 / xmm10, the field owner's in xmm11 / xmm12 / xmm13, limits in xmm0 / xmm1): when the owner is out of
// range but another player's character is in range of the victim, the owner's position is replaced by that player's
// for this one comparison. Nothing else changes: far from every player the game's rule still holds.
#include <string>

namespace cdmp::session2 {

// [session2] damage_range_players (default 1), the host only. Checks the hook site's bytes first.
void damage_range_start(bool on, bool guest);
bool damage_range_on();
// The server thread, about once a second (ai_handoff's check): where the users' characters are (x, y, z each).
void damage_range_players(const float* xyz, int count) noexcept;
std::string damage_range_describe();

}  // namespace cdmp::session2
