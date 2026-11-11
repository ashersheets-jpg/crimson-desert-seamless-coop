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

}  // namespace cdmp::input
