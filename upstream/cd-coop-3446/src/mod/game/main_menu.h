#pragma once
// Co-op+: the game's pause menu (Map, Inventory, Journal, Skills, Others) and its tab switches.
//
// Build 3576: every tab switch goes through one function, "open the panel with this name"
// (exe+0xF19180), on the menu's root object (UIGamePlayControl_Root_MainMenu). The root keeps
// the current page's index at +0x1C8. Found live on 2026-11-10 (PROGRESS.md, tcB268).

#include <cstdint>

namespace cdmp::game {

inline constexpr int kMenuPageCount = 5;  // Map 0, Inventory 1, Journal 2, Skills 3, Others 4

// Called on the game's thread before a switch from page `from` to page `to` (both 0..4).
// Return true to stop the switch (the game stays on `from`).
using MenuTabFilter = bool (*)(int from, int to);

// Hooks the switch function once (refused when the build is not recognised). True when hooked.
bool main_menu_install(MenuTabFilter filter);

// The page the menu last switched to (0..4), -1 before the first switch.
int main_menu_page();

// [dev]: find who writes the page index (a hardware write watch on root+0x1C8). Call every frame while the menu is
// open; logs each writer's address once. Only does anything after main_menu_dev_watch(true).
void main_menu_dev_watch(bool on);
void main_menu_dev_watch_tick();

}  // namespace cdmp::game
