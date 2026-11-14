#pragma once
// Co-op+ 2026-11-14: the shared cutscenes READY CHECK, on screen (the host's native base holds the story triggers:
// cdmp-native story_gate.h, [session2] story_gate=hold). The owner's design: a story trigger waits until both players
// are there and both pressed; when the friend is elsewhere both screens say so, the friend may go to them, and the
// waiting player may go on alone.
//
// The host's layer reads the gate (cdmp_story_gate_state) every 250 ms and sends the friend its part (Activity type 30:
// u8 op 1 | u8 shown); the friend's press comes back (op 2) and is handed to the gate (cdmp_story_gate_press(1)).
// The press is a hold of Interact (render/hud.cpp, the request prompt).

#include <cstdint>
#include <string>

namespace cdmp::net {

struct StoryTick {
    std::string line;  // our line ("" = none)
};
// Network loop.
StoryTick story_tick(bool connected, bool host, const std::string& partner_name, uint32_t now_ms);

// What our screen shows now (cdmp-native story_gate_logic.h Shown): 0 none, 1 we wait alone (hold: go on alone),
// 2 both here (hold: start), 3 we pressed (no prompt), 4 the friend waits for us (hold: go to them).
int story_shown();
// The HUD's hold of Interact fired on a story prompt (1 or 2).
void story_answer();
// Activity type 30 from the other player.
void story_on_event(const uint8_t* data, size_t n);

}  // namespace cdmp::net
