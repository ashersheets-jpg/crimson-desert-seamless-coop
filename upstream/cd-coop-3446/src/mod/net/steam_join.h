#pragma once
// Steam's own ways into a friend's world, on top of the room lobby
// (room_code.h): "Join Game" in the friends list, invites from the Steam
// overlay, and rich presence. Co-op+ addition (not in upstream cdmp).
//
// How a join reaches us:
//   - game not running: Steam starts it with "+connect_lobby <id>" on the
//     command line (join_launch_lobby());
//   - game running: Steam raises GameLobbyJoinRequested_t (lobby invite or
//     "Join Game" on a friend in a joinable lobby) or
//     GameRichPresenceJoinRequested_t (our rich presence "connect" string). We
//     register for both through SteamAPI_RegisterCallback. The game pumps
//     SteamAPI_RunCallbacks itself (its exe imports it; checked offline on
//     1.0.0.3446), so they fire on the game's thread; the handlers only store the
//     lobby id, and the network thread picks it up with join_take_request().
//
// Every Steam call is wrapped like the rest of the Steam code (a crash would be
// reported as a game crash); the first exception disables this module and the
// room code / peer_steamid paths keep working.

#include <cstdint>
#include <optional>
#include <string>

namespace cdmp::net {

// The lobby the command line asks for ("+connect_lobby <id>"), if any. Read once
// at startup: Steam passes it only when it launches the game for a join.
std::optional<uint64_t> join_launch_lobby();

// Resolves the Steam API and registers the callbacks once Steam is up. Cheap;
// call it from the network loop.
void join_poll();

// A lobby the player asked to join through Steam since the last call, or 0.
uint64_t join_take_request();

// Opens the Steam overlay's "invite friends" dialog for our lobby. False when
// the overlay or the lobby is not available (the caller says so on screen).
bool join_open_invite(uint64_t lobby);

// True while the Steam overlay (Shift+Tab, the invite dialog) is open: our pages leave the mouse and the keys to it.
bool join_overlay_active();

// Rich presence: what friends see in their list, and the "Join Game" button.
// `joinable` = friends may use "Join Game" (false for invite-only hosts).
void join_presence_hosting(uint64_t lobby, int members, int max_members, bool joinable = true);
void join_presence_guest(uint64_t lobby, const std::string& host_name);
void join_presence_clear();

// Name of a Steam user as the local player knows them ("" if unknown).
std::string join_persona_name(uint64_t steam_id);
// The local player's Steam name ("" until Steam is up).
std::string join_own_name();

// Unregisters the callbacks. Must run before the mod's code is unloaded, or the
// game's next SteamAPI_RunCallbacks would call into freed memory.
void join_shutdown();

bool join_available();
std::string join_status();

}  // namespace cdmp::net
