#pragma once
// Co-op+: is the game showing a full-screen menu (map, inventory, journal, pause...)?

#include <optional>

namespace cdmp::game {

// True while the game's "gameplay input" flag is off. False when not known (the code that
// owns the flag did not match this build), so callers keep drawing as before.
bool ui_menu_open();

// Co-op+ (B16, 2026-11-16): true while the game's Photo Mode is open (the pause menu, Others, Photo Mode). The
// game keeps gameplay input on there, so ui_menu_open() is false; our overlay hides for the photo with this.
// False when not known.
bool photo_mode_active();

// Co-op+: true while a cutscene (sequencer) plays; nullopt when the flag cannot be read.
std::optional<bool> cutscene_playing();

// Co-op+: address of the minigame strength gauge (float, 0..100), 0 when it cannot be found.
uintptr_t minigame_gauge_address();

}  // namespace cdmp::game
