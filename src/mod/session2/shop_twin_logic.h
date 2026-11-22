#pragma once
// Co-op+ (2026-11-09, build 1.0.0.3576): the guest's own shop scene ("twin"), built on the guest's client only. Pure
// part (no game): tests/optd_shop_twin_tests.cpp.
//
// Why: a shopkeeper's shop is a scene (cd_seq_funcnpc_*) the server prepares for the FIELD OWNER (the host). On the
// guest that scene is a copy in the host character's manager X, whose interaction entries carry no data condition for
// the guest (0x22EE2D0 refuses them, PROGRESS 2026-11-09), and whose branch (the shop dialog) belongs to the host:
// asking it would open the host's dialog too. The server cannot prepare a second scene for user 2 on 3576 (its prepare
// starts in protected code, ByJunter's twin was never called). So the guest's CLIENT gets its own copy:
//   - when the host's 2380 PrepareSequencerAck of a shop scene comes in, a second 2380 follows it: the same scene for
//     THIS client's character (actor, owner and the player binding = the guest's key), under a private id (the host's
//     id + kTwinIdBase); its first 2920 (the initial state) likewise. guest_seq_route puts it in the guest's own
//     manager M, which plays it: its interaction entries are the guest's, its NPC finishes its login natively;
//   - every request of the client about a twin id (branch 2880, chart events 2839, gameplay data 2752, trigger volumes
//     2620, stage positions 3117 / 2445...) is WITHHELD from the host's server (whose scenes never knew the id); a
//     branch is answered locally exactly as the server answers one (2436 + 2920), a chart event with its 2187;
//   - the host's 2292 DeleteSequencerAck of the scene is preceded by the twin's own.
// The interaction itself (3008 / 3073) and the trade (BuyStock / Sell) still go to the host's server: they name the
// NPC and the guest's own store component, never the scene id.
//
// Wire packets: u16 opcode | i8 slot | u16 payload length | payload (troc_frame.h).

#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace cdmp::session2::stl {

inline constexpr uint16_t kOpPrepare = 2356, kOpBranchState = 2752, kOpBranchEvent = 2369, kOpDelete = 2659,
                          kOpChartEventAck = 2105;
inline constexpr uint16_t kOpBranchReq = 2644, kOpChartEventReq = 2200, kOpAvoidByCutScene = 2698;
inline constexpr uint64_t kTwinIdBase = 0x10000000;  // a twin's id = the host scene's id + this (below 0x1FFFFFFF)
inline constexpr size_t kHeader = 5;
inline constexpr size_t kPrepareFixed = 92, kBinding = 11, kHashAt = 27;

inline uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline uint64_t rd64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }
inline void wr32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
inline void wr64(uint8_t* p, uint64_t v) { std::memcpy(p, &v, 8); }

inline std::vector<uint8_t> frame(uint16_t op, std::span<const uint8_t> payload) {
    std::vector<uint8_t> p(kHeader + payload.size());
    p[0] = static_cast<uint8_t>(op);
    p[1] = static_cast<uint8_t>(op >> 8);
    p[2] = 0xFF;
    p[3] = static_cast<uint8_t>(payload.size());
    p[4] = static_cast<uint8_t>(payload.size() >> 8);
    if (!payload.empty()) std::memcpy(p.data() + kHeader, payload.data(), payload.size());
    return p;
}
inline bool split(std::span<const uint8_t> pkt, uint16_t& op, std::span<const uint8_t>& payload) {
    if (pkt.size() < kHeader) return false;
    op = static_cast<uint16_t>(pkt[0] | (pkt[1] << 8));
    const size_t len = static_cast<size_t>(pkt[3] | (pkt[4] << 8));
    if (len != pkt.size() - kHeader) return false;
    payload = pkt.subspan(kHeader);
    return true;
}

inline uint64_t twin_of(uint64_t host_id) { return host_id + kTwinIdBase; }
inline bool is_twin_id(uint64_t id) { return id >= kTwinIdBase && id < 2 * kTwinIdBase; }

