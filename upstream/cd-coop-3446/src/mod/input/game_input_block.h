#pragma once
// Co-op+ (user 2026-11-06: the co-op main menu must stand alone and take the place of the game's title, which must not
// react underneath). The game's input device only takes input while GetForegroundWindow() and GetActiveWindow() name
// its own window (dos-instancias.md §2.6: exe 0x2E30E50 on 2976). While blocked, both answer "none" to calls made from
// the game's own code only, so the game ignores the keyboard, the mouse and every pad (PlayStation pads too, which we
// cannot mask at XInput) while the mod keeps reading them for its own screens. Windows, the overlay and our own code
// see the real answers.

namespace cdmp::input {

// Once (hooks user32's two functions). False if the hooks could not be placed (then nothing is blocked).
bool game_input_block_install();
// While true the game takes no input (any thread).
void set_game_input_blocked(bool on);
bool game_input_blocked();
// Co-op+ (2026-11-11, tcB382): a second, independent reason: our player is DOWN and waits for a revive. The client is
// not told about its death (revive_prompt.h), so without this the downed player could still walk and swing. The
// mod's own keys (revive choices, the friend's revive) keep working.
void set_game_input_downed(bool on);

// Co-op+ (2026-11-13, a tester on a PlayStation pad: "it would still be operating the native game main menu while it
// was showing the co op menu ... it would start a save game file while i was hovering over the invite friend button").
// The two user32 answers were only ever tested with the keyboard. Two more holds, each on its own:
// - pads_held(): while true, the XInput hook hands the game an idle pad (input/pad_input.cpp), whatever the game's own
//   focus test does with pads.
// - game_input_block_tick(): every frame from the overlay with the game's window. While held, the game is told what
//   Windows tells it when its window loses the focus (WM_ACTIVATE inactive, WM_KILLFOCUS: its input manager stops
//   reading every device and lets go of the keys, dos-instancias.md 2.6), again every 400 ms (a real Alt+Tab back
//   tells it the opposite); when the hold ends it is told it is active again.
// [dev] title_hold_mode: bit 1 the user32 answers, bit 2 the "inactive" messages, bit 4 the idle XInput pad
// (default 7; a test takes one away to see what the others do alone).
bool pads_held();
void game_input_block_tick(void* game_window);
void set_hold_mode(unsigned bits);
bool hold_no_button_mask();  // [dev] bit 8: the lobby does not keep its own buttons from the game (a test of the rest)

}  // namespace cdmp::input
