#include "mod/net/steam_join.h"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string_view>

#include "mod/log.h"
#include "mod/net/join_args.h"

namespace cdmp::net {
namespace {

using CSteamID = uint64_t;
using SteamAPICall_t = uint64_t;

// ------------------------------------------------------------ callback base
// The SDK's CCallbackBase (steam_api_internal.h), declared exactly as the SDK
// does. steam_api64.dll calls these through the vtable, so the layout must be
// the one MSVC gives that declaration: MSVC orders overloaded virtuals in its
// own way, and declaring them identically (same order, same overloads) is what
// makes our vtable match the one Steam expects. Do not reorder.
class SteamCallbackBase {
public:
    SteamCallbackBase() = default;
    virtual void Run(void* param) = 0;
    virtual void Run(void* param, bool io_failure, SteamAPICall_t call) = 0;
    int GetICallback() { return callback_id_; }
    virtual int GetCallbackSizeBytes() = 0;

protected:
    uint8_t flags_ = 0;    // m_nCallbackFlags: Steam sets "registered" here
    int callback_id_ = 0;  // m_iCallback: Steam writes the id it registered us for
};
static_assert(sizeof(SteamCallbackBase) == 16);

#pragma pack(push, 8)
// GameLobbyJoinRequested_t: k_iSteamFriendsCallbacks (300) + 33.
struct GameLobbyJoinRequested {
    CSteamID lobby;
    CSteamID friend_id;
};
static_assert(sizeof(GameLobbyJoinRequested) == 16);
// GameRichPresenceJoinRequested_t: k_iSteamFriendsCallbacks (300) + 37.
struct GameRichPresenceJoinRequested {
    CSteamID friend_id;
    char connect[256];  // k_cchMaxRichPresenceValueLength
};
static_assert(sizeof(GameRichPresenceJoinRequested) == 264);
#pragma pack(pop)

constexpr int kGameLobbyJoinRequested = 300 + 33;
constexpr int kGameRichPresenceJoinRequested = 300 + 37;

std::atomic<uint64_t> g_request{0};
std::atomic<uint64_t> g_requests_seen{0};

class LobbyJoinCallback final : public SteamCallbackBase {
public:
    void Run(void* param) override {
        if (!param) return;
        GameLobbyJoinRequested r;
        std::memcpy(&r, param, sizeof r);
        if (looks_like_lobby_id(r.lobby)) {
            g_request.store(r.lobby);
            g_requests_seen.fetch_add(1);
        }
    }
    void Run(void* param, bool, SteamAPICall_t) override { Run(param); }
    int GetCallbackSizeBytes() override { return static_cast<int>(sizeof(GameLobbyJoinRequested)); }
};

class PresenceJoinCallback final : public SteamCallbackBase {
public:
    void Run(void* param) override {
        if (!param) return;
        GameRichPresenceJoinRequested r;
        std::memcpy(&r, param, sizeof r);
        r.connect[sizeof r.connect - 1] = '\0';
        if (auto lobby = find_connect_lobby(std::string_view(r.connect))) {
            g_request.store(*lobby);
            g_requests_seen.fetch_add(1);
        }
    }
    void Run(void* param, bool, SteamAPICall_t) override { Run(param); }
    int GetCallbackSizeBytes() override { return static_cast<int>(sizeof(GameRichPresenceJoinRequested)); }
};

// GameOverlayActivated_t: k_iSteamFriendsCallbacks (300) + 31. Only the first byte (m_bActive) is read: later SDKs
// added fields after it.
constexpr int kGameOverlayActivated = 300 + 31;
std::atomic<bool> g_overlay_active{false};

class OverlayCallback final : public SteamCallbackBase {
public:
    void Run(void* param) override {
        if (!param) return;
        const bool active = *static_cast<const uint8_t*>(param) != 0;
        if (active != g_overlay_active.exchange(active)) g_overlay_changes.fetch_add(1);
    }
    void Run(void* param, bool, SteamAPICall_t) override { Run(param); }
    int GetCallbackSizeBytes() override { return 1; }
    static inline std::atomic<uint32_t> g_overlay_changes{0};
};

LobbyJoinCallback g_lobby_cb;
PresenceJoinCallback g_presence_cb;
OverlayCallback g_overlay_cb;

// ------------------------------------------------------------ flat API
// Export names checked against bin64\steam_api64.dll 09.31.86.04 (game 1.0.0.3446).
struct Api {
    int32_t (*get_hsteam_user)() = nullptr;
    void (*register_callback)(SteamCallbackBase*, int) = nullptr;
    void (*unregister_callback)(SteamCallbackBase*) = nullptr;
    void* (*friends)() = nullptr;  // SteamAPI_SteamFriends_v017
    void (*invite_dialog)(void*, CSteamID) = nullptr;
    bool (*set_rich_presence)(void*, const char*, const char*) = nullptr;
    void (*clear_rich_presence)(void*) = nullptr;
    const char* (*friend_name)(void*, CSteamID) = nullptr;
    const char* (*persona_name)(void*) = nullptr;
    void* frn = nullptr;
};

Api g_api;
std::mutex g_mutex;
bool g_resolved = false;
bool g_registered = false;
bool g_disabled = false;
std::string g_status = "join: waiting for Steam";

template <class R, class... P, class... A>
bool call(R& out, R (*fn)(P...), A... args) {
    if (!fn) return false;
    __try {
        out = fn(args...);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
template <class... P, class... A>
bool call_void(void (*fn)(P...), A... args) {
    if (!fn) return false;
    __try {
        fn(args...);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void fail(const char* what) {
    if (!g_disabled) log::error("Join: exception in {}; Steam join and invites disabled", what);
    g_disabled = true;
    g_status = "join: disabled after an error";
}

// Called with g_mutex held.
void resolve_locked() {
    if (g_resolved || g_disabled) return;
    HMODULE dll = GetModuleHandleW(L"steam_api64.dll");
    if (!dll) return;  // transient: the game loads Steam later
    Api& a = g_api;
    int missing = 0;
    auto get = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(dll, name));
        if (!fn) {
            log::warn("Join: export {} not found", name);
            ++missing;
        }
    };
    get(a.get_hsteam_user, "SteamAPI_GetHSteamUser");
    get(a.register_callback, "SteamAPI_RegisterCallback");
    get(a.unregister_callback, "SteamAPI_UnregisterCallback");
    get(a.friends, "SteamAPI_SteamFriends_v017");
    get(a.invite_dialog, "SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog");
    get(a.set_rich_presence, "SteamAPI_ISteamFriends_SetRichPresence");
    get(a.clear_rich_presence, "SteamAPI_ISteamFriends_ClearRichPresence");
    get(a.friend_name, "SteamAPI_ISteamFriends_GetFriendPersonaName");
    get(a.persona_name, "SteamAPI_ISteamFriends_GetPersonaName");
    if (missing) {
        // A different steam_api64.dll: permanent.
        g_disabled = true;
        g_status = "join: Steam API of this game version not supported";
        return;
    }
    int32_t hu = 0;
    if (!call(hu, a.get_hsteam_user)) return fail("GetHSteamUser");
    if (hu == 0) return;  // Steam not initialised yet
    if (!call(a.frn, a.friends)) return fail("SteamFriends");
    if (!a.frn) return;
    g_resolved = true;
    g_status = "join: ready";
}

}  // namespace

std::optional<uint64_t> join_launch_lobby() {
    const wchar_t* cmd = GetCommandLineW();
    if (!cmd) return std::nullopt;
    return find_connect_lobby(std::wstring_view(cmd));
}

void join_poll() {
    std::lock_guard lock(g_mutex);
    resolve_locked();
    if (!g_resolved || g_disabled || g_registered) return;
    if (!call_void(g_api.register_callback, static_cast<SteamCallbackBase*>(&g_lobby_cb),
                   kGameLobbyJoinRequested) ||
        !call_void(g_api.register_callback, static_cast<SteamCallbackBase*>(&g_presence_cb),
                   kGameRichPresenceJoinRequested)) {
        return fail("RegisterCallback");
    }
    // The Steam overlay's open / closed state: while it is open the mouse and the keys are Steam's, not our pages'
    // (playtest 2026-11-10: our mouse hook ate every click meant for the invite dialog). Not fatal if it fails.
    if (!call_void(g_api.register_callback, static_cast<SteamCallbackBase*>(&g_overlay_cb), kGameOverlayActivated)) {
        log::warn("Join: the Steam overlay's state is not available; our pages keep the mouse while it is open");
    }
    g_registered = true;
    g_status = "join: ready (Steam invites and Join Game)";
    log::info("Join: listening for Steam 'Join Game' and invites");
}

uint64_t join_take_request() {
    const uint64_t lobby = g_request.exchange(0);
    if (lobby) log::info("Join: Steam asked to join lobby {} (request {})", lobby, g_requests_seen.load());
    return lobby;
}

bool join_overlay_active() {
    static std::atomic<uint32_t> logged{0};
    const uint32_t n = OverlayCallback::g_overlay_changes.load();
    const bool active = g_overlay_active.load();
    if (n != logged.exchange(n)) log::info("Join: the Steam overlay is {}", active ? "open (its mouse and keys are Steam's)" : "closed");
    return active;
}

bool join_open_invite(uint64_t lobby) {
    std::lock_guard lock(g_mutex);
    resolve_locked();
    if (!g_resolved || g_disabled || !lobby) return false;
    if (!call_void(g_api.invite_dialog, g_api.frn, static_cast<CSteamID>(lobby))) {
        fail("ActivateGameOverlayInviteDialog");
        return false;
    }
    log::info("Join: opened the Steam invite dialog for lobby {}", lobby);
    return true;
}

namespace {
void set_presence_locked(const char* key, const char* value) {
    bool ok = false;
    if (!call(ok, g_api.set_rich_presence, g_api.frn, key, value)) fail("SetRichPresence");
}
}  // namespace

void join_presence_hosting(uint64_t lobby, int members, int max_members, bool joinable) {
    std::lock_guard lock(g_mutex);
    resolve_locked();
    if (!g_resolved || g_disabled || !lobby) return;
    char connect[64], group[32], size[16], status[96];
    std::snprintf(connect, sizeof connect, "+connect_lobby %llu", static_cast<unsigned long long>(lobby));
    std::snprintf(group, sizeof group, "%llu", static_cast<unsigned long long>(lobby));
    std::snprintf(size, sizeof size, "%d", members);
    std::snprintf(status, sizeof status, "Co-op+: hosting (%d/%d)", members, max_members);
    // "connect" is what puts "Join Game" on us in a friend's list; the group keys
    // show the party together there.
    set_presence_locked("connect", joinable ? connect : "");
    set_presence_locked("status", status);
    set_presence_locked("steam_player_group", group);
    set_presence_locked("steam_player_group_size", size);
}

void join_presence_guest(uint64_t lobby, const std::string& host_name) {
    std::lock_guard lock(g_mutex);
    resolve_locked();
    if (!g_resolved || g_disabled) return;
    char group[32], status[128];
    std::snprintf(group, sizeof group, "%llu", static_cast<unsigned long long>(lobby));
    std::snprintf(status, sizeof status, "Co-op+: in %s's world", host_name.empty() ? "a friend" : host_name.c_str());
    // No "connect" on a guest: the session holds two players, so a third would be
    // refused. Friends join through the host.
    set_presence_locked("connect", "");
    set_presence_locked("status", status);
    if (lobby) {
        set_presence_locked("steam_player_group", group);
        set_presence_locked("steam_player_group_size", "2");
    }
}

void join_presence_clear() {
    std::lock_guard lock(g_mutex);
    if (!g_resolved || g_disabled) return;
    if (!call_void(g_api.clear_rich_presence, g_api.frn)) fail("ClearRichPresence");
}

std::string join_persona_name(uint64_t steam_id) {
    std::lock_guard lock(g_mutex);
    resolve_locked();
    if (!g_resolved || g_disabled || !steam_id) return {};
    const char* name = nullptr;
    if (!call(name, g_api.friend_name, g_api.frn, static_cast<CSteamID>(steam_id))) {
        fail("GetFriendPersonaName");
        return {};
    }
    return name ? std::string(name) : std::string();
}

std::string join_own_name() {
    std::lock_guard lock(g_mutex);
    resolve_locked();
    if (!g_resolved || g_disabled) return {};
    const char* name = nullptr;
    if (!call(name, g_api.persona_name, g_api.frn)) {
        fail("GetPersonaName");
        return {};
    }
    return name ? std::string(name) : std::string();
}

void join_shutdown() {
    std::lock_guard lock(g_mutex);
    if (!g_registered) return;
    call_void(g_api.unregister_callback, static_cast<SteamCallbackBase*>(&g_lobby_cb));
    call_void(g_api.unregister_callback, static_cast<SteamCallbackBase*>(&g_presence_cb));
    call_void(g_api.unregister_callback, static_cast<SteamCallbackBase*>(&g_overlay_cb));
    g_overlay_active = false;
    g_registered = false;
    if (g_resolved && !g_disabled) call_void(g_api.clear_rich_presence, g_api.frn);
}

bool join_available() {
    std::lock_guard lock(g_mutex);
    return g_resolved && !g_disabled;
}

std::string join_status() {
    std::lock_guard lock(g_mutex);
    return g_status;
}

}  // namespace cdmp::net