// The host's 2380 payload of a shop scene -> the twin's 2380 payload (null when it is not one): kind 4 (a character's
// scene), its actor / owner and every binding to the host's character become `guest`, the id the twin's.
inline std::optional<std::vector<uint8_t>> twin_prepare(std::span<const uint8_t> pl, uint32_t guest,
                                                         bool (*is_shop)(uint32_t)) {
    if (pl.size() < kPrepareFixed || (pl.size() - kPrepareFixed) % kBinding != 0 || pl[0] != 4 || !guest) return {};
    if (!is_shop(rd32(pl.data() + kHashAt))) return {};
    const uint32_t host = rd32(pl.data() + 1);
    const uint64_t id = rd64(pl.data() + 9);
    if ((host >> 28) != 0xA || host == guest || is_twin_id(id)) return {};
    std::vector<uint8_t> t(pl.begin(), pl.end());
    wr32(t.data() + 1, guest);
    if (rd32(t.data() + 5) == host) wr32(t.data() + 5, guest);
    wr64(t.data() + 9, twin_of(id));
    for (size_t o = kPrepareFixed; o + kBinding <= t.size(); o += kBinding)
        if (rd32(t.data() + o + 4) == host) wr32(t.data() + o + 4, guest);
    return t;
}

// A 2920 / 2292 payload of the host's scene `id` (kind 4 | actor | id... for 2920; actor | id... for 2292) rewritten
// for the twin (actor = guest, id = the twin's).
inline std::vector<uint8_t> twin_branch_state(std::span<const uint8_t> pl, uint32_t guest) {
    std::vector<uint8_t> t(pl.begin(), pl.end());
    if (t.size() >= 13) {
        wr32(t.data() + 1, guest);
        wr64(t.data() + 5, twin_of(rd64(t.data() + 5)));
    }
    return t;
}
inline std::vector<uint8_t> twin_delete(std::span<const uint8_t> pl, uint32_t guest) {
    std::vector<uint8_t> t(pl.begin(), pl.end());
    if (t.size() >= 12) {
        wr32(t.data(), guest);
        wr64(t.data() + 4, twin_of(rd64(t.data() + 4)));
    }
    return t;
}
// The scene id a 2920 (kind | actor | id) or a 2292 (actor | id) names.
inline uint64_t id_of_2920(std::span<const uint8_t> pl) { return pl.size() >= 13 ? rd64(pl.data() + 5) : 0; }
inline uint64_t id_of_2292(std::span<const uint8_t> pl) { return pl.size() >= 12 ? rd64(pl.data() + 4) : 0; }

// A request of the client: its scene id (every sequencer request starts with it), 0 if too short.
inline uint64_t id_of_request(std::span<const uint8_t> pl) { return pl.size() >= 8 ? rd64(pl.data()) : 0; }

// The scene's root value of its initial 2920 (4 | actor | id | u32 0 | u32 -1 | u32 root | u32 0), 0 if not that form.
inline uint32_t root_of_initial_2920(std::span<const uint8_t> pl) {
    return pl.size() == 29 && rd32(pl.data() + 13) == 0 && rd32(pl.data() + 17) == 0xFFFFFFFFu ? rd32(pl.data() + 21) : 0;
}

