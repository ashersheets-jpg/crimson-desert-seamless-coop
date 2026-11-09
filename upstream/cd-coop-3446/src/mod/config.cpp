#include "mod/config.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace cdmp {
namespace {

constexpr wchar_t kDefaultIni[] =
    L"; Crimson Desert Multiplayer (cdmp) settings\r\n"
    L"[general]\r\n"
    L"player_name=Player\r\n"
    L"; Wait before starting, in milliseconds\r\n"
    L"startup_delay_ms=0\r\n"
    L"\r\n"
    L"[net]\r\n"
    L"enabled=1\r\n"
    L"; steam = play with a Steam friend; direct = straight to a friend over Radmin VPN / LAN / any VPN\r\n"
    L";   (peer_host = your FRIEND's VPN address on both PCs, role = host or guest); udp = tests on this PC\r\n"
    L"transport=udp\r\n"
    L"; guest connects to the host or to cdmp_peer; host waits for one player\r\n"
    L"; auto (Steam only): host your own world, and become a guest when you accept a\r\n"
    L";      Steam invite or press \"Join Game\" on a friend (Co-op+)\r\n"
    L"role=guest\r\n"
    L"; Co-op+: who may join you when you host. friends = any Steam friend playing\r\n"
    L"; (\"Join Game\"); invite = only friends you invite with Ctrl+F1\r\n"
    L"join=friends\r\n"
    L"; UDP: address of the host or of cdmp_peer.exe (a UDP host listens on peer_port)\r\n"
    L"peer_host=127.0.0.1\r\n"
    L"peer_port=37015\r\n"
    L"send_rate_hz=20\r\n"
    L"\r\n"
    L"[steam]\r\n"
    L"; Guest: the host's SteamID64 (empty = the only friend playing Crimson Desert)\r\n"
    L"; Host: accept only this friend (empty = any friend playing)\r\n"
    L"peer_steamid=\r\n"
    L"\r\n"
    L"[room]\r\n"    L"; Short room code: type your friend's code here to enter their world.\r\n"
    L"; HOST: leave it empty -- the mod creates a Steam lobby and shows the code\r\n"
    L";       in the panel (F8) and in cdmp.log. Read it out to your friend.\r\n"
    L"; GUEST: put the host's code here. Lower case and dashes are fine.\r\n"
    L"; Everything goes through Steam: no server of ours, no ports, no IP.\r\n"
    L"code=\r\n"
    L"\r\n"
    L"[overlay]\r\n"
    L"; 0 turns off the in-game overlay completely (no graphics hooks)\r\n"
    L"enabled=1\r\n"
    L"; Draw a cross on your own character (to check the projection)\r\n"
    L"self_marker=1\r\n"
    L"; Language of the panel's texts: en | es\r\n"
    L"language=en\r\n"
    L"; Co-op+: the other player's name over their head (Ctrl+F8 toggles it in game)\r\n"
    L"nameplates=1\r\n"
    L"\r\n"
    L"[sniffer]\r\n"
    L"; Needed for co-op (reads and sends the game's internal messages)\r\n"
    L"enabled=1\r\n"
    L"; 1 also records them to cdmp_packets_* files (50-100 MB per session), to report a problem\r\n"
    L"record=0\r\n"
    L"\r\n"
    L"[coop]\r\n"
    L"; 1 shows the other player (through cdmp_peer) as a character in your world\r\n"
    L"enabled=0\r\n"
    L"; How soon after the network your friend is drawn, at least (ms). It grows by itself on a jumpy connection;\r\n"
    L"; raise it only if your friend's character stutters\r\n"
    L"buffer_ms=10\r\n"
    L"; Sideways offset of the other player in metres (0 = exact position)\r\n"
    L"offset_m=0\r\n"
    L"; The enemy relay: the host's enemies appear as copies in the other player's\r\n"
    L"; world. It is also the ONLY thing that tells the mod which creature a copy is,\r\n"
    L"; so shared loot cannot work without it -- with this at 0 it silently never\r\n"
    L"; fires. The mod logs a warning if you set guest_loot=1 but leave this at 0.\r\n"
    L"enemy_auto=1\r\n"
    L"; Our blows on the other player's enemies: when you hit a copy of one of the\r\n"
    L"; host's enemies, the blow lands on the real enemy in the host's world. This is\r\n"
    L"; also what marks you as having taken part in the kill, which is the condition\r\n"
    L"; shared loot fires on: a creature you never touched gives you nothing.\r\n"
    L"guest_hits=0\r\n"
    L"; The stagger reaction on those blows (the host's enemies reel from the guest's hits as\r\n"
    L"; from the host's). 0 = damage only.\r\n"
    L"guest_hit_stagger=1\r\n"
    L"; An older, blunter way to hurt the host's enemies (send the damage number).\r\n"
    L"; It and guest_hits would both hurt the same enemy, so guest_hits wins; the mod\r\n"
    L"; says so in the log if you set both.\r\n"
    L"damage=0\r\n"
    L"; Boss safety. A boss fight here is a scripted stage that the HOST's client\r\n"
    L"; drives: it owns the phases, the cinematics and the battle end. The boss\r\n"
    L"; exists only in the host's world, so a guest's killing blow kills it outside\r\n"
    L"; that script -- no phase, no final cinematic, no battle end, and the host's\r\n"
    L"; quest is left stuck behind the arena barrier.\r\n"
    L"; 1 (default): the guest still does damage and so still helps, but never lands\r\n"
    L";              the killing blow, never staggers a boss, and nothing lands\r\n"
    L";              while the host is inside a stage cinematic.\r\n"
    L"; 0: the old behaviour, for comparison. Only with a save backup.\r\n"
    L"boss_safe=1\r\n"
    L"; Co-op+: dying during a boss fight. 1 (default): you wait until your partner beats the\r\n"
    L";   boss (then you get up) or is down too (then you both revive at the checkpoint).\r\n"
    L";   0: revive as usual.\r\n"
    L"boss_death_wait=1\r\n"
    L"; Shared loot (GUEST only). When you help the host kill one of their enemies,\r\n"
    L"; your own world was never going to give you anything for it: the creature only\r\n"
    L"; really exists in the host's world. With this on, your server makes its own\r\n"
    L"; copy of that creature once the copy you helped kill goes down, kills it here,\r\n"
    L"; and lets you loot it normally -- so each of you takes their own drop from the\r\n"
    L"; same kill, and your own quests and XP are untouched.\r\n"
    L"; It only ever fires for an enemy you actually helped kill, close to you\r\n"
    L"; (30 m), and rate-limited. Hosts do not need it (it is the host's own world).\r\n"
    L"guest_loot=0\r\n"
    L"; Co-op+: copy your saves to Documents\\CrimsonDesert-CoopPlus-SaveBackups when a\r\n"
    L"; co-op session starts, and warn on the panel if the game saves during one.\r\n"
    L"save_guard=1\r\n"
    L"\r\n"
    L"[scaling]\r\n"
    L"; Co-op+: with a guest in the HOST's world, the host's enemies are tougher:\r\n"
    L"; they take 1/health_2p of every blow (same as health_2p times the health).\r\n"
    L"; It heals enemies back by part of each blow (bosses: config\\scaling.toml, up to 4 players).\r\n"
    L"; 0 = off.\r\n"
    L"enabled=1\r\n"
    L"health_2p=1.7\r\n";

std::wstring read_string(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                         const std::string& fallback) {
    wchar_t buf[256];
    const std::wstring def(fallback.begin(), fallback.end());
    GetPrivateProfileStringW(section, key, def.c_str(), buf, 256, path.c_str());
    return buf;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr,
                                      0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr,
                        nullptr);
    return s;
}

