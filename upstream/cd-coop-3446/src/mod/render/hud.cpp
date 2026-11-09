#include "mod/render/hud.h"

#include <windows.h>

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <format>
#include <mutex>

#include "common/math3d.h"
#include "common/nameplate.h"
#include "mod/game/camera_reader.h"
#include "mod/game/ui_state.h"
#include "mod/log.h"
#include "mod/render/dx12_hook.h"
#include "mod/input/actions.h"
#include "mod/input/pad_input.h"
#include "mod/render/game_ui.h"
#include "mod/render/coop_page.h"
#include "mod/net/coop_activity.h"
#include "mod/net/coop_lobby.h"
#include "mod/settings_menu.h"
#include "mod/net/coop_worlds.h"
#include "mod/net/session_summary.h"
#include "mod/optin.h"

namespace cdmp::render {
namespace {

// Marker above the character's feet (m). Co-op+: 2.1 floated well clear of the head in the 3460
// echo tests (the name sits another ~28 px above the dot); the player character is ~1.8 m tall.
constexpr float kHeadHeight = 1.95f;
constexpr ImU32 kAccent = IM_COL32(230, 70, 60, 255);
constexpr ImU32 kShadow = IM_COL32(0, 0, 0, 200);

std::mutex g_mutex;
HudData g_data;
HudOptions g_options;
std::string g_problems[static_cast<int>(Problem::Count)];  // Co-op+ (T29)
ULONGLONG g_problem_changed = 0;
std::atomic<ULONGLONG> g_guide_since{0};  // Co-op+ (T31): when the controls card opened (0 = closed)

// Co-op+: the same colour at `alpha` of its own opacity (nameplate fade).
ImU32 faded(ImU32 color, float alpha) {
    const auto a = static_cast<ImU32>(((color >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.0f, 1.0f));
    return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

void draw_remote_marker(ImDrawList* dl, const game::ScreenPoint& p, const HudData& d,
                        float distance) {
    // Co-op+ (brief 6.2): fades and shrinks a little with distance (common/nameplate.h); the game's tag look.
    const float alpha = nameplate::alpha(distance);
    const float scale = nameplate::text_scale(distance);
    const std::string name = d.peer_name.empty() ? (g_options.english ? "Player 2" : "Jugador 2") : d.peer_name;
    const float frac = d.remote->health_max > 0 ? d.remote->health / d.remote->health_max : -1.0f;
    const std::string sub = d.partner_in_menu ? std::format("{:.0f} m  -  in a menu", distance) : std::format("{:.0f} m", distance);
    ui::nameplate(dl, p.x, p.y, name, frac, sub, alpha, scale);
}

}  // namespace

void hud_show_guide() { g_guide_since = GetTickCount64(); }

void hud_set_problem(Problem kind, std::string text) {
    std::lock_guard lock(g_mutex);
    std::string& slot = g_problems[static_cast<int>(kind)];
    if (slot == text) return;
    slot = std::move(text);
    if (!slot.empty()) g_problem_changed = GetTickCount64();
}

void hud_configure(const HudOptions& options) {
    std::lock_guard lock(g_mutex);
    g_options = options;
}

void hud_update(HudData data) {
    std::lock_guard lock(g_mutex);
    g_data = std::move(data);
}

// The status panel (top left). F8 shows / hides it; the nameplate has its own toggle.
void draw_panel(const HudData& d, const HudOptions& opt, float remote_distance, const std::string& camera_failure,
                const std::vector<std::string>& problems) {
    // ---- status panel
    // Co-op+: the game's panel look (game_ui.h): dark warm backing, gold serif title, a thin light rule.
    ImGui::SetNextWindowPos(ImVec2(24, 24));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 12));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 5));
    ImGui::Begin("cdmp", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::PopStyleVar(2);
    if (ui::serif_bold()) ImGui::PushFont(ui::serif_bold(), 24.0f);
    ImGui::TextColored(ImVec4(0.91f, 0.78f, 0.51f, 1), "Seamless Co-op");
    if (ui::serif_bold()) ImGui::PopFont();
    // GPL credit, always visible with the panel: the co-op engine is theirs.
    ImGui::TextDisabled("Co-op+ alpha, built on cdmp %s by ByJunter, port by nouzert1", CDMP_VERSION);
    ImGui::Separator();
    if (!opt.build_problem.empty()) {
        // Co-op+: an unsupported game build. Nothing else on the panel matters.
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextColored(ImVec4(1, 0.35f, 0.3f, 1), "%s", opt.build_problem.c_str());
        ImGui::PopTextWrapPos();
        ImGui::End();
        return;
    }
    const bool en = opt.english;
    // Co-op+ (T29): what stops co-op right now, and what to do.
    if (!problems.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        for (const auto& p : problems) ImGui::TextColored(ImVec4(1, 0.35f, 0.3f, 1), "%s", p.c_str());
        ImGui::PopTextWrapPos();
    }
    if (d.connected) {
        ImGui::Text(en ? "Network: connected to '%s' (%.1f ms)" : "Red: conectado a '%s' (%.1f ms)",
                    d.peer_name.c_str(), d.rtt_ms);
        // Co-op+ (T32): connection quality.
        if (d.loss_pct >= 0) {
            ImGui::SameLine();
            ImGui::TextColored(d.loss_pct < 2 ? ImVec4(0.5f, 0.9f, 0.5f, 1) : d.loss_pct < 8 ? ImVec4(1, 0.8f, 0.3f, 1)
                                                                                         : ImVec4(1, 0.35f, 0.3f, 1),
                               en ? "loss %.1f%%" : "perdida %.1f%%", d.loss_pct);
        }
    } else {
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1),
                           en ? "Network: looking for the other player..." : "Red: buscando al otro jugador...");
    }
    // (The guest's boss bar: drawn outside the panel, game style; see hud_draw.)
    if (!d.peer_problem.empty()) {
        // In English from the session; the essential part is the instruction.
        ImGui::TextColored(ImVec4(1, 0.35f, 0.3f, 1), en ? "Different mod versions: both of you update"
                                                          : "Versiones distintas del mod: actualizad los dos");
        ImGui::TextColored(ImVec4(1, 0.35f, 0.3f, 1), "%s", d.peer_problem.c_str());
    }
    if (d.local) {
        ImGui::Text(en ? "You: HP %.0f/%.0f  Stamina %.0f/%.0f" : "Tú: HP %.0f/%.0f  Stamina %.0f/%.0f",
                    d.local->health, d.local->health_max, d.local->stamina, d.local->stamina_max);
    } else {
        ImGui::TextDisabled(en ? "You: %s" : "Tú: %s", d.local_status.c_str());
    }
    if (d.remote && d.local) {
        ImGui::Text(en ? "Other player: HP %.0f/%.0f  %.0f m away" : "Otro jugador: HP %.0f/%.0f  a %.0f m",
                    d.remote->health, d.remote->health_max, remote_distance);
    }
    if (!d.enemy_zone.empty()) ImGui::TextColored(ImVec4(0.75f, 0.85f, 1, 1), "%s", d.enemy_zone.c_str());
    if (!d.scaling.empty()) ImGui::TextColored(ImVec4(0.75f, 0.85f, 1, 1), "%s", d.scaling.c_str());
    if (!d.save_notice.empty()) {  // Co-op+: wrapped (story sync notices are a few sentences)
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 44.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.8f, 0.3f, 1));
        ImGui::TextWrapped("%s", d.save_notice.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }
    if (!camera_failure.empty()) ImGui::TextDisabled(en ? "Camera: %s" : "Cámara: %s", camera_failure.c_str());

    // The room code, ALWAYS shown -- with the code when we have one, and an explicit
    // "no code" plus the reason when we do not.
    //
    // It used to be drawn only when a code existed, which made two very different
    // situations look identical from the player's side: "the mod has not managed to
    // open a room yet" and "the mod is not even trying" both rendered as nothing at
    // all. That cost several rounds of testing whose only visible symptom was an
    // absent line. A line that is always present, and says which case it is, turns
    // the panel into the diagnostic it should have been from the start.
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1, 1), en ? "Room code:" : "Codigo de sala:");
    ImGui::SameLine();
    if (!d.room_code.empty()) {
        // The host reads this out; the guest sees what they typed.
        if (d.room_hosting) {
            ImGui::TextColored(ImVec4(0.4f, 1, 0.5f, 1), "%s   %s", d.room_code.c_str(),
                               en ? "(tell your friend)" : "(diselo a tu amigo)");
        } else {
            ImGui::Text("%s   %s", d.room_code.c_str(), en ? "(joining...)" : "(entrando...)");
        }
    } else if (d.room_status.empty()) {
        ImGui::TextDisabled(en ? "(no code)" : "(sin codigo)");
    } else {
        // Say WHY, in the place the code would be, using the room module's own status
        // text -- so the panel and cdmp.log agree instead of the player having to go
        // read a file to find out what went wrong.
        ImGui::TextDisabled(en ? "(no code: %s)" : "(sin codigo: %s)", d.room_status.c_str());
    }
    if (!d.coop_hint.empty()) ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.6f, 1), "%s", d.coop_hint.c_str());
    ImGui::TextDisabled("Controls card: Shift+F1 (pad: hold View / Touchpad + left stick click)");
    // Co-op+ (T43): the record against this friend.
    for (const auto& l : d.score_lines) ImGui::TextColored(ImVec4(0.91f, 0.78f, 0.51f, 1), "%s", l.c_str());
    // Co-op+ (T30/T40): the settings (settings_menu.h).
    ImGui::Separator();
    for (const auto& l : settings::lines()) {
        if (l.selected) ImGui::TextColored(ImVec4(0.91f, 0.78f, 0.51f, 1), "%s", l.text.c_str());
        else ImGui::TextDisabled("%s", l.text.c_str());
    }
    ImGui::End();
}

