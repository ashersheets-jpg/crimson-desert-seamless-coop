#pragma once
// Co-op+ (2026-11-10, user: "Lootr" -- every world item can be taken once by EACH player; it disappears only for the
// one who took it). Phase 0: a probe. The server's pickup worker exe+0x2D37B10(gimmickComp, int* err, picker, kind)
// is logged (who, which gimmick, which item), and the host client's server packets for 3 s after it (opcode + first
// bytes), to learn the exact "item gone" sequence (build/decomp/notes_lootr.md, local).

#include <cstddef>
#include <cstdint>

namespace cdmp::session2 {

void lootr_start(bool probe, bool guest);
void lootr_host_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept;

}  // namespace cdmp::session2
