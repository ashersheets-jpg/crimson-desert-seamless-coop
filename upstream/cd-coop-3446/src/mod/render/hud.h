#pragma once
// What the overlay shows: a status panel and a marker over the other player.
// The network thread publishes data with hud_update(); the render hook calls
// hud_draw() once per frame on the game's present thread.

#include <optional>
#include <string>

#include "common/protocol.h"
#include "mod/game/player_reader.h"

namespace cdmp::render {

struct HudData {
    bool connected = false;
    std::string peer_name;
    float loss_pct = -1;      // Co-op+ (T32): packet loss over the last seconds (< 0 = not known yet)
    bool net_hud = false;
    bool we_host = false;
    std::vector<std::string> kill_feed;  // Co-op+ (T36): recent kills, oldest first
    std::vector<bool> kill_feed_ours;
    std::vector<std::string> score_lines;  // Co-op+ (T43): the record vs this friend (panel)
    std::string peer_title;                // Co-op+ (T43): the friend's title ("the Duelist"; " " = none)     // Co-op+ (T34): the other player is our guest     // Co-op+ (T32): the small ping / loss readout on screen ([overlay] net_hud)
    float rtt_ms = 0.0f;
    std::optional<game::PlayerSnapshot> local;
    std::string local_status;
    std::optional<proto::PlayerState> remote;
    std::string peer_problem;  // another protocol version or mod build ("" if none)
    std::string enemy_zone;    // phase 5: whose enemies the guest sees ("" = nothing to say)
    // [room] code: the code we are hosting (the host reads it out), or the code
    // we are looking for (the guest typed it). Empty when room codes are unused.
    std::string room_code;
    bool room_hosting = false;
    // Why there is no code, when there is none: room_code.cpp's own status text
    // (e.g. "room: ready", "room: waiting for steam_api64.dll"). Shown in the panel
    // where the code would be, so a missing code explains itself on screen instead
    // of only in the log.
    std::string room_status;
    // Co-op+: the Steam action available now (invite / leave) or a short notice
    // about the last one. "" = nothing to say.
    std::string coop_hint;
    // Co-op+ party scaling, e.g. "Enemies: health x1.7 (2 players)" ("" = off).
    std::string scaling;
    // Co-op+ save guard: "the game saved during co-op" notice ("" = none).
    std::string save_notice;
    // Co-op+: "<name> joined your world" and similar, for a few seconds ("" = none).
    // Drawn even when the panel is hidden.
    std::string toast;
    // Co-op+: "<name> is watching a cutscene" while it lasts (coop_activity.h; "" = none). Drawn even when the panel
    // is hidden.
    std::string activity;
    // Co-op+: we are dead and the game's own death screen stays away: our box (net::RevivePrompt:
    // 0 none, 1 Q/U choice, 2 waiting for the other player's boss fight, 3 checkpoint only).
    // Drawn even when the panel is hidden.
    uint8_t revive_prompt = 0;
    // Boss fight (boss_safe.h): the stage boss's health, read from the relayed
    // copy. The guest has no native boss bar -- the stage that draws it exists
    // only in the host's world -- so we draw our own.
    bool boss_active = false;
    std::string boss_name;
    float boss_hp = 0.0f;
    float boss_hp_max = 0.0f;
    bool boss_cutscene = false;
    // Co-op+ duel (coop_duel.h): the duel ground, the big title, the opponent's health.
    bool duel_active = false;
    proto::Vec3 duel_centre{};
    float duel_radius = 0;
    std::string duel_title, duel_subtitle;
    unsigned int duel_title_color = 0;
    float duel_title_alpha = 0;
    float duel_out_left_s = -1;
    // Co-op+ rock-paper-scissors duel: the hands over the two characters (coop_activity.h RpsView).
    bool partner_in_menu = false;
    std::string revive_line;
    std::string travel_line;
    std::string story_line;  // Co-op+ the ready check (coop_story.h)
    std::string chat_input, chat_quick;  // Co-op+ (T24): the open chat line / quick list
    std::vector<std::string> chat_log;   // recent chat lines  // Co-op+ (T19): a summon / party travel request  // Co-op+ (T15): "hold to revive" / progress (reviver) or the bleed-out (downed)  // Co-op+ (T09): their name tag says "in a menu"
    // Co-op+ (2026-11-05) hide and seek: we seek, so the friend is hidden from us (no name tag, no distance); while we
    // count, the screen is veiled with the count.
    bool hide_partner = false;
    bool area_active = false;  // the hiding area's ring
    proto::Vec3 area_centre{};
    float area_radius = 0;
    bool area_outside = false;
    bool veil = false;
    std::string veil_count, veil_line;
    bool rps_on = false;
    int rps_ours = -1, rps_theirs = -1;
    bool rps_ours_picked = false, rps_theirs_picked = false;
    int rps_winner = 0;
    float rps_reveal_t = -1;
};

struct HudOptions {
    bool self_marker = false;  // dev aid: draw a cross on our own character
    bool english = true;       // the panel's texts in English (else Spanish)
    bool nameplates = true;    // Co-op+: name over the other player (Ctrl+F8 toggles in game)
    // Co-op+: why the mod is off on this game build ("" = supported). Shown in red.
    std::string build_problem;
};

// Co-op+ (T29): something that stops co-op, in plain words with the next step (docs/error-messages.md). One line per
// kind; "" clears it. Shown in red on the panel, and for 15 s at the top of the screen whenever it changes.
enum class Problem { Network, Join, Backup, Guards, Count };
void hud_set_problem(Problem kind, std::string text);

// Co-op+ (T31): the controls card. Shown by the Help action (toggle) or by hud_show_guide (first run); hides after
// 60 s or on Help again.
void hud_show_guide();

void hud_configure(const HudOptions& options);
void hud_update(HudData data);
void hud_draw(float width, float height);

}  // namespace cdmp::render
