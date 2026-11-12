#pragma once
// Settings read from cdmp.ini next to the mod. Missing keys use the defaults
// below, and a default file is written if none exists.

#include <cstdint>
#include <string>

namespace cdmp {

struct Config {
    std::string player_name = "Player";
    uint32_t startup_delay_ms = 0;

    std::string peer_host = "127.0.0.1";
    uint16_t peer_port = 37015;
    uint32_t send_rate_hz = 20;
    bool net_enabled = true;
    // [net] transport=udp|steam, role=guest|host. A guest connects to the host
    // (or to cdmp_peer); a host waits for one player.
    bool net_transport_steam = false;
    // Co-op+ [net] transport=direct (also radmin / lan): UDP straight to the friend over a VPN or LAN (direct_ip.h).
    // peer_host = the friend's address on both sides; bind_host = our own (empty: the adapter that reaches the friend).
    bool net_transport_direct = false;
    std::string bind_host;
    // Runtime only (the co-op menu's Radmin VPN / Direct IP, direct_ip.h): the invite's secret. A host with one lets in
    // whoever knocks with it (peer_host may then be empty); a guest knocks with it.
    uint64_t direct_token = 0;
    bool direct_from_menu = false;  // runtime: this direct session was chosen in the menu (a failure goes back to Steam)
    bool net_role_host = false;
    // [net] role=auto (Co-op+): start as the host of our own world; a Steam invite
    // or "Join Game" switches to guest, and leaving switches back.
    bool net_role_auto = false;
    // [net] join = friends | invite (Co-op+): who may join a host. friends = any
    // Steam friend playing ("Join Game", upstream's behaviour); invite = only
    // friends we invited through the Steam overlay (Ctrl+F1).
    bool net_join_invite_only = false;
    // Runtime only, never read from the ini: the lobby a Steam join asked for.
    // The guest resolves the host from it instead of from [room] code.
    uint64_t room_lobby = 0;
    // [steam] peer_steamid: guest = the host's SteamID64 (0 = the only friend
    // playing); host = accept only this friend (0 = any friend playing).
    uint64_t steam_peer_id = 0;
    int steam_channel = 67;
    // [room] code: the short code that replaces peer_steamid. The HOST leaves it
    // empty and the mod creates a Steam lobby and shows the code in the panel and
    // the log; the GUEST types the host's code here and the mod resolves the
    // host's SteamID64 from the lobby. Steam only: no server of ours, no ports,
    // no IP. When empty (or when matchmaking is unavailable) the mod falls back
    // to peer_steamid and the "only friend playing" scan, exactly as before.
    std::string room_code;

    bool overlay_enabled = true;  // kill switch for the DX12 hook
    bool overlay_self_marker = true;  // dev aid while validating the projection
    bool overlay_english = true;      // [overlay] language = en | es: the panel's texts
    bool overlay_nameplates = true;   // [overlay] nameplates (Co-op+): the other player's name over their head

    bool sniffer_enabled = true;  // hook internal client/server packets (needed by co-op)
    bool sniffer_record = false;  // also write them to cdmp_packets_*.log/.bin (dev; 50-100 MB/session)