uint32_t read_uint(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                   uint32_t fallback) {
    return GetPrivateProfileIntW(section, key, static_cast<INT>(fallback), path.c_str());
}

// A distance in metres, within +-50; anything else keeps the fallback.
float read_metres(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                  float fallback) {
    const std::wstring s = read_string(path, section, key, "");
    if (s.empty()) return fallback;
    wchar_t* end = nullptr;
    const float v = std::wcstof(s.c_str(), &end);
    return end != s.c_str() && std::isfinite(v) && std::fabs(v) <= 50.0f ? v : fallback;
}

// A number within [lo, hi]; anything else keeps the fallback.
float read_float(const std::wstring& path, const wchar_t* section, const wchar_t* key, float fallback, float lo,
                 float hi) {
    const std::wstring s = read_string(path, section, key, "");
    if (s.empty()) return fallback;
    wchar_t* end = nullptr;
    const float v = std::wcstof(s.c_str(), &end);
    return end != s.c_str() && std::isfinite(v) && v >= lo && v <= hi ? v : fallback;
}

}  // namespace

Config load_config(const std::wstring& ini_path) {
    if (GetFileAttributesW(ini_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        HANDLE h = CreateFileW(ini_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            // UTF-16 BOM so GetPrivateProfileStringW reads it as Unicode.
            const wchar_t bom = 0xFEFF;
            DWORD written = 0;
            WriteFile(h, &bom, sizeof bom, &written, nullptr);
            WriteFile(h, kDefaultIni, sizeof kDefaultIni - sizeof(wchar_t), &written, nullptr);
            CloseHandle(h);
        }
    }

    Config c;
    c.player_name = narrow(read_string(ini_path, L"general", L"player_name", c.player_name));
    c.startup_delay_ms = read_uint(ini_path, L"general", L"startup_delay_ms", c.startup_delay_ms);
    c.net_enabled = read_uint(ini_path, L"net", L"enabled", 1) != 0;
    c.peer_host = narrow(read_string(ini_path, L"net", L"peer_host", c.peer_host));
    c.peer_port = static_cast<uint16_t>(read_uint(ini_path, L"net", L"peer_port", c.peer_port));
    c.send_rate_hz = std::clamp(read_uint(ini_path, L"net", L"send_rate_hz", c.send_rate_hz), 1u, 60u);
    {
        const std::wstring transport = read_string(ini_path, L"net", L"transport", "udp");
        c.net_transport_steam = transport == L"steam";
        c.net_transport_direct = transport == L"direct" || transport == L"radmin" || transport == L"lan";
        c.bind_host = narrow(read_string(ini_path, L"net", L"bind_host", ""));
    }
    {
        const std::wstring role = read_string(ini_path, L"net", L"role", "guest");
        c.net_role_auto = role == L"auto";
        c.net_role_host = role == L"host" || c.net_role_auto;  // auto starts as the host
        c.net_join_invite_only = read_string(ini_path, L"net", L"join", "friends") == L"invite";
    }
    c.steam_peer_id = _wcstoui64(read_string(ini_path, L"steam", L"peer_steamid", "").c_str(), nullptr, 10);
    c.steam_channel = static_cast<int>(read_uint(ini_path, L"steam", L"channel", 67));
    c.room_code = narrow(read_string(ini_path, L"room", L"code", ""));
    c.overlay_enabled = read_uint(ini_path, L"overlay", L"enabled", 1) != 0;
    c.overlay_self_marker = read_uint(ini_path, L"overlay", L"self_marker", 1) != 0;
    c.overlay_english = read_string(ini_path, L"overlay", L"language", "en") != L"es";
    c.overlay_nameplates = read_uint(ini_path, L"overlay", L"nameplates", 1) != 0;
    c.sniffer_enabled = read_uint(ini_path, L"sniffer", L"enabled", 1) != 0;
    c.sniffer_record = read_uint(ini_path, L"sniffer", L"record", 0) != 0;  // only when asked for
    c.dev_clone_hotkeys = read_uint(ini_path, L"dev", L"clone_hotkeys", 0) != 0;
    c.dev_mirror_delay_ms = std::min(read_uint(ini_path, L"dev", L"mirror_delay_ms", 300), 5000u);
    c.dev_mirror_offset_m = read_metres(ini_path, L"dev", L"mirror_offset_m", c.dev_mirror_offset_m);
    c.dev_combat_tests = read_uint(ini_path, L"dev", L"combat_tests", 0) != 0;
    c.dev_minimap_cycle = read_uint(ini_path, L"dev", L"minimap_cycle", 0) != 0;
    c.dev_aggro_test = read_uint(ini_path, L"dev", L"aggro_test", 0) != 0;
    c.dev_enemy_relay = read_uint(ini_path, L"dev", L"enemy_relay", 0) != 0;
    c.dev_piece4_test = read_uint(ini_path, L"dev", L"piece4_test", 0) != 0;
    c.dev_attack_probe = read_uint(ini_path, L"dev", L"attack_probe", 0) != 0;
    // Co-op+: the other player's enemy copies, on by default since they load and fight safely on 3479
    // ([coop] enemy_copies; the older [dev] key still turns them on).
    c.dev_enemy_copies = read_uint(ini_path, L"coop", L"enemy_copies", read_uint(ini_path, L"dev", L"enemy_copies", 1)) != 0;
    c.dev_enemy_keep_origin = read_uint(ini_path, L"dev", L"enemy_keep_origin", 0) != 0;
    c.dev_loot_test_original = read_uint(ini_path, L"dev", L"loot_test_original", 0) != 0;
    c.dev_no_copy = read_uint(ini_path, L"dev", L"no_copy", 0) != 0;
    c.dev_kill_key = read_uint(ini_path, L"dev", L"kill_key", 0) != 0;
    c.dev_tp_to = narrow(read_string(ini_path, L"dev", L"tp_to", ""));
    c.dev_script = narrow(read_string(ini_path, L"dev", L"script", ""));
    c.dev_tp_offset_m = static_cast<int32_t>(std::min(read_uint(ini_path, L"dev", L"tp_offset_m", 30), 5000u));
    c.dev_boss_hp = read_uint(ini_path, L"dev", L"boss_hp", 0);
    c.dev_partner_alive = read_uint(ini_path, L"dev", L"partner_alive", 0) != 0;
    c.dev_skip_test = read_uint(ini_path, L"dev", L"skip_test", 0) != 0;
    c.dev_trace_op = read_uint(ini_path, L"dev", L"trace_op", 0);
    c.dev_ui_preview = static_cast<int>(read_uint(ini_path, L"dev", L"ui_preview", 0));
    c.dev_pad_sim = static_cast<int>(read_uint(ini_path, L"dev", L"pad_sim", 0));
    c.dev_challenge_test = static_cast<int>(read_uint(ini_path, L"dev", L"challenge_test", 0));
    c.dev_duel_test = read_uint(ini_path, L"dev", L"duel_test", 0) != 0;
    c.dev_quest_test = static_cast<uint32_t>(read_uint(ini_path, L"dev", L"quest_test", 0));
    c.dev_hunt_test = read_uint(ini_path, L"dev", L"hunt_test", 0) != 0;
    c.dev_zone_copy_test = read_uint(ini_path, L"dev", L"zone_copy_test", 0) != 0;
    c.dev_online_guard_test = read_string(ini_path, L"dev", L"online_guard_test", "");
    c.dev_fake_peer_build = read_uint(ini_path, L"dev", L"fake_peer_build", 0);
    c.dev_desync_test = static_cast<int>(read_uint(ini_path, L"dev", L"desync_test", 0));
    c.coop_teammate_revive = read_uint(ini_path, L"coop", L"teammate_revive", 1) != 0;
    c.coop_shared_quest_credit = read_uint(ini_path, L"coop", L"shared_quest_credit", 0) != 0;
    c.coop_guest_finishes = read_uint(ini_path, L"coop", L"guest_finishes", 1) != 0;
    c.coop_respawn_at_partner = read_string(ini_path, L"coop", L"respawn_at", "checkpoint") == L"partner";
    c.dev_revive_test = read_uint(ini_path, L"dev", L"revive_test", 0) != 0;
    c.dev_copy_untargetable = read_uint(ini_path, L"dev", L"copy_untargetable", 0) != 0;
    c.dev_event_test = read_uint(ini_path, L"dev", L"event_test", 0);
    c.dev_rps_test = static_cast<int>(read_uint(ini_path, L"dev", L"rps_test", 0)) - 1;  // 1 scissors, 2 paper, 3 rock
    c.dev_cinema_test = read_uint(ini_path, L"dev", L"cinema_test", 0) != 0;
    c.dev_weather_test = read_uint(ini_path, L"dev", L"weather_test", 0);
    c.dev_versus_test = read_uint(ini_path, L"dev", L"versus_test", 0);
    c.dev_seat_test = read_uint(ini_path, L"dev", L"seat_test", 0) != 0;
    c.dev_scaling_as_host = read_uint(ini_path, L"dev", L"scaling_as_host", 0) != 0;
    c.dev_join_switch_s = read_uint(ini_path, L"dev", L"join_switch_s", 0);
    c.dev_join_switch_port = static_cast<uint16_t>(read_uint(ini_path, L"dev", L"join_switch_port", 0));
    c.dev_direct_loopback = read_uint(ini_path, L"dev", L"direct_loopback", 0) != 0;
    c.dev_quest_events_log = read_uint(ini_path, L"dev", L"quest_events_log", 0) != 0;
    c.dev_load_trace = static_cast<int>(std::min(read_uint(ini_path, L"dev", L"load_trace", 0), 1u));
    c.coop_enabled = read_uint(ini_path, L"coop", L"enabled", 0) != 0;
    // Co-op+ low latency (2026-11-06): the old [coop] delay_ms (150-300 ms on top of the game's own 200 ms) is no
    // longer read; buffer_ms is the smallest playout buffer after the network, adaptive above it (playout_buffer.h).
    c.coop_delay_ms = std::min(read_uint(ini_path, L"coop", L"buffer_ms", c.coop_delay_ms), 250u);
    c.coop_offset_m = read_metres(ini_path, L"coop", L"offset_m", c.coop_offset_m);
    c.coop_horse = read_uint(ini_path, L"coop", L"horse", 1) != 0;
    c.coop_equip = read_uint(ini_path, L"coop", L"equip", 1) != 0;
    c.coop_damage = read_uint(ini_path, L"coop", L"damage", 0) != 0;
    c.coop_map_pin = read_uint(ini_path, L"coop", L"map_pin", 1) != 0;
    // Co-op+ (3479): on by default -- the host's enemies may pick the guest's copy as a target, so the game's own
    // AI chooses between the two players, and their blows on the copy hurt the guest (live: targets switched to
    // the copy, its blow arrived as -29 hp). Hits react and can kill like any blow.
    c.coop_enemies_target_copy = read_uint(ini_path, L"coop", L"enemies_target_copy", 1) != 0;
    c.coop_guest_hits = read_uint(ini_path, L"coop", L"guest_hits", 0) != 0;
    c.coop_guest_hit_stagger = read_uint(ini_path, L"coop", L"guest_hit_stagger", 1) != 0;  // 2026-11-07: on
    c.coop_player_hit_react = read_uint(ini_path, L"coop", L"player_hit_react", 1) != 0;
    c.coop_player_hit_lethal = read_uint(ini_path, L"coop", L"player_hit_lethal", 1) != 0;
    // Co-op+: 3 = auto (the host sends its time of day, the guest takes it): the user wants the time to match.
    c.coop_time_sync = static_cast<int>(std::min(read_uint(ini_path, L"coop", L"time_sync", 3), 3u));
    // 2026-11-07 hotfix (playtester Rahat: the friend's prologue-ending cutscene "would get skipped and then he would
    // be stuck with the go to Hernand quest", then no guard at the inn): the relay can start a client-only copy of the
    // host's story cutscene in the guest's game, which the guest's own story never counts. Off until verified on
    // 3576; [coop] stage_cinema=1 turns it on.
    c.coop_stage_cinema = read_uint(ini_path, L"coop", L"stage_cinema", 0) != 0;
    c.coop_weather_sync = read_uint(ini_path, L"coop", L"weather_sync", 1) != 0;
    c.coop_share_story = read_uint(ini_path, L"coop", L"share_story", 1) != 0;
    c.dev_story_slot = read_string(ini_path, L"dev", L"story_slot", "");
    c.dev_save_io_log = read_uint(ini_path, L"dev", L"save_io_log", 0) != 0;
    c.dev_coop_world = read_string(ini_path, L"dev", L"coop_world", "");
    c.coop_coop_worlds = read_uint(ini_path, L"coop", L"coop_worlds", 0) != 0;
    c.dev_coop_world_partner = narrow(read_string(ini_path, L"dev", L"coop_world_partner", ""));
    c.dev_no_revive_prompt = read_uint(ini_path, L"dev", L"no_revive_prompt", 0) != 0;
    c.dev_summary_test = read_uint(ini_path, L"dev", L"summary_test", 0) != 0;
    c.coop_direct = read_uint(ini_path, L"coop", L"direct", 0) != 0;
    c.coop_predict = read_uint(ini_path, L"coop", L"predict", 1) != 0;
    c.coop_pose_stream = read_uint(ini_path, L"coop", L"pose_stream", 1) != 0;
    c.dev_lag_probe = read_uint(ini_path, L"dev", L"lag_probe", 0) != 0;
    c.dev_fishing_test = read_uint(ini_path, L"dev", L"fishing_test", 0) != 0;
    c.dev_hideseek_test = static_cast<int>(read_uint(ini_path, L"dev", L"hideseek_test", 0));
    c.dev_coop_world_as_host = read_uint(ini_path, L"dev", L"coop_world_as_host", 0) != 0;
    c.coop_live_quests = read_uint(ini_path, L"coop", L"live_quests", 1) != 0;
    c.coop_solo_prologue = read_uint(ini_path, L"coop", L"solo_prologue", 1) != 0;
    c.coop_copy_in_battle = read_uint(ini_path, L"coop", L"copy_in_battle", 1) != 0;
    c.coop_shared_xp = read_uint(ini_path, L"coop", L"shared_xp", 1) != 0;  // xp_share.h: on (sandbox gx2)
    c.coop_prologue_missions = std::min(read_uint(ini_path, L"coop", L"prologue_missions", 60), 2000u);
    c.coop_block_slowmo = read_uint(ini_path, L"coop", L"block_slowmo", 1) != 0;
    c.dev_block_slowmo_always = read_uint(ini_path, L"dev", L"block_slowmo_always", 0) != 0;
    c.dev_stat_log = read_uint(ini_path, L"dev", L"stat_log", 0) != 0;
    c.dev_live_quests_any_world = read_uint(ini_path, L"dev", L"live_quests_any_world", 0) != 0;
    c.dev_time_sync_key = read_uint(ini_path, L"dev", L"time_sync_key", 0) != 0;
    c.coop_enemy_auto = read_uint(ini_path, L"coop", L"enemy_auto", 1) != 0;
    c.coop_finish_arrow = read_uint(ini_path, L"coop", L"finish_arrow", 1) != 0;
    c.dev_rope_piece_lookup = read_uint(ini_path, L"dev", L"rope_piece_lookup", 0) != 0;
    c.coop_guest_loot = read_uint(ini_path, L"coop", L"guest_loot", 0) != 0;
    {
        const std::wstring m = read_string(ini_path, L"coop", L"loot_mode", "per_player");
        c.coop_loot_mode = m == L"host_only" ? 1 : m == L"first_come" ? 2 : 0;
        if (c.coop_loot_mode == 1) c.coop_guest_loot = false;
    }
    // Boss safety (boss_safe.h): default ON, because the failure it prevents is a
    // stuck quest in the HOST's save, not a cosmetic problem.
    c.coop_boss_safe = read_uint(ini_path, L"coop", L"boss_safe", 1) != 0;
    c.coop_boss_death_wait = read_uint(ini_path, L"coop", L"boss_death_wait", 1) != 0;
    c.dev_rope_bend = read_uint(ini_path, L"dev", L"rope_bend", 0) != 0;
    {
        const std::wstring v = read_string(ini_path, L"coop", L"enemy_minimap_icon", "");
        if (!v.empty()) c.coop_enemy_minimap_icon = static_cast<int>(std::wcstol(v.c_str(), nullptr, 0));
    }
    c.coop_copy_attack_slots = static_cast<int>(std::min(read_uint(ini_path, L"coop", L"copy_attack_slots", 2), 8u));  // Co-op+: 2 at once (the game gives the copy none)
    c.coop_map_pin_shape = static_cast<uint8_t>(std::min(read_uint(ini_path, L"coop", L"map_pin_shape", 4), 13u));
    c.coop_map_pin_colour = static_cast<uint8_t>(std::min(read_uint(ini_path, L"coop", L"map_pin_colour", 14), 14u));
    {
        // 0 / off, dry, 1 / on; anything else is the dry run.
        // Co-op+: real hiding by default (lab-tested on 3479: groups removed and restored intact), so the guest
        // fights the host's enemies and not doubles of its own. "dry" logs only; 0 turns it off.
        const std::wstring v = read_string(ini_path, L"coop", L"suppress", "1");
        c.coop_suppress = (v == L"0" || v == L"off") ? 0 : (v == L"1" || v == L"on") ? 2 : 1;
        c.coop_suppress_radius_m = read_float(ini_path, L"coop", L"suppress_radius_m", 150.0f, 50.0f, 300.0f);
        c.coop_suppress_off_m = read_float(ini_path, L"coop", L"suppress_off_m", c.coop_suppress_radius_m + 100.0f,
                                           c.coop_suppress_radius_m + 5.0f, c.coop_suppress_radius_m + 100.0f);
        c.coop_suppress_restore = read_uint(ini_path, L"coop", L"suppress_restore", 1) != 0;
        c.coop_suppress_restore_state = read_uint(ini_path, L"coop", L"suppress_restore_state", 1) != 0;
        c.coop_suppress_wait_fight_s = std::min(read_uint(ini_path, L"coop", L"suppress_wait_fight_s", 60), 600u);
        c.coop_suppress_respawn_grace_s =
            std::min(read_uint(ini_path, L"coop", L"suppress_respawn_grace_s", 60), 180u);
        c.coop_suppress_node_events = read_uint(ini_path, L"coop", L"suppress_node_events", 0) != 0;
        c.coop_suppress_lab = read_uint(ini_path, L"coop", L"suppress_lab", 0) != 0;
        c.coop_suppress_lab_radius_m = read_float(ini_path, L"coop", L"suppress_lab_radius_m", 60.0f, 10.0f, 300.0f);
        // Co-op+ D28: on by default (one-PC check 2026-11-02; user asked for bosses scaled to the party).
        c.scaling_enabled = read_uint(ini_path, L"scaling", L"enabled", 1) != 0;
        c.coop_save_guard = read_uint(ini_path, L"coop", L"save_guard", 1) != 0;
        c.scaling_health_2p = read_float(ini_path, L"scaling", L"health_2p", 1.7f, 1.0f, 4.0f);
        c.scaling_boss_health_2p = c.scaling_health_2p;
    }
    {
        // Decimal or 0x-hex; "-1" diagnostic.
        const std::wstring v = read_string(ini_path, L"coop", L"minimap_icon", "");
        if (!v.empty()) c.coop_minimap_icon = static_cast<int>(std::wcstol(v.c_str(), nullptr, 0));
    }
    return c;
}

}  // namespace cdmp
