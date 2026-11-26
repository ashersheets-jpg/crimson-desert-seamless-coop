#pragma once
// P8 session 13, the shopkeepers' server side (docs/research/pendientes/p8-impl-tenderos-c1.md), HOST: pure logic of
// [session2] shop_probe = 0 | 1 (read-only). No game call, no allocation in what the hooks use.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace cdmp::session2::shp {

// ---------------------------------------------------------------- per character counters

// What the server's stage update 0x288B7A0 did with one character's manager: entered (E0, the function's entry),
// reached the owner gate (0x288B87A; the earlier exits are +0x6A0 < 2, +0x68A, the field manager's +0x591) and how
// the gate went. Fixed table of N character keys, claimed with a CAS; the overflow is counted under key 0.
struct KeyRow {
    uint32_t key = 0;
    uint64_t entries = 0, gate = 0, owner = 0, not_owner = 0;
    uint32_t user_key = 0, field_owner = 0;  // the last seen at the gate
};
template <size_t N>
class KeyTable {
public:
    enum Col : uint8_t { Entries, Gate, Owner, NotOwner, Cols };
    void add(uint32_t key, Col c, uint32_t user_key = 0, uint32_t field_owner = 0) {
        Slot* s = slot(key);
        if (!s) {
            overflow_[c].fetch_add(1, std::memory_order_relaxed);
            return;
        }
        s->n[c].fetch_add(1, std::memory_order_relaxed);
        if (c != Entries) {
            s->user_key.store(user_key, std::memory_order_relaxed);
            s->field_owner.store(field_owner, std::memory_order_relaxed);
        }
    }
    size_t rows(KeyRow* out, size_t cap) const {
        size_t n = 0;
        for (size_t i = 0; i < N && n < cap; ++i) {
            const uint32_t k = s_[i].key.load(std::memory_order_acquire);
            if (!k) continue;
            KeyRow& r = out[n++];
            r.key = k;
            r.entries = s_[i].n[Entries].load(std::memory_order_relaxed);
            r.gate = s_[i].n[Gate].load(std::memory_order_relaxed);
            r.owner = s_[i].n[Owner].load(std::memory_order_relaxed);
            r.not_owner = s_[i].n[NotOwner].load(std::memory_order_relaxed);
            r.user_key = s_[i].user_key.load(std::memory_order_relaxed);
            r.field_owner = s_[i].field_owner.load(std::memory_order_relaxed);
        }
        return n;
    }
    uint64_t overflow(Col c) const { return overflow_[c].load(std::memory_order_relaxed); }
    // The count of one column for one key (0 when unknown).
    uint64_t of(uint32_t key, Col c) const {
        for (size_t i = 0; i < N; ++i) {
            if (s_[i].key.load(std::memory_order_acquire) == key) return s_[i].n[c].load(std::memory_order_relaxed);
        }
        return 0;
    }
    static constexpr size_t capacity() { return N; }

private:
    struct Slot {
        std::atomic<uint32_t> key{0};
        std::atomic<uint64_t> n[Cols] = {};
        std::atomic<uint32_t> user_key{0}, field_owner{0};
    };
    Slot* slot(uint32_t key) {
        if (!key) return nullptr;
        for (size_t i = 0; i < N; ++i) {
            uint32_t cur = s_[i].key.load(std::memory_order_acquire);
            if (cur == 0) {
                uint32_t expected = 0;
                cur = s_[i].key.compare_exchange_strong(expected, key, std::memory_order_acq_rel) ? key : expected;
            }
            if (cur == key) return &s_[i];
        }
        return nullptr;
    }
    Slot s_[N];
    std::atomic<uint64_t> overflow_[Cols] = {};
};

// ---------------------------------------------------------------- who asks

enum class Who : uint8_t { Host, User2, Unknown };
// By the requester's user key against user 2's (server_user2_user_key), else by its character key against user 2's
// character (server_user2_player_key). Unknown while neither of user 2's keys is known.
inline Who who(uint32_t user_key, uint32_t char_key, uint32_t user2_user, uint32_t user2_char) {
    if (user2_user && user_key) return user_key == user2_user ? Who::User2 : Who::Host;
    if (user2_char && char_key) return char_key == user2_char ? Who::User2 : Who::Host;
    return Who::Unknown;
}
inline const char* who_name(Who w) {
    switch (w) {
        case Who::Host: return "host";
        case Who::User2: return "USER 2";
        case Who::Unknown: return "unknown";
    }
    return "?";
}