    // Experiment 4c: F9 spawns a clone of the player, F10 removes it. Writes
    // packets into the game, so it is off unless explicitly enabled.
    bool dev_clone_hotkeys = false;
    uint32_t dev_mirror_delay_ms = 300;  // simulated latency of the mirrored clone
    float dev_mirror_offset_m = 2.5f;    // sideways offset; 0 = walk the player's exact path
    // Combat injection tests (combate-coop.md §6, T2/T3): WRITE to the game's
    // server. Only with a save backup, and never save afterwards.
    bool dev_combat_tests = false;
    // Ctrl+F9 / Ctrl+Shift+F9: next / previous minimap icon for the other player.
    bool dev_minimap_cycle = false;
    // [dev] native_extra: k=v,k=v appended to the native ini's [session2] the lobby writes (research runs only).
    std::string dev_native_extra;
    // Ctrl+F5: an enemy targets the other player's copy (aggro_test.h).
    bool dev_aggro_test = false;
    // Ctrl+F6: send the nearest enemy to the other player (enemy_relay.h).
    bool dev_enemy_relay = false;
    // Ctrl+F10 / Ctrl+F11 experiments of piece 4 (piece4_test.h); they write to the game.
    bool dev_piece4_test = false;
    // Every 2 s, log who sits in the attacker lists of our player and of the
    // other player's copy (attack_probe.h). Read only.
    bool dev_attack_probe = false;
    bool dev_enemy_copies = true;  // Co-op+: load the other player's enemy copies ([coop] enemy_copies)
    bool dev_enemy_keep_origin = false;
    bool dev_no_copy = false;   // Co-op+ dev: never load the other player's character ([dev] no_copy)
    bool dev_kill_key = false;  // Co-op+ dev: Ctrl+Shift+F9 kills our player ([dev] kill_key)
    int dev_lobby_skip_play = 0;  // dev: net/coop_lobby.h lobby_dev
    int dev_lobby_retry = 0;
    int dev_lobby_recover = 0;
    uint32_t dev_build_id = 0;  // [dev] build_id (hex): this copy says it is another build (the mismatch test)
    int dev_overlay_no_streamline = 0;  // dev: see render/dx12_hook.h ([dev] overlay_no_streamline)
    bool dev_wait_see_through = false;  // dev: the guest's waiting screen is see-through ([dev] wait_see_through)
    std::string dev_script;  // Co-op+ dev: packet script for Ctrl+Shift+F6 ([dev] script, net/dev_script.h)
    std::string dev_tp_to;  // Co-op+ dev: "x,y,z" -> Ctrl+Shift+F4 teleports there instead ([dev] tp_to)
    int32_t dev_tp_offset_m = 30;  // Co-op+ dev: Ctrl+Shift+F4 teleports this far east ([dev] tp_offset_m)
    uint32_t dev_boss_hp = 0;       // Co-op+ dev: enemies with this many max HP count as bosses ([dev] boss_hp)
    // Co-op+ dev (Borque's crash: a Steam invite accepted before a save was loaded): a HOST turns into a guest this
    // many seconds after its network started, as an accepted invite does (Kind::Join), once; 0 = off. As a guest it
    // then talks to join_switch_port (UDP; 0 = peer_port).
    uint32_t dev_join_switch_s = 0;
    uint16_t dev_join_switch_port = 0;
    // Co-op+ dev: the menu's Radmin VPN / Direct IP hosts on 127.0.0.1 (two copies on one PC) ([dev] direct_loopback).
    bool dev_direct_loopback = false;
    bool dev_scaling_as_host = false;  // Co-op+ dev: party scaling acts as the host ([dev] scaling_as_host)
    int dev_challenge_test = 0;      // Co-op+ dev: a simulated friend accepts our challenges ([dev] challenge_test)
    bool dev_duel_test = false;      // Co-op+ dev: one-PC duel with the echo peer (coop_duel.h)
    uint32_t dev_quest_test = 0;     // Co-op+ dev: party journal one-PC test (party_quests.h), a mission key
    bool dev_hunt_test = false;      // Co-op+ dev: hunting contest one-PC test (coop_hunt.h)
    bool dev_zone_copy_test = false; // Co-op+ dev: the guest's dry-run zone is copied for real (enemy_relay.h)
    bool coop_teammate_revive = true;
    bool coop_join_beside_host = true;  // Co-op+ 2026-11-11: [coop] join_beside_host: the guest starts at the host's side
    bool coop_downed_pose = true;  // Co-op+ 2026-11-11: [coop] downed_pose: a downed player lies down (shared world)
    bool coop_shared_quest_credit = false;
    // Co-op+ 2026-11-10 (user: "Lootr"): [coop] world_items = lootr (default: every item / plant lying in the world can be
    // taken once by EACH player) | first_come (the game's own: the first one takes it). Host; the native's lootr key.
    bool coop_world_items_lootr = true;  // Co-op+ 2026-11-09 B10 option 2: [coop] shared_quest_credit (host)
    bool coop_guest_finishes = true;
    // Co-op+ 2026-11-10: [coop] item_sharing (B11 give + B12 shared stash): on since tcB335 (the give works both ways; the
    // stash shows only once the host's warehouse is unlocked). Off: the game's own Discard, untouched.
    bool coop_item_sharing = true;  // 2026-11-10: on (the give works both ways, tcB335)
    bool coop_respawn_at_partner = false;  // Co-op+ (T26): [coop] respawn_at = checkpoint (default) | partner   // Co-op+ (T17): [coop] guest_finishes  // Co-op+ (T15): [coop] teammate_revive
    bool dev_revive_test = false;
    bool dev_copy_untargetable = false;
    uint32_t dev_event_test = 0;      // Co-op+ dev (T22): a world-event stage key announced after connecting  // Co-op+ dev (T21): the copy is never anyone's target      // Co-op+ dev: one-PC teammate revive (a dead player may revive the echo)
    int dev_desync_test = 0;            // Co-op+ dev: induce a desync 30 s after connecting (coop_sync.h, T06)
    uint32_t dev_fake_peer_build = 0;   // Co-op+ dev: a build-0 peer (the echo relay) counts as this build (T03)
    std::wstring dev_online_guard_test;  // Co-op+ dev: a module name the online guard also looks for (online_guard.h)
    bool dev_rb_test = false;        // Co-op+ dev: the menu's RB glyph, another way at each opening ([dev] rb_test)
    int dev_pad_sim = 0;             // Co-op+ dev: act as an Xbox (1) / PlayStation (2) pad ([dev] pad_sim)
    int dev_ui_preview = 0;          // Co-op+ dev: 1 = show the death screen ([dev] ui_preview)
    int dev_rps_test = -1;  // Co-op+ dev: simulated RPS opponent's hand 0/1/2 ([dev] rps_test=1..3, 0 = off)
    bool dev_cinema_test = false;  // Co-op+ dev: replay our own stage cutscenes as a guest would ([dev] cinema_test)
    uint32_t dev_weather_test = 0;  // Co-op+ dev: act as a guest whose host has this weather stage ([dev] weather_test)
    uint32_t dev_versus_test = 0;  // Co-op+ dev: simulated versus opponent pushing N times/s ([dev] versus_test)
    bool dev_seat_test = false;    // Co-op+ dev: the copy takes the NPC's seat in any wrestling match ([dev] seat_test)
    uint32_t dev_trace_op = 0;  // Co-op+ dev: trace who sends this opcode ([dev] trace_op)
    bool dev_skip_test = false;  // Co-op+ dev: act as if the other player skipped 3 s into our cutscenes ([dev] skip_test)
    bool dev_partner_alive = false;  // Co-op+ dev: the revive prompt sees the other player alive ([dev] partner_alive)
    bool dev_loot_test_original = false;  // Co-op+ dev: loot_twin uses the copy's original (one-PC test)
    bool dev_quest_events_log = false;  // Co-op+ dev: log the game events the quest system is fed (quest_events.h)
    int dev_load_trace = 0;  // Co-op+: 1 = log actor load-state steps, 2 = + unhide experiment (load_trace.h)

