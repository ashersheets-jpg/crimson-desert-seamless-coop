#pragma once
// Far shops (2026-11-09, tcB239): an NPC's dynamic interactions (shop, talk...) are added by a StageChart node of
// every scene that binds the NPC, but removed BY KEY: when one player's scene closes, the entries of the other
// player's live scene go with it (the guest then gets eErrNoInvalidInteractionInfo 0x15DBC102 at the shop).
// This ledger counts, per (interaction component, interaction index), which scenes of which stage manager hold the
// entry; a remove is skipped while a scene of ANOTHER manager still holds it. Pure logic (unit-testable).
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace cdmp::session2::ishare {

struct Holder {
    uint64_t scene = 0;     // the scene's id ([scene+0x30])
    uintptr_t manager = 0;  // its stage manager ([scene+0x28])
};

class Ledger {
public:
    // A node adds (ic, idx) for `scene`. `ic_empty`: the component had no dynamic entry before this add (whatever
    // the ledger held for this component is stale: an earlier actor at the same address).
    void on_add(uintptr_t ic, uint16_t idx, uint64_t scene, uintptr_t manager, bool ic_empty) {
        if (ic_empty) drop_ic(ic);
        auto& v = held_[{ic, idx}];
        for (Holder& h : v) {
            if (h.scene == scene) {
                h.manager = manager;
                return;
            }
        }
        v.push_back({scene, manager});
        if (held_.size() > kMax) held_.erase(held_.begin());  // bounded
    }
    // A node removes (ic, idx) for `scene`. True = skip the game's remove: a scene of another manager still holds
    // it; `keep` is that scene (the entry is handed to it).
    bool on_remove(uintptr_t ic, uint16_t idx, uint64_t scene, uintptr_t manager, uint64_t& keep) {
        const auto it = held_.find({ic, idx});
        if (it == held_.end()) return false;
        auto& v = it->second;
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i].scene == scene) {
                v.erase(v.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
        for (const Holder& h : v) {
            if (h.manager != manager) {
                keep = h.scene;
                return true;
            }
        }
        // Only scenes of the same manager (or none) remain: the game's own behaviour (it removes by key).
        held_.erase(it);
        return false;
    }
    size_t size() const { return held_.size(); }

private:
    static constexpr size_t kMax = 4096;
    void drop_ic(uintptr_t ic) {
        for (auto it = held_.lower_bound({ic, uint16_t{0}}); it != held_.end() && it->first.first == ic;) {
            it = held_.erase(it);
        }
    }
    std::map<std::pair<uintptr_t, uint16_t>, std::vector<Holder>> held_;
};

}  // namespace cdmp::session2::ishare
