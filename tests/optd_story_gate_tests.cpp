// Co-op+ (2026-11-14): src/mod/session2/story_gate_logic.h (the shared cutscenes ready check).
#include <cstdio>

#include "mod/session2/story_gate_logic.h"

using namespace cdmp::session2;
using sgl::Verdict;

namespace {
int g_failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)
constexpr uint32_t kCabin = 0xC1B64D01, kNext = 0x3828C658;
}  // namespace

int main() {
    // The cs7c bytes: the host's own entry into a story volume, and an NPC's.
    {
        const uint8_t own[] = {0xBC, 0x01, 0, 0, 0, 0, 0, 0, 0x03, 0, 0, 0x20, 0x01, 0x01, 0x00, 0x10, 0xA0, 0x01};
        sgl::Volume v;
        CHECK(sgl::parse_volume(own, sizeof own, v));
        CHECK(v.seq == 0x1BC && v.kind == 0x20000003 && v.in && v.actor == 0xA0100001);
        const uint8_t out[] = {0x24, 0, 0, 0, 0, 0, 0, 0, 0x0C, 0, 0, 0x20, 0x00, 0xB2, 0x00, 0x10, 0xB0, 0x00};
        CHECK(sgl::parse_volume(out, sizeof out, v));
        CHECK(!v.in && v.actor == 0xB01000B2);
        CHECK(!sgl::parse_volume(out, 16, v));
        CHECK(sgl::is_story_name("cd_seq_quest_intro_0310_00"));
        CHECK(!sgl::is_story_name("cd_seq_contents_weather_01"));
        CHECK(!sgl::is_story_name(nullptr));
        CHECK(sgl::is_gate_name("cd_seq_quest_intro_0310_00"));
        CHECK(!sgl::is_gate_name("cd_seq_quest_intro_0310_04"));
        CHECK(!sgl::is_gate_name("cd_seq_quest_intro_0310_dlc"));
        CHECK(!sgl::is_gate_name("cd_seq_quest_intro_0300"));
        CHECK(!sgl::is_gate_name("cd_seq_quest_intro_trigger"));
        CHECK(!sgl::is_gate_name("cd_seq_contents_x_00"));
        CHECK(sgl::is_cinematic_name("cd_seq_quest_intro_0310_dlc"));
        CHECK(!sgl::is_cinematic_name("cd_seq_quest_intro_0310_00"));
        CHECK(sgl::same_step("cd_seq_quest_intro_0310_00", "cd_seq_quest_intro_0310_dlc"));
        CHECK(!sgl::same_step("cd_seq_quest_intro_0310_00", "cd_seq_quest_intro_0300_01_dlc"));
        CHECK(!sgl::same_step("cd_seq_quest_intro_0310_00", "cd_seq_quest_intro_0320_dlc"));
        CHECK(!sgl::same_step("", "cd_seq_quest_intro_0310_dlc"));
    }
    // Both arrive, both press: the second press releases both; the scene is done for both.
    {
        sgl::Gate g;
        CHECK(g.on_enter(sgl::kHost, 0x1BC, kCabin, true, 10) == Verdict::Hold);
        CHECK(sgl::shown(g, sgl::kHost) == sgl::Shown::WaitingAlone);
        CHECK(sgl::shown(g, sgl::kUser2) == sgl::Shown::FriendWaiting);
        CHECK(g.on_branch(sgl::kHost, 0x1BC) == Verdict::Hold);
        CHECK(g.on_branch(sgl::kHost, 0x1BD) == Verdict::Pass);
        CHECK(g.on_branch(sgl::kUser2, 0x1BC) == Verdict::Pass);
        CHECK(g.on_enter(sgl::kUser2, 0x1BF, kCabin, true, 20) == Verdict::Hold);
        CHECK(g.together());
        CHECK(sgl::shown(g, sgl::kHost) == sgl::Shown::WaitingTogether);
        CHECK(g.press(sgl::kUser2) == 0);
        CHECK(sgl::shown(g, sgl::kUser2) == sgl::Shown::ReadyTogether);
        CHECK(sgl::shown(g, sgl::kHost) == sgl::Shown::WaitingTogether);
        CHECK(g.press(sgl::kHost) == 3);
        CHECK(sgl::shown(g, sgl::kHost) == sgl::Shown::None);
        CHECK(g.did(sgl::kHost, kCabin) && g.did(sgl::kUser2, kCabin));
        // Entered again later (a replay of the volume): not held again.
        CHECK(g.on_enter(sgl::kHost, 0x1BC, kCabin, true, 30) == Verdict::Pass);
    }
    // Alone: the waiting player goes on; the friend plays it later at the spot, without a wait.
    {
        sgl::Gate g;
        CHECK(g.on_enter(sgl::kHost, 0x1BC, kCabin, true, 10) == Verdict::Hold);
        CHECK(g.press(sgl::kHost) == 1);
        CHECK(sgl::shown(g, sgl::kUser2) == sgl::Shown::None);
        CHECK(g.on_enter(sgl::kUser2, 0x1BF, kCabin, true, 50) == Verdict::Pass);
        CHECK(g.did(sgl::kUser2, kCabin));
    }
    // A repeated entry of the held volume is held too; another scene's passes (one wait at a time).
    {
        sgl::Gate g;
        CHECK(g.on_enter(sgl::kUser2, 0x1BF, kCabin, true, 10) == Verdict::Hold);
        CHECK(g.on_enter(sgl::kUser2, 0x1BF, kCabin, true, 11) == Verdict::Hold);
        CHECK(g.on_enter(sgl::kUser2, 0x1C0, kNext, true, 12) == Verdict::Pass);
    }
    // Leaving the volume before the start cancels the wait (entry and exit both dropped); the friend's ready is undone.
    {
        sgl::Gate g;
        CHECK(g.on_enter(sgl::kHost, 0x1BC, kCabin, true, 10) == Verdict::Hold);
        CHECK(g.on_enter(sgl::kUser2, 0x1BF, kCabin, true, 10) == Verdict::Hold);
        CHECK(g.press(sgl::kUser2) == 0);
        CHECK(g.on_leave(sgl::kHost, 0x1BD) == Verdict::Pass);
        CHECK(g.on_leave(sgl::kHost, 0x1BC) == Verdict::Drop);
        CHECK(!g.p[sgl::kUser2].ready && g.p[sgl::kUser2].held);
        CHECK(sgl::shown(g, sgl::kUser2) == sgl::Shown::WaitingAlone);
        CHECK(!g.did(sgl::kHost, kCabin));
        CHECK(g.on_enter(sgl::kHost, 0x1BC, kCabin, true, 40) == Verdict::Hold);
        CHECK(g.together());
    }
    // Solo (no friend linked): nothing waits. The friend leaves while one waits: released.
    {
        sgl::Gate g;
        CHECK(g.on_enter(sgl::kHost, 0x1BC, kCabin, false, 10) == Verdict::Pass);
        CHECK(g.on_enter(sgl::kHost, 0x1BC, kNext, true, 10) == Verdict::Hold);
        CHECK(g.unlink() == 1);
        CHECK(sgl::shown(g, sgl::kHost) == sgl::Shown::None);
        CHECK(g.press(sgl::kHost) == 0);
    }
    // Different scenes at once: each is "alone" (no together), each press releases only its player.
    {
        sgl::Gate g;
        CHECK(g.on_enter(sgl::kHost, 0x1BC, kCabin, true, 10) == Verdict::Hold);
        CHECK(g.on_enter(sgl::kUser2, 0x1C0, kNext, true, 10) == Verdict::Hold);
        CHECK(!g.together());
        CHECK(sgl::shown(g, sgl::kHost) == sgl::Shown::WaitingAlone);
        CHECK(g.press(sgl::kUser2) == 2);
        CHECK(sgl::shown(g, sgl::kUser2) == sgl::Shown::FriendWaiting);
    }
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("story gate: all passed\n");
    return 0;
}