    // Co-op over the network ([coop]): the other player (or, through
    // cdmp_peer --mode relay with nobody else connected, yourself) appears as
    // a remote character. Writes packets into the game; off by default.
    bool coop_enabled = false;
    uint32_t coop_delay_ms = 10;  // [coop] buffer_ms: the smallest playout buffer after the network (playout_buffer.h)
    float coop_offset_m = 0.0f;    // sideways offset of the remote character
    // Experimental, off by default: the copy's horse is not created by the
    // client (owned load) or crashes the game (ownerless load); live tests 1-2.
    bool coop_horse = true;        // the other player's horse + mount/dismount fixes (on since 2026-11-02, D30)
    // Works since the copy got its own character number (live test 3).
    bool coop_equip = true;        // relay equipment changes
    // Mirrored damage: each world keeps its enemies; hits by the other player
    // are applied to our enemy of the same type nearest to theirs. Off by
    // default: it lets the other side hurt our world's NPCs (security review
    // H1); guest_hits replaces it.
    bool coop_damage = false;
    // The other player as a pin on the world map and minimap.
    bool coop_map_pin = true;
    uint8_t coop_map_pin_shape = 4;    // 0-13 (the game's default pin: 4)
    uint8_t coop_map_pin_colour = 14;  // 0-14 (the game's default pin: 14)
    // The other player's icon on the minimap: offset in the game's icon
    // table (0x124 = an outlined arrow, the one chosen in the live test;
    // 0x126 = our own arrow; no other colours exist), 0 = none,
    // -1 = diagnostic (log what the game decides).
    int coop_minimap_icon = 0x124;
    // Our enemies may attack the other player's copy (aggro_test.h). Off
    // until the hits reach the other player and they see our enemies.
    bool coop_enemies_target_copy = true;
    // The other player's blows on the copies of our enemies hurt them
    // (guest_hits.h). Off until tested with two players.
    bool coop_guest_hits = false;
    // ...and stagger them (restriction + hit buff, pieza4-tambaleo.md):
    // experimental until a live test shows the stagger.
    bool coop_guest_hit_stagger = false;
    // Our player reacts (stagger, knock-down) to the other player's enemies'
    // blows on our copy over there: experimental until tested.
    bool coop_player_hit_react = true;
    // ...and can die from them (a normal death): experimental until tested.
    bool coop_player_hit_lethal = true;
    // How many of our enemies at a time may attack the other player's copy
    // (copia-objetivo-sin-ataques.md); 0 = the game decides (they mostly wait).
    int coop_copy_attack_slots = 2;
    // Time of day (hora-del-dia.md): 0 off, 1 send ours (host), 2 take the
    // other player's (guest).
    int coop_time_sync = 3;
    // Co-op+: the guest's weather follows the host's (weather_sync.h).
    bool coop_weather_sync = true;
    // Co-op+: a guest may copy this host's story into their save (story_sync.h).
    bool coop_share_story = true;
    // Co-op+ dev: the slot folder story sync uses instead of the newest one (tests).
    std::wstring dev_story_slot;
    // Co-op+ dev: log the game's save file calls (save_io.h).
    bool dev_save_io_log = false;
    // Co-op+ dev: a co-op world active from the start (save_io.h; tests of the save separation).
    std::wstring dev_coop_world;
    // Co-op+: a co-op world (separate saves) per friend, switched to when a session starts (coop_worlds.h).
    bool coop_coop_worlds = false;
    std::string dev_coop_world_partner;  // Co-op+ dev: partner key for one-PC tests (the echo peer has no Steam id)
    bool coop_pose_stream = true;
    bool coop_direct = false;  // Co-op+ low latency: positions straight into the copy's buffers (puppet_visibility.cpp)
    bool coop_predict = true;  // Co-op+ low latency: a predicted position between the other player's moves (coop.cpp)  // Co-op+ low latency: our moves from our transform every frame (local_capture.cpp)
    bool dev_lag_probe = false;  // Co-op+ dev: write where we and the copy are drawn every 4 ms (lag_probe.h)
    bool dev_fishing_test = false;  // Co-op+ dev: fishing contest one-PC test (the echoed score is ignored)
    int dev_hideseek_test = 0;  // Co-op+ dev: hide and seek one-PC test (1 we seek, 2 we hide; coop_hideseek.h)
    bool dev_summary_test = false;  // Co-op+ dev: the session summary card for any session, 3 s after it ends
    bool dev_no_revive_prompt = false;  // Co-op+ dev: no revive prompt and no revive keys (death-screen research)
    bool dev_coop_world_as_host = false;  // Co-op+ dev: the co-op worlds side acts as the host (first-session card)
    // Co-op+: quest steps follow between the players live, while both are in a co-op world (quest_live.h).
    bool coop_live_quests = true;
    // Co-op+ (2026-11-07, prologue_hold.h): [coop] solo_prologue: the opening tutorial is played solo; co-op waits
    // while a save has fewer than prologue_missions completed missions.
    bool coop_solo_prologue = true;
    bool coop_shared_xp = true;
    bool coop_copy_in_battle = true;  // [coop] copy_in_battle: the host's enemies fight the guest's copy (aggro_test.h)  // [coop] shared_xp: every skill-point gain shared (xp_share.h)
    uint32_t coop_prologue_missions = 60;
    // Co-op+: abilities' slow motion (Focus...) does not slow the world while a co-op session is up (net/slowmo.h).
    bool coop_block_slowmo = true;
    bool dev_block_slowmo_always = false;
    bool dev_stat_log = false;  // Co-op+ dev: our stats once a second (game/player_reader.h)  // Co-op+ dev: also with no partner (one-PC tests)
    bool dev_live_quests_any_world = false;  // Co-op+ dev: also outside co-op worlds (one-PC tests)
    // Co-op+: the host's stage cinematics (boss intros...) play for the guest (stage_cinema.h).
    bool coop_stage_cinema = false;  // off by default since 2026-11-07 (config.cpp: the skipped prologue ending)
    // Host: copies of our enemies that fight go to the guest on their own
    // (Ctrl+F7 turns them off and on). Needs [dev] enemy_relay=1.
    bool coop_enemy_auto = true;
    // Host: the guest's lethal blows end with an arrow (normal death) instead of a collision.
    bool coop_finish_arrow = true;
    // The other player's tree hook names our own piece of that tree (calls a game function).
    bool dev_rope_piece_lookup = false;
    // Guest: loot from the host's enemies we help kill, via a twin on our server (loot_twin.h).
    bool coop_guest_loot = false;
    // Co-op+ (T10): who loots a kill both players took part in. 0 per_player (each their own drop), 1 host_only
    // (only the host), 2 first_come (the first to loot it; the other's body is emptied). [coop] loot_mode.
    int coop_loot_mode = 0;
    // Boss safety (boss_safe.h, pendiente-jefes.md plan J1). A boss fight here is
    // a scripted STAGE that the host's client drives: it owns the phases, the
    // cinematics and the set_battleEnd that closes the fight. The boss exists
    // only in the host's world, so a guest's lethal blow kills it outside that
    // script -- no phase, no final cinematic, no battle end, and the host's
    // quest is left stuck behind the arena barrier.
    //
    // On (default): the guest still does damage (and so still helps), but never
    // lands the killing blow, never staggers a boss, and nothing either way
    // lands while the host is inside a stage cinematic. The host's own blow
    // does the finish, which is what advances its script. Off restores the old
    // behaviour exactly, for comparison.
    bool coop_boss_safe = true;
    // Co-op+ (A9, user request): a player who dies during a boss fight waits until the other player beats
    // the boss (then gets up) or is down too (then both revive at the checkpoint). [coop] boss_death_wait
    bool coop_boss_death_wait = true;
    // Minimap icon (table offset) of the other player's enemy copies; 0 = the game's (none).
    // +0x10A Actor_Enemy (the red dot); +0x112 is the boss diamond (minimapa-icono-enemigo.md).
    int coop_enemy_minimap_icon = 0x10A;
    // The tree the other player's copy pulls bends here too (calls game code).
    bool dev_rope_bend = false;
    bool dev_time_sync_key = false;  // Ctrl+Shift+F12: our time 3 h forward
    // Phase 5 (native_suppress.h), guest: within suppress_radius_m of the
    // host our own hostile enemies are hidden and the host's copies stand in
    // for them; beyond suppress_off_m they come back. [coop] suppress = 0 (off),
    // dry (only logs what it would hide; the default until tested) or 1.
    int coop_suppress = 1;  // 0 off, 1 dry run, 2 on
    float coop_suppress_radius_m = 150.0f;
    float coop_suppress_off_m = 250.0f;
    bool coop_suppress_restore = true;        // our natives come back when the zone ends
    bool coop_suppress_restore_state = true;  // with what the server said about them meanwhile
    uint32_t coop_suppress_wait_fight_s = 60;  // wait this long at most for a fight with them to end
    uint32_t coop_suppress_respawn_grace_s = 60;  // the host died and reappeared: keep the zone this long
    bool coop_suppress_node_events = false;   // node and faction events too
    // Lab (one PC, no partner): Ctrl+Home turns a zone around you on and off;
    // the relay of our enemies is off then.
    bool coop_suppress_lab = false;
    // [scaling] (Co-op+, party_scaling.h): with a guest in the host's world the
    // host's enemies take 1/health_2p of every blow. On by default since 2026-11-02 (D28).
    bool scaling_enabled = true;  // Co-op+ D28: on by default
    // [coop] save_guard (Co-op+, save_guard.h): back up the saves when a session
    // starts and warn when the game saves during one.
    bool coop_save_guard = true;
    float scaling_health_2p = 1.7f;
    float scaling_boss_health_2p = 1.7f;  // from cdcoop\config\scaling.toml when present (Co-op+)
    float coop_suppress_lab_radius_m = 60.0f;
};

Config load_config(const std::wstring& ini_path);

}  // namespace cdmp