// The local answers to a withheld request about a twin: what the host's server answers for its own shop scenes. The
// server keeps a state per scene, the type of its last 2920 (tcB165: the same request, branch 0, is answered "back to
// the menu" (2) after the greeting and "open the store" (3) from the menu; the client loops on the menu if it always
// gets 2). Transitions, from 30 host captures (state = the last 2920 type; the initial 2920 is type 0):
//   state 0, branch N -> 2708 (on, N != 0), 2436 N, 2920 type 1 N                       (the greeting starts)
//   state 1, branch N -> 2436 N, 2708 (off), 2920 type 2 N root 0                       (the greeting ends: the menu)
//   state 2, branch 0 -> 2436 0, 2920 type 3 0                                          (Buy: the store opens)
//   state 2, branch 1 -> 2436 1, 2920 type 4 1                                          (Sell, host alone tcB169)
//   state 2, branch 3 -> 2436 3, 2920 type 7 3                                          (Leave)
//   state 2, branch N -> type N + 3 (not seen yet: the menu's choices look like the chart's nodes 3, 4, ..)
//   state 3..6, branch N -> 2436 N, 2920 type 2 N root 0                                (the store closes: the menu)
//   state 7, branch N -> 2436 N, 2920 type 0 N root 0                                   (the scene is over)
// Message forms: 2880 StageBranchStateReq id u64 | branch u32 | u16; 2436 (4 | actor | id | N | u8 0);
// 2708 SetAvoidByCutSceneAck (4 | actor | id | u8 on | actor); 2920 (4 | actor | id | u32 type | u32 N [| root | 0]).
//   2839 UpdateStageChartEventReq  id u64 | 9 bytes          -> 2187 (4 | actor | id | the same 9 bytes)
//   anything else: no answer (withheld only).
inline std::vector<uint8_t> branch_state(uint32_t guest, uint64_t id, uint32_t type, uint32_t n, bool with_root,
                                         uint32_t root) {
    std::vector<uint8_t> st(with_root ? 29 : 21, 0);
    st[0] = 4;
    wr32(st.data() + 1, guest);
    wr64(st.data() + 5, id);
    wr32(st.data() + 13, type);
    wr32(st.data() + 17, n);
    if (with_root) wr32(st.data() + 21, root);
    return st;
}
// The next state of a scene from `state` on a request for branch `n` (the table above).
inline uint32_t next_branch_type(uint32_t state, uint32_t n) {
    switch (state) {
        case 0: return 1;
        case 1: return 2;
        case 2: return n == 3 ? 7 : n + 3;
        case 3: case 4: case 5: case 6: return 2;
        case 7: return 0;
        default: return 2;
    }
}
inline std::vector<std::vector<uint8_t>> local_answers(uint16_t op, std::span<const uint8_t> pl, uint32_t guest,
                                                       uint32_t root, uint32_t& state) {
    std::vector<std::vector<uint8_t>> out;
    const uint64_t id = id_of_request(pl);
    if (op == kOpBranchReq && pl.size() >= 12) {
        const uint32_t branch = rd32(pl.data() + 8);
        uint8_t ev[18] = {4};
        wr32(ev + 1, guest);
        wr64(ev + 5, id);
        wr32(ev + 13, branch);
        auto avoid = [&](bool on) {
            uint8_t av[18] = {4};
            wr32(av + 1, guest);
            wr64(av + 5, id);
            av[13] = on ? 1 : 0;
            wr32(av + 14, guest);
            return frame(kOpAvoidByCutScene, av);
        };
        const uint32_t type = next_branch_type(state, branch);
        if (state == 0 && branch != 0) out.push_back(avoid(true));
        out.push_back(frame(kOpBranchEvent, ev));
        if (state == 1) out.push_back(avoid(false));
        const bool with_root = type == 2 || type == 0;
        out.push_back(frame(kOpBranchState, branch_state(guest, id, type, branch, with_root, root)));
        state = type;
    } else if (op == kOpChartEventReq && pl.size() == 17) {
        uint8_t a[22] = {4};
        wr32(a + 1, guest);
        wr64(a + 5, id);
        std::memcpy(a + 13, pl.data() + 8, 9);
        out.push_back(frame(kOpChartEventAck, a));
    }
    return out;
}
inline std::vector<std::vector<uint8_t>> local_answers(uint16_t op, std::span<const uint8_t> pl, uint32_t guest) {
    uint32_t st = 0;
    return local_answers(op, pl, guest, 0, st);
}