// Co-op+ (the Co-op page's Status section): what the old panel says, as plain lines.
std::vector<std::string> status_lines(const HudData& d, const HudOptions& opt, const std::vector<std::string>& problems,
                                      float remote_distance) {
    std::vector<std::string> out;
    if (!opt.build_problem.empty()) out.push_back(opt.build_problem);
    for (const auto& p : problems) out.push_back(p);
    out.push_back(d.connected ? std::format("Network: connected to '{}' ({:.1f} ms{})", d.peer_name, d.rtt_ms,
                                            d.loss_pct >= 0 ? std::format(", loss {:.1f} %", d.loss_pct) : "")
                              : "Network: looking for the other player...");
    if (!d.peer_problem.empty()) out.push_back(d.peer_problem);
    if (d.local) {
        out.push_back(std::format("You: HP {:.0f}/{:.0f}, stamina {:.0f}/{:.0f}", d.local->health, d.local->health_max,
                                  d.local->stamina, d.local->stamina_max));
    } else {
        out.push_back("You: " + d.local_status);
    }
    if (d.remote && d.local) {
        out.push_back(std::format("Other player: HP {:.0f}/{:.0f}, {:.0f} m away", d.remote->health, d.remote->health_max,
                                  remote_distance));
    }
    if (!d.room_code.empty()) out.push_back("Room code: " + d.room_code + (d.room_hosting ? " (tell your friend)" : ""));
    else if (!d.room_status.empty()) out.push_back("Room code: none (" + d.room_status + ")");
    if (!d.enemy_zone.empty()) out.push_back(d.enemy_zone);
    if (!d.scaling.empty()) out.push_back(d.scaling);
    if (!d.save_notice.empty()) out.push_back(d.save_notice);
    if (!d.coop_hint.empty()) out.push_back(d.coop_hint);
    for (const auto& l : d.score_lines) out.push_back(l);
    out.push_back(std::string("Co-op+ alpha, built on cdmp ") + CDMP_VERSION + " by ByJunter, port by nouzert1");
    return out;
}

