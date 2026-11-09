#pragma once
// Co-op+: the overlay drawn the way the game draws its own screens (user 2026-11-02: "I want the UI for everything
// we have added to look like it belongs in the game"). The game's look, from its own screens: a serif face (Windows'
// Palatino Linotype is the closest one every PC has), dark bands that fade out to the sides with thin light rules
// above and below (its dialogs), warm gold titles, the red "Death has found you." with a red brush stroke, and key
// prompts drawn as small light key caps. Button prompts follow the input method (input/pad_input.h): key caps on a
// keyboard, coloured A/B/X/Y on Xbox pads, the four shapes on PlayStation pads.
//
// Texts may carry action tokens, "{Challenge} Challenge  {Decline} Decline": each token is drawn as that action's
// button(s) (input/actions.h). "{*}" switches the text that follows to gold and back (a chosen item).

#include <string>
#include <vector>

struct ImDrawList;
struct ImFont;

namespace cdmp::render::ui {

// Once, right after ImGui::CreateContext: the serif fonts.
void load_fonts();

// The serif font (nullptr until loaded: ImGui's default is used).
ImFont* serif();
ImFont* serif_bold();

// A centred notice band at `y` (screen px): title (gold, may be empty) over body text, game dialog style.
void band(float screen_w, float y, const std::string& title, const std::string& body, float alpha = 1.0f);

// Co-op+ (T31): a centred card, game dialog style: a gold title, lines with action tokens, a dimmer footer.
void guide_card(float screen_w, float screen_h, const std::string& title, const std::vector<std::string>& lines,
                const std::string& footer, float alpha = 1.0f);

// Co-op+ (2026-11-05): the session summary card, the game's results-screen look: a gold title over a subtitle, two
// columns headed "You" and the friend's name (in their colour), one row per line (theirs "" = one value for both),
// and a footer with action tokens.
struct SummaryLine {
    std::string label, ours, theirs;
};
void summary_card(float screen_w, float screen_h, const std::string& title, const std::string& subtitle,
                  const std::string& partner, const std::vector<SummaryLine>& rows, const std::string& footer,
                  float alpha = 1.0f);

// Co-op+ (2026-11-05): hide and seek, the seeker's closed eyes: the screen darkened all over, a large count in gold
// and the lines under it (split at line breaks).
void veil(float screen_w, float screen_h, const std::string& count, const std::string& lines, float alpha = 1.0f);

// A one-line band (lighter), e.g. the running state of a challenge.
void strip(float screen_w, float y, const std::string& text, float alpha = 1.0f);

// The game's death screen: darkened screen, red title with its brush stroke, the question, the choices.
void death_screen(float screen_w, float screen_h, const std::string& title, const std::string& question,
                  const std::string& choices);

// A large centred title over a short line (the duel's "3", "Fight!", "Victory"...), its colour, fading with alpha.
void title_card(float screen_w, float screen_h, const std::string& title, const std::string& line, unsigned int color,
                float alpha = 1.0f);

// A health bar at the top of the screen, the game's boss bar look: the name in gold over a thin dark bar with a red
// fill (the guest's boss bar, the duel opponent's health). `note` under it ("" = none).
void boss_bar(float screen_w, float screen_h, const std::string& name, float frac, const std::string& note = {});

// Co-op+ (T34): the other player's colour (name tag, partner frame, their chat lines) and mark: round for a guest,
// a diamond for a host, so the two read apart without colour too.
void set_partner(unsigned int colour, bool round_mark);
unsigned int partner_colour();

// Co-op+ (T36): the kill feed, right-aligned under the top right corner; the friend's lines in their colour.
void kill_feed(float screen_w, float screen_h, const std::vector<std::string>& lines, const std::vector<bool>& ours);

// Co-op+ (T24): recent chat lines on the left, above the partner frame, in the game's serif with a soft shadow.
void chat_log(float screen_w, float screen_h, const std::vector<std::string>& lines);

// Co-op+ (T14): the partner frame on the left edge (above the game's date and place text): a gold diamond and the
// name, a red health bar and a thin stamina bar under it, a status line ("in a menu", distance). Below 25 % health
// the frame's edge pulses red and the status says so. hp / stamina < 0 = unknown (no bar).
void party_frame(float screen_w, float screen_h, const std::string& name, float hp, float stamina,
                 const std::string& status, double time_s);

// A medallion over a character's head (world markers): a hand of rock-paper-scissors (0 scissors, 1 paper, 2 rock;
// -1 = "?" still choosing, -2 = a seal: picked, hidden) drawn in `color`, with a caption under it. `size` px wide.
void hand_medallion(ImDrawList* dl, float x, float y, float size, int hand, const std::string& caption,
                    unsigned int color, float alpha = 1.0f);

// The other player's name tag over their head, the game's NPC tag look: a small gold diamond, the name in the serif
// face, a thin framed health bar (frac < 0: none) and the distance under it, faded by `alpha`, sized by `scale`.
// B16b: the game's hold prompt at (x, y): `key` in a ring filled to `progress` (0..1), `label` to its right.
void hold_prompt(ImDrawList* dl, float x, float y, float progress, const std::string& key, const std::string& label,
                 float alpha = 1.0f);
void nameplate(ImDrawList* dl, float x, float y, const std::string& name, float frac, const std::string& distance,
               float alpha, float scale);

// Text with action tokens at `x, y`, `size` px high (returns the width drawn). Measure only when dl is null.
float rich_text(ImDrawList* dl, float x, float y, float size, const std::string& text, unsigned int color,
                ImFont* font = nullptr);

}  // namespace cdmp::render::ui