// ---- twins of other scenes (B14, 2026-11-21: the Hernand inn's arm-wrestling table) --------------------------------
// [session2] twin_extra (guest) names resources whose host scenes get a twin like the shops. Their branches are
// answered from a table of the host server's own answers (host captures, docs/research/armwrest-twin.md), not the
// shop's rule. Per (state, branch) the answers, in order:
//   AvoidOn / AvoidOff  2698 SetAvoidByCutSceneAck (4 | actor | id | u8 on | actor)
//   Event               2369 StageOnBranchEventAck (4 | actor | id | branch | u8 = the request's flag)
//   State               2752 StageBranchStateAck (4 | actor | id | next | branch | root | 0, the 29-byte form); held
//                       back, with the answers after it, until the client's StageBranchStateDelayEndReq 2654 (id
//                       u64) when the request's flag (u16 at +12) is set (cs83w3 / cs83w4: Participate's and Play
//                       Again's branches; the server's 2752 came right after the 2654, its AvoidOff ~20 ms later)
// A (state, branch) the table lacks: 2369 and 2752 type 0 (the scene back to its start); the caller logs it.
inline constexpr uint16_t kOpBranchDelayEndReq = 2654;
enum class Ans : uint8_t { AvoidOn, AvoidOff, Event, State };
struct ChartStep {
    uint32_t state, branch, next;
    uint8_t n;
    Ans a[6];
};
inline constexpr uint32_t kResArmwrest = 0x94FEA6AEu;  // cd_minigame_armwrest_base
// The Hernand inn table, from the host's own matches (cs83w3: a win; cs83w4: the menu's Back, a loss, Play Again):
//   0 -b5-> 1 (the menu: Participate b0 / Back b1)        1 -b1-> 2 -b0-> 0 (Back)
//   1 -b0 (held)-> 3 -b0-> 4 -b1-> 5 -b0-> 6 -b0-> 7 (the match)
//   7 -b0-> 8 -b0-> 10 (won)                                7 -b1-> 9 -b0-> 10 (lost)
//   10 (Play Again b6 / Back b7) -b6 (held)-> 3 ...       10 -b7 (held)-> 11 -b0-> 0 (Back)
inline constexpr ChartStep kArmwrestSteps[] = {
    {0, 5, 1, 2, {Ans::Event, Ans::State}},
    {1, 0, 3, 4, {Ans::AvoidOn, Ans::Event, Ans::State, Ans::AvoidOff}},
    {1, 1, 2, 4, {Ans::AvoidOn, Ans::Event, Ans::State, Ans::AvoidOff}},
    {2, 0, 0, 2, {Ans::Event, Ans::State}},
    {3, 0, 4, 4, {Ans::AvoidOn, Ans::Event, Ans::AvoidOff, Ans::State}},
    {4, 1, 5, 4, {Ans::AvoidOn, Ans::Event, Ans::AvoidOff, Ans::State}},
    {5, 0, 6, 4, {Ans::AvoidOn, Ans::Event, Ans::AvoidOff, Ans::State}},
    {6, 0, 7, 2, {Ans::Event, Ans::State}},
    {7, 0, 8, 2, {Ans::Event, Ans::State}},
    {7, 1, 9, 2, {Ans::Event, Ans::State}},
    {8, 0, 10, 2, {Ans::Event, Ans::State}},
    {9, 0, 10, 2, {Ans::Event, Ans::State}},
    {10, 6, 3, 4, {Ans::AvoidOn, Ans::Event, Ans::State, Ans::AvoidOff}},
    {10, 7, 11, 4, {Ans::AvoidOn, Ans::Event, Ans::State, Ans::AvoidOff}},
    {11, 0, 0, 2, {Ans::Event, Ans::State}},
};
inline std::span<const ChartStep> chart_of(uint32_t res) {
    if (res == kResArmwrest) return kArmwrestSteps;
    return {};
}
struct TableAnswer {
    std::vector<std::vector<uint8_t>> now;  // to queue for the game at once
    std::vector<std::vector<uint8_t>> later;  // held for the client's 2654: the 2752 and what follows it
    bool known = true;                      // the (state, branch) was in the table
    uint32_t next = 0;
};
inline TableAnswer table_answers(std::span<const ChartStep> chart, std::span<const uint8_t> pl, uint32_t guest,
                                 uint32_t root, uint32_t& state) {
    TableAnswer out;
    if (pl.size() < 12) return out;
    const uint64_t id = id_of_request(pl);
    const uint32_t branch = rd32(pl.data() + 8);
    const uint16_t flag = pl.size() >= 14 ? static_cast<uint16_t>(pl[12] | (pl[13] << 8)) : 0;
    const ChartStep* step = nullptr;
    for (const ChartStep& s : chart) {
        if (s.state == state && s.branch == branch) {
            step = &s;
            break;
        }
    }
    static constexpr Ans kUnknown[] = {Ans::Event, Ans::State};
    const std::span<const Ans> seq = step ? std::span<const Ans>(step->a, step->n) : std::span<const Ans>(kUnknown);
    out.known = step != nullptr;
    out.next = step ? step->next : 0;
    bool held = false;  // the State and everything after it wait for the client's 2654
    for (const Ans a : seq) {
        if (a == Ans::State && flag) held = true;
        auto& to = held ? out.later : out.now;
        if (a == Ans::AvoidOn || a == Ans::AvoidOff) {
            uint8_t av[18] = {4};
            wr32(av + 1, guest);
            wr64(av + 5, id);
            av[13] = a == Ans::AvoidOn ? 1 : 0;
            wr32(av + 14, guest);
            to.push_back(frame(kOpAvoidByCutScene, av));
        } else if (a == Ans::Event) {
            uint8_t ev[18] = {4};
            wr32(ev + 1, guest);
            wr64(ev + 5, id);
            wr32(ev + 13, branch);
            ev[17] = static_cast<uint8_t>(flag);
            to.push_back(frame(kOpBranchEvent, ev));
        } else {
            to.push_back(frame(kOpBranchState, branch_state(guest, id, out.next, branch, true, root)));
        }
    }
    state = out.next;
    return out;
}

