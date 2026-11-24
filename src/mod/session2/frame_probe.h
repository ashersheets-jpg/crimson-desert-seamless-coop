#pragma once
// Co-op+ (2026-11-24, P1 Borque (5)(6): the joined player cannot fish or mine). LOG ONLY, [session2] frame_probe = 0 | 1
// (default 0), both roles. build/decomp/notes_gimmick_handoff.md (private) section 3: on 3732 the fish is summoned by
// GimmickExecuteSummonCharacterFrameEventReq 2594, which the client builds only inside 0xA0CB80, the frame-event runner
// of ClientGimmickActorComponent; it returns silently unless [gimmick+0x90] (the controller) is the local player's user,
// and the server's 2594 handler never checks the requester. This probe answers which gimmick a cast runs its events
// on (the fishing spot or the rod's own device) and whether the joined player's game stops there:
//   - both games, a mid hook at 0xA0CB80's entry: per gimmick key, calls and "would refuse" (controller != local
//     user), the first refusals of new keys as lines, a summary every 30 s;
//   - the host, at the server's receive: every 2594 (summon) and 2304 (InteractionDoStepReq, the bite) by origin.
#include <cstdint>
#include <string>

namespace cdmp::session2 {

// mod.cpp's worker, both roles. Nothing for 0.
void frame_probe_start(uint32_t on, bool guest);
// HOST, ServerNet::OnRecvPacket's mid hook (server.cpp): one request about to be handled. Returns at once when off.
void frame_probe_server_ingress(uint16_t op, uintptr_t packet, uint16_t size, bool from_user2) noexcept;

}  // namespace cdmp::session2