// ---------------------------------------------------------------- the stage element's resource name

// The base name after the last '/' or '\' (as 0x288AA30 skips the directories before hashing it).
inline std::string_view base_name(std::string_view path) {
    const size_t cut = path.find_last_of("/\\");
    return cut == std::string_view::npos ? path : path.substr(cut + 1);
}
// A shop scene: the resource's base name starts with cd_seq_funcnpc_ (p8-s12-tenderos.md §2.1).
inline bool is_funcnpc(std::string_view path) { return base_name(path).starts_with("cd_seq_funcnpc_"); }

// ---------------------------------------------------------------- the funcnpc ids seen (host's 2661 path)

// The sequencer ids 0x288AAEF saw prepared for a cd_seq_funcnpc_* element (the id 0x287C200 wrote to playData+8).
// A small ring: newest replaces oldest. Lock-free, any thread.
template <size_t N>
class IdRing {
public:
    void add(uint64_t id) {
        if (!id || has(id)) return;
        const size_t i = next_.fetch_add(1, std::memory_order_relaxed) % N;
        ids_[i].store(id, std::memory_order_release);
    }
    bool has(uint64_t id) const {
        if (!id) return false;
        for (size_t i = 0; i < N; ++i) {
            if (ids_[i].load(std::memory_order_acquire) == id) return true;
        }
        return false;
    }
    size_t size() const {
        size_t n = 0;
        for (size_t i = 0; i < N; ++i) n += ids_[i].load(std::memory_order_relaxed) != 0;
        return n;
    }

private:
    std::atomic<uint64_t> ids_[N] = {};
    std::atomic<size_t> next_{0};
};

// ---------------------------------------------------------------- S2, the funcnpc twin (probe only)

// [session2] funcnpc_twin = 0 | probe | 1 (p8-impl-tenderos-s2.md). In this build '1' is NOT applied: it acts as
// probe and says so (the call 0x287C200 for user 2's manager needs T2 and the Req routing settled first).
enum class TwinMode : uint8_t { Off, Probe };
struct TwinKey {
    TwinMode mode = TwinMode::Off;
    bool asked_on = false;  // '1' was asked (held as probe)
    bool bad = false;
};
inline TwinKey parse_twin(std::string_view s) {
    TwinKey k;
    if (s.empty() || s == "0") return k;
    if (s == "probe") {
        k.mode = TwinMode::Probe;
    } else if (s == "1") {
        k.mode = TwinMode::Probe;
        k.asked_on = true;
    } else {
        k.bad = true;
    }
    return k;
}

// What 0x288AAEF sees when the host's server prepared a stage element, and what is known of user 2 (read by the
// module's thread every second: its character and that character's ServerSequencerStageManager).
struct TwinFacts {
    bool funcnpc = false;         // the element's resource is a cd_seq_funcnpc_*
    bool prepared = false;        // 0x287C200's result 0
    bool host_scene = false;      // owner = the manager's character, and that character's user owns the field
    bool user2_known = false;     // user 2's character key and its object are known
    bool user2_mgr_ok = false;    // its manager has the ServerSequencerStageManager vtable
    bool already = false;         // this host id already had its twin decided (one per host scene)
};
enum class TwinVerdict : uint8_t { NotFuncnpc, Failed, NotHostScene, NoUser2, NoUser2Manager, Already, Would, Count };
inline TwinVerdict decide_twin(const TwinFacts& f) {
    if (!f.funcnpc) return TwinVerdict::NotFuncnpc;
    if (!f.prepared) return TwinVerdict::Failed;
    if (!f.host_scene) return TwinVerdict::NotHostScene;
    if (!f.user2_known) return TwinVerdict::NoUser2;
    if (!f.user2_mgr_ok) return TwinVerdict::NoUser2Manager;
    if (f.already) return TwinVerdict::Already;
    return TwinVerdict::Would;
}
inline const char* twin_verdict_name(TwinVerdict v) {
    switch (v) {
        case TwinVerdict::NotFuncnpc: return "not a shop scene";
        case TwinVerdict::Failed: return "the host's preparation failed";
        case TwinVerdict::NotHostScene: return "not a scene of the field owner's character";
        case TwinVerdict::NoUser2: return "user 2 not known";
        case TwinVerdict::NoUser2Manager: return "user 2's manager not readable";
        case TwinVerdict::Already: return "twin already decided for this id";
        case TwinVerdict::Would: return "WOULD CREATE a twin";
        case TwinVerdict::Count: break;
    }
    return "?";
}

