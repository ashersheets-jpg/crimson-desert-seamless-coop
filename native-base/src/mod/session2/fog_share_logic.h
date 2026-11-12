#pragma once
// Co-op+ (2026-11-09, build 1.0.0.3576), ROADMAP B04 "shared map": the pure logic of fog_share.cpp (no game access).
// FogDataAck 2196 (S->C, at each player's login): 47 chunks per player, payload u8 seq (1 = the first chunk, 2 = the
// rest) | u32 the player's user key | the fog bitmap's next bytes (32700 a chunk, the last 16568; 1,520,768 bytes in
// all, the same for every save: tcB186's host and guest). Revealed cells are set bits (runs of 0x01 / 0x80 / 0xC0 /
// 0x03 ...), so the union of two players' maps is the bytewise OR of their chunks, chunk by chunk.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cdmp::session2::fsl {

inline constexpr uint16_t kOpFogDataAck = 2196;
inline constexpr size_t kFogHead = 5;  // u8 seq | u32 user
inline constexpr size_t kMaxChunks = 64;

// The host's chunks in order: a seq-1 chunk starts a new map.
struct Chunks {
    std::vector<std::vector<uint8_t>> data;
};
inline bool record(Chunks& c, std::span<const uint8_t> payload) {
    if (payload.size() <= kFogHead) return false;
    if (payload[0] == 1) c.data.clear();
    if (c.data.size() >= kMaxChunks) return false;
    c.data.emplace_back(payload.begin() + kFogHead, payload.end());
    return true;
}
// The guest's chunk `index` OR the host's chunk of the same index (same size only). How many bytes changed.
inline size_t merge(std::span<uint8_t> payload, const Chunks& host, size_t index) {
    if (payload.size() <= kFogHead || index >= host.data.size()) return 0;
    const std::vector<uint8_t>& h = host.data[index];
    if (payload.size() - kFogHead != h.size()) return 0;
    size_t n = 0;
    for (size_t i = 0; i < h.size(); ++i) {
        const uint8_t b = static_cast<uint8_t>(payload[kFogHead + i] | h[i]);
        if (b != payload[kFogHead + i]) {
            payload[kFogHead + i] = b;
            ++n;
        }
    }
    return n;
}

}  // namespace cdmp::session2::fsl
