#pragma once
// Co-op+ 2026-11-24 (docs/research/taming-refusal-3732.md, "Per-player focus slot"): a ServerSequencerStageManager's
// FOCUS slot (+0x90 / +0x98 / +0xA0) is "the focused stage of the manager's owning player" (ByJunter's S46 notes). The
// joined player's own scenes live in the HOST's manager, so without this both players share one slot: a flagged
// branch of one player's scene is refused while the other's stage has the focus (the joined player's taming), and a
// shop buy is checked against the other player's focus.
//
// FocusSlots keeps the joined player's own triple apart and puts the right owner's triple in the manager around each
// piece of server work: enter(owner) before it, leave(token) after it. Nested work keeps a stack. Between pieces of
// work the manager holds the HOST's triple, as in single player. Pure: no game call, no allocation, no lock (the
// server's thread only); the manager's three fields are reached through Mem:
//   bool read(Triple&)          the three fields as they are
//   bool write(const Triple&)   all three
// A failed read or write changes nothing: the work then runs with the triple that is in the manager (as without the
// mod's slot).

#include <cstdint>

namespace cdmp::session2::fsl {

struct Triple {
    uint64_t f90 = 0, f98 = 0, fa0 = 0;
    bool operator==(const Triple&) const = default;
};

enum class Owner : uint8_t { Host = 0, User2 = 1 };

inline constexpr const char* owner_name(Owner o) { return o == Owner::User2 ? "user 2" : "the host"; }

class FocusSlots {
public:
    static constexpr uint32_t kMaxDepth = 16;
    static constexpr uint32_t kRefused = 0xFFFFFFFFu;

    // Before a piece of work done for `o`. The token is for leave().
    template <class Mem>
    uint32_t enter(Mem& m, Owner o) {
        if (depth_ == 0 && current_ != Owner::Host) {
            // A leave was missed (the work never came back to us): give the manager the host's triple again first.
            ++repairs_;
            swap_to(m, Owner::Host);
        }
        if (depth_ >= kMaxDepth) {
            ++overflow_;
            return kRefused;
        }
        const uint32_t token = depth_;
        stack_[depth_++] = current_;
        if (o != current_ && !swap_to(m, o)) ++failed_;
        return token;
    }

    // After that work: back to the owner that was in the manager at the matching enter.
    template <class Mem>
    void leave(Mem& m, uint32_t token) {
        if (token == kRefused) return;
        if (token >= depth_) {
            ++unbalanced_;
            return;
        }
        if (token + 1 != depth_) ++unbalanced_;  // inner leaves were missed: unwind them too
        while (depth_ > token) {
            const Owner prev = stack_[--depth_];
            if (prev != current_ && !swap_to(m, prev)) ++failed_;
        }
    }

    // User 2 logged out (or a new session): its own triple starts empty. While its triple is in the manager, the
    // reset is applied when it is swapped out.
    void reset_user2() {
        if (current_ == Owner::User2) {
            reset_pending_ = true;
        } else {
            saved_[1] = Triple{};
        }
    }

    Owner current() const { return current_; }
    uint32_t depth() const { return depth_; }
    // The triple kept for `o` while the OTHER owner's is in the manager (meaningless for current()).
    const Triple& kept(Owner o) const { return saved_[idx(o)]; }
    uint64_t swaps() const { return swaps_; }
    uint64_t failed() const { return failed_; }
    uint64_t overflow() const { return overflow_; }
    uint64_t unbalanced() const { return unbalanced_; }
    uint64_t repairs() const { return repairs_; }

private:
    static constexpr uint32_t idx(Owner o) { return o == Owner::User2 ? 1u : 0u; }

    template <class Mem>
    bool swap_to(Mem& m, Owner o) {
        if (o == current_) return true;
        Triple live;
        if (!m.read(live)) return false;
        if (!m.write(saved_[idx(o)])) return false;
        saved_[idx(current_)] = live;
        if (current_ == Owner::User2 && reset_pending_) {
            saved_[1] = Triple{};
            reset_pending_ = false;
        }
        current_ = o;
        ++swaps_;
        return true;
    }

    Triple saved_[2] = {};  // [host, user 2]: the one of the owner whose triple is NOT in the manager now
    Owner current_ = Owner::Host;
    Owner stack_[kMaxDepth] = {};
    uint32_t depth_ = 0;
    bool reset_pending_ = false;
    uint64_t swaps_ = 0, failed_ = 0, overflow_ = 0, unbalanced_ = 0, repairs_ = 0;
};

}  // namespace cdmp::session2::fsl
