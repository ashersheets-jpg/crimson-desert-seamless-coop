#pragma once
// Co-op+ (2026-11-10, user: "Lootr" -- every world item can be taken once by EACH player; it disappears only for the
// one who took it). The pure part of lootr.cpp: packet shapes (recorded on 3576, tcB324, PROGRESS.md) and the table.
//
// After a pickup the server sends the picker (and every other player near it): 2214 (u32 gimmick | u32 gimmick),
// 2760 SyncFieldGimmickSavedByCoolTime (u32 | u32 | 16-byte save key | u32), 3110 AddItemToInventory (the picker
// only), 2096 (u32 gimmick | ...), 3067 RemoveActorAck (u8 4 | u32 0 | u8 reason | n x u32 key).
// The guest's interaction: 3008 InteractionPreemptionStepReq (u32 requester | u32 target | u32 interaction | ...,
// 25 B) answered by 2120 (u32 actor | the 3008 payload), then 3073 InteractionDoStepReq (u32 requester | u32 target |
// u32 interaction | u32 step, 16 B) answered by 2794 (u32 actor | the 3073 payload); the pickup follows ~1.8 s later.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace cdmp::session2::lrl {

inline constexpr uint16_t kOpGimmickGone = 3059;      // u32 gimmick | u32 gimmick
inline constexpr uint16_t kOpCoolTime = 2985;         // SyncFieldGimmickSavedByCoolTimeAck
inline constexpr uint16_t kOpGimmickState = 2883;     // u32 gimmick | ...
inline constexpr uint16_t kOpRemoveActor = 3037;      // RemoveActorAck
inline constexpr uint16_t kOpPreemptReq = 2642, kOpPreemptAck = 2308;
inline constexpr uint16_t kOpDoStepReq = 2304, kOpDoStepAck = 2363;
inline constexpr size_t kHeader = 5;

inline uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
inline uint16_t opcode_of(std::span<const uint8_t> pk) { return pk.size() >= 2 ? static_cast<uint16_t>(pk[0] | (pk[1] << 8)) : 0; }
// slot: 0xFF on the server's Acks, 0x00 on a client's Req (tcB328: a Req made with 0xFF never reached the worker).
inline std::vector<uint8_t> make(uint16_t op, std::span<const uint8_t> payload, uint8_t slot = 0xFF) {
    std::vector<uint8_t> p(kHeader + payload.size());
    p[0] = static_cast<uint8_t>(op);
    p[1] = static_cast<uint8_t>(op >> 8);
    p[2] = slot;
    p[3] = static_cast<uint8_t>(payload.size());
    p[4] = static_cast<uint8_t>(payload.size() >> 8);
    if (!payload.empty()) std::memcpy(p.data() + kHeader, payload.data(), payload.size());
    return p;
}

// What a packet for the guest says about gimmick `g` (or save key `key16`): 0 nothing, 1 it is about g (hold all),
// 2 a 3067 listing g among others (strip g).
inline int about(std::span<const uint8_t> pk, uint32_t g, const uint8_t* key16) {
    const uint16_t op = opcode_of(pk);
    if (pk.size() < kHeader) return 0;
    const uint8_t* pl = pk.data() + kHeader;
    const size_t n = pk.size() - kHeader;
    if ((op == kOpGimmickGone || op == kOpGimmickState) && n >= 4) return rd32(pl) == g ? 1 : 0;
    if (op == kOpCoolTime && n >= 24 && key16) return std::memcmp(pl + 8, key16, 16) == 0 ? 1 : 0;
    if (op == kOpRemoveActor && n >= 10 && pl[0] == 4) {
        const size_t keys = (n - 6) / 4;
        bool has = false;
        for (size_t i = 0; i < keys; ++i) has = has || rd32(pl + 6 + 4 * i) == g;
        if (!has) return 0;
        return keys == 1 ? 1 : 2;
    }
    return 0;
}

// The 3067 without key g (the others stay, in order); the u16 size fixed.
inline std::vector<uint8_t> strip_key(std::span<const uint8_t> pk, uint32_t g) {
    std::vector<uint8_t> out(pk.begin(), pk.begin() + kHeader + 6);
    const size_t keys = (pk.size() - kHeader - 6) / 4;
    for (size_t i = 0; i < keys; ++i) {
        const uint8_t* k = pk.data() + kHeader + 6 + 4 * i;
        if (rd32(k) != g) out.insert(out.end(), k, k + 4);
    }
    const size_t len = out.size() - kHeader;
    out[3] = static_cast<uint8_t>(len);
    out[4] = static_cast<uint8_t>(len >> 8);
    return out;
}

// A 3067 with only key g (the replay of a stripped one): the same head (u8 4 | u32 | u8 reason).
inline std::vector<uint8_t> only_key(std::span<const uint8_t> pk, uint32_t g) {
    std::vector<uint8_t> out(pk.begin(), pk.begin() + kHeader + 6);
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(g >> (8 * i)));
    out[3] = 10;
    out[4] = 0;
    return out;
}

// The answer the server gives an interaction request: u32 actor | the request's payload.
inline std::vector<uint8_t> answer(uint16_t ack_op, uint32_t actor, std::span<const uint8_t> req_payload) {
    std::vector<uint8_t> pl(4 + req_payload.size());
    std::memcpy(pl.data(), &actor, 4);
    if (!req_payload.empty()) std::memcpy(pl.data() + 4, req_payload.data(), req_payload.size());
    return make(ack_op, pl);
}

}  // namespace cdmp::session2::lrl