// The minigame requests that name the table's scene (cs83w3): SpawnMiniGameCharacterReq 2281 and
// UnspawnMiniGameCharacterReq 2361 (u8 4 | u64 scene id | u16 minigame | ...), and the server's
// SpawnMiniGameCharacterAck 2810 (u8 4 | u32 actor | u64 scene id | ...). The joined player's game names its twin; the
// host's server knows only the host's scene.
inline constexpr uint16_t kOpSpawnMiniGameCharacterReq = 2281, kOpUnspawnMiniGameCharacterReq = 2361,
                          kOpSpawnMiniGameCharacterAck = 2810;
// C>S payload: a twin's id at +1 becomes the host's; returns the host's id (0: not such a request, nothing changed).
inline uint64_t minigame_req_to_host(uint16_t op, std::span<uint8_t> pl) {
    if ((op != kOpSpawnMiniGameCharacterReq && op != kOpUnspawnMiniGameCharacterReq) || pl.size() < 9 || pl[0] != 4)
        return 0;
    const uint64_t id = rd64(pl.data() + 1);
    if (!is_twin_id(id)) return 0;
    wr64(pl.data() + 1, id - kTwinIdBase);
    return id - kTwinIdBase;
}
// S>C payload: this client's own 2810 (actor = `own`) naming a host scene `has_twin` says has a twin gets the twin's id;
// returns the twin's id (0: unchanged).
template <class HasTwin>
inline uint64_t minigame_ack_to_twin(uint16_t op, std::span<uint8_t> pl, uint32_t own, HasTwin&& has_twin) {
    if (op != kOpSpawnMiniGameCharacterAck || pl.size() < 13 || pl[0] != 4 || !own || rd32(pl.data() + 1) != own)
        return 0;
    const uint64_t id = rd64(pl.data() + 5);
    if (is_twin_id(id) || !has_twin(id)) return 0;
    wr64(pl.data() + 5, twin_of(id));
    return twin_of(id);
}

// twin_extra_chart (2026-11-22, B14, cs92 against cs87): the server runs a scene's chart event on its own side. In the
// host's own match the client's two UpdateStageChartEventReq of the table's state 7 (events 7/0 and 7/1) are answered
// 2 ms later by CatchActionResultAck (the server's grip of player and opponent), the client then starts the push
// (EchoActionSessionIDReq, DoFrameEventReq) and the quick-time results follow. A twin answers its chart events locally,
// so for the joined player the server never ran the catch: its match stood at "E Push". Forms: 2200 C>S id u64 |
// 9 bytes (17 B); 2105 S>C u8 4 | u32 actor | id u64 | the same 9 bytes (22 B).
// C>S payload: a twin's chart event names the host's scene; returns the host's id (0: not such a request, unchanged).
inline uint64_t chart_req_to_host(uint16_t op, std::span<uint8_t> pl) {
    if (op != kOpChartEventReq || pl.size() != 17) return 0;
    const uint64_t id = rd64(pl.data());
    if (!is_twin_id(id)) return 0;
    wr64(pl.data(), id - kTwinIdBase);
    return id - kTwinIdBase;
}
// twin_extra_chart=3 (hypothesis (f)): no forwarding; the twin's own answer to the match's first chart event (7/0) also
// carries the server's grip, made here: CatchActionResultAck 2562 (3732), 55 B, copied from cs92's host capture
// (t 363.589) with the catcher (+0) and the target (+4) replaced: u32 catcher | u32 target | u32 2 | ... | f32 0.1 |
// u32 the target's action 0x18F128E3 | u32 0x64528294 | ... | u32 the catcher's action 0x63F9EFFD (+50) | u8 0x17.
inline constexpr uint16_t kOpCatchActionResultAck = 2562;
inline constexpr uint8_t kCatchTemplate[55] = {
    0x01, 0x00, 0x10, 0xA0, 0xF7, 0x0A, 0x10, 0xB0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCD, 0xCC, 0xCC, 0x3D, 0xE3, 0x28, 0xF1, 0x18, 0x94, 0x82, 0x52,
    0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xFD, 0xEF, 0xF9, 0x63, 0x17};
