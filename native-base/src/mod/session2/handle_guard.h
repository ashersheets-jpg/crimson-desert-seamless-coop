#pragma once
// Co-op+ (2026-11-10, tcB342): the guest crashed entering the host's world at exe+0x36E601E (read of 0x8), 0.2 s after
// the host's character appeared on it. exe+0x36E5FC0(obj, flag) calls obj's virtual +0x460, which hands back a handle,
// and reads through it without a null check; with another player's character loading, the handle can be empty.
// Guard (both roles, build 3576, bytes checked): a mid hook right after that call; an empty handle leaves the
// function through its own early-out (exe+0x36E619A), as when its entry conditions fail. Counted and logged.

#include <string>

namespace cdmp::session2 {

void handle_guard_start();
std::string handle_guard_describe();

}  // namespace cdmp::session2