// ---------------------------------------------------------------- the packets

inline uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}
inline uint64_t le64(const uint8_t* p) { return static_cast<uint64_t>(le32(p)) | static_cast<uint64_t>(le32(p + 4)) << 32; }

inline constexpr uint16_t kOpPreemptionReq = 2641;   // InteractionPreemptionStepReq: u32 requester | u32 NPC | u32 interaction | ...
inline constexpr uint16_t kOpDoStepReq = 2526;       // InteractionDoStepReq: u32 requester | u32 NPC | u32 interaction | u32
inline constexpr uint16_t kOpBranchReq = 2726;       // StageBranchStateReq: u64 sequencer | ... (the branch after it)
inline constexpr uint16_t kOpChartEvent = 2811;      // StageChart event Req: u64 sequencer @0
inline constexpr uint16_t kOpPreemptionOk = 3168;    // ...SuccessAck: u32 requester | u32 requester | u32 NPC | u32 interaction
inline constexpr uint16_t kOpPreemptionFail = 2929;  // ...FailAck
inline constexpr uint16_t kOpDoStepOk = 2612;        // InteractionDoStepSuccessAck
inline constexpr uint16_t kOpDoInteraction = 3140;   // the interaction ran
inline constexpr uint16_t kOpBranchAck = 2455;       // StageBranchStateAck: u8 kind | u32 actor | u64 seq | u32 state
// Co-op+ 2026-11-24: PrepareSequencerAck on 3732 (hold_scene_order_logic.h kOpPrepare): u8 kind | u32 actor @1 | u32
// owner @5 | u64 sequencer @9 | ...; to user 2 with owner = user 2's character: a scene of user 2's OWN. pp57-armwrest:
// a server stage's +0x98 and [+0x48]+0x60 read 0 (no owner there), so the per-player focus slot knows user 2's scenes
// by these ids (= the stage's +0x30).
inline constexpr uint16_t kOpPrepareAck = 2362;
struct PrepareOwner {
    bool ok = false;
    uint32_t owner = 0;
    uint64_t id = 0;
};
inline PrepareOwner parse_prepare_owner(std::span<const uint8_t> p) {
    PrepareOwner r;
    if (p.size() < 17) return r;
    r.ok = true;
    r.owner = le32(p.data() + 5);
    r.id = le64(p.data() + 9);
    return r;
}
inline constexpr uint16_t kOpOnBranchEvent = 2239;   // StageOnBranchEventAck

