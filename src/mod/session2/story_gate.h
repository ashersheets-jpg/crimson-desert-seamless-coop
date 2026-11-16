#pragma once
// Co-op+ 2026-11-14: the shared cutscenes READY CHECK (docs/research/shared-cutscenes.md; the owner's design: a story
// trigger waits until both players are there and both pressed, a prompt on both screens, the waiting player may go on
// alone). [session2] story_gate = 0 (default) | probe | hold. The decisions: story_gate_logic.h.
//   probe (read only): every story trigger of either player is logged with its scene's name (a player's
//          SequencerTriggerVolumeInOutReq 2808 and the StageBranchStateReqs 2644 of story scenes, cd_seq_quest_*),
//          with the sequencer's resource name and file-name hash when a stage element of that id was prepared.
//   hold:  a player's own entry into a story scene's volume is held (with that sequencer's 2644s) until both players
//          pressed (both at the same scene) or the player went on alone; the Co-op+ layer shows the prompt
//          (cdmp_story_gate_state / cdmp_story_gate_press).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cdmp::session2 {

void story_gate_set_mode(const std::string& mode);
bool story_gate_on() noexcept;
// shop_probe's P point (after a stage element's preparation, any manager): the sequencer id, its resource base
// name (up to 40 bytes read) and its file-name hash (playData +0x20: the same for both players' copies). Any server thread.
void story_gate_note_prepared(uintptr_t mgr, uint64_t id, uint32_t hash, const char* name, size_t n) noexcept;
// probe: a Req of user 2 (server.cpp, before it is queued) or of the host's own client (the sniffer's PostPacket hook):
// opcode and payload (after the 5-byte header). Any thread; read only.
void story_gate_observe(bool user2, uint16_t opcode, const uint8_t* payload, size_t size) noexcept;

// hold. User 2 (the tunnel's offer, session2.cpp): true = the Req is taken by the gate (held or dropped). Released Reqs
// come back through story_gate_next_release (peek; offered before any new Req) and story_gate_release_done (pop).
bool story_gate_take_user2(const std::vector<uint8_t>& packet) noexcept;
bool story_gate_next_release(std::vector<uint8_t>& out) noexcept;
void story_gate_release_done() noexcept;
// The host's own client (the sniffer's PostPacket hook, the whole wire packet): true = taken (the original post is
// skipped; a release posts it again through sniffer_inject_c2s).
bool story_gate_take_host(const uint8_t* packet, size_t size) noexcept;
// Research (2026-11-14, cs8c: the host's client starts the scene itself, the held Reqs do not stop it): the host's
// stack at PostPacket's entry (rsp, up to 36 KB read: the 2808 sender's frame is ~32 KB) for its own entry into a main
// story scene or a cinematic and their 2644s; the words that are return addresses into the exe (a call right before
// them) are logged (a few times).
// The frames come from RtlVirtualUnwind from PostPacket's entry (rip = the function, the hook's rsp and nonvolatile
// registers: rbx rbp rsi rdi r12 r13 r14 r15).
void story_gate_host_stack(const uint8_t* packet, size_t size, uint64_t rsp, uint64_t rip, const uint64_t nv[8]) noexcept;
// The observer copy (observer_seq_copy.cpp), hold mode: true = the host's sequencer `host_seq` is a story scene
// (cd_seq_quest_*; by its id's prepared name, or by `hash`, the 2661's file-name hash @27, when a player's manager
// prepared a story scene of that hash), so the host's copy is not sent to the guest (its 2981s and 2810 neither).
// 2026-11-14: only when user 2 had its own copy (the guest's game held both copies of the cabin's cinematic and never
// started its own). 2026-11-16 (the testers' zips): ANY host story scene -- with story scenes together each player
// plays their own; the host's next step's cinematic (cd_seq_quest_intro_0100_dlc) copied to the guest while the
// guest's own cinematic played left the guest on a black screen when its own ended.
bool story_gate_skip_copy(uint64_t host_seq, uint32_t hash = 0) noexcept;
// observer_hold_scene_order (hold mode): true = `hash` is the file-name hash of a story scene (cd_seq_quest_*) user 2's
// manager prepared: its 2661 waits for the 2005s of the NPCs it binds like a trade scene's (cs9i: user 2's cabin
// cinematic went out 330 ms before its comrades reached the guest, and the guest's game never had it).
bool story_gate_story_hash(uint32_t hash) noexcept;
// Co-op+ 2026-11-15 (research, read only): [session2] where_probe=<hex rva>:<hex bytes of whole instructions, >= 5>.
// A mid hook at exe+rva logs the caller chain (RtlVirtualUnwind, 12 deep) of its first 6 hits, then stays quiet.
void where_probe_start(const std::string& spec);
// The same by user 2's sequencer id (the 2661's +9; cs9j: the wire's +27 hash did not match): its scene is a story one.
bool story_gate_user2_story(uint64_t id) noexcept;
// observer_hold_acks' route (every packet to user 2's live session, hold mode): true = kept here (the caller drops
// it from its queue). User 2's own step cinematic (a cd_seq_quest_*_dlc 2356 whose step has a main scene _00) and the
// later packets of its id stay on the host until that step is released for user 2 (both pressed, user 2 went on
// alone, or its entry passed), then go to user 2's queue in order: without the cinematic its game cannot start the
// scene early (cs9n: the guest's cutscene began the moment it entered, before the presses).
bool story_gate_keep_user2(uint16_t opcode, std::span<const uint8_t> payload, const std::vector<uint8_t>& packet) noexcept;
// The layer: a player's press (0 the host, 1 user 2); the released players (bit 0 host, bit 1 user 2).
int story_gate_press(int who) noexcept;
// The layer: the state (see cdmp_story_gate_state in story_gate.cpp); 0 = off.
int story_gate_state(uint8_t* out, int cap) noexcept;

}  // namespace cdmp::session2