inline std::vector<uint8_t> catch_result(uint32_t catcher, uint32_t target) {
    std::vector<uint8_t> pl(std::begin(kCatchTemplate), std::end(kCatchTemplate));
    wr32(pl.data(), catcher);
    wr32(pl.data() + 4, target);
    return frame(kOpCatchActionResultAck, pl);
}
// The opponent the server spawned for this client's minigame: SpawnMiniGameCharacterAck 2810 payload u8 4 | u32 actor |
// u64 scene | u32 node | u32 opponent (+17) | ...; 0 when too short.
inline uint32_t minigame_opponent(std::span<const uint8_t> pl) { return pl.size() >= 21 ? rd32(pl.data() + 17) : 0; }

// A chart event sent on to the host's server for a twin: the host's scene id and its 9 bytes.
struct ForwardedChart {
    uint64_t host = 0;
    uint8_t ev[9] = {};
    uint64_t at_ms = 0;
};
// S>C payload: a 2105 naming the host's scene and the 9 bytes of a forwarded chart event (sent within `max_age_ms`)
// gets the twin's id; the entry is used up. Returns the twin's id (0: unchanged).
inline uint64_t chart_ack_to_twin(uint16_t op, std::span<uint8_t> pl, std::vector<ForwardedChart>& pending,
                                  uint64_t now_ms, uint64_t max_age_ms) {
    if (op != kOpChartEventAck || pl.size() != 22 || pl[0] != 4) return 0;
    const uint64_t id = rd64(pl.data() + 5);
    if (is_twin_id(id)) return 0;
    for (auto it = pending.begin(); it != pending.end(); ++it) {
        if (it->host != id || now_ms - it->at_ms > max_age_ms || std::memcmp(it->ev, pl.data() + 13, 9) != 0) continue;
        pending.erase(it);
        wr64(pl.data() + 5, twin_of(id));
        return twin_of(id);
    }
    return 0;
}