struct Interaction {
    bool ok = false;
    uint32_t requester = 0, npc = 0, interaction = 0;
};
// 2671 / 2720 (the 2671 handler reads u32 [rbp+0x70], [rbp+0x78], [rbp-0x58] in this order: 0x29E9053..0x29E90D2).
inline Interaction parse_interaction(std::span<const uint8_t> p) {
    Interaction r;
    if (p.size() < 12) return r;
    r.ok = true;
    r.requester = le32(p.data());
    r.npc = le32(p.data() + 4);
    r.interaction = le32(p.data() + 8);
    return r;
}
struct SeqReq {
    bool ok = false;
    uint64_t seq = 0;
    uint32_t after = 0;  // the next 4 bytes (2436: the branch; raw)
};
inline SeqReq parse_seq_req(std::span<const uint8_t> p) {
    SeqReq r;
    if (p.size() < 8) return r;
    r.ok = true;
    r.seq = le64(p.data());
    if (p.size() >= 12) r.after = le32(p.data() + 8);
    return r;
}
struct BranchAck {
    bool ok = false;
    uint32_t actor = 0;
    uint64_t seq = 0;
    uint32_t state = 0;
};
// 2981 and 2524 alike (guest_seq_route reads both so): u8 kind | u32 actor @1 | u64 sequencer @5 [| u32 @13].
inline BranchAck parse_branch_ack(std::span<const uint8_t> p) {
    BranchAck r;
    if (p.size() < 13) return r;
    r.ok = true;
    r.actor = le32(p.data() + 1);
    r.seq = le64(p.data() + 5);
    if (p.size() >= 17) r.state = le32(p.data() + 13);
    return r;
}
// The S>C acks of an interaction. Success 2663 / 2916 / 2525: u32 requester | u32 requester | u32 NPC | u32
// interaction (the session 11 capture "2663 A0100001 A0100001 B0100153 000F4321"). The FailAck 3000 is DIFFERENT:
// u32 requester | u32 NPC | u32 interaction | u32 native error @12 (handler 0xBB4680 reads 5 x u32 then 3 x u8, 23 B;
// p2b host capture: A0100001 B01002EC 000F4321 58C8345D FFFFFFFF 00 01 00). S46: ported by hand from the other AI's
// branch opcion-d-tenderos (S41 P2); the old parse read the interaction as the NPC and the error as the interaction.
// Only this identity prefix is read; no other field is validated. Another opcode: not ok.
inline Interaction parse_interaction_ack(std::span<const uint8_t> p, uint16_t opcode = kOpPreemptionOk) {
    Interaction r;
    if (p.size() < 16 || (opcode != kOpPreemptionOk && opcode != kOpPreemptionFail && opcode != kOpDoStepOk &&
                          opcode != kOpDoInteraction)) {
        return r;
    }
    r.ok = true;
    r.requester = le32(p.data());
    const bool failed = opcode == kOpPreemptionFail;
    r.npc = le32(p.data() + (failed ? 4 : 8));
    r.interaction = le32(p.data() + (failed ? 8 : 12));
    return r;
}
// The 3000's native error @12 (0 when it is not a well-formed 3000).
inline uint32_t fail_ack_error(std::span<const uint8_t> p) { return p.size() >= 16 ? le32(p.data() + 12) : 0; }

// Co-op+ 2026-11-24 (the joined player's refused taming; docs/research/taming-refusal-3732.md): the branch step's
// condition call 0x25C82A0(node, manager = [scene+0x28], context, scene, ...) first finds the target node's entry in
// the scene's processor list; when that entry's element flag [[entry+0x88]+0x75] is set (an exclusive node) it asks
// 0x21CABF0(manager, scene) and REFUSES when that says yes. 0x21CABF0's first checks: the manager's current stage
// (0x21CAF70: [mgr+0xA0] when its low 29 bits are set, else [mgr+0x90]; stp::current_of) looked up in the manager;
// none or not live -> no; the same stage as the scene ([+0x30] equal) -> no; another live stage -> yes, unless its
// later checks clear it (the two stages' +0x78 group through a table; a type-6 scene's list). The log line's verdict.
enum class Excl : uint8_t { NoCurrent, CurrentGone, ThisScene, Another };
inline Excl exclusive_verdict(uint64_t current, bool current_live, uint64_t scene_id) {
    constexpr uint64_t kMask = 0x1FFFFFFF;
    if (!(current & kMask)) return Excl::NoCurrent;
    if (current == scene_id) return Excl::ThisScene;
    if (!current_live) return Excl::CurrentGone;
    return Excl::Another;
}
inline const char* excl_name(Excl e) {
    switch (e) {
        case Excl::NoCurrent: return "the manager has no current stage: not blocked by one";
        case Excl::CurrentGone: return "its current stage id is no live stage of the manager: not blocked by one";
        case Excl::ThisScene: return "its current stage IS this scene: not blocked";
        case Excl::Another:
            return "ANOTHER live stage is the manager's current one: an exclusive node is refused for it (unless "
                   "0x2286490's group / type-6 checks clear it)";
    }
    return "?";
}

}  // namespace cdmp::session2::shp
