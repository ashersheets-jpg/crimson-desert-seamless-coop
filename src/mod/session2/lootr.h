#pragma once
// Co-op+ (2026-11-10, user: "Lootr" -- every world item can be taken once by EACH player; it disappears only for the
// one who took it). Phase 1 (host side): an item the HOST takes stays for the guest. The host's real pickup runs as
// always (its save's cool time and the regrow stay right); what the server sends the guest about that gimmick (2214,
// 2760, 2096, 3067) is held back, so the guest still sees it; when the guest takes it (3008 / 3073), we answer like
// the server, add the same item to the guest's bag on the server thread (trade_probe entry 0xC0DE0005) and replay the
// held packets. The item = what the host's pickup added to its bag. Not yet: the guest taking first (the host's own
// client), chests and bodies, keeping the list across sessions. lootr_probe: logs the pickup worker and the host's
// packets after it. build/decomp/notes_lootr.md (local); recordings in PROGRESS.md (tcB324-326).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cdmp::session2 {

// bodies: enemy / animal body loot per player too (the second looter gets the game's own second roll).
void lootr_start(bool on, bool probe, bool guest, bool bodies);
// [dev] lootr_kill_hits (tests only, host): whatever the host hits dies at once.
void lootr_set_kill_hits(int mode);
void lootr_host_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept;
// server.cpp, a packet about to be queued for the live session 2: true = held back whole; `p` may be rewritten (a
// RemoveActorAck without the kept gimmick).
bool lootr_on_guest_out(std::vector<uint8_t>& p) noexcept;
// session2.cpp offer(), user 2's request: 1 = answered here (dropped), 0 = pass.
int lootr_on_guest_req(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
// Network thread: requests to hand the server as user 2's (the add of its copy).
void lootr_tick(std::vector<std::vector<uint8_t>>& to_server) noexcept;
std::string lootr_describe();
// lootr_probe (host, the SERVER thread: trade_probe's entry 0xC0DE0006): the gimmicks around the host that have a Drop
// event (chests, boxes, breakables) are logged with name, position and rolls left. build/decomp/notes_find_chests.md.
void lootr_chest_scan(uintptr_t host_actor) noexcept;
// Chests per player, phase 1 ("shared open, each their own content"; tcB365): a chest one player opens puts its
// content into that player's bag (the gimmick Drop worker, mode 1). What the bag gained is kept as the OTHER player's
// share and added to their bag as soon as they are within 6 m of the chest (at once when they stand there), for 30
// minutes. Not yet: the chest staying closed on the other player's screen, shares across sessions.
void lootr_set_chests(bool on);
// Enemy / animal bodies, the same way (2026-11-11): what one player loots from a body goes to the other player too
// when they are within 30 m of the body (now, or within 10 minutes). No notice: the game's own "acquired" lines.
void lootr_set_body_share(bool on);
// The SERVER thread (trade_probe's entry 0xC0DE0007, asked by lootr_tick while a share waits).
void lootr_chest_shares(uintptr_t host_actor) noexcept;
// trade_probe.cpp: the add worker (the owner's client gets the game's own "acquired" notice). The err, 0 = added.
int lootr_add_item(uintptr_t holder, uint32_t key, uint64_t count) noexcept;
// Phase 2a (host's client, OnRecvPacket, the game's thread): what the host is told about a gimmick the GUEST took
// first is blanked in place (gimmick key 0 / zero save key), so the host still sees it; the originals are kept.
void lootr_host_recv(uint8_t* packet, uint16_t size) noexcept;
// B19 (2026-11-21, [session2] lootr_persist, lootr_persist_logic.h): Lootr's shares kept across sessions. Call
// lootr_set_persist before lootr_start. It arms at the host's load of a co-op world slot (guest_save_host's LoadSlot
// hook; without its hooks nothing is read, written or given). Each host save of slot N takes a snapshot of what each
// player is still owed (a world item the other took; a chest's other share; a world item not taken within 30 minutes
// and a chest share past its 3 hours wait at their place instead of being forgotten), written to
// <co-op folder>\cdmp_lootr\slot<N>.txt when the game commits that save. Loading slot N brings that file's shares back,
// less those on the ledger (given_guest.txt: shares the joined player got and then saved). A share waiting at its
// place goes to its player within 6 m.
void lootr_set_persist(bool on);
// guest_save_host.cpp, on the server thread, each after the game's own call went through (result 0):
void lootr_host_loaded(uint32_t slot) noexcept;   // LoadSlot of a host world slot
void lootr_host_saved(uint32_t slot) noexcept;    // SavePlayerToSlot of a host world slot: the snapshot
void lootr_host_flushed(uint32_t slot) noexcept;  // FlushPendingSaveData committed that slot: its file
void lootr_guest_saved() noexcept;                // user 2's save went through and was exported: the ledger
// v3: trade_probe's 0xC0DE0005 worker (the server thread) ran an in-session give's add for `requester` (ok = added).
void lootr_server_added(uint32_t requester, uint32_t item, uint64_t count, bool ok) noexcept;

}  // namespace cdmp::session2
