#pragma once
// Piece 4 of host-authoritative enemies (docs/research/pieza4-capturas.md §6,
// pieza4-botin-reaccion.md §5): our blows on the copy of one of the other
// player's enemies hurt the real enemy in THEIR world.
//
// Both sides run the same code:
// - Learner: our own blows on our own enemies teach what each of our attacks
//   (accessor action hash + frame) costs on a defence-1.0 enemy ("base"),
//   which flags its result has, and keep its AttackTargetAck /
//   ApplyActionRestrictionAck as templates for the other side's copy of us.
// - Sender: a DoFrameEventReq of our player whose targets include enemy
//   copies (our server ignores those silently, pieza4-capturas.md §1.3)
//   becomes one GuestHit event per copy.
// - Owner of the enemy: applies the blow with attribution to its own player
//   (VaryStatCheatReq; a lethal blow goes down to 1 HP and is finished by a
//   collision of our player, a normal kill with skill XP and loot),
//   and injects the AttackTargetAck of that attack from the other player's
//   copy (the AI notices it), with the stagger when that attack staggers
//   ([coop] guest_hit_stagger: restriction + hit buff, pieza4-tambaleo.md).

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/learnt_file.h"
#include "common/protocol.h"

namespace cdmp::net {

// Co-op+ (T17): [coop] guest_finishes (default 1). 0: the guest's blows never kill one of the host's enemies (left at
// 1 HP for the host), as with bosses -- the strict anti-grief mode.
void guest_hits_set_finishes(bool on);

void guest_hits_set_enabled(bool on, bool stagger);
// [coop] finish_arrow: a lethal blow of the other player ends with our usual
// arrow (a normal death animation) instead of the old collision.
void guest_hits_set_finish_arrow(bool on);
// cdmp_learnt.txt (learnt_store.cpp): what our attacks do and the enemies'
// defence. Learnt whether or not co-op hits are on, so playing alone counts too.
void guest_hits_learnt_get(LearntCombat& out);
void guest_hits_learnt_put(const LearntCombat& in);
// Our gear changed (local_capture): the next clean blows tell how much our
// attacks changed, and every attack learnt before is rescaled by that.
void guest_hits_on_gear_change();
// Co-op+ duel (coop_duel.h): how much one of our attacks (StartActionReq action hash) takes from a defence-1.0
// enemy, learnt or from the seed table (the strongest of its hit frames); 0 when it is not a known attack.
double guest_hits_action_damage(uint32_t action_hash);
// Co-op+ hunting contest (coop_hunt.h): one of our attacks (DoFrameEventReq, arrows included) named `key` as a target
// within the last `within_ms`.
bool guest_hits_we_hit(uint32_t key, uint64_t within_ms);
// Co-op+ (2026-11-07, the user's "Journey together" showed the guest's kills as the host's): the mod finished this
// enemy of ours for the OTHER player (their lethal blow; our server names us as the killer) within `within_ms`.
bool guest_hits_finished_for_partner(uint32_t key, uint64_t within_ms);
// [dev] script "guesthit": a GuestHit as a real blow of ours with attack hash:frame on the nearest copy of the other
// player's enemies (for testing the host's damage and stagger without simulated attack input). False if none is near.
bool guest_hits_dev_hit(uint32_t hash, uint32_t frame);

// Sniffer writer thread.
// Co-op+ (2026-11-25): the templates of a real staggering blow (a player's sword on a bandit, 3576; the ones this module
// uses until the host's own blows teach it): AttackTargetAck 219 B, restriction 69 B, hit buff 77 B, with the keys
// they name. The duel's stand-in when its attacker has learnt no stagger of its own (player_hits.cpp).
void guest_hits_default_stagger(std::vector<uint8_t>& ata, std::vector<uint8_t>& aar, std::vector<uint8_t>& buff,
                                uint32_t& victim, uint32_t& attacker);
void guest_hits_observe_c2s(const uint8_t* packet, size_t size);
void guest_hits_observe_s2c(const uint8_t* packet, size_t size);

// Co-op (network thread, every loop): GuestHit events to send; also applies
// the other player's hits that are due and finishes lethal ones.
std::vector<proto::GameEvent> guest_hits_take_events();
// Co-op (network thread): a GuestHit from the other player, about one of OUR
// enemies. Applied `delay_ms` later (the playout delay of their copy), from
// guest_hits_take_events.
void guest_hits_on_event(const proto::GameEvent& ev, uint64_t delay_ms);

}  // namespace cdmp::net
