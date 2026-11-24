#pragma once
// Co-op+ (T19, 2026-11-24): where a hop to the friend lands (Go to my friend, the answer to a summon, the join beside
// the host, respawn_at=partner). It was the friend's position + 1 m on x: beside a cliff's edge that can be open air,
// beside a rock face it can be inside the rock. Now a spot of the friend's own path: the newest point it passed
// through 1 to 4 m from where it stands now (ground or water it really was in), not more than 2 m above or below it;
// with none (it has not moved since it arrived), 1 m beside it on x as before (pp52-lowsave: landing ON the friend
// put the two characters inside each other). Pure logic, unit-tested (tests.cpp).
#include <array>
#include <cstddef>
#include <cstdint>

namespace cdmp::net {

struct LandingSpot {
    float x = 0, y = 0, z = 0;
    bool from_path = false;  // a point of the friend's path; false: 1 m beside the friend on x
};

class LandingTrail {
public:
    static constexpr size_t kSize = 48;
    static constexpr float kStepM = 0.5f;               // a point is kept once the friend moved this far from the last
    static constexpr float kMinM = 1.0f, kMaxM = 4.0f;  // horizontal distance from the friend's spot now
    static constexpr float kMaxDyM = 2.0f;              // not a ledge above or below it
    static constexpr uint32_t kMaxAgeMs = 600000;       // ten minutes: ground stays ground, this only bounds staleness
    static constexpr float kLiftM = 0.5f;               // as before: half a metre up, the game puts us down
    static constexpr float kSideM = 1.0f;               // no path point yet: beside the friend on x, as before

    void clear() { n_ = head_ = 0; }
    void note(float x, float y, float z, uint32_t now_ms) {
        if (n_) {
            const P& last = ring_[(head_ + kSize - 1) % kSize];
            const float dx = x - last.x, dy = y - last.y, dz = z - last.z;
            if (dx * dx + dy * dy + dz * dz < kStepM * kStepM) return;
        }
        ring_[head_] = P{x, y, z, now_ms};
        head_ = (head_ + 1) % kSize;
        if (n_ < kSize) ++n_;
    }
    LandingSpot spot(float x, float y, float z, uint32_t now_ms) const {
        for (size_t i = 0; i < n_; ++i) {
            const P& p = ring_[(head_ + kSize - 1 - i) % kSize];
            if (now_ms - p.t > kMaxAgeMs) break;  // the older ones are older still
            const float dx = p.x - x, dy = p.y - y, dz = p.z - z;
            const float d2 = dx * dx + dz * dz;
            if (d2 < kMinM * kMinM || d2 > kMaxM * kMaxM || dy > kMaxDyM || dy < -kMaxDyM) continue;
            return LandingSpot{p.x, p.y + kLiftM, p.z, true};
        }
        return LandingSpot{x + kSideM, y + kLiftM, z, false};
    }
    size_t size() const { return n_; }

private:
    struct P {
        float x = 0, y = 0, z = 0;
        uint32_t t = 0;
    };
    std::array<P, kSize> ring_{};
    size_t n_ = 0, head_ = 0;
};

}  // namespace cdmp::net
