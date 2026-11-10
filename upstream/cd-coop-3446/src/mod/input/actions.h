#pragma once
// Co-op+: every action the mod adds, with its keyboard keys and its controller buttons, in one table. Code asks
// action_down(); the overlay draws the action's button for the current input method (render/glyphs.h), so a prompt
// written as "{Challenge} Challenge" shows Shift+F5 on a keyboard, View + D-pad Up on an Xbox pad and Touchpad +
// D-pad Up on a PlayStation pad.
//
// Controller layout. The game uses every button (Game8 controls list, docs/research/progression.md), so the co-op
// actions are chords: hold View (Xbox) / the touchpad button or Create (PlayStation), then press
//   Y/Triangle invite   B/Circle leave   D-pad Left ping   D-pad Right go to friend
//   D-pad Up challenge / accept / end   D-pad Down decline   X/Square panel   A/Cross name tags   RB/R1 resync   LB/L1 revive a friend (hold)
//   right stick click: summon the friend to you
// A tap on View still reaches the game (camera). In a Co-op+ box the plain buttons answer it:
//   revive  X/Square instant, Y/Triangle checkpoint     rock-paper-scissors  X scissors, Y paper, B rock
//   cutscene skip  Start/Options or B                   arm wrestling mash  any face button
//   challenge picker  D-pad Left / Right (keyboard: the arrow keys) choose, the challenge chord sends

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cdmp::input {

enum class Action : uint8_t {
    Invite, Leave, Ping, GoToFriend, Challenge, Decline, Panel, Nameplates,
    ReviveNow, ReviveCheckpoint, RpsScissors, RpsPaper, RpsRock, Skip, Mash, MenuPrev, MenuNext, Resync, ReviveFriend, Summon, Chat, Help,
    MenuUp, MenuDown, MenuSelect, MenuBack,  // the player interaction menu (W / S, Space, Esc; D-pad, A, B)
    Interact,  // the game's own interact button (R / pad A): held on the friend, or to accept a request
    Count
};

struct Binding {
    std::string_view name;   // the token in prompt texts: "{Challenge}"
    int vk;                  // keyboard key (0 = none)
    bool ctrl, shift;        // keyboard modifiers
    uint16_t pad;            // pad button(s): any of them
    uint16_t pad_shown;      // the one drawn
    bool chord;              // needs the chord modifier held
};

const Binding& binding(Action a);

// The action's keyboard keys or pad buttons are down now. `focused`: the game window is in front (keyboard only).
bool action_down(Action a, bool focused);

// Co-op+ (the Co-op page, render/coop_page.h): the action counts as down for the next 200 ms, as if its keys were
// pressed (a button on the page that does what a hotkey does). Any thread.
void trigger(Action a);

// Rising edge helper: one per caller and action.
class Edge {
public:
    bool operator()(bool down) {
        const bool r = down && !was_;
        was_ = down;
        return r;
    }

private:
    bool was_ = false;
};

// Action by its token name ("Challenge"), Count when unknown.
Action action_by_name(std::string_view name);

// Shared world (2026-11-10): takes the hints of actions with nothing bound out of a line: a bracket naming one goes
// ("(first to 2;  {Challenge} stops)" -> "(first to 2)"), so does "{Challenge} to accept" outside a bracket.
void strip_unbound_hints(std::string& s);

// Co-op+ (T30): [keys] in cdmp.ini rebinds the co-op hotkeys, by action name: "Invite=Ctrl+F1", "Panel=F9",
// "Summon=Shift+G"; "<Action>_pad=..." the pad button (A B X Y LB RB L3 R3 Up Down Left Right). Read once at start.
// Returns warnings for the player: names or keys not understood, two co-op actions on the same keys, a co-op hotkey
// with no Ctrl / Shift (the game probably uses that key too). Empty = all fine.
std::vector<std::string> load_bindings(const std::wstring& ini_path);

// Any co-op chord is bound to a pad button (false in the shared-world build unless [keys] binds one): only then is
// the View / touchpad button kept from the game as the chord modifier.
bool chords_bound();

// "Ctrl+F1" for the keyboard part of an action (the panel's key list).
std::string key_text(Action a);

}  // namespace cdmp::input
