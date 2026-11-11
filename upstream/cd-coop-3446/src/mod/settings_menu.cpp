#include "mod/settings_menu.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
#include <functional>
#include <mutex>

#include "mod/input/actions.h"
#include "mod/log.h"
#include "mod/native_layer.h"
#include "mod/net/coop_lobby.h"
#include "mod/net/coop_revive.h"
#include "mod/net/guest_hits.h"
#include "mod/net/loot_twin.h"
#include "mod/net/party_scaling.h"
#include "mod/net/party_quests.h"
#include "mod/net/remote_puppet.h"
#include "mod/net/stage_cinema.h"
#include "mod/net/story_sync.h"
#include "mod/net/revive_prompt.h"
#include "mod/net/weather_sync.h"
#include "mod/net/slowmo.h"
#include "mod/optin.h"

namespace cdmp::settings {
namespace {

struct Item {
    const char* label;
    const wchar_t* section;
    const wchar_t* key;
    std::vector<std::string> shown;    // what the panel says
    std::vector<std::wstring> stored;  // what cdmp.ini gets
    int index = 0;
    bool live = true;                  // applied now (else at the next start)
    std::function<void(int)> apply;
    std::function<int()> current;      // Co-op+: the value lives elsewhere (no ini key: section nullptr)
    std::string category = "General";  // Co-op+: the Co-op page's section (render/coop_page.h)
    std::string help;                  // its one-line explanation there
};

// Co-op+ (the Co-op page): each setting's section and explanation, by its label.
struct Tag {
    const char* label;
    const char* category;
    const char* help;
};
constexpr Tag kTags[] = {
    {"EXPERIMENTAL: co-op saves + live quests", "Saves & Story",
     "A separate save for every friend you play with (your own saves are never touched), and quest steps you both "
     "have finish for both. Off: co-op plays in your own save."},
    {"Who can join", "Session",
     "Any Steam friend can join you from Steam (Join Game), or only friends you invite."},
    {"Loot of shared kills", "Combat & Loot",
     "Who loots an enemy you fought together: each of you your own drop, only the host, or whoever gets there first."},
    {"Guest finishes the host's enemies", "Combat & Loot",
     "Off: the guest's blows stop at 1 HP, so the host lands the final blow (keeps story fights in order)."},
    {"Tougher enemies with a guest", "Combat & Loot",
     "Enemies near the host get more health while a guest is in the world."},
    {"  Enemy health with a guest", "Combat & Loot", "How much more health enemies get with a guest."},
    {"  Boss health with a guest", "Combat & Loot", "How much more health bosses get with a guest."},
    {"Revive each other", "Reviving", "Hold the revive button by your fallen friend to bring them back."},
    {"After a checkpoint revive", "Reviving",
     "Where you stand after reviving at a checkpoint: there, or back beside your friend."},
    {"Wait for my friend if I die at a boss", "Reviving",
     "Falling in a boss fight while your friend still fights: wait for them instead of reviving at once."},
    {"Weather follows the host", "World", "The guest's sky, rain and time of day follow the host's."},
    {"Slow motion (Focus) in co-op", "World",
     "Off: Focus does not slow the world while you play together, so both worlds keep the same speed."},
    {"Ping and packet loss on screen", "Display", "A small connection readout in the top right corner."},
    {"My friend's story (scenes, quest names)", "Saves & Story",
     "Hidden: no spoilers from a friend further in the story (their quest names and story scenes)."},
    {"Copy my friend's story into my save (guest)", "Saves & Story",
     "As a guest: take the host's story progress into your co-op save with them."},
    {"Let my guest copy my story (host)", "Saves & Story", "As the host: let your guest take your story progress."},
    {"Duel ground (when I place it)", "Challenges", "How large the ring is when you start a duel."},
    {"Player colours", "Display", "The colours of the host and the guest on name tags, the map and the party frame."},
    {"My friend's map pin colour", "Display", "The colour of your friend's pin on the map and minimap."},
    {"Name over my friend (Ctrl+F8 too)", "Display", "Your friend's name and health over their head."},
};

std::mutex g_mutex;
std::wstring g_ini;
std::vector<Item> g_items;
int g_sel = 0;
std::atomic<bool> g_open{false};
std::atomic<bool> g_respawn_partner{false};
std::atomic<bool> g_net_hud{false};
std::atomic<bool> g_colourblind{false};
std::atomic<float> g_ring{20.0f};
float g_health = 1.5f, g_boss = 1.7f;

std::vector<std::string> on_off() { return {"on", "off"}; }
std::vector<std::wstring> one_zero() { return {L"1", L"0"}; }

// 1.0 .. 3.0 in tenths.
Item multiplier(const char* label, const wchar_t* key, float value, std::function<void(int)> apply) {
    Item it{label, L"scaling", key, {}, {}, 0, true, std::move(apply)};
    for (int i = 10; i <= 30; ++i) {
        it.shown.push_back(std::format("x{:.1f}", i / 10.0));
        it.stored.push_back(std::format(L"{:.1f}", i / 10.0));
    }
    it.index = std::clamp(static_cast<int>(std::lround(value * 10.0f)) - 10, 0, 20);
    return it;
}

void change(int delta) {
    std::lock_guard lock(g_mutex);
    if (g_items.empty()) return;
    Item& it = g_items[g_sel];
    const int n = static_cast<int>(it.shown.size());
    if (it.current) it.index = it.current();
    it.index = (it.index + delta % n + n) % n;
    if (it.section) WritePrivateProfileStringW(it.section, it.key, it.stored[it.index].c_str(), g_ini.c_str());
    if (it.live && it.apply) it.apply(it.index);
    log::info("Settings: {} = {}{}", it.label, it.shown[it.index], it.live ? "" : " (next start)");
}

}  // namespace

void init(const std::wstring& ini_path, const Config& cfg, float health_2p, float boss_health_2p) {
    std::lock_guard lock(g_mutex);
    g_ini = ini_path;
    g_health = health_2p;
    g_boss = boss_health_2p;
    g_respawn_partner = cfg.coop_respawn_at_partner;
    g_items.clear();
    {
        Item x{"EXPERIMENTAL: co-op saves + live quests", nullptr, nullptr, {"on", "off (on = a save per friend)"}, {},
               1, true, [](int i) { optin::answer(i == 0); }};
        x.current = [] { return optin::panel_index(); };
        g_items.push_back(std::move(x));
    }
    g_items.push_back({"Who can join", L"net", L"join", {"any Steam friend", "only friends I invite"},
                       {L"friends", L"invite"}, cfg.net_join_invite_only ? 1 : 0, false, nullptr});
    g_items.push_back({"Loot of shared kills", L"coop", L"loot_mode", {"each their own", "host only", "first come"},
                       {L"per_player", L"host_only", L"first_come"}, cfg.coop_loot_mode, true,
                       [](int i) { net::loot_set_mode(i); }});
    g_items.push_back({"Guest finishes the host's enemies", L"coop", L"guest_finishes", on_off(), one_zero(),
                       cfg.coop_guest_finishes ? 0 : 1, true, [](int i) { net::guest_hits_set_finishes(i == 0); }});
    g_items.push_back({"Revive each other", L"coop", L"teammate_revive", on_off(), one_zero(),
                       cfg.coop_teammate_revive ? 0 : 1, true,
                       [dev = cfg.dev_revive_test](int i) { net::revive_partner_set(i == 0, dev); }});
    g_items.push_back({"Shared quest credit (same step: done for both)", L"coop", L"shared_quest_credit", on_off(),
                       one_zero(), cfg.coop_shared_quest_credit ? 0 : 1, true,
                       [](int i) { net::lobby_set_shared_quest_credit(i == 0); }});
    g_items.push_back({"Items in the world", L"coop", L"world_items", {"each their own (Lootr)", "first come"},
                       {L"lootr", L"first_come"}, cfg.coop_world_items_lootr ? 0 : 1, true,
                       [](int i) { net::lobby_set_world_items_lootr(i == 0); }});
    g_items.push_back({"After a checkpoint revive", L"coop", L"respawn_at", {"stay at the checkpoint", "back to my friend"},
                       {L"checkpoint", L"partner"}, cfg.coop_respawn_at_partner ? 1 : 0, true,
                       [](int i) { g_respawn_partner = i == 1; }});
    g_items.push_back({"Wait for my friend if I die at a boss", L"coop", L"boss_death_wait", on_off(), one_zero(),
                       cfg.coop_boss_death_wait ? 0 : 1, true, [](int i) { net::revive_set_boss_wait(i == 0); }});
    g_items.push_back({"Weather follows the host", L"coop", L"weather_sync", on_off(), one_zero(),
                       cfg.coop_weather_sync ? 0 : 1, true, [](int i) { net::weather_set_enabled(i == 0); }});
    g_items.push_back({"Slow motion (Focus) in co-op", L"coop", L"block_slowmo",
                       {"off: both worlds keep the same speed", "on: as in single player"}, one_zero(),
                       cfg.coop_block_slowmo ? 0 : 1, true, [](int i) { net::slowmo_set_block(i == 0); }});
    g_items.push_back({"Tougher enemies with a guest", L"scaling", L"enabled", on_off(), one_zero(),
                       cfg.scaling_enabled ? 0 : 1, true, [](int i) { net::scaling_set_enabled(i == 0); }});
    // T40: the 2-player multipliers.
    g_items.push_back(multiplier("  Enemy health with a guest", L"panel_health_2p", health_2p, [](int i) {
        g_health = (i + 10) / 10.0f;
        net::scaling_set_2p(g_health, g_boss);
    }));
    g_items.push_back(multiplier("  Boss health with a guest", L"panel_boss_health_2p", boss_health_2p, [](int i) {
        g_boss = (i + 10) / 10.0f;
        net::scaling_set_2p(g_health, g_boss);
    }));
    g_net_hud = GetPrivateProfileIntW(L"overlay", L"net_hud", 0, ini_path.c_str()) != 0;
    g_items.push_back({"Ping and packet loss on screen", L"overlay", L"net_hud", on_off(), one_zero(),
                       g_net_hud ? 0 : 1, true, [](int i) { g_net_hud = i == 0; }});
    // T39: story spoilers.
    {
        const bool guard = GetPrivateProfileIntW(L"coop", L"spoiler_guard", 0, ini_path.c_str()) != 0;
        net::cinema_set_spoiler_guard(guard);
        net::party_quests_set_spoiler_guard(guard);
        g_items.push_back({"My friend's story (scenes, quest names)", L"coop", L"spoiler_guard",
                           {"shown", "hidden: no spoilers"}, {L"0", L"1"}, guard ? 1 : 0, true, [](int i) {
                               net::cinema_set_spoiler_guard(i == 1);
                               net::party_quests_set_spoiler_guard(i == 1);
                           }});
    }
    // T42: the duel ground.
    {
        const int ring = GetPrivateProfileIntW(L"coop", L"duel_ring_m", 20, ini_path.c_str());
        const int idx = ring <= 12 ? 0 : ring >= 35 ? 2 : 1;
        constexpr float kRings[] = {12.0f, 20.0f, 35.0f};
        g_ring = kRings[idx];
        g_items.push_back({"Duel ground (when I place it)", L"coop", L"duel_ring_m", {"small, 12 m", "normal, 20 m", "large, 35 m"},
                           {L"12", L"20", L"35"}, idx, true, [](int i) {
                               constexpr float r[] = {12.0f, 20.0f, 35.0f};
                               g_ring = r[i];
                           }});
    }
    // T34: colours.
    g_colourblind = GetPrivateProfileIntW(L"overlay", L"colourblind", 0, ini_path.c_str()) != 0;
    // Co-op+ story sync (story_sync.h): the guest asks; the host decides whether it shares.
    {
        Item copy{"Copy my friend's story into my save (guest)", nullptr, nullptr, {"no", "yes"}, {}, 0, true,
                  [](int i) { net::story_set_wanted(i == 1); }};
        copy.current = [] { return net::story_wanted_index(); };
        g_items.push_back(std::move(copy));
    }
    g_items.push_back({"Let my guest copy my story (host)", L"coop", L"share_story", on_off(), one_zero(),
                       cfg.coop_share_story ? 0 : 1, true, [](int i) { net::story_set_share(i == 0); }});
    g_items.push_back({"Player colours", L"overlay", L"colourblind", {"gold host, blue guest", "colour-blind safe"},
                       {L"0", L"1"}, g_colourblind ? 1 : 0, true, [](int i) { g_colourblind = i == 1; }});
    {
        Item pin{"My friend's map pin colour", L"coop", L"map_pin_colour", {}, {}, cfg.coop_map_pin_colour, true,
                 [shape = cfg.coop_map_pin_shape](int i) { net::puppet_map_pin_style(shape, static_cast<uint8_t>(i)); }};
        for (int i = 0; i < 15; ++i) {
            pin.shown.push_back(i == 14 ? "15 (the game's)" : std::to_string(i + 1));
            pin.stored.push_back(std::to_wstring(i));
        }
        g_items.push_back(std::move(pin));
    }
    g_items.push_back({"Name over my friend (Ctrl+F8 too)", L"overlay", L"nameplates", on_off(), one_zero(),
                       cfg.overlay_nameplates ? 0 : 1, false, nullptr});
    for (Item& it : g_items) {
        for (const Tag& t : kTags) {
            if (std::string_view(it.label) == t.label) {
                it.category = t.category;
                it.help = t.help;
            }
        }
    }
    if (kNativeLayer) {
        // On the native base (native_layer.h) the server shares the world: what these chose is its job now (loot,
        // kills, Focus via the native ui_pause) or no longer exists (co-op saves, story copies).
        const std::string_view hidden[] = {"EXPERIMENTAL: co-op saves + live quests", "Loot of shared kills",
                                           "Guest finishes the host's enemies", "Slow motion (Focus) in co-op",
                                           "Copy my friend's story into my save (guest)", "Let my guest copy my story (host)",
                                           // party scaling works through the sniffer (VaryHPAck / VaryStatCheatReq): off
                                           "Tougher enemies with a guest", "  Enemy health with a guest",
                                           "  Boss health with a guest"};
        std::erase_if(g_items, [&](const Item& it) {
            return std::find(std::begin(hidden), std::end(hidden), std::string_view(it.label)) != std::end(hidden);
        });
    }
}

void set_panel_open(bool open) { g_open = open; }
bool panel_open() { return g_open.load(); }

bool hook_key(unsigned vk, bool down) {
    if (!g_open.load()) return false;
    if (vk != VK_UP && vk != VK_DOWN && vk != VK_LEFT && vk != VK_RIGHT) return false;
    if (down) step(vk == VK_LEFT ? -1 : vk == VK_RIGHT ? 1 : 0, vk == VK_UP ? -1 : vk == VK_DOWN ? 1 : 0);
    return true;
}

void step(int dx, int dy) {
    if (dy) {
        std::lock_guard lock(g_mutex);
        const int n = static_cast<int>(g_items.size());
        if (n) g_sel = (g_sel + dy % n + n) % n;
    }
    if (dx) change(dx);
}

std::vector<Line> lines() {
    std::vector<Line> out;
    std::lock_guard lock(g_mutex);
    out.push_back({"Settings: Up/Down choose, Left/Right change (pad: D-pad)", false});
    for (int i = 0; i < static_cast<int>(g_items.size()); ++i) {
        const Item& it = g_items[i];
        const int idx = it.current ? it.current() : it.index;
        out.push_back({std::format("{}{}: {}{}", i == g_sel ? "> " : "  ", it.label, it.shown[idx],
                                   it.live ? "" : "  (next start)"),
                       i == g_sel});
    }
    out.push_back({kNativeLayer ? "Keys: change them under [keys] in coopplus.ini, e.g. Invite=Ctrl+F1 (README)"
                               : "Keys: change them under [keys] in cdmp.ini, e.g. Invite=Ctrl+F1 (README)",
                  false});
    return out;
}

std::vector<PageRow> page_rows(const std::string& category) {
    std::vector<PageRow> out;
    std::lock_guard lock(g_mutex);
    for (const Item& it : g_items) {
        if (it.category != category) continue;
        const int idx = it.current ? it.current() : it.index;
        std::string label = it.label;
        while (!label.empty() && label.front() == ' ') label.erase(label.begin());
        out.push_back({label, it.shown[idx], it.help, it.live, static_cast<int>(it.shown.size())});
    }
    return out;
}

void page_change(const std::string& category, int row, int delta) {
    int found = -1;
    {
        std::lock_guard lock(g_mutex);
        int r = 0;
        for (int i = 0; i < static_cast<int>(g_items.size()); ++i) {
            if (g_items[i].category != category) continue;
            if (r++ == row) found = i;
        }
        if (found < 0) return;
        g_sel = found;
    }
    change(delta);
}

bool respawn_at_partner() { return g_respawn_partner.load(); }
bool net_hud() { return g_net_hud.load(); }
bool colourblind() { return g_colourblind.load(); }
float duel_ring_m() { return g_ring.load(); }

}  // namespace cdmp::settings
