#pragma once
// Co-op+ on the native base (docs/design/native-join-and-saves.md): the native cdmp.ini for each launch mode. The
// native base reads its role at start (host stages and guest stages are process roles), so the Co-op+ layer writes
// this file before it restarts the game into co-op, and writes Solo again when the session is over. Header-only and
// Windows-free: the unit tests check the text.
//
// Solo   : the native base installs nothing (the player's own saves, untouched).
// Host   : the host stages; the tunnel accepts the partner (Steam: its SteamID; direct: udp on 37015); the saves are
//          redirected into the co-op save folder of this friend (coop_save_dir, relative to ...\Pearl Abyss\CD).
// Guest  : the guest stages, remote; the tunnel goes to the host (Steam: its SteamID; direct: its address); the
//          guest's saves are redirected into its own co-op save folder for this host.

#include <cstdint>
#include <string>

namespace cdmp::net {

enum class NativeMode : uint8_t { Solo = 0, Host = 1, Guest = 2 };

struct NativeModeSpec {
    NativeMode mode = NativeMode::Solo;
    bool steam = true;               // the tunnel over Steam (else udp: Radmin VPN / LAN)
    uint64_t partner_steam_id = 0;   // Steam: host = the guest to accept (0 = friends playing), guest = the host
    std::string partner_address;     // udp guest: the host's IPv4 address
    uint16_t port = 37015;           // udp: the host listens here
    std::string coop_save_dir;       // relative to %LOCALAPPDATA%\Pearl Abyss\CD (e.g. coop_worlds\host_<id>)
    std::string coop_save_seed = "keep";  // keep | copy | new (native config.h s2_coop_save_dir)
    std::string player_name = "Player";
};

inline const char* native_mode_name(NativeMode m) {
    switch (m) {
        case NativeMode::Host: return "host";
        case NativeMode::Guest: return "guest";
        default: return "solo";
    }
}

// Keeps an ini value on one line and free of the characters that would end it early.
inline std::string native_ini_value(const std::string& v) {
    std::string out;
    for (const char c : v) {
        if (c == '\r' || c == '\n' || c == ';' || c == '[' || c == ']' || c == '"') continue;
        out += c;
    }
    return out;
}

// The whole cdmp.ini (CRLF lines) for that mode. The first line marks it as Co-op+'s, so a hand-made ini is never
// mistaken for one Co-op+ may rewrite.
inline std::string native_mode_ini_text(const NativeModeSpec& s) {
    std::string t;
    auto line = [&](const std::string& l) { t += l + "\r\n"; };
    line("; Written by Co-op+ (coopplus.asi): the native base's mode for this launch. Co-op+ rewrites this file;");
    line("; edit coopplus.ini instead.");
    line(std::string("; coopplus_mode=") + native_mode_name(s.mode));
    line("[general]");
    line("player_name=" + native_ini_value(s.player_name));
    if (s.mode == NativeMode::Solo) {
        line("[net]");
        line("enabled=0");
        line("[overlay]");
        line("enabled=0");
        line("[coop]");
        line("enabled=0");
        line("[session2]");
        line("enabled=0");
        return t;
    }
    const bool host = s.mode == NativeMode::Host;
    line("[net]");
    line("enabled=1");
    line(std::string("transport=") + (s.steam ? "steam" : "udp"));
    line(std::string("role=") + (host ? "host" : "guest"));
    if (!host && !s.steam) line("peer_host=" + native_ini_value(s.partner_address));
    line("peer_port=" + std::to_string(s.port));
    if (host && !s.steam) line("host_loopback_only=0");
    line("[steam]");
    line("peer_steamid=" + (s.partner_steam_id ? std::to_string(s.partner_steam_id) : std::string()));
    line("[overlay]");
    line("enabled=0");  // one Present hook only: Co-op+'s (native-coopplus-layer.md)
    line("[sniffer]");
    line("enabled=1");  // the packet bridge to the Co-op+ layer
    line("record=0");
    line("[coop]");
    line("enabled=0");
    line("[session2]");
    line("enabled=1");
    line("save_guard=redirect");
    if (!s.coop_save_dir.empty()) {
        line("coop_save_dir=" + native_ini_value(s.coop_save_dir));
        line("coop_save_seed=" + native_ini_value(s.coop_save_seed));
    }
    if (host) {
        for (const char* k : {"probe=1", "host_save=sandbox", "create_session=1", "accept_tunnel=1", "share_field=1",
                              "relay_fix=1", "clock_gate=1", "identity_fix=1", "child_hold=1", "observer_hold=1",
                              "observer_load_defer=1", "observer_hold_family=1", "observer_hold_acks=1",
                              "character_hold=1", "child_settle_ms=4000", "child_reannounce=1", "observer_seq_copy=1",
                              "observer_seq_copy_2322=1", "observer_milestone=1", "observer_milestone_send=1",
                              "observer_milestone_reload=1", "observer_milestone_reload_any=1", "guest_own_save=1",
                              "horse_follow=1", "horse_follow_reverse=1", "user2_fast_travel=1",
                              // B02b (2026-11-09): merchants in co-op: the login guard (merchant_unhide), the guest's
                              // store check on the server (guest_shop_check), the host's shop choices for the guest
                              "merchant_unhide=1", "guest_shop_check=1", "shop_twin=1",
                              // B10 (2026-11-09): each player's quest events to its own quests (option 3); B04: the
                              // guest's map fog joined with the host's at its login
                              "quest_own_progress=1", "fog_share=1", "anti_grief=1",
                              // B02 far world (2026-11-09, tcB201): the far guest's own NPC stages (U-1 + its timers,
                              // the world anchor, its NPCs' controller)
                              "user2_stages=1", "user2_timer_route=1", "user2_world_anchor=1", "user2_npc_control=1"}) {
            line(k);
        }
    } else {
        for (const char* k : {"guest=1", "guest_mode=remote", "guest_crash_dump=1", "guest_layer_guard=1",
                              "guest_remote_update_guard=1", "guest_update_guard=1", "guest_reload_wait_fix=1",
                              "guest_own_save=1", "guest_seq_route=1", "guest_fast_travel_veto=probe",
                              // B02b: the shopkeepers shown (merchant_unhide), the guest's own shop scene (shop_twin)
                              "merchant_unhide=1", "guest_shop_check=1", "shop_twin=1"}) {
            line(k);
        }
    }
    for (const char* k : {"ui_pause_block=1", "remote_interp=1", "mount_sync=1", "mount_seat=1", "mount_relink=1",
                          "ride_filter=1", "ride_dismount=native", "remote_climb=1", "wallup_send=1",
                          "dismount_probe=1"}) {
        line(k);
    }
    return t;
}

// The mode a Co-op+-written cdmp.ini is in (Solo for anything else, a hand-made ini included).
inline NativeMode native_mode_of_text(const std::string& text) {
    const auto at = text.find("; coopplus_mode=");
    if (at == std::string::npos) return NativeMode::Solo;
    const std::string rest = text.substr(at + 16, 5);
    if (rest.rfind("host", 0) == 0) return NativeMode::Host;
    if (rest.rfind("guest", 0) == 0) return NativeMode::Guest;
    return NativeMode::Solo;
}

// The co-op save folders: one per friend for the host, one per host for the guest (never the player's own saves).
inline std::string native_coop_dir(NativeMode m, uint64_t partner_steam_id, const std::string& partner_address) {
    const std::string who = partner_steam_id ? std::to_string(partner_steam_id) : native_ini_value(partner_address);
    std::string safe;
    for (const char c : who) safe += (c == '.' || c == ':') ? '_' : c;
    return std::string(m == NativeMode::Host ? "coop_worlds\\with_" : "coop_worlds\\guest_of_") +
           (safe.empty() ? "friend" : safe);
}

}  // namespace cdmp::net