// ---- the shop dialog entries (2026-11-09) --------------------------------------------------------------------------
// SetQuestDialogAck 3086 (S->C): u8 4 | u32 actor | u32 NPC | n x {u32 group, u32 index}. A shopkeeper's menu choices
// (Buy / Sell) are the entries of group kShopDialogGroup (Renee 0x0A / 0x0B, tcB116); the NPC's scene functions put
// them in the per-player table of the player whose client controls the NPC (the host) only, so the guest's 3086 for
// that NPC lacks them and its shop menu stays empty (the dialog closes itself, tcB161). The host's entries are sent
// to the guest (tunnel control state 17) and added to the guest's own 3086 for that NPC.
inline constexpr uint16_t kOpQuestDialogAck = 2180;
inline constexpr uint32_t kShopDialogGroup = 0x000F4440;
inline constexpr uint8_t kDialogState = 17, kDialogVersion = 1;
inline constexpr size_t kMaxDialogNpcs = 256, kMaxDialogsPerNpc = 8;
struct Dialog {
    uint32_t group = 0, index = 0;
    bool operator==(const Dialog&) const = default;
};
// The shared entries of a 3086 payload (and its NPC); false when it is not one. Shop group only, or with `all` every
// group but the greeting (1): the NPCs' quest-dialog stages the server runs for the field owner only (B02a, tcB186:
// 0xF435B / 0xF42B6 / 0xF4250 / ... on the host, none on the guest), [session2] dialog_share_all.
inline bool dialogs_of(std::span<const uint8_t> pl, uint32_t& npc, std::vector<Dialog>& out, bool all) {
    out.clear();
    if (pl.size() < 9 || pl[0] != 4 || (pl.size() - 9) % 8 != 0) return false;
    npc = rd32(pl.data() + 5);
    for (size_t o = 9; o + 8 <= pl.size(); o += 8) {
        const Dialog d{rd32(pl.data() + o), rd32(pl.data() + o + 4)};
        if ((d.group == kShopDialogGroup || (all && d.group != 1)) && out.size() < kMaxDialogsPerNpc) out.push_back(d);
    }
    return true;
}
inline bool shop_dialogs_of(std::span<const uint8_t> pl, uint32_t& npc, std::vector<Dialog>& out) {
    out.clear();
    if (pl.size() < 9 || pl[0] != 4 || (pl.size() - 9) % 8 != 0) return false;
    npc = rd32(pl.data() + 5);
    for (size_t o = 9; o + 8 <= pl.size(); o += 8) {
        const Dialog d{rd32(pl.data() + o), rd32(pl.data() + o + 4)};
        if (d.group == kShopDialogGroup && out.size() < kMaxDialogsPerNpc) out.push_back(d);
    }
    return true;
}
// The guest's 3086 payload with the host's shop entries for its NPC added in front (the order the host's has them),
// those already present skipped. Null: nothing to add.
inline std::optional<std::vector<uint8_t>> with_shop_dialogs(std::span<const uint8_t> pl, const std::vector<Dialog>& add) {
    if (pl.size() < 9 || pl[0] != 4 || (pl.size() - 9) % 8 != 0 || add.empty()) return {};
    std::vector<Dialog> missing;
    for (const Dialog& d : add) {
        bool have = false;
        for (size_t o = 9; o + 8 <= pl.size(); o += 8)
            if (rd32(pl.data() + o) == d.group && rd32(pl.data() + o + 4) == d.index) have = true;
        if (!have) missing.push_back(d);
    }
    if (missing.empty()) return {};
    std::vector<uint8_t> t(pl.begin(), pl.begin() + 9);
    for (const Dialog& d : missing) {
        uint8_t e[8];
        wr32(e, d.group);
        wr32(e + 4, d.index);
        t.insert(t.end(), e, e + 8);
    }
    t.insert(t.end(), pl.begin() + 9, pl.end());
    return t;
}
// State 17: u8 17 | u8 1 | u16 n | n x {u32 NPC | u8 m | m x {u32 group, u32 index}}
struct NpcDialogs {
    uint32_t npc = 0;
    std::vector<Dialog> dialogs;
};
inline std::vector<uint8_t> make_dialog_payload(std::span<const NpcDialogs> v) {
    std::vector<uint8_t> p{kDialogState, kDialogVersion, 0, 0};
    size_t n = 0;
    for (const NpcDialogs& e : v) {
        if (n >= kMaxDialogNpcs) break;
        uint8_t h[5];
        wr32(h, e.npc);
        h[4] = static_cast<uint8_t>(e.dialogs.size() < kMaxDialogsPerNpc ? e.dialogs.size() : kMaxDialogsPerNpc);
        p.insert(p.end(), h, h + 5);
        for (size_t i = 0; i < h[4]; ++i) {
            uint8_t d[8];
            wr32(d, e.dialogs[i].group);
            wr32(d + 4, e.dialogs[i].index);
            p.insert(p.end(), d, d + 8);
        }
        ++n;
    }
    p[2] = static_cast<uint8_t>(n);
    p[3] = static_cast<uint8_t>(n >> 8);
    return p;
}
inline std::optional<std::vector<NpcDialogs>> parse_dialog_payload(std::span<const uint8_t> p) {
    if (p.size() < 4 || p[0] != kDialogState || p[1] != kDialogVersion) return {};
    const size_t n = static_cast<size_t>(p[2] | (p[3] << 8));
    if (n > kMaxDialogNpcs) return {};
    std::vector<NpcDialogs> out;
    size_t o = 4;
    for (size_t i = 0; i < n; ++i) {
        if (o + 5 > p.size()) return {};
        NpcDialogs e;
        e.npc = rd32(p.data() + o);
        const size_t m = p[o + 4];
        o += 5;
        if (m > kMaxDialogsPerNpc || o + 8 * m > p.size()) return {};
        for (size_t k = 0; k < m; ++k, o += 8) e.dialogs.push_back({rd32(p.data() + o), rd32(p.data() + o + 4)});
        out.push_back(std::move(e));
    }
    if (o != p.size()) return {};
    return out;
}

}  // namespace cdmp::session2::stl
