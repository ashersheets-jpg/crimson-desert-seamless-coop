#pragma once
// Chooses which game build's address table the mod compiles against.
//
// WHY THIS FILE IS A SWITCH RATHER THAN THE TABLE. The addresses used to live
// here directly. When Steam auto-updated the game from 1.0.0.3424 to 1.0.0.3446
// mid-session, every value in the file became wrong at once and the fix meant
// rewriting the header that 13 other files include. Keeping each build's table in
// its own header makes the next patch the same shape of change: add
// offsets_<build>.h, point the include below at it, done.
//
// The build targeted is 3460 (Co-op+ port; it builds on 3446, which builds on 3424). Each older table is kept because the newer one
// re-exports the values a patch does not move from it, and because the
// diff between two builds' tables is the most useful thing a future port can read.
//
// Consumers write `namespace off = build_3663;`. To retarget, change the include
// here and that alias in the consumers.

#include "mod/game/offsets_3663.h"
