#pragma once
// Co-op+ (2026-11-08, build 1.0.0.3576): the guest trades at the host's merchants with its own money, stock and
// inventory (shop_guest_logic.h says what the game checks and the rule). [session2] guest_shop_check = 0 | 1
// (default 0 until it is proven in the game), both roles:
//   two mid hooks in the store check 0x23C7350 (every buy / sell / store operation on the server, the store window's
//   checks on the client): A at 0x23C74BB (the field manager has no current stage), B at 0x23C777B (the compare of
//   that current stage with the merchant's linked stage). For the guest's character only (the guest's client: its
//   player; the host's server: user 2's character). Nothing is written in the game but the two registers / the one
//   stack slot of that call.

#include <string>

namespace cdmp::session2 {

void shop_guest_install(bool on, bool guest);
std::string shop_guest_describe();

}  // namespace cdmp::session2
