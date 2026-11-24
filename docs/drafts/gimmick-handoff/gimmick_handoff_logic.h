#pragma once
// DRAFT (2026-11-24; to src/mod/session2/ once cs116 shows which gimmick a cast's fish summon belongs to and what tells
// "the joined player uses K"). Tests: gimmick_handoff_tests.cpp beside it (traced by hand, not compiled yet).
// Co-op+ GimmickHandoff, [session2] gimmick_handoff = 0 | probe | 1 (HOST decides, both games apply): while the joined
// player (user 2) uses a world gimmick (a fishing spot, an ore vein, a tree), its controller [K+0x90] is user 2, so
// user 2's own game runs that gimmick's frame events (the client runner 0xA0CB80 and the request paths 0xA00E40 /
// 0xA01370 run a gimmick's events only when its controller is the local user; every world gimmick's controller is the
// host's user). Private notes: build/decomp/notes_gimmick_handoff.md §4 (the design, the risks R1-R6).
// This header is the decision logic only (no game access): which gimmick to hand over, when to hand it back.
//   hand over K when, all at once: user 2 asked for K in the last kIntentMs; K is a gimmick (type 7), not docked to a
//     character; K's controller is the host's user; user 2 is within kNearM of K; the host has not used K for
//     kHostIdleMs. "Asked" is still to be defined from cs116: NOT any refused event (user 2's game refuses ~500
//     ambient gimmick events a minute: plants, birds), but the use itself (an interaction the server took, or the
//     refused event a cast / a drill makes).
//   hand back when: user 2 has not used K for kUser2IdleMs and is farther than kBackFarM; or user 2 leaves / loads
//     (every handed K at once); or the host asks for K while user 2 has been idle on it for kHostIdleMs.
// Probe mode: the same decisions, logged as "would", nothing written. Tunnel: user 2's "I use K" as control state 19
// (guest -> host; 1-18 are taken, common/troc_frame.h).

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cdmp::session2::ghl {

inline constexpr uint64_t kIntentMs = 2'000;
inline constexpr uint64_t kHostIdleMs = 5'000;
inline constexpr uint64_t kUser2IdleMs = 10'000;
inline constexpr float kNearM = 15.0f;
inline constexpr float kBackFarM = 10.0f;
inline constexpr size_t kMaxTracked = 64;

enum class Mode : uint8_t { Off, Probe, On };

// What the caller reads of K on the server thread right before a decision (SEH-guarded reads).
struct Facts {
    bool exists = false;     // the actor is still there
    uint8_t type = 0;        // [actor+1]; 7 = gimmick
    bool docked = false;     // docked to a player's character (the rod / drill devices: another track)
    uint32_t controller = 0; // [K+0x90]
    float user2_m = 1e9f;    // user 2's character to K, metres
};

enum class Act : uint8_t { Hand, HandBack };
enum class Why : uint8_t { Intent, User2Idle, User2Gone, HostAsks, Removed };

struct Decision {
    uint32_t key = 0;
    Act act = Act::Hand;
    Why why = Why::Intent;
};

class Book {
public:
    // user 2 uses K (see the header's "asked").
    void on_user2_intent(uint32_t key, uint64_t now_ms) {
        Entry* e = entry(key);
        if (e) e->intent_ms = now_ms ? now_ms : 1;
    }
    // A request of the host's client for K reached the server (accepted or refused).
    void on_host_use(uint32_t key, uint64_t now_ms) {
        Entry* e = entry(key);
        if (e) e->host_ms = now_ms ? now_ms : 1;
    }
    // A request of user 2 for K the server accepted (after the hand-over).
    void on_user2_use(uint32_t key, uint64_t now_ms) {
        const auto it = map_.find(key);
        if (it != map_.end()) it->second.user2_ms = now_ms ? now_ms : 1;
    }
    // The hand-over was written (or, in probe, would have been): K counts as user 2's from now on.
    void handed(uint32_t key, uint64_t now_ms) {
        Entry* e = entry(key);
        if (!e) return;
        e->handed = true;
        e->handed_ms = now_ms ? now_ms : 1;
        e->user2_ms = e->handed_ms;
        ++hand_count_;
    }
    void handed_back(uint32_t key) {
        const auto it = map_.find(key);
        if (it == map_.end()) return;
        if (it->second.handed) ++back_count_;
        map_.erase(it);
    }
    // user 2 left, loads or lost the link: every handed K goes back (the caller writes the host's user back).
    std::vector<Decision> on_user2_gone() {
        std::vector<Decision> out;
        for (const auto& [k, e] : map_) {
            if (e.handed) out.push_back({k, Act::HandBack, Why::User2Gone});
        }
        return out;
    }
    // K's RemoveActorAck: on-demand level gimmicks change keys.
    void on_removed(uint32_t key) { map_.erase(key); }

    // Every second on the server thread. `facts(key)` reads K now; `host_user` / `user2_user` are the two users' keys.
    // probe: a "handed" K was never written, so its controller is still the host's (not a reason to forget it).
    template <class FactsFn>
    std::vector<Decision> tick(uint64_t now_ms, uint32_t host_user, uint32_t user2_user, bool probe, FactsFn&& facts) {
        std::vector<Decision> out;
        std::vector<uint32_t> drop;
        for (auto& [k, e] : map_) {
            const Facts f = facts(k);
            if (!f.exists) {
                if (e.handed) out.push_back({k, Act::HandBack, Why::Removed});
                drop.push_back(k);
                continue;
            }
            if (!e.handed) {
                const bool intent = e.intent_ms && now_ms >= e.intent_ms && now_ms - e.intent_ms <= kIntentMs;
                const bool host_idle = !e.host_ms || (now_ms >= e.host_ms && now_ms - e.host_ms >= kHostIdleMs);
                if (intent && f.type == 7 && !f.docked && f.controller == host_user && host_user && user2_user &&
                    f.user2_m <= kNearM && host_idle) {
                    out.push_back({k, Act::Hand, Why::Intent});
                } else if (!intent && (!e.intent_ms || now_ms - e.intent_ms > 60'000)) {
                    drop.push_back(k);  // an old intent that never qualified
                }
                continue;
            }
            const bool u2_idle = now_ms >= e.user2_ms && now_ms - e.user2_ms >= kUser2IdleMs;
            const bool host_asks = e.host_ms && e.host_ms > e.handed_ms && now_ms >= e.host_ms &&
                                   now_ms - e.host_ms <= kIntentMs && now_ms >= e.user2_ms &&
                                   now_ms - e.user2_ms >= kHostIdleMs;
            if (!probe && f.controller != user2_user) {
                drop.push_back(k);  // someone else changed it: not ours any more
            } else if (host_asks) {
                out.push_back({k, Act::HandBack, Why::HostAsks});
            } else if (u2_idle && f.user2_m > kBackFarM) {
                out.push_back({k, Act::HandBack, Why::User2Idle});
            }
        }
        for (const uint32_t k : drop) map_.erase(k);
        return out;
    }

    size_t tracked() const { return map_.size(); }
    size_t handed_now() const {
        size_t n = 0;
        for (const auto& [k, e] : map_) n += e.handed ? 1u : 0u;
        return n;
    }
    uint64_t hand_count() const { return hand_count_; }
    uint64_t back_count() const { return back_count_; }
    uint64_t full() const { return full_; }

private:
    struct Entry {
        uint64_t intent_ms = 0, host_ms = 0, user2_ms = 0, handed_ms = 0;
        bool handed = false;
    };
    Entry* entry(uint32_t key) {
        if (!key) return nullptr;
        auto it = map_.find(key);
        if (it != map_.end()) return &it->second;
        if (map_.size() >= kMaxTracked) {
            ++full_;
            return nullptr;
        }
        return &map_[key];
    }
    std::unordered_map<uint32_t, Entry> map_;
    uint64_t hand_count_ = 0, back_count_ = 0, full_ = 0;
};

inline const char* why_name(Why w) {
    switch (w) {
        case Why::Intent: return "user 2 uses it";
        case Why::User2Idle: return "user 2 left it";
        case Why::User2Gone: return "user 2 left the world";
        case Why::HostAsks: return "the host asks for it";
        case Why::Removed: return "it was removed";
    }
    return "?";
}

}  // namespace cdmp::session2::ghl