void hud_draw(float width, float height) {
    static bool panel_visible = false;  // Co-op+: hidden until F8 / chord + X (the game shows no debug boxes)
    static bool plates_visible = true;
    static bool plates_init = false;
    static bool f8_was_down = false;
    static game::CameraReader camera;
    static bool camera_init = false;
    static ULONGLONG last_diag = 0;

    // F8: the panel. Ctrl+F8 (Co-op+): the nameplate over the other player. Pads: chord + X / chord + A.
    static input::Edge panel_edge, plates_edge;
    const bool focus = GetForegroundWindow() != nullptr;
    // Co-op+ (2026-11-06): F8 / the panel chord open the Co-op page (render/coop_page.h); the old status panel is shown
    // when the page's Status section turns it on.
    if (plates_edge(input::action_down(input::Action::Nameplates, focus))) plates_visible = !plates_visible;
    else if (panel_edge(input::action_down(input::Action::Panel, focus))) {
        // At the title with the lobby closed, F8 brings the lobby back; elsewhere it opens / closes the Co-op page.
        const net::LobbyView lv = net::lobby_view();
        if (lv.at_title && !lv.show && !coop_page_open()) net::lobby_reopen();
        else coop_page_toggle();
    }
    panel_visible = coop_page_details() && !coop_page_open();
    (void)f8_was_down;

    HudData d;
    HudOptions opt;
    std::vector<std::string> problems;
    bool problem_fresh = false;
    {
        std::lock_guard lock(g_mutex);
        d = g_data;
        opt = g_options;
        for (const auto& p : g_problems) {
            if (!p.empty()) problems.push_back(p);
        }
        problem_fresh = g_problem_changed && GetTickCount64() - g_problem_changed < 15000;
    }
    if (!plates_init) {
        plates_visible = opt.nameplates;
        plates_init = true;
    }
    if (!camera_init) {
        camera.init();
        camera_init = true;
    }
    const auto cam = camera.read();

    // Is the camera position stored in world space or relative to the map
    // chunk like the player's physics position? Whichever is closer to us.
    std::optional<proto::Vec3> cam_world;
    bool cam_is_local = false;
    if (cam && d.local) {
        const float to_world = math::length(math::sub(cam->position, d.local->position));
        const float to_local = math::length(math::sub(cam->position, d.local->local_position));
        cam_is_local = to_local < to_world;
        cam_world = cam_is_local ? math::add(cam->position, d.local->chunk_origin) : cam->position;
    }

    // ---- status panel
    float remote_distance = 0;
    if (d.remote && d.local) remote_distance = math::length(math::sub(d.remote->pos, d.local->position));
    // The Co-op page goes on top of everything, whichever way this function ends.
    struct PageLast {
        float w, h;
        const HudData& d;
        std::vector<std::string> status;
        ~PageLast() { coop_page_draw(w, h, d, status); }
    } page_last{width, height, d, status_lines(d, opt, problems, remote_distance)};
    settings::set_panel_open(panel_visible && opt.build_problem.empty());
    // Co-op+ (T34): host gold / guest blue, or the colour-blind-safe pair (Okabe-Ito orange / sky blue).
    {
        const bool cb = settings::colourblind();
        const ImU32 host_col = cb ? IM_COL32(230, 159, 0, 255) : IM_COL32(232, 200, 130, 255);
        const ImU32 guest_col = cb ? IM_COL32(86, 180, 233, 255) : IM_COL32(120, 180, 245, 255);
        ui::set_partner(d.we_host ? guest_col : host_col, d.we_host);
    }
    // Co-op+ (T32): the small connection readout, top right ([overlay] net_hud, F8 settings).
    if (d.connected && d.net_hud && !game::ui_menu_open()) {
        const bool good = d.rtt_ms < 120 && d.loss_pct < 2, ok = d.rtt_ms < 250 && d.loss_pct < 8;
        const ImU32 col = good ? IM_COL32(130, 220, 130, 230) : ok ? IM_COL32(255, 205, 80, 230) : IM_COL32(255, 90, 75, 230);
        const std::string t = d.loss_pct >= 0 ? std::format("Co-op  {:.0f} ms  {:.0f}% loss", d.rtt_ms, d.loss_pct)
                                              : std::format("Co-op  {:.0f} ms", d.rtt_ms);
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const float fs = std::max(13.0f, height / 1080.0f * 16.0f);
        ImFont* f = ui::serif() ? ui::serif() : ImGui::GetFont();
        const ImVec2 sz = f->CalcTextSizeA(fs, FLT_MAX, 0, t.c_str());
        const float x = width - sz.x - 24, y = 14;
        dl->AddText(f, fs, ImVec2(x + 1, y + 1), IM_COL32(0, 0, 0, 200), t.c_str());
        dl->AddText(f, fs, ImVec2(x, y), col, t.c_str());
    }
    // Co-op+ (2026-11-05): hide and seek, the seeker counting with eyes closed. Under every card and notice.
    if (d.veil && !game::ui_menu_open()) ui::veil(width, height, d.veil_count, d.veil_line);
    // Co-op+: the experimental opt-in, once, on the first launch of this build (optin.h). It waits for an answer;
    // the controls card waits behind it.
    static input::Edge optin_yes, optin_no;
    const bool optin_up = optin::card_visible() && opt.build_problem.empty();
    if (optin_up) {
        if (optin_yes(input::action_down(input::Action::Challenge, focus))) {
            optin::answer(true);
        } else if (optin_no(input::action_down(input::Action::Decline, focus))) {
            optin::answer(false);
        } else {
            ui::guide_card(width, height, "NEW: co-op saves + live quests  (experimental)",
                           {"Each friend you play with gets a co-op save of its own, like Elden Ring Seamless Co-op.",
                            "The first time you play together, you both begin a new journey at the start of the story.",
                            "Your own saves are never touched: leave the session and you are back in your own world.",
                            "Quest steps your friend finishes (go to, defeat, talk) finish for you too, live.",
                            "",
                            "{Challenge}   TURN IT ON",
                            "{Decline}   Not now",
                            "",
                            "Change it any time: {Panel} > Settings > \"EXPERIMENTAL: co-op saves + live quests\"."},
                           "Experimental: tell us how it goes in the Discord (#bug-reports).");
        }
        if (g_guide_since.load()) g_guide_since = GetTickCount64();  // the controls card comes after
    }
    // Co-op+ (2026-11-05): the host, once, for a co-op save made by an earlier version (a copy of their own progress):
    // a new journey for both, or keep it (coop_worlds.h).
    static input::Edge world_new, world_cont;
    const bool world_up = !optin_up && net::worlds_card_visible() && opt.build_problem.empty();
    if (world_up) {
        const std::string name = net::worlds_card_partner();
        if (world_new(input::action_down(input::Action::Challenge, focus))) {
            net::worlds_answer(true);
        } else if (world_cont(input::action_down(input::Action::Decline, focus))) {
            net::worlds_answer(false);
        } else {
            ui::guide_card(width, height, "A New Journey with " + name + "?",
                           {"Your co-op save with " + name + " was made by an earlier version of Co-op+,",
                            "as a copy of your own progress. Co-op saves now begin at the start of the story.",
                            "",
                            "{Challenge}   Begin a new journey together (the old co-op save is kept as a backup)",
                            "{Decline}   Continue the old co-op save",
                            "",
                            "Your own saves are never touched. " + name + "'s game follows your choice."},
                           "Asked once for this co-op save.");
        }
        if (g_guide_since.load()) g_guide_since = GetTickCount64();
    }
    // Co-op+ (T31): the controls card.
    static input::Edge help_edge;
    if (help_edge(input::action_down(input::Action::Help, focus)) && opt.build_problem.empty()) {
        g_guide_since = g_guide_since.load() ? 0 : GetTickCount64();
    }
    // Co-op+ (2026-11-05 playtest): switching saves only when the player agrees (coop_worlds.h).
    static input::Edge switch_go, switch_later;
    const bool switch_up = !optin_up && !world_up && net::worlds_switch_card_visible() && opt.build_problem.empty();
    if (switch_up) {
        const net::SwitchCard c = net::worlds_switch_card();
        if (switch_go(input::action_down(input::Action::Challenge, focus))) {
            net::worlds_switch_answer(true);
        } else if (switch_later(input::action_down(input::Action::Decline, focus))) {
            net::worlds_switch_answer(false);
        } else if (c.into && c.new_game) {
            ui::guide_card(width, height, "A New Journey with " + c.name,
                           {"Your adventure with " + c.name + " begins at the start of the story, for both of you,",
                            "in a co-op save of its own. Your own saves are never touched.",
                            "",
                            "{Challenge}   Begin now: return to the title screen and start the new game",
                            "{Decline}   Later: Esc > Others > Title Screen when you are ready, then New Game",
                            "",
                            "Each of you accepts the agreement and chooses a difficulty."},
                           "Nothing changes until you choose.");
        } else if (c.into) {
            ui::guide_card(width, height, "Continue Your Journey with " + c.name,
                           {"Load your co-op save with " + c.name + ". Your own saves are never touched.",
                            "",
                            "{Challenge}   Load now: return to the title screen and load it",
                            "{Decline}   Later: Esc > Others > Title Screen when you are ready, then Play"},
                           "Nothing changes until you choose.");
        } else {
            ui::guide_card(width, height, c.name + " Has Left",
                           {"Return to your own save? Your co-op save with " + c.name + " is kept for next time.",
                            "",
                            "{Challenge}   Return now: back to the title screen and your own save",
                            "{Decline}   Later: Esc > Others > Title Screen when you are ready, then Play"},
                           "Nothing changes until you choose.");
        }
        if (g_guide_since.load()) g_guide_since = GetTickCount64();
    }
    // Co-op+ (2026-11-05): the session summary once the friend is gone (session_summary.h). It waits for the other
    // cards, menus, cutscenes and loading (we must be standing in the world), then stays 15 s; {Decline} closes it.
    {
        constexpr ULONGLONG kShowMs = 15000, kFadeMs = 1500;
        static ULONGLONG shown_at = 0;
        static input::Edge close_edge;
        const net::SummaryCard sc = net::summary_card();
        const bool clear = !optin_up && !world_up && !switch_up && net::worlds_banner().empty() && d.local &&
                           !d.revive_prompt && !game::ui_menu_open() && !game::cutscene_playing().value_or(false);
        const bool close = close_edge(input::action_down(input::Action::Decline, focus));
        if (!sc.pending) {
            shown_at = 0;
        } else if (clear) {
            if (!shown_at) {
                shown_at = GetTickCount64();
                net::summary_card_shown();
            }
            const ULONGLONG age = GetTickCount64() - shown_at;
            if (age >= kShowMs || (close && age > 400)) {
                net::summary_card_done();
                shown_at = 0;
            } else {
                const float a = age < 400 ? static_cast<float>(age) / 400.0f
                                : age > kShowMs - kFadeMs ? static_cast<float>(kShowMs - age) / kFadeMs
                                                          : 1.0f;
                std::vector<ui::SummaryLine> rows;
                for (const auto& r : sc.rows) rows.push_back({r.label, r.ours, r.theirs});
                ui::summary_card(width, height, sc.title, sc.subtitle, sc.partner, rows,
                                 "Until your next journey together.        {Decline}  Close", a);
                if (g_guide_since.load()) g_guide_since = GetTickCount64();
            }
        }
    }
    // While the mod drives the game to the title and back: one centred card, the game's title-card style.
    if (const std::string b = net::worlds_banner(); !b.empty()) {
        const size_t nl = b.find('\n');
        ui::title_card(width, height, b.substr(0, nl), nl == std::string::npos ? std::string() : b.substr(nl + 1),
                       IM_COL32(232, 200, 130, 255), 1.0f);
    }
    if (const ULONGLONG since = optin_up || world_up || switch_up ? 0 : g_guide_since.load()) {
        const ULONGLONG age = GetTickCount64() - since;
        if (age > 60000 || game::ui_menu_open()) {
            g_guide_since = 0;
        } else {
            const float a = age > 58000 ? static_cast<float>(60000 - age) / 2000.0f : 1.0f;
            ui::guide_card(width, height, "Seamless Co-op+",
                           {"{Invite}  Invite a Steam friend (or they pick Join Game on you in Steam)",
                            "{Panel}  Co-op panel and settings          {Leave}  Leave a friend's world",
                            "{GoToFriend}  Go to your friend          {Summon}  Ask your friend to come to you",
                            input::connected_pad() == input::Method::Keyboard
                                ? "{Ping}  Quick messages and map pins          {Chat}  Type a chat line"
                                : "{Ping}  Quick messages (D-pad Left/Right choose) and map pins",
                            "{Challenge}  Challenge: duel, race, hide and seek, hunting, fishing, rock-paper-scissors, arm wrestling",
                            "{ReviveFriend}  Hold by a downed friend to revive          {Resync}  Resync if things look wrong",
                            "Don't save while your friend is in your world: Co-op+ backs your saves up for you."},
                           input::connected_pad() == input::Method::Keyboard
                               ? "{Help}  closes this card and opens it again any time"
                               : "Hold View / Touchpad for every co-op button.   {Help}  closes this card, opens it any time",
                           a);
        }
    }
    if (panel_visible) draw_panel(d, opt, remote_distance, cam ? std::string() : camera.last_failure(), problems);
    if (problem_fresh && !panel_visible && opt.build_problem.empty()) {
        for (size_t i = 0; i < problems.size(); ++i) {
            ui::strip(width, height * (0.10f + 0.05f * static_cast<float>(i)), problems[i]);
        }
    }
    if (!d.toast.empty()) {
        // Co-op+: notices, top centre, whatever F8 says; the game's dialog look (game_ui.h). A first line
        // ending in a line break becomes the notice's gold title.
        const auto nl = d.toast.find('\n');
        ui::band(width, height * 0.07f, nl == std::string::npos ? std::string() : d.toast.substr(0, nl),
                 nl == std::string::npos ? d.toast : d.toast.substr(nl + 1));
    }

    if (d.boss_active && d.boss_hp_max > 0) {
        // The guest gets no native boss bar: the stage that draws it (BossNameTagView2) runs only in the host's
        // world. So draw one, the game's way (game_ui.h).
        ui::boss_bar(width, height, d.boss_name.empty() ? std::string("Boss") : d.boss_name, d.boss_hp / d.boss_hp_max,
                     d.boss_cutscene ? "The host is in a cutscene: wait" : "");
    } else if (d.duel_active && d.remote && d.remote->health_max > 0) {
        // Co-op+ duel: the opponent's health, as a boss bar.
        ui::boss_bar(width, height, d.peer_name.empty() ? std::string("Your opponent") : d.peer_name,
                     d.remote->health / d.remote->health_max);
    }
    if (!d.duel_title.empty() && d.duel_title_alpha > 0.01f) {  // the duel's or the hunting contest's
        ui::title_card(width, height, d.duel_title, d.duel_subtitle, d.duel_title_color, d.duel_title_alpha);
    }

    if (!d.activity.empty()) {
        // Co-op+: the other player's cutscene, a running challenge..., under the notices.
        ui::strip(width, height * 0.175f, d.activity);
    }

    if (!d.revive_line.empty()) ui::strip(width, height * 0.74f, d.revive_line);  // Co-op+ (T15)
    if (!d.travel_line.empty()) ui::strip(width, height * 0.23f, d.travel_line);  // Co-op+ (T19)
    if (!d.chat_input.empty()) ui::strip(width, height * 0.82f, d.chat_input);    // Co-op+ (T24)
    if (!d.chat_quick.empty()) ui::strip(width, height * 0.82f, d.chat_quick);
    if (!d.chat_log.empty() && !game::ui_menu_open()) ui::chat_log(width, height, d.chat_log);
    if (!d.kill_feed.empty() && !game::ui_menu_open()) ui::kill_feed(width, height, d.kill_feed, d.kill_feed_ours);
    if (d.revive_prompt) {
        // Co-op+: the game's own death screen, redrawn (revive_prompt.h): same words, same buttons.
        const char* choices = d.revive_prompt == 2 ? "Your partner is still fighting the boss. Hold on..."
                              : d.revive_prompt == 3 ? "Everyone is down.      {ReviveCheckpoint} Revive at Checkpoint"
                                                     : "{ReviveNow} Instant Revive (30% Health)        "
                                                       "{ReviveCheckpoint} Revive at Checkpoint";
        ui::death_screen(width, height, "Death has found you.", "Return to your path?", choices);
    }

    // Co-op+ (T14): the partner frame, whenever connected (not over a full-screen menu or a cutscene).
    if (d.connected && d.remote && !panel_visible && !game::ui_menu_open() && !game::cutscene_playing().value_or(false) &&
        !d.revive_prompt && !d.veil) {
        const float hp = d.remote->health_max > 0 ? d.remote->health / d.remote->health_max : -1.0f;
        const float st = d.remote->stamina_max > 0 ? d.remote->stamina / d.remote->stamina_max : -1.0f;
        std::string status = d.partner_in_menu ? std::string("in a menu") : std::string();
        if (d.hide_partner) {
            status = "hiding";  // hide and seek: no distance to give them away
        } else if (d.local) {
            const std::string dist = std::format("{:.0f} m", remote_distance);
            status = status.empty() ? dist : status + "  -  " + dist;
        }
        std::string frame_name = d.peer_name.empty() ? std::string("Your friend") : d.peer_name;
        if (d.peer_title.find_first_not_of(' ') != std::string::npos) frame_name += ", " + d.peer_title;  // T43
        ui::party_frame(width, height, frame_name, hp, st, status,
                        ImGui::GetTime());
    }

    // ---- world markers (not over a full-screen menu: they showed on top of the map)
    if (!cam || !cam_world || game::ui_menu_open()) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // A ring of gold on the ground with posts: the duel ground (coop_duel.h) and the hiding area (coop_hideseek.h);
    // red while we are out. A big ring gets more segments and taller posts (the ground rises and falls under it).
    auto ground_ring = [&](const proto::Vec3& centre, float radius, bool out) {
        const ImU32 ring = out ? IM_COL32(210, 60, 45, 210) : IM_COL32(232, 200, 130, 170);
        const int segments = std::clamp(static_cast<int>(radius * 2.0f), 96, 256) / 8 * 8;
        const float post_h = radius > 40.0f ? 3.0f : 1.6f;
        std::optional<game::ScreenPoint> prev;
        for (int i = 0; i <= segments; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
            const proto::Vec3 p{centre.x + radius * std::cos(a), centre.y + 0.08f, centre.z + radius * std::sin(a)};
            auto sp = game::project(*cam, *cam_world, p, width, height);
            if (sp && prev && (i % 2 == 0)) {
                dl->AddLine(ImVec2(prev->x, prev->y), ImVec2(sp->x, sp->y), IM_COL32(0, 0, 0, 120), 5.0f);
                dl->AddLine(ImVec2(prev->x, prev->y), ImVec2(sp->x, sp->y), ring, 2.5f);
            }
            if (sp && i % 8 == 0 && i < segments) {
                const proto::Vec3 top{p.x, p.y + post_h, p.z};
                if (auto st = game::project(*cam, *cam_world, top, width, height)) {
                    dl->AddLine(ImVec2(sp->x, sp->y), ImVec2(st->x, st->y), IM_COL32(0, 0, 0, 110), 4.0f);
                    dl->AddLine(ImVec2(sp->x, sp->y), ImVec2(st->x, st->y), ring, 2.0f);
                    dl->AddCircleFilled(ImVec2(st->x, st->y), 3.5f, ring);
                }
            }
            prev = sp;
        }
    };
    if (d.duel_active && d.duel_radius > 0) ground_ring(d.duel_centre, d.duel_radius, d.duel_out_left_s >= 0);
    if (d.area_active && d.area_radius > 0) ground_ring(d.area_centre, d.area_radius, d.area_outside);

    if (d.remote && plates_visible && !d.hide_partner && nameplate::alpha(remote_distance) > 0.0f) {
        const proto::Vec3 head{d.remote->pos.x, d.remote->pos.y + kHeadHeight, d.remote->pos.z};
        if (auto p = game::project(*cam, *cam_world, head, width, height)) {
            draw_remote_marker(dl, *p, d, remote_distance);
        }
    }

    // B16b (user 2026-11-09): the player interaction menu -- hold R on the other player, the game's hold prompt (the
    // R in a ring that fills), as at shopkeepers; full after kHoldRingMs it opens the challenges (later trading too).
    // Within 3 m, the other player near the middle of the screen, our game window in front.
    {
        constexpr ULONGLONG kHoldRingMs = 800;
        static ULONGLONG hold_since = 0;
        static bool fired = false;
        DWORD fg_pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &fg_pid);
        const bool ours = fg_pid == GetCurrentProcessId();
        // A pad (2026-11-09): the co-op chord + D-pad Up (the Challenge action) held fills the same ring.
        const bool on_pad = input::method() != input::Method::Keyboard;
        const bool r_down = ours && ((GetAsyncKeyState('R') & 0x8000) != 0 ||
                                     (on_pad && input::action_down(input::Action::Challenge, true)));
        bool shown = false;
        std::string menu_t;
        std::vector<std::string> menu_i;
        size_t menu_p = 0;
        const bool menu_open = net::activity_menu_snapshot(menu_t, menu_i, menu_p);
        if (!menu_open && d.remote && !d.hide_partner && remote_distance <= 3.0f) {
            const proto::Vec3 chest{d.remote->pos.x, d.remote->pos.y + 1.1f, d.remote->pos.z};
            if (auto p = game::project(*cam, *cam_world, chest, width, height)) {
                if (std::fabs(p->x - width * 0.5f) < width * 0.22f && std::fabs(p->y - height * 0.5f) < height * 0.32f) {
                    shown = true;
                    float t = 0.0f;
                    const ULONGLONG now_ms = GetTickCount64();
                    if (r_down && !fired) {
                        if (!hold_since) hold_since = now_ms;
                        t = static_cast<float>(now_ms - hold_since) / static_cast<float>(kHoldRingMs);
                        if (t >= 1.0f) {
                            fired = true;
                            net::activity_request_menu();
                            log::info("Hud: the hold-R prompt on the other player opened the interaction menu");
                        }
                    }
                    if (fired) t = 1.0f;
                    if (on_pad) ui::hold_prompt(dl, p->x + 48.0f, p->y, t, "", "{Challenge} Interact");
                    else ui::hold_prompt(dl, p->x + 48.0f, p->y, t, "R", "Interact");
                }
            }
        }
        if (!r_down || !shown) {
            hold_since = 0;
            if (!r_down) fired = false;
        }
        net::activity_set_hold_prompt(shown);
    }

    // B16b: the open player interaction menu (net::activity_menu_snapshot), the shop menu's look.
    {
        std::string title;
        std::vector<std::string> items;
        size_t pick = 0;
        if (net::activity_menu_snapshot(title, items, pick)) {
            ui::choice_menu(ImGui::GetForegroundDrawList(), static_cast<float>(width), static_cast<float>(height),
                            d.peer_name.empty() ? std::string("Your friend") : d.peer_name, title, items, pick);
        }
    }

    // Co-op+ rock-paper-scissors duel: each player's hand over their character, as the town game shows the child's.
    if (d.rps_on) {
        constexpr const char* kHandName[3] = {"Scissors", "Paper", "Rock"};
        const bool reveal = d.rps_reveal_t >= 0;
        // A short "pop" as the hands come out.
        const float pop = reveal ? std::min(1.0f, 0.6f + d.rps_reveal_t * 2.0f) : 1.0f;
        auto medal = [&](const proto::Vec3& feet, int hand, bool picked, bool won, const std::string& who) {
            const proto::Vec3 over{feet.x, feet.y + 2.75f, feet.z};  // above the name tag
            auto p = game::project(*cam, *cam_world, over, width, height);
            if (!p) return;
            const float size = std::clamp(520.0f / std::max(p->depth, 1.0f), 34.0f, 78.0f) * pop;
            const int shown = reveal ? hand : picked ? -2 : -1;
            const ImU32 col = reveal && won ? IM_COL32(232, 200, 130, 255)
                              : reveal && d.rps_winner != 0 ? IM_COL32(170, 160, 150, 255)
                                                            : IM_COL32(222, 214, 200, 255);
            const std::string caption = reveal && hand >= 0 ? kHandName[hand] : picked ? who + " has chosen" : who;
            ui::hand_medallion(dl, p->x, p->y - size * 0.5f, size, shown, caption, col);
        };
        if (d.remote) {
            medal(d.remote->pos, d.rps_theirs, d.rps_theirs_picked, d.rps_winner == 2,
                  d.peer_name.empty() ? std::string("Your friend") : d.peer_name);
        }
        if (d.local) medal(d.local->position, d.rps_ours, d.rps_ours_picked, d.rps_winner == 1, "You");
    }

    std::optional<game::ScreenPoint> self;
    if (d.local) {
        const proto::Vec3 chest{d.local->position.x, d.local->position.y + 1.2f,
                                d.local->position.z};
        self = game::project(*cam, *cam_world, chest, width, height);
        if (opt.self_marker && self) {
            const ImVec2 c(self->x, self->y);
            dl->AddLine(ImVec2(c.x - 8, c.y), ImVec2(c.x + 8, c.y), IM_COL32(80, 220, 255, 255), 2);
            dl->AddLine(ImVec2(c.x, c.y - 8), ImVec2(c.x, c.y + 8), IM_COL32(80, 220, 255, 255), 2);
        }
    }

    // Numbers that let us verify the projection from the log alone: in third
    // person our own chest should land near the horizontal centre.
    const ULONGLONG now = GetTickCount64();
    if (now - last_diag >= 5000 && d.local) {
        last_diag = now;
        const auto st = status();
        log::info("Overlay diag: frames {} | cam {} space, axes {} | cam->player {:.2f} m | "
                  "self on screen {} | fov {:.1f} deg",
                  st.frames_drawn, cam_is_local ? "chunk-local" : "world",
                  cam->axes_from_view ? "from view matrix" : "fallback (cross)",
                  math::length(math::sub(*cam_world, d.local->position)),
                  self ? std::format("({:.0f}, {:.0f}) of {:.0f}x{:.0f}, depth {:.2f}", self->x,
                                     self->y, width, height, self->depth)
                       : std::string("behind camera"),
                  2.0f * std::atan(1.0f / cam->p11) * 57.2958f);
    }
}

}  // namespace cdmp::render
