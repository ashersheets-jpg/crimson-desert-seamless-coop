#pragma once
// Co-op+: what the player is using (keyboard and mouse, an Xbox pad, a PlayStation pad) and the pad's buttons, so the
// mod's prompts show the right button icons and every Co-op+ action has a controller binding (user 2026-11-02:
// "a majority of people play this game on controller including myself").
//
// - Xbox pads (and anything Steam Input turns into one): XInput. The game reads XInput too; our hook on
//   XInputGetState lets us keep buttons from the game while the mod uses them (a held chord, a choice box).
// - PlayStation pads (DualShock 4, DualSense, DualSense Edge): their HID input reports, read in parallel with the game
//   (the game reads them natively). These buttons cannot be kept from the game.
// - Keyboard and mouse: any key or button, or the mouse moving.
// Whichever was used last is the input method. Pad buttons use XInput's bits for both families (PlayStation: cross =
// A, circle = B, square = X, triangle = Y, Options = Start, Create/Share = Back, plus kPadTouchpad).

#include <cstdint>

namespace cdmp::input {

enum class Method : uint8_t { Keyboard = 0, Xbox = 1, PlayStation = 2 };

inline constexpr uint16_t kPadUp = 0x0001, kPadDown = 0x0002, kPadLeft = 0x0004, kPadRight = 0x0008;
inline constexpr uint16_t kPadStart = 0x0010, kPadBack = 0x0020, kPadL3 = 0x0040, kPadR3 = 0x0080;
inline constexpr uint16_t kPadLB = 0x0100, kPadRB = 0x0200, kPadTouchpad = 0x0400;  // 0x0400 is free in XInput
inline constexpr uint16_t kPadRT = 0x0800;  // right trigger / R2 pulled (free in XInput too; read only, never masked)
inline constexpr uint16_t kPadA = 0x1000, kPadB = 0x2000, kPadX = 0x4000, kPadY = 0x8000;
inline constexpr uint16_t kPadFace = kPadA | kPadB | kPadX | kPadY;
inline constexpr uint16_t kPadDpad = kPadUp | kPadDown | kPadLeft | kPadRight;

// Starts the polling thread, the PlayStation reader and the XInput hook (once).
void input_start();

// The method used last (Keyboard until something else is touched).
Method method();

// Co-op+ (user 2026-11-03): the pad plugged in now, whether or not it was used last (PlayStation before Xbox;
// Keyboard = no pad). The controls card shows this pad's buttons.
Method connected_pad();

// All pads' buttons now (XInput and PlayStation pads OR'ed), as the player presses them (before our mask).
uint16_t pad_buttons();

// The co-op chord modifier is held: View/Back on Xbox, the touchpad button or Create on PlayStation.
bool chord_held();

// The chord modifier exists at all (input/actions.cpp: some co-op action is bound to a chord). Off, View / the
// touchpad button are the game's alone and chord_held() is always false.
void set_chord_modifier(bool on);

// Keeps `buttons` from the game (XInput pads only) for the next `ms` milliseconds; renew it while needed.
void mask_from_game(uint16_t buttons, uint32_t ms);

// [dev] pad_sim: act as if this pad were in use (1 Xbox, 2 PlayStation; 0 off) and let the dev script press its
// buttons (dev_pad_press). One-PC test without a controller.
void set_sim(int family);
void dev_pad_press(uint16_t buttons, uint32_t ms);

}  // namespace cdmp::input
