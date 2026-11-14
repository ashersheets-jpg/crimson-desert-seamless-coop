#pragma once
// Co-op+ 2026-11-14: the shared cutscenes READY CHECK, its decisions (story_gate.h is the glue; tests in
// tests/optd_story_gate_tests.cpp). The owner's design (docs/research/shared-cutscenes.md): a story trigger waits
// until both players are there and both pressed; when the friend is elsewhere, both screens say so (the friend gets
// "go to them") and the waiting player may go on alone; the friend then plays that scene later at the spot.
//
// A story trigger = a player's own SequencerTriggerVolumeInOutReq 2808 entry (in = 1, the actor its own character) on
// a story scene (cd_seq_quest_*), held with the StageBranchStateReqs 2644 of that sequencer that follow it. The two
// players' copies of a scene have separate sequencer ids (each player's own stage manager), so they are matched by the
// scene resource's file-name hash. No game memory here.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cdmp::session2::sgl {

enum class Verdict : uint8_t { Pass, Hold, Drop };
constexpr int kHost = 0, kUser2 = 1;

struct Player {
    bool held = false;
    bool ready = false;
    uint32_t hash = 0;  // the held scene's file-name hash
    uint64_t seq = 0;   // its sequencer id (the player's own)
    uint64_t since_ms = 0;
};

struct Gate {
    Player p[2];
    std::vector<uint32_t> done[2];  // scenes released for each player (played, or gone on alone)

    bool did(int who, uint32_t hash) const {
        for (uint32_t h : done[who]) if (h == hash) return true;
        return false;
    }
    void mark(int who, uint32_t hash) {
        if (!did(who, hash)) done[who].push_back(hash);
        if (done[who].size() > 512) done[who].erase(done[who].begin());
    }

    // A player's own story volume entry. `linked`: both players are in the world (else nothing waits).
    Verdict on_enter(int who, uint64_t seq, uint32_t hash, bool linked, uint64_t now_ms) {
        Player& me = p[who];
        if (me.held) return me.seq == seq ? Verdict::Hold : Verdict::Pass;  // one wait at a time
        if (!linked || !hash || did(who, hash)) return Verdict::Pass;
        if (did(1 - who, hash)) {  // the friend already went through it: this player plays it now, at the spot
            mark(who, hash);
            return Verdict::Pass;
        }
        me = Player{true, false, hash, seq, now_ms ? now_ms : 1};
        return Verdict::Hold;
    }
    // A StageBranchStateReq 2644 of a player: held when it is for the held sequencer.
    Verdict on_branch(int who, uint64_t seq) const { return p[who].held && p[who].seq == seq ? Verdict::Hold : Verdict::Pass; }
    // A player's own volume exit: leaving the held volume before it started cancels the wait (the server never saw the
    // entry, so the exit goes too).
    Verdict on_leave(int who, uint64_t seq) {
        if (!p[who].held || p[who].seq != seq) return Verdict::Pass;
        p[who] = Player{};
        p[1 - who].ready = false;
        return Verdict::Drop;
    }
    bool together() const { return p[0].held && p[1].held && p[0].hash == p[1].hash; }

    // The player's press (hold of Interact). Both waiting at the same scene: ready, and the second press starts it
    // for both. Otherwise the press is "go on alone". Returns the players to release (bit 0 host, bit 1 user 2).
    int press(int who) {
        if (!p[who].held) return 0;
        if (together()) {
            p[who].ready = true;
            if (!p[1 - who].ready) return 0;
            mark(0, p[0].hash);
            mark(1, p[1].hash);
            p[0] = p[1] = Player{};
            return 3;
        }
        mark(who, p[who].hash);
        p[who] = Player{};
        return 1 << who;
    }
    // The friend left the world: nothing waits for them.
    int unlink() {
        int r = 0;
        for (int w = 0; w < 2; ++w) {
            if (p[w].held) {
                mark(w, p[w].hash);
                r |= 1 << w;
            }
            p[w] = Player{};
        }
        return r;
    }
};

// What a player's screen shows (story_gate's state for the layer).
enum class Shown : uint8_t {
    None = 0,
    WaitingAlone = 1,      // we wait; the friend is not at the scene: hold to go on alone
    WaitingTogether = 2,   // both here: hold when ready
    ReadyTogether = 3,     // we pressed; the friend has not yet
    FriendWaiting = 4,     // the friend waits at a scene we are not at: go to them
};
inline Shown shown(const Gate& g, int who) {
    const Player& me = g.p[who];
    const Player& them = g.p[1 - who];
    if (me.held) {
        if (!g.together()) return Shown::WaitingAlone;
        return me.ready ? Shown::ReadyTogether : Shown::WaitingTogether;
    }
    return them.held ? Shown::FriendWaiting : Shown::None;
}

// 2808 on 3663 (cs7c): u64 sequencer | u32 kind (0x2000xxxx) | u8 in (1) / out (0) | u32 actor | ...
struct Volume {
    uint64_t seq = 0;
    uint32_t kind = 0;
    bool in = false;
    uint32_t actor = 0;
};
inline bool parse_volume(const uint8_t* d, size_t n, Volume& v) {
    if (!d || n < 17) return false;
    v.seq = 0;
    v.kind = 0;
    v.actor = 0;
    for (int i = 0; i < 8; ++i) v.seq |= static_cast<uint64_t>(d[i]) << (8 * i);
    for (int i = 0; i < 4; ++i) v.kind |= static_cast<uint32_t>(d[8 + i]) << (8 * i);
    v.in = d[12] != 0;
    for (int i = 0; i < 4; ++i) v.actor |= static_cast<uint32_t>(d[13 + i]) << (8 * i);
    return (v.kind >> 24) == 0x20;
}
inline bool is_story_name(const char* name) {
    const char k[] = "cd_seq_quest_";
    for (size_t i = 0; i + 1 < sizeof k; ++i) if (!name || name[i] != k[i]) return false;
    return true;
}
// The scenes that wait (cs7c / cs7d, the prologue): a quest step's main scene is cd_seq_quest_<quest>_<step>_00 (the
// cabin: cd_seq_quest_intro_0310_00, whose branch starts the cinematic cd_seq_quest_intro_0310_dlc 0.5 s later); the
// step's sub-scenes _01 ... _15 (the comrades along the way) and the cinematics themselves (_dlc) never wait.
inline bool is_gate_name(const char* name) {
    if (!is_story_name(name)) return false;
    size_t n = 0;
    while (n < 64 && name[n]) ++n;
    return n >= 16 && name[n - 3] == '_' && name[n - 2] == '0' && name[n - 1] == '0';
}

}  // namespace cdmp::session2::sgl
