#pragma once
// Co-op+ (2026-11-09, build 1.0.0.3576), ROADMAP B10 quest sync, HOST only. The design the user picked (2026-11-08,
// after a Discord poll): option 3 as the base -- each player plays its OWN quests in the shared world -- plus option 2
// as an optional bonus -- an objective one player completes also counts for the other when the other has that step
// active.
//
// What the game does natively (docs/research/s25/politica-misiones.md, re-found on 3576): every game event (kill,
// zone in / out, interaction ...) reaches the acting user's main character's ServerQuestActorComponent, whose single
// callback (registered as the thunk 0x2B9C660 -> 0x2B7A7A0) asks the FIELD for the quest manager (ServerField::vf40
// 0x2D98440, returning at 0x2B7A7DB) and applies the event to it (0x2B9A320 / 0x2B9A9C0 / 0x2B9AB70 / 0x2B9AFE0 /
// 0x2B9B310, the actor in r9). In the host's field vf40 is the HOST's manager: the guest's events advance the host's
// quests and never its own.
//
// [session2] quest_own_progress = 0 | 1: a mid hook right after that vf40 call (0x2B7A7DB, rax = the manager) gives
// the event the acting character's OWN manager ([[[char+0x68]+0x138]+0x18], vtables checked) -- for the host that is
// the same manager as before; for user 2 its own.
// [session2] quest_shared_credit = 0 | 1 (needs quest_own_progress): the callback (the thunk, an inline hook) runs a
// second time for the same event with the OTHER player's manager; the game's own apply functions only advance steps
// that manager has active, so nothing is credited for a step the other player has not reached.
// Both default 0 until a live test shows quest progress per player.

#include <string>

namespace cdmp::session2 {

void quest_route_start(bool own_progress, bool shared_credit, bool guest);
std::string quest_route_describe();

}  // namespace cdmp::session2
