#pragma once
// Co-op+ (user 2026-11-06: "a Co-op section in the settings where you can adjust everything that our mod does, like
// our F8 panel but integrated into the game's actual menu when you press start"): the Co-op page.
//
// While the game's own menu is open (Esc / Start: Map, Inventory, Journal, Skills, Others along the top) a "Co-op" tab
// is drawn after "Others", in the same place and look (the menu scales with the screen height and stays centred:
// 1920x1080 positions, the way coop_worlds.cpp clicks the game's own tabs). Clicking it, F8 (anywhere, also outside
// the menu) or the pad's panel chord opens the page: the game's framed-menu look, sections on the left (Session,
// Saves & Story, Combat & Loot, Reviving, World, Display, Challenges, Controls, Status), the settings of the chosen
// section on the right (Left / Right change; settings_menu.h writes cdmp.ini and applies them), a line explaining the
// chosen one, and Session's buttons (invite, leave, go to friend, bring my friend, resync) that do what their hotkeys
// do. Keyboard (arrows, Tab / Q / E, Enter / Space, Esc), mouse (hover, click, wheel; clicks do not reach the game
// while the page is open) and pads (D-pad, A, B, LB / RB; held back from the game on Xbox pads).

#include <string>
#include <vector>

namespace cdmp::render {

struct HudData;

// Render thread, every frame (render/hud.cpp), after everything else. `status` = the old panel's lines.
void coop_page_draw(float width, float height, const HudData& d, const std::vector<std::string>& status);

bool coop_page_open();
void coop_page_toggle();
// [dev] wait_see_through (gn2, 2026-11-12): the guest's "Waiting for Host" screen is drawn see-through, so a test
// screenshot shows what the game does behind it.
void coop_page_dev_see_through(bool on);
// The old status panel (top left) is shown while the page is closed (the page's Status section turns it on/off).
bool coop_page_details();

// Keyboard hook thread (net/coop_activity.cpp): true = the key is the page's (kept from the game).
bool coop_page_hook_key(unsigned vk, bool down);

}  // namespace cdmp::render
