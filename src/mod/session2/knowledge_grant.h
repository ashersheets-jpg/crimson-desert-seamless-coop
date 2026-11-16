#pragma once
// Co-op+ 2026-11-16, B04 (the knowledge half: "knowledge learned by one counts for both"). [session2] knowledge_grant
// = 0 (default) | 1, HOST. The host's server learns a knowledge for a player in FUN 0x2E7C210 (3663; 3576 0x2E14C20,
// the end of every learn path: AcquireKnowledge's message 0x7FB, follow-learn, rewards). After it, when the learner
// is one of the two players' characters and its level of that knowledge rose, the OTHER player's character gets the
// same level through the game's own learn entry (the thunk 0x2E7E1F0, 3576 0x2E16BC0: it posts the learn to that
// actor, whose game sends its client the usual NormalLearnKnowledgeListAck). The other's level is read first, so the
// two never grant back and forth. Research: PROGRESS 2026-11-16 (the decomp agent's report). Nothing natively shares
// knowledge between players (ShareKnowledge* is NPC friendship).

#include <string>

namespace cdmp::session2 {

void knowledge_grant_start(bool on, bool guest);
std::string knowledge_grant_describe();  // "" when off

}  // namespace cdmp::session2
