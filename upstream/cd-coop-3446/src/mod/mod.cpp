#include "mod/mod.h"
#include "mod/native_layer.h"
#include "mod/input/actions.h"
#include "mod/input/pad_input.h"

#include <array>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "build_id.h"
#include "common/client_session.h"
#include "common/clock.h"
#include "common/transport.h"
#include "common/udp.h"
#include "mod/config.h"
#include "mod/game/game_errors.h"
#include "mod/game/main_menu.h"
#include "mod/game/offsets.h"
#include "mod/game/player_reader.h"
#include "mod/game/safe_memory.h"
#include "mod/crash_log.h"
#include "mod/online_guard.h"
#include "mod/log.h"
#include "mod/net/coop.h"
#include "mod/net/aggro_test.h"
#include "mod/net/attack_probe.h"
#include "mod/net/load_trace.h"
#include "mod/net/quest_events.h"
#include "mod/net/quest_advance.h"
#include "mod/optin.h"
#include "mod/net/slowmo.h"
#include "mod/net/puppet_visibility.h"
#include "mod/net/enemy_relay.h"
#include "mod/net/horse_diag.h"
#include "mod/game/actor_lookup.h"
#include "mod/net/local_capture.h"
#include "mod/net/guest_hits.h"
#include "mod/net/loot_twin.h"
#include "mod/net/learnt_store.h"
#include "mod/net/native_suppress.h"
#include "mod/net/piece4_test.h"
#include "mod/net/player_hits.h"
#include "mod/net/puppet_minimap.h"
#include "mod/net/remote_puppet.h"
#include "mod/net/revive_prompt.h"
#include "mod/net/dev_script.h"
#include "mod/net/coop_activity.h"
#include "mod/net/coop_sync.h"
#include "mod/net/coop_revive.h"
#include "mod/net/coop_travel.h"
#include "mod/net/coop_chat.h"
#include "mod/net/kill_feed.h"
#include "mod/net/session_summary.h"
#include "mod/net/coop_hideseek.h"
#include "mod/net/coop_fishing.h"
#include "mod/net/fast_lane.h"
#include "mod/net/coop_lobby.h"
#include "mod/net/native_mode.h"
#include "mod/net/lag_probe.h"
#include "mod/two_copy.h"
#include "mod/net/coop_race.h"
#include "mod/net/scoreboard.h"
#include "mod/net/state_audit.h"
#include "mod/net/coop_duel.h"
#include "mod/net/party_quests.h"
#include "mod/net/coop_hunt.h"
#include "mod/net/stage_cinema.h"
#include "mod/net/weather_sync.h"
#include "mod/net/party_scaling.h"
#include "mod/net/save_guard.h"
#include "mod/net/story_sync.h"
#include "mod/net/quest_live.h"
#include "mod/save_io.h"
#include "mod/net/coop_worlds.h"
#include "mod/game/ui_state.h"
#include "mod/net/boss_safe.h"
#include "mod/net/steam_transport.h"
#include "mod/net/room_code.h"
#include "mod/net/steam_join.h"
#include "mod/net/direct_ip.h"
#include "mod/net/prologue_hold.h"
#include "mod/net/xp_share.h"
#include "mod/net/time_sync.h"
#include "mod/net/troc_sniffer.h"
#include "mod/render/dx12_hook.h"
#include "mod/render/hud.h"
#include "mod/settings_menu.h"
#ifdef CDMP_COOPPLUS_SIM
#include "sim/config.hpp"
#endif

#pragma comment(lib, "version.lib")

namespace cdmp {
namespace {

// The commit this build comes from (build_id.h): both players must match.
constexpr uint32_t kModBuild = CDMP_BUILD_ID;

HMODULE g_self = nullptr;
HANDLE g_thread = nullptr;
HANDLE g_stop = nullptr;
HANDLE g_late_thread = nullptr;

bool stop_requested(DWORD wait_ms = 0) {
    return WaitForSingleObject(g_stop, wait_ms) == WAIT_OBJECT_0;
}

std::wstring module_path(HMODULE module) {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) return {};
        if (n < path.size()) {
            path.resize(n);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::wstring dir_of(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash + 1);
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

std::string file_version(const std::wstring& path) {
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) return "unknown";
    std::vector<uint8_t> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return "unknown";
    VS_FIXEDFILEINFO* info = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &len) || !info)
        return "unknown";
    return std::format("{}.{}.{}.{}", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                       HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
}

// Facts that identify the exact game build. Every offset/signature we find
// later is only valid for one build, so the log must always say which one.
// Co-op+: the game build this mod was made for (the same PE timestamp guard the
// readers and the sniffer use). On any other build co-op cannot work, so the
// network side does not start either, and the panel says why.
bool game_build_supported() {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return base == game::build_3576::kImageBase && nt->FileHeader.TimeDateStamp == game::build_3576::kPeTimestamp;
}

bool log_process_info() {
    const std::wstring exe = module_path(nullptr);
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);

    log::info("Host process: {} (PID {})", narrow(exe), GetCurrentProcessId());
    log::info("Host version: {}  image base: 0x{:X}  image size: 0x{:X}  PE timestamp: 0x{:08X}",
              file_version(exe), base, nt->OptionalHeader.SizeOfImage, nt->FileHeader.TimeDateStamp);

    const bool in_game = exe.ends_with(L"\\CrimsonDesert.exe");
    log::info("{}", in_game ? "Running inside CrimsonDesert.exe"
                            : "Not running inside the game (test host); game features stay off");

    if (HMODULE winmm = GetModuleHandleW(L"winmm.dll")) {
        log::info("winmm.dll loaded from: {}", narrow(module_path(winmm)));
    }
    return in_game;
}

// Outside the game (test host) there is no player to read, so we walk a 2 m
// circle instead, flagged as synthetic.
proto::PlayerState synthetic_state(uint32_t t_ms) {
    const float angle = static_cast<float>(t_ms % 8000) / 8000.0f * 6.2831853f;
    proto::PlayerState s;
    s.pos = {2.0f * std::cos(angle), 0.0f, 2.0f * std::sin(angle)};
    s.rot = {0.0f, std::sin(angle * 0.5f), 0.0f, std::cos(angle * 0.5f)};
    s.flags = proto::kStateSynthetic;
    return s;
}

// Where the local player's state comes from: game memory inside the game,
// the synthetic circle in the test host.
class LocalSource {
public:
    explicit LocalSource(bool in_game) : in_game_(in_game) {
        if (!in_game_) return;
        if (reader_.init()) {
            log::info("Game reader enabled (build 1.0.0.3576 offsets)");
        } else {
            log::warn("Game reader DISABLED: {}. Only networking will run.",
                      reader_.last_failure());
        }
    }

    // nullopt while the player is not in the world: nothing is sent then.
    std::optional<proto::PlayerState> sample(uint32_t now) {
        if (!in_game_) return synthetic_state(now);

        last_ = reader_.read();
        // Log only transitions, not every failed 20 Hz read.
        const std::string status = last_ ? "in world" : reader_.last_failure();
        if (status != status_) {
            log::info("Local player: {}", status);
            status_ = status;
        }
        if (!last_) return std::nullopt;

        proto::PlayerState s;
        s.pos = last_->position;
        s.rot = last_->rotation;
        s.health = last_->health;
        s.health_max = last_->health_max;
        s.stamina = last_->stamina;
        s.stamina_max = last_->stamina_max;
        return s;
    }

    const std::optional<game::PlayerSnapshot>& snapshot() const { return last_; }
    bool in_game() const { return in_game_; }
    const std::string& status() const { return status_; }

    std::string describe() const {
        if (!in_game_) return "synthetic";
        if (!last_) return "unavailable (" + status_ + ")";
        const auto& p = *last_;
        return std::format("pos ({:.2f}, {:.2f}, {:.2f}) rot ({:.3f}, {:.3f}, {:.3f}, {:.3f}) "
                           "hp {:.1f}/{:.1f} stamina {:.1f}/{:.1f} | check vs game global {:.2f} m",
                           p.position.x, p.position.y, p.position.z, p.rotation.x, p.rotation.y,
                           p.rotation.z, p.rotation.w, p.health, p.health_max, p.stamina,
                           p.stamina_max, p.static_pos_error);
    }

private:
    bool in_game_;
    game::PlayerReader reader_;
    std::optional<game::PlayerSnapshot> last_;
    std::string status_;
};

// Why a network session ended (Co-op+): the player stopped the mod, accepted a
// Steam join into another world, or left the world they were visiting.
struct SessionEnd {
    enum class Kind { Stop, Join, Leave, Direct } kind = Kind::Stop;
    uint64_t lobby = 0;  // Join: the lobby Steam asked for
    net::DirectRequest direct;  // Direct: the co-op menu's Radmin VPN / Direct IP choice (Host, Join, back to Steam)
};

// True when the game owns the foreground window, so hotkeys typed into another
// program (or into the Steam overlay's text boxes) do not trigger co-op actions.
bool game_has_focus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// One key press (not a held key): true on the frame the combination goes down.
struct HotkeyEdge {
    bool was_down = false;
    bool pressed(bool down) {
        const bool edge = down && !was_down;
        was_down = down;
        return edge;
    }
};
bool key_down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

std::string g_compat_note;
std::wstring g_onboard_ini;  // Co-op+ (T31): cdmp.ini when the controls card has not been shown yet ("" = it has)  // Co-op+ (T28): other mods next to the game, shown once in the world
uint32_t g_audit_due = 0;  // Co-op+ (T08): when the last session ended (0 = no audit due)

// Co-op+ (T08): the state audit, once, 30 s after a session ended (net/state_audit.h).
void audit_tick(uint32_t now) {
    if (!g_audit_due || now - g_audit_due < 30000) return;
    g_audit_due = 0;
    const net::StateAudit audit = net::state_audit();
    if (audit.clean) {
        log::info("State audit 30 s after the session: clean ({})", audit.line);
    } else {
        log::warn("State audit 30 s after the session: LEFT BEHIND: {} ({})", audit.leaks, audit.line);
    }
}

SessionEnd run_network(const Config& cfg, LocalSource& local) {
    using Kind = SessionEnd::Kind;
    // Co-op+ low latency: 1 ms timer resolution while the network runs, so its 1 ms waits are 1 ms and not 15.6.
    // Through winmm.dll (our own loader's name; it forwards to the system one).
    using TimePeriodFn = unsigned(__stdcall*)(unsigned);
    const HMODULE winmm = GetModuleHandleW(L"winmm.dll");
    const auto time_begin = winmm ? reinterpret_cast<TimePeriodFn>(GetProcAddress(winmm, "timeBeginPeriod")) : nullptr;
    const auto time_end = winmm ? reinterpret_cast<TimePeriodFn>(GetProcAddress(winmm, "timeEndPeriod")) : nullptr;
    if (time_begin) time_begin(1);
    struct TimerBack {
        TimePeriodFn end;
        ~TimerBack() {
            if (end) end(1);
        }
    } timer_back{time_begin ? time_end : nullptr};
    if (!net::startup()) {
        log::error("Winsock startup failed");
        render::hud_set_problem(render::Problem::Network,
                                "Co-op can't start: Windows networking did not start. Restart the PC; if it stays, "
                                "send cdmp.log (Diagnose Co-op+).");
        return {};
    }

    // A guest knows whom to talk to; a host learns it from the first Hello.
    const bool host = cfg.net_role_host;
    const uint64_t network_started_ms = now_ms();  // Co-op+ dev: [dev] join_switch_s
    std::unique_ptr<net::Transport> transport;
    net::SteamTransport* steam = nullptr;
    std::optional<net::PeerAddr> peer;
    // [room]: a guest with a code retries the lookup in the loop below (Steam
    // needs a moment to join the lobby and see the host field).
    bool room_code_pending = false;
    std::string room_code_text;
    // Co-op+: the lobby a Steam invite / "Join Game" named (resolved like a code).
    uint64_t room_lobby = 0;
    bool room_failed_logged = false;
    // The host's lobby creation is retried in the loop for the same reason: this
    // point in startup is reached BEFORE the save finishes loading (the mod loads
    // as winmm.dll, long before WorldSystem exists), so a single attempt here
    // happens against a world that is not there yet and is then never repeated.
    // Observed live: the attempt fired at 16:06:52 and the player only entered the
    // world at 16:08:17, 85 s later, so no lobby was ever created and the panel
    // never showed a room code -- with the log looking otherwise healthy.
    bool room_host_pending = false;
    if (cfg.net_transport_steam) {
        if (!local.in_game()) {
            log::error("The Steam transport only works inside the game");
            net::cleanup();
            return {};
        }
        auto st = std::make_unique<net::SteamTransport>(cfg.steam_channel, host, cfg.steam_peer_id);
        steam = st.get();
        transport = std::move(st);
        // [room] code: the short code that replaces the SteamID64. The host
        // creates the lobby and reads the code out; the guest resolves the
        // host's SteamID64 from it. If anything fails, the old paths
        // (peer_steamid, then the "only friend playing" scan) still work.
        if (!host && cfg.room_lobby) {
            // Co-op+: a Steam join names the lobby itself, so there is no code to decode.
            room_lobby = cfg.room_lobby;
            room_code_pending = true;
            room_code_text = net::room_code_encode(room_lobby);
            if (room_code_text.empty()) room_code_text = std::to_string(room_lobby);
            log::info("Room: joining through Steam, lobby {} (room {})", room_lobby, room_code_text);
        } else if (cfg.room_code.empty()) {
            if (host) {
                // One attempt now, and the loop retries if it does not take.
                //
                // room_available() is NOT used as a gate here any more. It was, and
                // that is why the retry added earlier never fired: room_available()
                // resolves the Steam flat API and latches g_disabled when
                // steam_api64.dll is not loaded YET -- which it is not this early,
                // because the mod loads as winmm.dll before the game's Steam
                // initialisation. The gate was therefore false, the whole block was
                // skipped, room_host_pending never became true, and the retry in the
                // loop had nothing to retry. The log showed no Room line at all,
                // which is exactly what a silently-skipped block looks like.
                //
                // room_host_open() already calls room_available() and returns an
                // empty string when it is not ready, so calling it unconditionally is
                // both safe and the only way the retry can be armed.
                const std::string code = net::room_host_open();
                if (!code.empty()) {
                    log::info("Room: your code is {} -- send it to your friend", code);
                } else {
                    room_host_pending = true;
                    log::info("Room: not ready to host yet (Steam or the world is still coming up); "
                              "will keep trying");
                }
            }
        } else {
            uint64_t lobby = 0;
            if (!net::room_code_decode(cfg.room_code, lobby)) {
                log::warn("Room: '{}' is not a valid code ({} characters, letters and digits)",
                          cfg.room_code, net::kRoomCodeLen);
                render::hud_set_problem(render::Problem::Join,
                                        std::format("The room code '{}' in cdmp.ini is not valid ({} letters and digits)."
                                                    " Fix it under [room] code= and restart the game, or use Join Game "
                                                    "in Steam instead.", cfg.room_code, net::kRoomCodeLen));
            } else {
                room_code_pending = true;
                room_code_text = net::room_code_normalise(cfg.room_code);
                log::info("Room: looking for the host of room {} ...", room_code_text);
            }
        }
        if (!host && cfg.steam_peer_id) {
            peer = net::PeerAddr::steam(cfg.steam_peer_id);
            steam->set_peer(cfg.steam_peer_id);
        }
    } else if (cfg.net_transport_direct) {
        // Co-op+ direct play (direct_ip.h): Radmin VPN / LAN / any VPN, straight to the friend's address. A host with an
        // invite (the co-op menu's Host) needs no friend address: whoever knocks with the invite's secret gets in.
        const bool invite_host = host && cfg.direct_token != 0;
        const auto friend_ep = cfg.peer_host.empty() ? std::nullopt : net::Endpoint::resolve(cfg.peer_host, cfg.peer_port);
        std::string why;
        std::optional<uint32_t> local_ip;
        if (friend_ep && friend_ep->is_loopback()) {
            local_ip = friend_ep->ip, why = "this PC (a test with two copies)";
        } else if (invite_host && cfg.dev_direct_loopback) {
            local_ip = net::Endpoint{0x0100007Fu, 0}.ip, why = "[dev] direct_loopback (two copies on this PC)";
        } else if ((friend_ep || invite_host) && !cfg.bind_host.empty()) {
            if (const auto b = net::Endpoint::resolve(cfg.bind_host, 0)) local_ip = b->ip, why = "bind_host";
            else why = "bind_host '" + cfg.bind_host + "' is not an address";
        } else if (friend_ep) {
            local_ip = net::direct_local_address_for(friend_ep->ip, why);
        } else if (invite_host) {
            std::string name;
            local_ip = net::direct_host_address(name, why);
            if (local_ip) why = "the adapter '" + name + "'";
        }
        log::info("Direct: our addresses: {}", net::direct_own_addresses());
        net::DirectState ds;
        if ((!invite_host && (!friend_ep || friend_ep->ip == 0)) || !local_ip) {
            const std::string text = !local_ip && (friend_ep || invite_host)
                ? "Direct co-op: " + why + "."
                : "Direct co-op: set peer_host in cdmp.ini to your FRIEND's VPN address (Radmin VPN shows it).";
            log::error("{} (peer_host '{}')", text, cfg.peer_host);
            render::hud_set_problem(render::Problem::Network, text);
            ds.error = text;
            net::direct_set_state(ds);
            net::cleanup();
            return {};
        }
        auto udp = std::make_unique<net::UdpTransport>();
        const net::Endpoint local{*local_ip, host ? cfg.peer_port : uint16_t{0}};
        if (!udp->open_direct(local, friend_ep ? friend_ep->ip : 0, cfg.direct_token, !host)) {
            const std::string text = "Direct co-op: cannot use " + local.str() + " (" + net::UdpSocket::last_error() +
                                     "); is another copy of the game hosting?";
            log::error("{}", text);
            render::hud_set_problem(render::Problem::Network, text);
            ds.error = text;
            net::direct_set_state(ds);
            net::cleanup();
            return {};
        }
        log::info("Direct: {} on {} ({}), friend {}{}", host ? "hosting" : "joining", local.str(), why,
                  friend_ep ? friend_ep->str() : std::string("anyone with the invite"),
                  cfg.direct_token ? " (invite)" : "");
        if (!host) peer = net::PeerAddr::udp(*friend_ep);
        ds.active = true;
        ds.host = host;
        ds.address = net::direct_dotted(*local_ip);
        if (friend_ep) ds.friend_address = net::direct_dotted(friend_ep->ip);
        if (invite_host) ds.invite = net::direct_invite_encode(*local_ip, cfg.peer_port, cfg.direct_token);
        net::direct_set_state(ds);
        transport = std::move(udp);
    } else {
        // UDP is only for tests on one PC (cdmp_peer, cdmp_host): it never
        // leaves this computer. Between players only Steam is used: it hides
        // the IPs, encrypts, and the host accepts only friends.
        auto udp = std::make_unique<net::UdpTransport>();
        bool ok = false;
        if (host) {
            ok = udp->open(cfg.peer_port, /*loopback_only=*/true);
        } else if (auto ep = net::Endpoint::resolve(cfg.peer_host, cfg.peer_port)) {
            if (!ep->is_loopback()) {
                log::error("UDP only connects within this PC (tests); to play with someone use transport=steam");
                net::cleanup();
                return {};
            }
            peer = net::PeerAddr::udp(*ep);
            ok = udp->open(0, true);
        } else {
            log::error("Cannot resolve peer host '{}'", cfg.peer_host);
            net::cleanup();
            return {};
        }
        if (!ok) {
            log::error("Cannot open UDP socket: {}", net::UdpSocket::last_error());
            net::cleanup();
            return {};
        }
        transport = std::move(udp);
    }
    render::hud_set_problem(render::Problem::Network, {});  // Co-op+ (T29)
    log::info("Network: {} as {}; peer {}", transport->status(), host ? "host" : "guest",
              peer ? peer->str() : (host ? "= whoever connects" : "= the only friend playing"));

    // Co-op+ low latency: the fast lane (fast_lane.h) sends our movement from its own thread; it uses its copy of the
    // peer, which this loop keeps current (fast_lane_set_peer), while this loop uses `peer` itself.
    const auto loop_thread = std::this_thread::get_id();
    ClientSession session(
        cfg.player_name, kModBuild,
        [&](const std::vector<uint8_t>& bytes) {
            if (std::this_thread::get_id() == loop_thread) {
                if (peer) transport->send(*peer, bytes);
            } else if (const auto p = net::fast_lane_peer()) {
                transport->send(*p, bytes);
            }
        },
        [](const std::string& text) { log::info("Session: {}", text); },
        host ? ClientSession::Role::Responder : ClientSession::Role::Initiator);
    // Co-op+ (T03): the game version goes into the handshake; a mismatch (or another mod build) is refused.
    if (local.in_game()) session.set_game_version(file_version(module_path(nullptr)));
    session.set_test_peer_build(cfg.dev_fake_peer_build);
    std::string shown_problem;  // the refusal last shown as a notice
    bool was_connected = false;
    uint64_t refused_uninvited = 0;  // Co-op+ invite-only: Hellos from outside the lobby
    uint32_t peer_since = 0;  // host: when it locked onto its player
    uint32_t last_friend_scan = 0;
    size_t last_friend_count = SIZE_MAX;

    std::unique_ptr<net::CoopLink> coop;
    if (cfg.coop_enabled && local.in_game()) {
        coop = std::make_unique<net::CoopLink>(session, cfg.coop_delay_ms, cfg.coop_offset_m,
                                               cfg.coop_map_pin);
        // A real other player (Steam): each side only takes what the other role sends.
        coop->set_strict_roles(steam != nullptr);
        coop->set_predict(cfg.coop_predict);
        net::puppet_set_direct(cfg.coop_direct);
        net::fast_lane_start(*transport, [&session](proto::GameEvent ev) { session.send_event(std::move(ev), now_ms()); });
    }
    struct FastLaneStop {
        ~FastLaneStop() { net::fast_lane_stop(); }
    } fast_lane_stop_on_return;

    const uint32_t state_interval = 1000 / cfg.send_rate_hz;
    bool reduced_rate = false;  // Co-op+ (T11): poor link, half rate
    std::string revive_line;    // Co-op+ (T15)
    std::string travel_line;    // Co-op+ (T19)
    std::string chat_input, chat_quick;  // Co-op+ (T24)
    std::vector<std::string> chat_log;
    uint32_t last_state = 0;
    uint32_t last_report = now_ms();
    uint32_t last_room_attempt = 0;  // host lobby retry, see the loop below
    int room_attempts = 0;           // how many retries have failed, for the log
    // Ctrl+F12 writes a numbered mark in the log: both players press it at the
    // same moment ("now!") so their logs can be lined up afterwards.
    bool mark_was_down = false;
    int marks = 0;
    std::vector<uint8_t> buf(proto::kMaxPacketSize);
    // Co-op+ Steam join: hotkeys, rich presence, and a short notice for the panel.
    HotkeyEdge invite_key, leave_key;
    uint64_t host_leave_at = 0, host_gone_at = 0;  // tcB291: the host's Leave waits for the guest to go
    uint32_t last_presence = 0;
    std::string presence_said;
    std::string notice;
    uint32_t notice_until = 0;
    std::string toast, toast_name;
    uint32_t toast_until = 0;
    std::string activity_banner;  // Co-op+: coop_activity.h
    auto say = [&](std::string text) {
        log::info("Co-op+: {}", text);
        notice = std::move(text);
        notice_until = now_ms() + 6000;
    };
    SessionEnd end;

    // Co-op+ low latency: where a loop's time goes (the wait for input, everything else), logged every 10 s.
    std::vector<int64_t> loop_wait_us, loop_rest_us;
    std::array<std::vector<int64_t>, 12> loop_sections;  // [dev] where "the rest" goes: up to each of five points
    auto section_mark = std::chrono::steady_clock::now();
    const auto loop_section = [&](size_t i) {
        const auto t = std::chrono::steady_clock::now();
        loop_sections[i].push_back(std::chrono::duration_cast<std::chrono::microseconds>(t - section_mark).count());
        section_mark = t;
    };
    auto loop_mark = std::chrono::steady_clock::now();
    uint32_t loop_log_at = now_ms();
    const auto us_since = [](std::chrono::steady_clock::time_point t) {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t).count();
    };
    while (!stop_requested()) {
        loop_rest_us.push_back(us_since(loop_mark));
        if (now_ms() - loop_log_at >= 10000 && !loop_wait_us.empty()) {
            loop_log_at = now_ms();
            const auto pct = [](std::vector<int64_t>& v, size_t p) {
                std::sort(v.begin(), v.end());
                return v[std::min(v.size() - 1, v.size() * p / 100)] / 1000.0;
            };
            log::info("Timing: network loop, 10 s: {} loops | waiting for input p50 {:.1f} p95 {:.1f} ms | the rest "
                      "p50 {:.1f} p95 {:.1f} max {:.1f} ms",
                      loop_wait_us.size(), pct(loop_wait_us, 50), pct(loop_wait_us, 95), pct(loop_rest_us, 50),
                      pct(loop_rest_us, 95), pct(loop_rest_us, 100));
            std::string parts;
            for (auto& s : loop_sections) {
                if (!s.empty()) parts += std::format(" {:.1f}/{:.1f}", pct(s, 50), pct(s, 95));
                s.clear();
            }
            log::info("Timing: network loop sections (p50/p95 ms): before-coop, coop-1, coop-2-rest, rate, state, "
                      "player-block, activity, weather, summary, sync, resync, coop-update:{}", parts);
            // Co-op+ (2026-11-07, FPS): guarded reads in the last 10 s, all threads (safe_memory.cpp).
            {
                static game::SafeReadStats last{};
                const game::SafeReadStats s = game::safe_read_stats();
                log::info("Timing: guarded reads, 10 s: {} from the range cache, {} VirtualQuery calls, {} faulted",
                          s.hits - last.hits, s.queries - last.queries, s.faults - last.faults);
                last = s;
            }
            loop_wait_us.clear();
            loop_rest_us.clear();
        }
        transport->poll(now_ms());

        // Co-op+ (T07): Leave on any transport (it was read only with Steam, so a room-code / UDP guest could not).
        if (leave_key.pressed(input::action_down(input::Action::Leave, game_has_focus()))) {
            // tcB289: for both roles this is the game's own Return to Title; the guest's link ends at once.
            // tcB291: the host with a guest says goodbye first and returns once the guest has gone (or after 6 s).
            if (host && session.state() == ClientSession::State::Connected) {
                if (!host_leave_at) {
                    net::activity_send_bye();
                    host_leave_at = now_ms() + 6000;
                    log::info("Leave: goodbye sent to the guest; back to the title in 6 s at the latest");
                }
            } else {
                net::lobby_session_ended();
                game::main_menu_return_to_title();
                if (!host) {
                    end = {Kind::Leave, 0};
                    break;
                }
            }
        }
        if (host_leave_at) {
            // The guest's layer link ends when it leaves; its game then says goodbye to our server (about 0.5 s).
            if (session.state() != ClientSession::State::Connected && !host_gone_at) host_gone_at = now_ms() + 2500;
            if (now_ms() >= host_leave_at || (host_gone_at && now_ms() >= host_gone_at)) {
                log::info("Leave: back to the title ({})", host_gone_at ? "the guest has gone" : "the guest did not go in time");
                host_leave_at = host_gone_at = 0;
                net::lobby_session_ended();
                game::main_menu_return_to_title();
            }
        }
        if (!host && net::activity_take_bye()) {
            net::lobby_session_ended();
                game::main_menu_return_to_title();
            end = {Kind::Leave, 0};
            break;
        }
        // Co-op+: the co-op menu's Radmin VPN / Direct IP (Host, Join, Back to Steam) restarts the session on it.
        if (auto want = net::direct_take_request()) {
            log::info("Direct: the co-op menu asks for {}", want->kind == net::DirectRequest::Kind::Host   ? "Host"
                                                            : want->kind == net::DirectRequest::Kind::Join ? "Join"
                                                                                                           : "Steam");
            end = {Kind::Direct, 0, *want};
            break;
        }
        // Co-op+ dev ([dev] join_switch_s): this host becomes a guest, as when a Steam invite is accepted; once a game.
        if (static bool switched = false; host && cfg.dev_join_switch_s && !switched &&
                                          now_ms() - network_started_ms >= uint64_t{cfg.dev_join_switch_s} * 1000) {
            switched = true;
            log::info("Join: [dev] join_switch_s: acting as if a Steam invite was accepted now");
            end = {Kind::Join, 0};
            break;
        }
        if (steam) {
            net::join_poll();
            // A Steam invite accepted, or "Join Game" pressed on a friend: end this
            // session and come back as that world's guest.
            if (const uint64_t want = net::join_take_request()) {
                if (want == net::room_lobby_id()) {
                    log::info("Join: already in lobby {}; nothing to do", want);
                } else if (kNativeLayer && !net::lobby_view().at_title) {
                    // Shared world (user, 2026-11-10): inviting and joining happen at the title screen's co-op menu
                    // only. An invite accepted from inside a world is not followed.
                    log::info("Join: lobby {} asked for while in a world: ignored (join from the title screen)", want);
                    say("to join a friend, go back to the title screen first");
                } else {
                    end = {Kind::Join, want};
                    break;
                }
            }
            const bool focus = game_has_focus();
            if (invite_key.pressed(input::action_down(input::Action::Invite, focus))) {
                if (kNativeLayer && !net::lobby_view().show) {
                    // Shared world: the title menu's Invite Friends card is the only invite (no in-game key).
                } else if (!host) {
                    say("only the host can invite (you are in a friend's world)");
                } else if (!net::room_lobby_id()) {
                    say("no room yet: load a save, wait a few seconds, then invite again");
                } else if (!net::join_open_invite(net::room_lobby_id())) {
                    say("the Steam overlay is not available (Steam > Settings > In Game)");
                } else {
                    say("Steam overlay opened: pick a friend to invite");
                }
            }
            // Rich presence: only when it changes, at most every 2 s.
            if (now_ms() - last_presence >= 2000) {
                last_presence = now_ms();
                const bool linked = session.state() == ClientSession::State::Connected;
                std::string want;
                if (host && net::room_lobby_id()) {
                    want = std::format("h{}:{}", net::room_lobby_id(), linked ? 2 : 1);
                } else if (!host && linked) {
                    want = std::format("g{}:{}", net::room_lobby_id(), session.peer_name());
                }
                if (want != presence_said) {
                    presence_said = want;
                    if (want.empty()) {
                        net::join_presence_clear();
                    } else if (host) {
                        net::join_presence_hosting(net::room_lobby_id(), linked ? 2 : 1, 2,
                                                   !net::room_invite_only());
                    } else {
                        net::join_presence_guest(net::room_lobby_id(), session.peer_name());
                    }
                }
            }
        }

        // The host's lobby, retried until it takes. Startup runs before the save is
        // loaded, so the first attempt reliably fails on a cold start; without this
        // the host never gets a room code at all and the guest has nothing to type.
        //
        // Gated on in_game() and spaced 5 s apart because the attempt BLOCKS: Steam
        // lobby creation is asynchronous and wait_call polls it for up to 10 s per
        // call. This runs on the mod's own thread, so the game does not stall, but a
        // failing attempt still costs the loop that long -- retrying every tick
        // would starve the state sync that shares it. Once in the world the first
        // attempt succeeds, so this normally costs one call.
        if (room_host_pending && steam && steam->ready() && local.in_game() &&
            now_ms() - last_room_attempt >= 5000) {
            last_room_attempt = now_ms();
            const std::string code = net::room_host_open();
            if (!code.empty()) {
                room_host_pending = false;
                log::info("Room: your code is {} -- send it to your friend", code);
            } else if (room_attempts++ < 3) {
                // Say WHY on the first few failures. The previous version retried
                // silently, so a retry that never succeeded was indistinguishable
                // from a retry that never ran -- which is exactly the dead end this
                // took two rounds to get out of.
                log::info("Room: attempt {} did not produce a code; {}", room_attempts,
                          net::room_status());
            }
        }

        // Steam guest without a configured host: the only friend playing.
        if (steam && !host && !peer && steam->ready() && now_ms() - last_friend_scan >= 2000) {
            last_friend_scan = now_ms();
            // A room code wins over the friends scan: it names one host exactly.
            if (room_code_pending) {
                const uint64_t host_id = room_lobby ? net::room_guest_resolve_lobby(room_lobby)
                                                    : net::room_guest_resolve(room_code_text);
                if (host_id) {
                    peer = net::PeerAddr::steam(host_id);
                    steam->set_peer(host_id);
                    room_code_pending = false;
                    log::info("Room: room {} found; connecting to the host", room_code_text);
                } else if (!room_failed_logged) {
                    log::info("Room: room {} not found yet; keep the host's game open and the "
                              "lobby alive, we keep trying", room_code_text);
                    room_failed_logged = true;  // one line is enough; the loop keeps retrying
                }
            } else {
                const auto playing = steam->friends_playing();
                if (playing.size() == 1) {
                    peer = net::PeerAddr::steam(playing[0].id);
                    steam->set_peer(playing[0].id);
                    log::info("Network: connecting to '{}' (the only friend playing)", playing[0].name);
                } else if (playing.size() != last_friend_count) {
                    last_friend_count = playing.size();
                    log::info("Network: {} friends playing; put the host's SteamID64 in [steam] "
                              "peer_steamid (listed above when Steam got ready)",
                              playing.size());
                }
            }
        }

        // Everything that is waiting, not one datagram per loop: with ~100
        // events/s (a busy city) handling one per loop built a backlog of
        // seconds in the first real test (RTT up to 2.9 s).
        net::PeerAddr from;
        // Co-op+ low latency (2026-11-06): wait for input in 1 ms slices (timer at 1 ms, set below) and stop as soon
        // as our game captured something to send. One 5 ms wait made a loop of ~11.5 ms (87 a second): our moves
        // waited up to that long to go out, and the other player's as long to come in.
        int wait_slices = 5;
        const auto wait_from = std::chrono::steady_clock::now();
        for (int handled = 0; handled < 512;) {
            std::optional<size_t> n;
            bool event_handled = false;  // the fast lane gave its event to the copy already
            if (net::fast_lane_running()) {
                // The fast lane receives; it wakes us for each datagram and for our slow outbox.
                auto d = net::fast_lane_next(wait_slices > 0 ? 5 : 0);
                wait_slices = 0;
                if (!d) break;
                from = d->from;
                n = std::min(d->bytes.size(), buf.size());
                std::memcpy(buf.data(), d->bytes.data(), *n);
                event_handled = d->event_handled;
            } else {
                n = transport->recv(from, buf, wait_slices > 0 ? 1 : 0);
                if (!n) {
                    if (wait_slices-- > 0 && !net::coop_outbox_waiting()) continue;
                    break;
                }
            }
            ++handled;
            wait_slices = 0;
            if (host && !peer) {
                // Lock onto the first player who greets us.
                // Only a Hello of our protocol: one of another version would
                // never connect and would keep the place (security review M2).
                auto packet = proto::decode({buf.data(), *n});
                // Co-op+ invite-only: only someone who joined our lobby, i.e. accepted
                // our invite, may take the place.
                const bool invited = !steam || !net::room_invite_only() || net::room_is_member(from.value);
                if (!invited && refused_uninvited++ % 50 == 0) {
                    log::info("Network: {} is not in our lobby; invite-only, ignored", from.str());
                }
                if (invited && packet && packet->version == proto::kVersion &&
                    std::holds_alternative<proto::Hello>(packet->body)) {
                    peer = from;
                    peer_since = now_ms();
                    if (steam) steam->set_peer(from.value);
                    log::info("Network: player at {} is joining", from.str());
                }
            }
            if (peer && from == *peer) session.on_datagram({buf.data(), *n}, now_ms(), event_handled);
        }

        loop_wait_us.push_back(us_since(wait_from));
        loop_mark = std::chrono::steady_clock::now();
        section_mark = loop_mark;
        const uint32_t now = now_ms();
        session.tick(now);
        const bool connected = session.state() == ClientSession::State::Connected;
        // Co-op+ (prologue_hold.h): while either of us plays the prologue, what reaches into the games waits (the link,
        // chat, the party frame and the lobby stay).
        const bool linked = connected && !net::prologue_held();
        net::fast_lane_set_peer(peer, connected);
        net::RevivePrompt revive_prompt = net::RevivePrompt::None;
        if (host && was_connected && !connected && peer) {
            log::info("Network: {} left; waiting for a player", peer->str());
            peer.reset();
            if (steam) steam->set_peer(0);
        } else if (host && !was_connected && !connected && peer && now - peer_since > 5000) {
            log::info("Network: {} did not complete the handshake; waiting for a player", peer->str());
            peer.reset();
            if (steam) steam->set_peer(0);
        }
        // Co-op+: a short toast when the other player arrives or leaves, shown even
        // with the panel hidden.
        if (connected && !was_connected) {
            toast_name = session.peer_name().empty() ? std::string("Your friend") : session.peer_name();
            toast = host ? std::format("Co-op\n{} joined your world", toast_name)
                         : std::format("Co-op\nYou are in {}'s world", toast_name);
            toast_until = now + 6000;
        } else if (!connected && was_connected) {
            toast = host ? std::format("Co-op\n{} left your world", toast_name)
                    : kNativeLayer
                          ? std::format("Co-op\nLost the connection to {}'s world. Reconnecting by itself"
                                        " (Menu > Co-op > Leave the session goes back to the title screen)", toast_name)
                          : std::format("Co-op\nLost the connection to {}'s world. Reconnecting by itself;"
                                        " {{Leave}} goes back to your own world", toast_name);
            toast_until = now + 6000;
        }
        // Co-op+ (T29): a guest that cannot reach the host says so after 45 s, with what to check.
        {
            static uint32_t looking_since = 0;
            if (connected || host) {
                looking_since = 0;
                render::hud_set_problem(render::Problem::Join, {});
            } else {
                if (!looking_since) looking_since = now ? now : 1;
                if (now - looking_since > 60000 && transport->status().rfind("steam: waiting", 0) == 0) {
                    render::hud_set_problem(render::Problem::Join,
                                            "Steam is not ready for co-op: start the game from Steam while online, "
                                            "with the Steam overlay on (Steam > Settings > In Game).");
                } else if (now - looking_since > 45000 && session.peer_problem().empty()) {
                    const std::string who = toast_name.empty() ? std::string("the host") : toast_name;
                    render::hud_set_problem(
                        render::Problem::Join,
                        std::format("Can't reach {} yet. Check: both games running with a save loaded, the same "
                                    "Co-op+ zip, Steam online. Still stuck: Ctrl+F2, then Join Game again.", who));
                }
            }
        }
        // Co-op+ (T29): a save backup that failed is said where it is seen.
        if (coop) render::hud_set_problem(render::Problem::Backup, net::save_guard_problem());
        // Co-op+ (T08): 30 s after a session ends (the host's weather goes after 20 s), nothing of it may be left.
        if (!connected && was_connected) g_audit_due = now ? now : 1;
        if (!connected) audit_tick(now);
        was_connected = connected;
        // Co-op+ (T03): a refused handshake (versions) as a notice too, once per new reason.
        if (const std::string& problem = session.peer_problem(); problem != shown_problem) {
            shown_problem = problem;
            if (!problem.empty()) {
                toast = "Co-op\n" + problem;
                toast_until = now + 12000;
            }
        }
        loop_section(0);
        if (coop) {
            // Co-op+ party scaling: the host's enemies get tougher while a guest is in.
            // [dev] scaling_as_host: one-PC test of party scaling (the echo session runs as guest).
            const bool scale_host = host || cfg.dev_scaling_as_host;
            net::scaling_set_party(scale_host && linked ? 2 : 1, scale_host);
            net::scaling_tick();
            // Co-op+ save safety: a backup when a session starts, a warning on saves during it.
            net::save_guard_set_connected(connected, session.peer_name());
            // Co-op+ story sync: who we are linked to, and its notices.
            net::story_set_link(linked, session.role() == ClientSession::Role::Responder, session.peer_name(), now);
            if (std::string t = local.snapshot() ? net::story_take_toast() : std::string(); !t.empty()) {
                toast = std::move(t);
                toast_until = now + 25000;  // a few sentences, sometimes right after a loading screen
            }
            // Co-op+ live quests: the partner's finished quest steps go into our game (quest_live.h).
            net::quest_live_tick(linked, session.peer_name(), now);
            net::slowmo_set_connected(linked);
            if (std::string t = local.snapshot() ? net::quest_live_take_toast() : std::string(); !t.empty()) {
                toast = std::move(t);
                toast_until = now + 8000;
            }
            // Co-op+ co-op worlds: a save per friend; switched to when a session starts (coop_worlds.h).
            {
                net::WorldsInput wi;
                wi.now_ms = now;
                wi.connected = connected;
                if (connected && peer && peer->kind == net::PeerAddr::Kind::Steam) {
                    wi.partner_key = "steam-" + std::to_string(peer->value);
                } else if (connected && peer && cfg.net_transport_direct && peer->kind == net::PeerAddr::Kind::Udp &&
                           !peer->endpoint().is_loopback()) {
                    // direct play: the friend's VPN address names the co-op save (Radmin keeps it per account)
                    std::string ip = peer->endpoint().str();
                    ip = ip.substr(0, ip.rfind(':'));
                    std::replace(ip.begin(), ip.end(), '.', '-');
                    wi.partner_key = "direct-" + ip;
                }
                wi.partner_name = session.peer_name();
                wi.in_world = local.snapshot().has_value();
                if (const auto snap = local.snapshot()) {
                    wi.has_pos = true;
                    wi.pos_x = snap->position.x;
                    wi.pos_z = snap->position.z;
                }
                wi.menu_open = game::ui_menu_open();
                wi.host = session.role() == ClientSession::Role::Responder;
                net::worlds_tick(wi);
                // Co-op+ (2026-11-06): the title lobby. At the title = no character and no loading for 3 s.
                {
                    static uint32_t title_since = 0;
                    const bool out = local.in_game() && !wi.in_world && !net::sniffer_world_loading();
                    if (!out) title_since = 0;
                    else if (!title_since) title_since = now ? now : 1;
                    net::LobbyInput li;
                    li.now_ms = GetTickCount64();
                    li.at_title = out && now - title_since >= 3000;
                    li.in_world = wi.in_world;
                    li.loading = net::sniffer_world_loading();
                    li.run_generation = net::capture_template_generation();
                    li.connected = connected;
                    li.host = wi.host;
                    li.own_name = cfg.player_name;
                    li.partner_name = session.peer_name();
                    li.steam = !peer || peer->kind == net::PeerAddr::Kind::Steam;
                    li.direct = cfg.net_transport_direct;
                    li.direct_token = cfg.direct_token;
                    li.direct_port = cfg.peer_port;
                    if (peer && peer->kind == net::PeerAddr::Kind::Steam) li.partner_steam_id = peer->value;
                    if (peer && peer->kind == net::PeerAddr::Kind::Udp) {
                        const std::string ep = peer->endpoint().str();
                        li.partner_address = ep.substr(0, ep.rfind(':'));
                    }
                    net::lobby_tick(li);
                    // Co-op+ (2026-11-07): the prologue is played solo (prologue_hold.h).
                    net::PrologueInput pi;
                    pi.now_ms = li.now_ms;
                    pi.connected = connected;
                    pi.in_world = wi.in_world;
                    pi.generation = li.run_generation;
                    pi.new_save_run = net::lobby_view().plan == net::LobbyPlan::NewSave;
                    if (uint32_t carrier = 0; net::capture_carried_for(5000, &carrier)) pi.riding = carrier ? carrier : 1;
                    pi.partner_name = session.peer_name();
                    net::prologue_tick(pi);
                    net::xp_share_tick(connected && !net::prologue_held());  // Co-op+ shared XP (xp_share.h)
                }
                if (std::string t = net::worlds_take_toast(); !t.empty()) {
                    toast = std::move(t);
                    toast_until = now + 15000;
                }
            }
        }
        loop_section(1);
        if (coop) {
            std::optional<proto::Vec3> pos;
            proto::Quat rot;
            if (local.snapshot()) {
                pos = local.snapshot()->position;
                rot = local.snapshot()->rotation;
                const auto& s = *local.snapshot();
                // Co-op+ (T31): the controls card, the first time ever in the world with Co-op+.
                if (!g_onboard_ini.empty()) {
                    render::hud_show_guide();
                    WritePrivateProfileStringW(L"general", L"onboarded", L"1", g_onboard_ini.c_str());
                    log::info("Co-op+: first run: controls card shown (the Help key, Shift+F1, opens it again)");
                    g_onboard_ini.clear();
                }
                // Co-op+ (T28): the compatibility note, once, when we are first in the world.
                if (!g_compat_note.empty()) {
                    toast = "Co-op\n" + g_compat_note;
                    toast_until = now + 12000;
                    g_compat_note.clear();
                }
                // [dev] kill_key: Ctrl+Shift+F9 kills us (repeatable death tests on one PC).
                static bool kill_was_down = false;
                const bool kill_down = cfg.dev_kill_key && (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                                       (GetAsyncKeyState(VK_SHIFT) & 0x8000) && (GetAsyncKeyState(VK_F9) & 0x8000);
                if (kill_down && !kill_was_down && s.health > 0.0f) {
                    net::hits_dev_kill_self(static_cast<int64_t>(s.health * 1000.0f));
                }
                kill_was_down = kill_down;
                // [dev] kill_key also gives Ctrl+Shift+F4: teleport 30 m east (DoTeleportAck research).
                static bool tp_was_down = false;
                const bool tp_down = cfg.dev_kill_key && (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                                     (GetAsyncKeyState(VK_SHIFT) & 0x8000) && (GetAsyncKeyState(VK_F4) & 0x8000);
                if (tp_down && !tp_was_down) {
                    float tx = s.position.x + static_cast<float>(cfg.dev_tp_offset_m), ty = s.position.y, tz = s.position.z;
                    if (!cfg.dev_tp_to.empty()) std::sscanf(cfg.dev_tp_to.c_str(), "%f,%f,%f", &tx, &ty, &tz);
                    net::revive_dev_teleport(tx, ty, tz);
                }
                tp_was_down = tp_down;
                // [dev] script: Ctrl+Shift+F6 sends the packets written in that file (net/dev_script.h).
                static bool script_was_down = false;
                const bool script_down = !cfg.dev_script.empty() && (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                                         (GetAsyncKeyState(VK_SHIFT) & 0x8000) && (GetAsyncKeyState(VK_F6) & 0x8000);
                if (script_down && !script_was_down) {
                    net::dev_script_run(cfg.dev_script, net::capture_own_player_key(), s.position.x, s.position.y,
                                        s.position.z);
                }
                script_was_down = script_down;
                // Co-op+: Ctrl+F4 takes us to the other player (alive, not in a menu, 1 m beside them).
                static bool go_was_down = false;
                const bool go_down = linked && input::action_down(input::Action::GoToFriend, game_has_focus());
                if (go_down && !go_was_down && net::hideseek_active()) {
                    toast = "Hide and seek\nNo shortcuts to your friend during the game";
                    toast_until = now + 4000;
                } else if (go_down && !go_was_down && s.health > 0.0f) {
                    if (const auto& rs = session.remote_state(); rs && rs->health > 0.0f) {
                        net::revive_teleport_to(rs->pos.x + 1.0f, rs->pos.y + 0.5f, rs->pos.z);
                        log::info("Co-op: to the other player at ({:.0f}, {:.0f})", rs->pos.x, rs->pos.z);
                    }
                }
                go_was_down = go_down;
                // Co-op+: Ctrl+F3 pings our position to the other player (their map + a toast).
                // Co-op+ (T24): the Ping action opens the quick messages (coop_chat.h); "Go here", "Danger here!",
                // "Loot here" also drop the pin as before.
                {
                    const net::ChatView cv = net::chat_tick(connected, session.peer_name(), game_has_focus(), now);
                    chat_input = cv.input;
                    chat_quick = cv.quick;
                    chat_log = cv.log;
                    if (!cv.toast.empty()) {
                        toast = cv.toast;
                        toast_until = now + 6000;
                    }
                    if (cv.ping) {
                        net::capture_emit_ping(s.position.x, s.position.y, s.position.z);
                        log::info("Co-op: pinged our position ({:.0f}, {:.0f}) to the other player", s.position.x,
                                  s.position.z);
                    }
                }
                net::hits_note_own_health(s.health, s.health_max);  // duel hits need our HP (player_hits.h)
                // Co-op+: our own revive choices when the game's death screen stays away (revive_prompt.h).
                std::optional<float> partner_hp;
                if (linked) {
                    if (const auto& rs = session.remote_state()) partner_hp = rs->health;
                    if (cfg.dev_partner_alive) partner_hp = 100.0f;  // one-PC test of the boss-fight wait
                }
                revive_prompt = cfg.dev_no_revive_prompt  // research: the game's own death input only
                                    ? net::RevivePrompt::None
                                    : net::revive_prompt_tick(linked, s.health, s.health_max, partner_hp,
                                                              game_has_focus(), now);
                // Co-op+ (T26): [coop] respawn_at=partner -- a revive that put us back at the checkpoint (more than
                // 50 m from where we fell) is followed by the hop to the other player, once we stand again.
                {
                    static bool was_dead = false;
                    static proto::Vec3 died_at{};
                    static uint32_t up_at = 0;
                    const bool dead = s.health_max > 0 && s.health <= 0;
                    if (dead && !was_dead) died_at = s.position;
                    if (!dead && was_dead) up_at = now ? now : 1;
                    was_dead = dead;
                    if (up_at && now - up_at > 3000) {
                        up_at = 0;
                        const float dx = s.position.x - died_at.x, dz = s.position.z - died_at.z;
                        const auto& rs = session.remote_state();
                        if (settings::respawn_at_partner() && linked && rs && rs->health > 0 && dx * dx + dz * dz > 50.0f * 50.0f) {
                            net::revive_teleport_to(rs->pos.x + 1.0f, rs->pos.y + 0.5f, rs->pos.z);
                            toast = "Co-op\nBack to " + session.peer_name();
                            toast_until = now + 4000;
                            log::info("Co-op: respawn_at=partner: from the checkpoint to the other player");
                        }
                    }
                }
                // Co-op+ (T15): reviving each other.
                {
                    std::optional<net::ReviveRemote> rr;
                    if (linked) {
                        if (const auto& rs = session.remote_state()) {
                            rr = net::ReviveRemote{rs->health, rs->health_max, rs->pos};
                        }
                    }
                    const auto rv = net::revive_partner_tick(linked, s.health, s.health_max, s.position, rr,
                                                             session.peer_name(), game_has_focus(), now);
                    revive_line = rv.line;
                    // Co-op+ (T19): summon / party travel.
                    std::optional<proto::Vec3> partner_at;
                    if (rr) partner_at = rr->pos;
                    const auto tr = net::travel_tick(linked, s.position, partner_at, session.peer_name(),
                                                     game_has_focus(), now);
                    travel_line = tr.line;
                    if (!tr.toast.empty()) {
                        toast = tr.toast;
                        toast_until = now + 5000;
                    }
                    if (!rv.toast.empty()) {
                        toast = rv.toast;
                        toast_until = now + 6000;
                    }
                }
            }
            loop_section(5);
            // Co-op+: shared cutscenes (notice, everyone skips) and minigame notices (coop_activity.h). Every loop,
            // also when our position is not readable (in a minigame the player snapshot goes away).
            {
                std::optional<std::array<float, 3>> partner_pos, own_pos;
                if (linked) {
                    if (const auto& rs = session.remote_state()) partner_pos = std::array<float, 3>{rs->pos.x, rs->pos.y, rs->pos.z};
                }
                if (pos) own_pos = std::array<float, 3>{pos->x, pos->y, pos->z};
                auto act = net::activity_tick(linked, host, game_has_focus(), own_pos, partner_pos, session.peer_name(), now);
                loop_section(6);
                activity_banner = std::move(act.banner);
                if (std::string pl = net::prologue_line(); !pl.empty()) activity_banner = std::move(pl);  // prologue_hold.h
                if (!act.toast.empty()) {
                    toast = std::move(act.toast);
                    toast_until = now + 6000;
                }
                net::weather_tick(linked, host, own_pos.has_value(), own_pos, now);  // Co-op+: weather_sync.h
                loop_section(7);
                net::summary_tick(connected, session.peer_name(), own_pos, partner_pos, now);  // Co-op+: session_summary.h
                loop_section(8);
                // Co-op+ (T06): digests; a resync (Shift+F8 / View + RB, or the digests out of step) drops the
                // session and joins again: everything of the other player is removed here and sent again.
                net::sync_tick(linked, now);
                loop_section(9);
                static input::Edge resync_key;
                static uint32_t last_auto_resync = 0;
                std::string why = net::sync_take_resync();
                const bool manual = resync_key(input::action_down(input::Action::Resync, game_has_focus()));
                if (linked && !why.empty() && last_auto_resync && now - last_auto_resync < 120000) {
                    log::info("Sync: out of step ({}), but resynced less than 2 minutes ago; waiting", why);
                    why.clear();
                }
                if (linked && (manual || !why.empty())) {
                    if (!manual) last_auto_resync = now;
                    log::info("Sync: resync ({}): leaving and joining again", manual ? "asked for" : why);
                    toast = manual ? std::format("Co-op\nResynchronising with {}...", session.peer_name())
                                   : std::format("Co-op\nOut of step with {}: resynchronising...", session.peer_name());
                    toast_until = now + 6000;
                    session.disconnect(now);
                }
            }
            loop_section(10);
            coop->update(pos, rot, now);
            if (host && linked) net::aggro_keep_copy_in_battle(cfg.coop_copy_in_battle);  // Co-op+ (aggro_test.h)
            loop_section(11);
        }

        loop_section(2);
        // Co-op+ (T11): a poor link (rtt over 250 ms or the reliable channel backing up) halves the state rate
        // before anything else is given up; back to full rate once it is good again (rtt under 150 ms).
        {
            static bool reduced = false;
            static uint32_t reduced_since = 0;  // held at least 5 s (a burst of enemy copies does not flap it)
            const auto& nst = session.stats();
            const size_t pending = session.reliable_pending();
            const bool poor = nst.rtt_ms > 250.0f || pending > 32;
            const bool good = nst.rtt_ms < 150.0f && pending < 8;
            if (!reduced && poor) {
                reduced = true;
                reduced_since = now;
                log::info("Net: poor link (rtt {:.0f} ms, {} reliable pending): state updates at half rate", nst.rtt_ms, pending);
            } else if (reduced && good && now - reduced_since >= 5000) {
                reduced = false;
                log::info("Net: link good again (rtt {:.0f} ms): state updates at full rate", nst.rtt_ms);
            }
            reduced_rate = reduced;
        }
        loop_section(3);
        if (now - last_state >= (reduced_rate ? state_interval * 2 : state_interval)) {
            last_state = now;
            if (auto state = local.sample(now)) session.send_local_state(*state, now);

            render::HudData hud;
            hud.connected = session.state() == ClientSession::State::Connected;
            hud.peer_name = session.peer_name();
            hud.we_host = host;
            net::scoreboard_tick(connected);  // Co-op+ (T43)
            if (connected) {
                hud.score_lines = net::scoreboard_lines(session.peer_name());
                hud.peer_title = net::scoreboard_partner_title();
            }
            hud.rtt_ms = session.stats().rtt_ms;
            // Co-op+ (T32): packet loss over the last ~3 s.
            {
                static uint64_t last_lost = 0, last_recv = 0;
                static uint32_t last_at = 0;
                static float loss = -1;
                const auto& st = session.stats();
                if (!connected) {
                    loss = -1;
                    last_at = 0;
                } else if (!last_at || now - last_at >= 3000) {
                    const uint64_t dl = st.lost - std::min(st.lost, last_lost), dr = st.received - std::min(st.received, last_recv);
                    if (last_at && dl + dr > 0) loss = 100.0f * static_cast<float>(dl) / static_cast<float>(dl + dr);
                    last_lost = st.lost, last_recv = st.received, last_at = now ? now : 1;
                }
                hud.loss_pct = loss;
                hud.net_hud = settings::net_hud();
            }
            hud.local = local.snapshot();
            hud.local_status = local.status();
            hud.remote = session.remote_state();
            hud.peer_problem = session.peer_problem();
            hud.partner_in_menu = connected && net::activity_partner_in_menu();
            hud.revive_line = revive_line;
            hud.travel_line = travel_line;
            hud.chat_input = chat_input;
            hud.chat_quick = chat_quick;
            hud.chat_log = chat_log;
            for (auto& l : net::kill_feed_tick(connected, session.peer_name(), now)) {  // Co-op+ (T36)
                hud.kill_feed.push_back(std::move(l.text));
                hud.kill_feed_ours.push_back(l.ours);
            }
            hud.revive_prompt = static_cast<uint8_t>(revive_prompt);
            if (cfg.dev_ui_preview == 1) hud.revive_prompt = 1;  // [dev] ui_preview: the death screen
            // In co-op, put the marker on the character actually drawn (it
            // plays the network state `delay` ms later).
            if (coop && hud.remote) {
                if (auto pos = net::puppet_position()) hud.remote->pos = *pos;
            }
            if (coop) hud.enemy_zone = net::suppress_hud_text();
            if (coop) hud.scaling = net::scaling_hud_text();
            if (coop) {
                hud.save_notice = net::save_guard_hud_text();
                if (std::string s = net::worlds_hud_text(); !s.empty()) {
                    hud.save_notice += (hud.save_notice.empty() ? "" : "\n") + s;
                }
                if (std::string s = net::story_hud_text(); !s.empty()) {
                    hud.save_notice += (hud.save_notice.empty() ? "" : "\n") + s;
                }
                if (std::string s = net::quest_live_hud_text(); !s.empty()) {
                    hud.save_notice += (hud.save_notice.empty() ? "" : "\n") + s;
                }
            }
            // Boss bar (boss_safe.h): the guest has no native one.
            // Co-op+: guest only (now that the boss's health reaches boss_safe, the host would get two bars).
            if (const net::BossHud b = net::boss_hud(); b.active && !host) {
                hud.boss_active = true;
                hud.boss_name = b.name;
                hud.boss_hp = b.hp;
                hud.boss_hp_max = b.hp_max;
                hud.boss_cutscene = b.cutscene;
            }
            // The room code, so the host can read it out and the guest sees
            // what they are joining. When there is no code, carry the reason as
            // well: the panel shows it in place of the code, so a missing code is
            // visible on screen rather than only in cdmp.log.
            if (!room_code_text.empty()) {
                hud.room_code = room_code_text;
                hud.room_hosting = false;
            } else if (!net::room_current_code().empty()) {
                hud.room_code = net::room_current_code();
                hud.room_hosting = true;
            }
            if (hud.room_code.empty()) hud.room_status = net::room_status();
            // Co-op+: what the player can do right now, or what just happened.
            if (steam) {
                if (!notice.empty() && static_cast<int32_t>(now - notice_until) < 0) {
                    hud.coop_hint = notice;
                } else if (host) {
                    hud.coop_hint = net::room_lobby_id() && !kNativeLayer ? "Ctrl+F1: invite a Steam friend" : "";
                } else {
                    hud.coop_hint = "Ctrl+F2: leave and go back to your world";
                }
            }
            // Co-op+: the other player pinged their position (Ctrl+F3).
            if (static uint32_t pings_seen = 0; net::coop_pings_received() != pings_seen) {
                pings_seen = net::coop_pings_received();
                const std::string who = session.peer_name().empty() ? "Your friend" : session.peer_name();
                toast = std::format("{} pinged their position (see your map)", who);
                toast_until = now + 6000;
            }
            // Co-op+: once per session, when we first stand in the world: how to use co-op (the status panel is
            // hidden by default, as the game shows no debug boxes).
            // Shared world (user, 2026-11-10): no key banner at all. Invites are the title menu's; challenges move to
            // hold-interact on the friend and the panel into the main menu's Co-op Settings.
            if (static bool welcomed = false; !kNativeLayer && !welcomed && hud.local) {
                welcomed = true;
                toast = "Seamless Co-op\n{Invite} Invite a friend      {Challenge} Challenge      {Panel} Co-op panel";
                toast_until = now + 9000;
            }
            if (!toast.empty() && static_cast<int32_t>(now - toast_until) < 0) hud.toast = toast;
            hud.activity = activity_banner;
            if (kNativeLayer) {  // shared world: no hotkey is named in a line (hold interact, or Menu > Co-op)
                input::strip_unbound_hints(hud.toast);
                input::strip_unbound_hints(hud.activity);
            }
            if (const net::RpsView rv = net::activity_rps_view(); rv.on) {
                hud.rps_on = true;
                hud.rps_ours = rv.ours, hud.rps_theirs = rv.theirs;
                hud.rps_ours_picked = rv.ours_picked, hud.rps_theirs_picked = rv.theirs_picked;
                hud.rps_winner = rv.winner;
                hud.rps_reveal_t = rv.reveal_t;
            }
            if (const net::HuntHud hh = net::hunt_hud(); !hh.title.empty()) {
                hud.duel_title = hh.title;
                hud.duel_subtitle = hh.subtitle;
                hud.duel_title_color = hh.color;
                hud.duel_title_alpha = hh.alpha;
            }
            if (const net::FishingHud fh = net::fishing_hud(); !fh.title.empty()) {  // Co-op+ (2026-11-05)
                hud.duel_title = fh.title;
                hud.duel_subtitle = fh.subtitle;
                hud.duel_title_color = fh.color;
                hud.duel_title_alpha = fh.alpha;
            }
            if (const net::RaceHud rh = net::race_hud(); !rh.title.empty()) {  // Co-op+ (T41)
                hud.duel_title = rh.title;
                hud.duel_subtitle = rh.subtitle;
                hud.duel_title_color = rh.color;
                hud.duel_title_alpha = rh.alpha;
            }
            if (const net::HideSeekHud sh = net::hideseek_hud(); !sh.title.empty() || sh.veil || sh.area) {  // 2026-11-05
                hud.duel_title = sh.title;
                hud.duel_subtitle = sh.subtitle;
                hud.duel_title_color = sh.color;
                hud.duel_title_alpha = sh.alpha;
                hud.veil = sh.veil;
                hud.area_active = sh.area;
                hud.area_centre = {sh.area_centre[0], sh.area_centre[1], sh.area_centre[2]};
                hud.area_radius = sh.area_radius;
                hud.area_outside = sh.outside;
                hud.veil_count = sh.veil_count;
                hud.veil_line = sh.veil_line;
            }
            hud.hide_partner = connected && net::hideseek_partner_hidden();
            if (const net::MatchHud mh = net::activity_match_hud(); !mh.title.empty()) {
                hud.duel_title = mh.title;
                hud.duel_subtitle = mh.subtitle;
                hud.duel_title_color = mh.color;
                hud.duel_title_alpha = mh.alpha;
            }
            if (const net::DuelHud dh = net::duel_hud(); dh.active) {
                hud.duel_active = true;
                hud.duel_centre = {dh.centre[0], dh.centre[1], dh.centre[2]};
                hud.duel_radius = dh.radius;
                hud.duel_title = dh.title;
                hud.duel_subtitle = dh.subtitle;
                hud.duel_title_color = dh.title_color;
                hud.duel_title_alpha = dh.title_alpha;
                hud.duel_out_left_s = dh.out_left_s;
            }
            render::hud_update(std::move(hud));
        }

        const bool mark_down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_F12) & 0x8000);
        loop_section(4);
        if (mark_down && !mark_was_down) log::warn("=== MARK {} (Ctrl+F12) ===", ++marks);
        mark_was_down = mark_down;
        if (now - last_report >= 5000) {
            last_report = now;
            const auto& st = session.stats();
            const char* state =
                session.state() == ClientSession::State::Connected ? "connected" : "connecting";
            std::string remote = "none";
            if (const auto& rs = session.remote_state()) {
                remote = std::format("({:.2f}, {:.2f}, {:.2f}) hp {:.1f}", rs->pos.x, rs->pos.y,
                                     rs->pos.z, rs->health);
            }
            // Co-op+: whether our enemies on the other player's copy get to attack it (only when it changes).
            if (static std::string last_bt; true) {
                std::string bt = net::aggro_battle_type_stats();
                if (bt != last_bt) {
                    log::info("Aggro: {} | attack slots on the copy {}", bt, net::aggro_copy_attack_slots());
                    last_bt = std::move(bt);
                }
            }
            // Co-op+ (T11): bandwidth since the last line.
            static uint64_t last_out = 0, last_in = 0;
            static uint32_t last_bw_ms = 0;
            const uint32_t bw_span = std::max<uint32_t>(1, now_ms() - last_bw_ms);
            const double kbps_out = static_cast<double>(st.bytes_sent - last_out) / bw_span,
                         kbps_in = static_cast<double>(st.bytes_received - last_in) / bw_span;
            last_out = st.bytes_sent, last_in = st.bytes_received, last_bw_ms = now_ms();
            log::info("Net: {} | sent {} recv {} lost {} rejected {} | rtt {:.1f} ms | out {:.1f} kB/s in {:.1f} kB/s | "
                      "remote states {} | last remote {} | reliable pending {} refused {} | unknown kinds {} | {}",
                      state, st.sent, st.received, st.lost, st.rejected, st.rtt_ms, kbps_out, kbps_in, st.remote_states,
                      remote, session.reliable_pending(), session.reliable_refused(), st.unknown_kinds,
                      transport->status());
            if (!session.peer_problem().empty()) log::warn("Net: {}", session.peer_problem());
            log::info("Local: {}", local.describe());
            // Co-op+: where our horse is (mount tests walk to it; the other player's copy rides it).
            if (const uint32_t hk = net::capture_horse_present()) {
                if (const auto hp = game::actor_world_position(hk)) {
                    log::info("Local: our horse 0x{:08X} at ({:.2f}, {:.2f}, {:.2f})", hk, hp->x, hp->y, hp->z);
                }
            }
            if (coop) log::info("{}", coop->describe());
            if (coop && cfg.scaling_enabled) log::info("{}", net::scaling_describe());
            if (cfg.overlay_enabled) {
                const auto ov = render::status();
                log::info("Overlay: installed {} disabled {} | presents {} drawn {} | queue {}{}",
                          ov.installed, ov.disabled, ov.presents, ov.frames_drawn,
                          ov.queue_source.empty() ? "-" : ov.queue_source,
                          ov.last_error.empty() ? "" : " | error: " + ov.last_error);
            }
        }
    }

    net::save_guard_set_connected(false, {});
    net::scaling_set_party(1, host);  // nobody in our world until the next session says so
    net::fast_lane_stop();  // before the transport and the session go
    coop.reset();
    if (was_connected) g_audit_due = now_ms();  // a Leave / Join: audited by the next session's loop (T08)
    // Co-op+ (T08): our own weather back now (it came back only from the next session's loop, which may not start).
    if (local.in_game()) net::weather_tick(false, host, true, std::nullopt, now_ms());
    session.disconnect(now_ms());
    // Co-op+ (T07): nothing of the other player stays on screen (their name tag stood where they were after a
    // Leave until the next session drew a new HUD).
    render::hud_update(render::HudData{});
    transport.reset();
    net::cleanup();
    if (steam) {
        // Friends must not see "Join Game" on a lobby we are leaving; the next session
        // sets its own presence once it has one.
        net::join_presence_clear();
        // Leaving or stopping: out of the lobby. (A Join leaves the old lobby itself
        // when it enters the new one.)
        if (end.kind != Kind::Join) net::room_leave();
    }
    log::info("Network stopped ({})", end.kind == Kind::Join    ? "joining another world"
                                      : end.kind == Kind::Leave ? "left the other player's world"
                                                                : "mod stopping");
    return end;
}

// Applies the settings that depend on being the host or the guest. Called once at
// startup, and again by Co-op+ when a Steam join or a leave changes the role.
void apply_role_settings(const Config& cfg, bool in_game, bool role_changed) {
    if (!(in_game && cfg.coop_enabled)) return;
    net::loot_set_enabled(cfg.coop_guest_loot && !cfg.net_role_host);
    net::loot_set_shared_xp(false);  // the twin-kill route is retired: shared XP is xp_share.h
    net::loot_set_mode(cfg.coop_loot_mode);
    // Co-op+: time of day follows the host ([coop] time_sync=3, the default).
    if (cfg.coop_time_sync == 3) {
        net::time_sync_set_mode(cfg.net_role_host ? net::TimeSyncMode::Send : net::TimeSyncMode::Apply);
    }
    // Phase 5: only the guest hides its own enemies (the host relays them).
    net::SuppressSettings s;
    s.mode = net::SuppressMode::Off;
    if (!cfg.net_role_host && cfg.coop_suppress != 0) {
        s.mode = cfg.coop_suppress == 2 ? net::SuppressMode::On : net::SuppressMode::DryRun;
    }
    s.radius_m = cfg.coop_suppress_radius_m;
    s.off_m = cfg.coop_suppress_off_m;
    s.restore = cfg.coop_suppress_restore;
    s.restore_state = cfg.coop_suppress_restore_state;
    s.wait_fight_s = cfg.coop_suppress_wait_fight_s;
    s.respawn_grace_s = cfg.coop_suppress_respawn_grace_s;
    s.node_events = cfg.coop_suppress_node_events;
    s.lab = cfg.coop_suppress_lab;
    s.lab_radius_m = cfg.coop_suppress_lab_radius_m;
    s.english = cfg.overlay_english;
    // At startup a host never configured the zone (as upstream); after a role change
    // it must be reconfigured either way, or a former guest keeps hiding enemies.
    if (s.mode != net::SuppressMode::Off || role_changed) net::suppress_configure(s);
    if (s.mode != net::SuppressMode::Off && s.lab) {
        log::info("Suppress lab: automatic enemy copies off (they would be hidden too)");
    }
}

// Co-op+: runs network sessions back to back. With [net] role=auto the player
// hosts their own world; a Steam join switches to guest, and leaving (Ctrl+F2)
// switches back. Without a Steam join, a session runs until the mod stops, exactly
// as before.
void run_coop(const Config& base, LocalSource& local, bool in_game) {
    Config cfg = base;
    if ((cfg.net_transport_steam || cfg.net_transport_direct) && (cfg.player_name.empty() || cfg.player_name == "Player")) {
        // The name the other player sees: their Steam name unless the ini sets one.
        // Steam comes up a few seconds after the mod; nothing can connect before it.
        // Direct play may run without Steam (Radmin VPN, an offline copy): a short wait, then the ini's name.
        for (int i = 0; i < (cfg.net_transport_direct ? 8 : 60); ++i) {
            net::join_poll();  // listen for a Steam join already while we wait
            if (std::string name = net::join_own_name(); !name.empty()) {
                cfg.player_name = std::move(name);
                log::info("Co-op+: playing as '{}' (Steam name)", cfg.player_name);
                break;
            }
            if (stop_requested(1000)) return;
        }
    }
    // Co-op+ on the native base (net/native_mode.h): this launch is the restart into co-op: the same partner, in that
    // role, before anything else connects (lobby_native_init dropped a stale one already).
    if (kNativeLayer) {
        const net::NativeResume r = net::native_resume_read(own_file(dir_of(module_path(g_self)), L".ini"));
        if (net::native_resume_fresh(r)) {
            cfg.net_role_host = r.mode == net::NativeMode::Host;
            if (r.steam && cfg.net_transport_steam) {
                cfg.steam_peer_id = r.partner_steam_id;
                cfg.room_lobby = 0;
                cfg.room_code.clear();
            } else if (!r.steam) {
                if (r.direct) {
                    // Radmin VPN / Direct IP: the same token and port as before the restart (the host accepts only it).
                    cfg.net_transport_steam = false;
                    cfg.net_transport_direct = true;
                    cfg.direct_from_menu = true;
                    cfg.direct_token = r.direct_token;
                    if (r.direct_port) cfg.peer_port = r.direct_port;
                    cfg.room_lobby = 0;
                    cfg.room_code.clear();
                    cfg.steam_peer_id = 0;
                }
                if (!r.partner_address.empty()) {
                    cfg.peer_host = r.partner_address;  // the guest: the host; the host: the friend it accepts
                    if (cfg.net_role_host) cfg.bind_host.clear();
                }
            }
            apply_role_settings(cfg, in_game, true);
            log::info("Co-op+: co-op restart: {} with {}", cfg.net_role_host ? "hosting" : "joining",
                      r.steam ? std::to_string(r.partner_steam_id) : r.partner_address);
        }
    }
    Config steam_named = cfg;  // what a Leave goes back to
    if (cfg.net_transport_steam) {
        if (const auto lobby = net::join_launch_lobby()) {
            // Steam started the game for a join ("Join Game" / an accepted invite).
            log::info("Join: Steam started the game to join lobby {}", *lobby);
            cfg.net_role_host = false;
            cfg.room_lobby = *lobby;
            cfg.room_code.clear();
            cfg.steam_peer_id = 0;
            apply_role_settings(cfg, in_game, true);
        }
    }
    for (;;) {
        const SessionEnd end = run_network(cfg, local);
        if (stop_requested()) break;
        if (end.kind == SessionEnd::Kind::Stop && cfg.direct_from_menu) {
            // The menu's Radmin VPN / Direct IP could not start (no VPN address, the port taken...): the menu shows
            // why (direct_state().error); back to Steam rather than retrying the same thing.
            log::warn("Direct: could not start; back to {}", steam_named.net_transport_steam ? "Steam" : "the ini's transport");
            cfg = steam_named;
            apply_role_settings(cfg, in_game, true);
            if (stop_requested(1500)) break;
            continue;
        }
        if (end.kind == SessionEnd::Kind::Stop) {
            // Co-op+ (T08): the network could not start (or stopped by itself). This used to end the worker, so co-op
            // stayed off for the rest of the game session (seen after a Leave on the one-PC transport). Try again.
            log::warn("Co-op+: the network did not start; trying again in 10 s");
            render::hud_set_problem(render::Problem::Network,
                                    "Co-op could not start its connection; trying again every 10 s. If this stays, "
                                    "restart the game from Steam and check cdmp.log (Diagnose Co-op+).");
            for (int i = 0; i < 10; ++i) {
                audit_tick(now_ms());
                if (stop_requested(1000)) return;
            }
            continue;
        }
        if (end.kind == SessionEnd::Kind::Direct) {
            const net::DirectRequest& r = end.direct;
            if (r.kind == net::DirectRequest::Kind::Steam) {
                cfg = steam_named;
                net::direct_set_state({});
            } else {
                cfg.net_transport_steam = false;
                cfg.net_transport_direct = true;
                cfg.direct_from_menu = true;
                cfg.net_role_host = r.kind == net::DirectRequest::Kind::Host;
                cfg.peer_port = r.target.port;
                cfg.direct_token = r.target.token;
                cfg.room_lobby = 0;
                cfg.room_code.clear();
                cfg.steam_peer_id = 0;
                if (cfg.net_role_host) {
                    cfg.peer_host = r.peer_ip ? net::direct_dotted(r.peer_ip) : std::string();
                    cfg.bind_host.clear();
                } else {
                    cfg.peer_host = net::direct_dotted(r.target.ip);
                }
            }
        } else if (end.kind == SessionEnd::Kind::Join) {
            cfg.net_role_host = false;
            if (cfg.dev_join_switch_port) cfg.peer_port = cfg.dev_join_switch_port;
            cfg.room_lobby = end.lobby;
            cfg.room_code.clear();
            cfg.steam_peer_id = 0;
        } else {
            // Leave: back to our own world, hosting it if role=auto, as configured otherwise (a menu-chosen direct
            // session goes back to Steam too).
            cfg = steam_named;
            net::direct_set_state({});
            if (!base.net_role_auto && !base.net_role_host) cfg.net_role_host = true;
        }
        log::info("Co-op+: now {}", cfg.net_role_host ? "hosting our own world" : "joining a friend's world");
        apply_role_settings(cfg, in_game, true);
        // Let Steam settle the old session before the new one starts.
        if (stop_requested(1500)) break;
    }
}

// The game's own top-level window, once it is visible and reasonably large.
bool game_window_visible() {
    struct Ctx {
        DWORD pid;
        bool found;
    } ctx{GetCurrentProcessId(), false};
    EnumWindows(
        [](HWND hwnd, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            RECT r{};
            if (pid == c->pid && IsWindowVisible(hwnd) && GetClientRect(hwnd, &r) &&
                r.right - r.left >= 640 && r.bottom - r.top >= 360) {
                c->found = true;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

struct LateInit {
    bool in_game;
    bool overlay;
    bool sniffer;
    bool clone_hotkeys;
    uint32_t mirror_delay_ms;
    float mirror_offset_m;
    bool coop;
    // NOTE: there was a `float coop_offset_m;` here, written by every call site and read by
    // none -- the same shape as the relay bug fixed in round 31 (a value passed in and never
    // consumed). It is gone rather than left in place, because this struct is initialised
    // POSITIONALLY: a dead field in the middle is a live hazard, one that invites the next
    // person to insert something at the wrong slot and shift every following value by a
    // position. Nothing is lost -- the offset reaches the puppet through CoopLink instead,
    // which is constructed with cfg.coop_offset_m directly (mod.cpp:314) and calls
    // puppet_set_offset from coop.cpp:64 and :123.
    bool coop_horse;
    bool coop_equip;
    bool coop_damage;
    bool combat_tests;
    bool record;
    std::wstring dir;
    int minimap_icon;
    bool minimap_cycle;
    bool aggro_test;
    bool enemies_target_copy;
    // Was `dev_enemy_relay` alone, which is why the relay thread never started for a normal
    // user: the dev switch defaults to false and the real switch, [coop] enemy_auto, was
    // never consulted here. Carries the same decision run_network makes, so the loop, g_auto
    // and the config log cannot disagree. See the note at its use site.
    bool relay_wanted;
    bool piece4_test;
    bool guest_hits, guest_hit_stagger, player_hit_react, player_hit_lethal;
    bool attack_probe;
    int copy_attack_slots;
    uint8_t pin_shape, pin_colour;
};

// LateInit is built with a POSITIONAL aggregate initialiser (the CreateThread call below), so
// its field ORDER is load-bearing and its field COUNT must match that one initialiser. Adding,
// removing or reordering a field without updating it shifts every following value by a
// position -- and since these are mostly bools and floats, that compiles CLEAN and produces a
// mod configured wrong in ways no compiler will mention.
//
// 80 is the MEASURED size. This started as `<= 128`, which was tested and found worthless:
// 128 bytes of extra fields were added on purpose and the build still passed. A guard that does
// not fire on the case it exists for guards nothing.
//
// To read sizeof out of MSVC, do not try to interpolate it into static_assert's message -- the
// message is fixed text and the value does not appear, at any bound. What works is a template
// whose instantiation error NAMES the type with the size in it:
//
//     template <class T, std::size_t = sizeof(T)> struct probe_size {
//         static_assert(sizeof(T) == 0, "...");
//     };
//     probe_size<LateInit> p;   // error reads: probe_size<...LateInit,80>
//
// That is where the 80 above came from. It cost several builds to find, so it is written down.
static_assert(sizeof(LateInit) == 80,
              "LateInit changed size: check that the aggregate initialiser at its CreateThread "
              "call site still lists every field in the same order and count, then set this "
              "bound to the new size (see the note above for how to read it).");

// Dev: Ctrl+F9 / Ctrl+Shift+F9 walk the game's icon table for the other
// player's minimap icon. The game only draws a changed icon if it creates it
// anew (live test: changing it in place made it vanish), so each step
// reloads the copy.
void minimap_cycle_loop() {
    log::info("Minimap: Ctrl+F9 / Ctrl+Shift+F9 change the other player's icon");
    bool was_down = false;
    while (!stop_requested(50)) {
        const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool down = ctrl && (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        if (down && !was_down) {
            const bool back = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            int at = net::puppet_minimap_offset();
            if (at < 0xEA || at > 0x16C) at = game::build_3576::kMinimapIconPlayerArrow;
            else at += back ? -2 : 2;
            if (at > 0x16C) at = 0xEA;
            if (at < 0xEA) at = 0x16C;
            net::puppet_minimap_set_offset(at);
            net::puppet_request_reload();
        }
        was_down = down;
        net::puppet_minimap_tick();
    }
}

// Combat injection tests (dev, combate-coop.md §6). They post client->server
// packets into the game's own server, next to the nearest NPC:
//   Ctrl+F6  T2: VaryStatByAccInputReq(npc, HP, -5)      does HP drop? death? loot?
//   Ctrl+F7  T3: AttackByCollisionReq(npc, player, ...)  native hit reaction/damage?
// Results show in cdmp_packets.log (VaryHPAck, AttackTargetAck, NotifyHandleDeadAck...).
void combat_test_loop() {
    log::warn("Combat tests ON: Ctrl+F6 = T2 (HP -5 on nearest NPC), Ctrl+F7 = T3 (collision hit). "
              "These WRITE to the game: do not save afterwards");
    game::PlayerReader reader;
    reader.init();
    bool f6_was = false, f7_was = false;
    auto packet = [](uint16_t op, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> p(5 + payload.size());
        const uint16_t len = static_cast<uint16_t>(payload.size());
        std::memcpy(p.data(), &op, 2);
        p[2] = 0;  // slot 0, as in every captured Req
        std::memcpy(p.data() + 3, &len, 2);
        std::memcpy(p.data() + 5, payload.data(), payload.size());
        return p;
    };
    while (!stop_requested(50)) {
        const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool f6 = ctrl && (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
        const bool f7 = ctrl && (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
        if ((f6 && !f6_was) || (f7 && !f7_was)) {
            auto me = reader.read();
            auto npc = me ? net::capture_nearest_actor(me->position, 15.0f) : std::nullopt;
            // Prefer the enemy the player just hit (certainly has hit points).
            if (npc && net::capture_last_attack_target() && npc->key != net::capture_last_attack_target()) {
                log::info("Combat test: nearest enemy 0x{:08X} is not the last one you hit (0x{:08X}); "
                          "using the nearest",
                          npc->key, net::capture_last_attack_target());
            }
            if (!npc) {
                log::warn("Combat test: no enemy with hit points moving within 15 m (hit one first)");
            } else if (f6) {
                std::vector<uint8_t> b(16);
                const uint32_t hp_stat = 0x000F4240;
                const int64_t delta = -5000;  // -5 HP (x1000)
                std::memcpy(b.data(), &npc->key, 4);
                std::memcpy(b.data() + 4, &hp_stat, 4);
                std::memcpy(b.data() + 8, &delta, 8);
                const bool ok = net::sniffer_inject_c2s(packet(2565, b));  // VaryStatByAccInputReq on 3460 (2976: 2181)
                log::info("Combat test T2: VaryStatByAccInputReq(0x{:08X} at {:.1f} m, HP, -5) {}",
                          npc->key, npc->distance, ok ? "queued" : "NOT queued");
            } else {
                // victim, attacker, u32, vec3 position, vec3 direction, u32 hash, u32.
                std::vector<uint8_t> b(44, 0);
                const uint32_t player = net::capture_own_player_key();
                uint32_t hash = net::capture_last_attack_hash();
                if (!hash) hash = 0xF2EE2678;  // a hash seen in captura 2
                const float pos[3] = {npc->pos.x, npc->pos.y + 1.0f, npc->pos.z};
                const float dx = npc->pos.x - me->position.x, dz = npc->pos.z - me->position.z;
                const float len = std::sqrt(dx * dx + dz * dz) + 1e-4f;
                const float dir[3] = {dx / len, 0.0f, dz / len};
                std::memcpy(b.data(), &npc->key, 4);
                std::memcpy(b.data() + 4, &player, 4);
                std::memcpy(b.data() + 12, pos, 12);
                std::memcpy(b.data() + 24, dir, 12);
                std::memcpy(b.data() + 36, &hash, 4);
                const bool ok = net::sniffer_inject_c2s(packet(2150, b));  // AttackByCollisionReq on 3460 (2976: 2815)
                log::info("Combat test T3: AttackByCollisionReq(0x{:08X} at {:.1f} m, hash 0x{:08X}) {}",
                          npc->key, npc->distance, hash, ok ? "queued" : "NOT queued");
            }
        }
        f6_was = f6;
        f7_was = f7;
    }
}

// Single-PC mirror (dev): F9 spawns a copy of the player that replays the
// player's own events after `delay_ms`, F10 removes it, F11 pauses it. Same
// sender (local_capture) and receiver (remote_puppet) as co-op, minus the
// network and with the same game clock on both ends.
void mirror_hotkey_loop(uint32_t delay_ms) {
    log::info("Mirror hotkeys active: F9 = spawn your copy, F10 = remove it, "
              "F11 = mirroring on/off (delay {} ms)", delay_ms);
    auto mirror_sink = [delay_ms](const proto::GameEvent& ev) {
        if (ev.kind == proto::EventKind::Damage) return;  // same world: would double every hit
        net::puppet_event(ev, static_cast<int64_t>(delay_ms), delay_ms);
    };
    game::PlayerReader reader;
    reader.init();
    bool mirror_on = true;
    bool f9_was = false, f10_was = false, f11_was = false;
    while (!stop_requested(50)) {
        const bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        const bool f10 = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
        const bool f11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
        if (f11 && !f11_was) {
            mirror_on = !mirror_on;
            log::info("Mirror {}", mirror_on ? "ON" : "OFF");
            net::capture_set_sink(mirror_on && net::puppet_spawned() ? net::EventSink(mirror_sink)
                                                                     : net::EventSink{});
        }
        if (f9 && !f9_was) {
            auto me = reader.read();
            auto info = me ? net::capture_spawn_info(me->position, me->rotation) : std::nullopt;
            if (!me) {
                log::warn("F9: player not in world ({})", reader.last_failure());
            } else if (!info) {
                log::warn("F9: no templates yet (load a save with the sniffer on, then try again)");
            } else if (net::puppet_spawn(*info)) {
                if (mirror_on) net::capture_set_sink(mirror_sink);
            }
        }
        if (f10 && !f10_was) {
            net::capture_set_sink({});
            if (!net::puppet_remove()) log::warn("F10: nothing to remove");
        }
        f9_was = f9;
        f10_was = f10;
        f11_was = f11;
    }
    net::capture_set_sink({});
}

// Hooks are installed only once the game is fully up: installing the overlay
// at process start (creating our probe D3D12 device while the game and
// Streamline initialise theirs) left the game stuck on a black window.
DWORD WINAPI late_init(LPVOID param) {
    std::unique_ptr<LateInit> init(static_cast<LateInit*>(param));
    if (init->in_game) {
        log::info("Late init: waiting for the game to finish starting up");
        const auto world_slot = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
                                game::build_3576::kWorldSystemSlot;
        // WorldSystem exists from the title screen on, i.e. after the game
        // created its device and swap chain and started presenting.
        while (!stop_requested(500)) {
            uintptr_t world = 0;
            game::safe_read(world_slot, &world, sizeof world);
            if (world != 0 && game_window_visible()) break;
        }
        if (stop_requested(5000)) return 0;
        log::info("Late init: game is up, installing hooks");
    }
    bool sniffer_ok = false;
    if (init->sniffer && init->in_game) sniffer_ok = net::sniffer_install(init->dir, g_stop, init->record);
    if (sniffer_ok) game::game_errors_install();  // drained by the sniffer's writer thread
    net::load_trace_install();  // Co-op+ dev diagnostic, no-op unless [dev] load_trace=1
    // Co-op+ live quests: finished steps follow through the game's own quest server (quest_advance.h), when the
    // player opted in (optin.h; now or later from the first-launch card).
    optin::set_sniffer_ready(sniffer_ok && init->coop);
    if (sniffer_ok && init->coop && net::quest_live_dev_any_world()) net::quest_advance_install();
    net::quest_events_install();  // Co-op+ dev diagnostic, no-op unless [dev] quest_events_log=1
    if (init->overlay) render::install(render::hud_draw);
    if (init->combat_tests && sniffer_ok) {
        CloseHandle(CreateThread(
            nullptr, 0, [](LPVOID) -> DWORD { combat_test_loop(); return 0; }, nullptr, 0, nullptr));
    }
    // Templates and injection both live in the sniffer's hooks.
    if (kNativeLayer && init->coop) {
        // Co-op+ on the native base (native_layer.h): no sniffer, no puppet, no enemy copies; the link carries only
        // what the layer adds (activities, chat, waypoints, the party frame's state), so it is ready at once.
        net::coop_set_ready();
        log::info("Native layer: the co-op link is ready (no sniffer, no copies: the native base shares the world)");
    } else if ((init->coop || init->clone_hotkeys) && !sniffer_ok) {
        log::warn("Co-op and the mirror need the sniffer ([sniffer] enabled=1); not active");
    } else if (init->coop) {
        // Combat values learnt in earlier sessions, and kept from now on
        // (cdmp_learnt.txt; learnt also while playing alone).
        net::learnt_load(init->dir);
        net::xp_share_load(init->dir);  // Co-op+: kill experience per enemy kind (xp_share.h)
        CloseHandle(CreateThread(
            nullptr, 0,
            [](LPVOID p) -> DWORD {
                std::unique_ptr<std::wstring> dir(static_cast<std::wstring*>(p));
                net::learnt_save_loop(*dir, g_stop);
                return 0;
            },
            new std::wstring(init->dir), 0, nullptr));
        // The co-op link sets the puppet offset (it depends on who is connected).
        net::puppet_set_horse_enabled(init->coop_horse);
        net::puppet_set_equip_enabled(init->coop_equip);
        // The mirrored damage and the other player's hits on our enemies'
        // copies would both hurt the same enemy: hits win.
        if (init->coop_damage && init->guest_hits) log::info("Co-op: mirrored damage off ([coop] guest_hits=1)");
        net::capture_set_damage_enabled(init->coop_damage && !init->guest_hits);
        net::puppet_install_climb_fix();
        net::puppet_visibility_install();  // Co-op+: copies are drawn (3460)
        net::puppet_install_rope_bend();
        // Horse and enemy copies (the other player may send theirs at any
        // time) need the crash containment (docs/INFORME-CABALLO-CRASH.md)
        // and the action-layer guards; without them they are refused.
        // Co-op+ (T06): on 1 launch in 80 (2026-11-03, desync1) a hook here did not take and the enemy relay stayed
        // off for the whole session; a few more tries, 2 s apart, before giving up.
        bool contained = false, guarded = false;
        for (int attempt = 1; attempt <= 5 && !stop_requested(); ++attempt) {
            contained = net::horse_diag_install(/*contain=*/true);
            guarded = contained && net::puppet_install_layer_guards();
            if (guarded) {
                if (attempt > 1) log::info("Crash guards installed on try {}", attempt);
                break;
            }
            log::warn("Crash guards: try {} of 5 failed{}", attempt, attempt < 5 ? "; again in 2 s" : "");
            if (attempt < 5) Sleep(2000);
        }
        // Co-op+ (3549 port): the mount/dismount fixes are installed after the tries. The climb fix above used to
        // try the guards itself, and when that first try did not take (1 launch in 2 on 1.0.0.3549) it turned the
        // horse copy off for the session although the guards took 2 s later.
        if (guarded) {
            net::puppet_install_ride_fix();
        } else {
            net::puppet_set_horse_enabled(false);
        }
        if (!contained) {
            log::warn("Horse and enemy copies disabled: their crash containment could not be installed");
            render::hud_set_problem(render::Problem::Guards,
                                    "Your friend's horse and the shared enemies are off this time (a safety check did "
                                    "not install). Restart the game to try again.");
        } else if (!guarded) {
            // This one line is the whole visible trace of the enemy relay being dead. It was
            // printed once at boot, between dozens of status lines, and it went unnoticed for
            // a long time -- a full echo session with a copy in the world produced no relay
            // activity at all and nobody connected the two, because a feature that fails
            // safely and silently looks exactly like a feature that works.
            //
            // So it now says what it costs, and it does not leave the reader to infer that
            // enemy copies, shared loot and joint boss damage are ALL off as a result.
            log::warn("Enemy copies disabled: their crash guards could not be installed");
            render::hud_set_problem(render::Problem::Guards,
                                    "Shared enemies are off this time (a safety check did not install). Restart the "
                                    "game to try again.");
            log::warn("  -> the enemy relay is OFF for this whole session: no copies of the other");
            log::warn("     player's enemies, so no shared loot and no guest hits on them either.");
            log::warn("     Everything else (your own copy, movement, equipment, horses) still works.");
            log::warn("     Usually an offset for this game build; see docs/RELEVO-INIMIGOS-NUNCA-LIGOU.md");
        }
        net::enemy_set_receive_enabled(guarded);
        // Sending OUR enemies (Ctrl+F6 manual / Ctrl+F7 automatic).
        //
        // THIS USED TO TEST `init->enemy_relay`, WHICH IS `cfg.dev_enemy_relay` -- a DEV
        // switch, default false (config.h:54). So the thread only started when someone put
        // `[dev] enemy_relay=1` in cdmp.ini, and the feature the user actually asks for,
        // `[coop] enemy_auto` (default ON), was never consulted here.
        //
        // The failure was silent and looked like success: run_network calls enemy_set_auto(true)
        // on cfg.coop_enemy_auto, so `g_auto` was set, the config log said the relay was on,
        // and nothing ever consumed it because the loop that drives it was never created.
        // A flag that is set and never read is indistinguishable from a working feature.
        //
        // Measured live 2026-10-30: the crash guard installed, the relay reported no error,
        // the player stood 200 m from Bleed Bandits camp with good link stats, and not one
        // `Enemy relay:` line appeared in the whole session. Fixed by asking the same two
        // switches run_network asks, so the thread, g_auto and the log all agree.
        //
        // WHY THIS IS NOT gated on role=host, even though docs/research/pendiente-robustez.md:429
        // prescribes exactly that (`pasar a [coop] enemies=auto, solo con role=host`).
        //
        // The relay is symmetric and decides per message, not per role: whoever RECEIVES an
        // EnemyZoneMsg copies, whoever needs enemies sends one. coop.cpp:356 states it --
        // "the guest's request (host) or the host's answer (guest)". A host-only gate here
        // would silence the guest's own Ctrl+F6/Ctrl+F7 and, more importantly, would stop the
        // guest from ever answering, which is how the zone handshake completes.
        //
        // Gating on role would ALSO not have caught the real bug: in the echo profile the
        // single process plays both roles, so a role check is true for one of them whatever
        // you write, and the thread still would not have started for a normal user.
        //
        // The concern behind line 429 was real -- a guest that copies its own enemies sends
        // them to the host -- but the guard for it is already in the data path, not here:
        // auto_tick() returns early unless g_partner_zone is on, and that only turns on from a
        // message the OTHER side sent. With no link there is nothing to copy.
        const bool relay_wanted = init->relay_wanted;
        if (relay_wanted && guarded) {
            CloseHandle(CreateThread(
                nullptr, 0, [](LPVOID) -> DWORD { net::enemy_test_loop(g_stop); return 0; }, nullptr, 0, nullptr));
        } else if (relay_wanted && !guarded) {
            log::warn("Enemy relay: wanted but not started (its crash guards did not install)");
        }
        net::capture_install_equip_hook();
        if (init->attack_probe) {
            CloseHandle(CreateThread(
                nullptr, 0, [](LPVOID) -> DWORD { net::attack_probe_loop(g_stop); return 0; }, nullptr, 0, nullptr));
        }
        if (init->piece4_test) {
            CloseHandle(CreateThread(
                nullptr, 0, [](LPVOID) -> DWORD { net::piece4_test_loop(g_stop); return 0; }, nullptr, 0, nullptr));
        }
        net::guest_hits_set_enabled(init->guest_hits, init->guest_hit_stagger);
        net::hits_set_react(init->player_hit_react, init->player_hit_lethal);
        net::puppet_map_pin_style(init->pin_shape, init->pin_colour);
        // Before any puppet loads: the game picks its icon when it spawns.
        const bool minimap = net::puppet_minimap_install(init->minimap_icon);
        if ((init->aggro_test || init->enemies_target_copy) && net::aggro_test_install()) {
            net::aggro_set_copy_targetable(init->enemies_target_copy);
            net::aggro_set_copy_attack_slots(init->enemies_target_copy ? init->copy_attack_slots : 0);
            net::hits_set_enabled(init->enemies_target_copy);
            if (init->aggro_test) {
                CloseHandle(CreateThread(
                    nullptr, 0, [](LPVOID) -> DWORD { net::aggro_test_loop(g_stop); return 0; }, nullptr, 0,
                    nullptr));
            }
        }
        net::coop_set_ready();
        if (minimap) {
            CloseHandle(CreateThread(
                nullptr, 0,
                [](LPVOID cycle) -> DWORD {
                    if (cycle) {
                        minimap_cycle_loop();
                    } else {
                        while (!stop_requested(1000)) net::puppet_minimap_tick();
                    }
                    return 0;
                },
                init->minimap_cycle ? reinterpret_cast<LPVOID>(1) : nullptr, 0, nullptr));
        }
        if (init->clone_hotkeys) log::info("Mirror hotkeys off while co-op is enabled");
    } else if (init->clone_hotkeys) {
        net::puppet_set_offset(init->mirror_offset_m);
        net::puppet_install_climb_fix();
        net::puppet_install_ride_fix();
        net::puppet_visibility_install();  // Co-op+: copies are drawn (3460)
        net::capture_install_equip_hook();
        mirror_hotkey_loop(init->mirror_delay_ms);
    }
    return 0;
}

// Party scaling settings (Co-op+): the switch is [scaling] enabled in cdmp.ini; the curves
// come from cdcoop\config\scaling.toml next to the mod when it is there (our unit-tested
// sim layer, brief 6.5), else from [scaling] health_2p. The TOML can turn scaling off,
// never on: the give-back is experimental until a live test.
scaling::Settings scaling_settings(const Config& cfg, const std::wstring& dir) {
    scaling::Settings s{cfg.scaling_enabled, cfg.scaling_health_2p, cfg.scaling_boss_health_2p};
#ifdef CDMP_COOPPLUS_SIM
    std::error_code ec;
    const std::filesystem::path toml_dir = std::filesystem::path(dir) / L"cdcoop" / L"config";
    if (!std::filesystem::exists(toml_dir / L"scaling.toml", ec)) return s;
    const cdcoop::sim::CoopConfig cc = cdcoop::sim::load_config(toml_dir);
    for (const auto& e : cc.errors) log::warn("Co-op+ config: {}", e);
    if (!cc.scaling.enabled) {
        s.enabled = false;
        log::info("Co-op+ config: scaling.toml says enabled = false");
        return s;
    }
    const auto qol = cc.comfort.qol_state();
    for (int n = 2; n <= scaling::kMaxParty; ++n) {
        s.health[n] = static_cast<float>(
            cdcoop::sim::compute(cc.scaling, n, cdcoop::sim::EnemyTier::Regular, qol).health);
        s.boss_health[n] =
            static_cast<float>(cdcoop::sim::compute(cc.scaling, n, cdcoop::sim::EnemyTier::Boss, qol).health);
    }
    s.health_2p = s.health[2];
    s.boss_health_2p = s.boss_health[2];
    log::info("Co-op+ config: scaling.toml health by party size 2/3/4: regular x{:.2f}/{:.2f}/{:.2f}, bosses "
              "x{:.2f}/{:.2f}/{:.2f}",
              s.health[2], s.health[3], s.health[4], s.boss_health[2], s.boss_health[3], s.boss_health[4]);
#else
    (void)dir;
#endif
    return s;
}

DWORD WINAPI worker(LPVOID) {
    const std::wstring dir = dir_of(module_path(g_self));
    // Co-op+ dev: two copies on one PC (two_copy.h); the game's short-lived second process of a role stays untouched.
    const std::wstring role = test_role();
    if (!role.empty() && two_copy::extra_process(role, kNativeLayer)) return 0;
    log::open(own_file(dir, L".log"));
    crash_log::install();
    log::info("=== Crimson Desert Multiplayer {} (build {:08X}{}, protocol {}) ===", CDMP_VERSION, kModBuild,
              CDMP_BUILD_DIRTY ? " + local changes" : "", proto::kVersion);
    log::info("Mod file: {}", narrow(module_path(g_self)));

    try {
        const bool in_game = log_process_info();
        // Co-op+: after a game update the mod must stay out of the way, visibly.
        const bool build_ok = !in_game || game_build_supported();
        std::string build_problem;
        if (!build_ok) {
            build_problem = std::format("Co-op+ is off: this mod was made for game version 1.0.0.3576, not {}. "
                                        "The game runs normally; update Co-op+ to play together.",
                                        file_version(module_path(nullptr)));
            log::warn("{}", build_problem);
        }
        Config cfg = load_config(own_file(dir, L".ini"));
        // Co-op+ on the native base (native_layer.h): the world-simulating systems are off in coopplus.asi.
        if (const std::string off = apply_native_layer(cfg); kNativeLayer) {
            log::info("Native layer (coopplus.asi next to the native base's cdmp.asi): off whatever the ini says: {}",
                      off.empty() ? "nothing was on" : off);
        }
        // Co-op+ (T28): a second ASI loader loads us twice; only the first copy runs.
        if (in_game && !online_guard::first_instance()) {
            log::warn("Co-op+ was loaded twice (two ASI loaders next to the game?); this copy stays off");
            return 0;
        }
        if (in_game) g_compat_note = online_guard::compat_check();
        // Co-op+ (T02): never next to an anti-cheat (online_guard.h). Nothing of ours is hooked then, not even the
        // overlay, so the message goes out in a plain Windows box (its own thread: the game does not wait for it).
        if (in_game && build_ok) {
            if (std::string why = online_guard::check(cfg.dev_online_guard_test); !why.empty()) {
                std::thread([why] {
                    MessageBoxA(nullptr, why.c_str(), "Crimson Desert Seamless Co-op+", MB_OK | MB_ICONINFORMATION);
                }).detach();
                log::warn("Online guard: nothing installed (no overlay, no packet hooks, no co-op)");
                return 0;
            }
        }
        // Co-op+ story sync: a story taken from a host goes into the save now, before the game loads one.
        // Co-op+ co-op worlds (save_io.h) + live quests: the experimental opt-in (optin.h), asked on the first launch.
        // Co-op+ on the native base: the restart into co-op (net/native_mode.h), before the network starts.
        if (kNativeLayer && in_game && build_ok && cfg.coop_enabled) net::lobby_native_init(own_file(dir, L".ini"), dir);
        optin::init(own_file(dir, L".ini"), in_game && build_ok && cfg.coop_enabled, cfg.coop_coop_worlds,
                    GetPrivateProfileIntW(L"general", L"experimental_asked", 0, (own_file(dir, L".ini")).c_str()) != 0);
        if (optin::enabled() && !cfg.dev_coop_world_partner.empty()) net::worlds_configure(true, cfg.dev_coop_world_partner);
        net::worlds_dev_as_host(cfg.dev_coop_world_as_host);
        if (!role.empty() && kNativeLayer) {
            // Co-op+ 2026-11-07 (tcB1): on the native base the native cdmp.asi redirects both roles' saves (its sandbox /
            // save_cdmp_guest) and the layer's save systems are off (native_layer.h): no scratch world needed here.
            log::info("Two copies: role {} on the native base (its save redirect covers the saves)", narrow(role));
            two_copy::keep_full_speed();
        } else if (!role.empty()) {
            // Two copies on one PC: never on the real save folder.
            if (cfg.dev_coop_world.empty()) {
                log::warn("Two copies: CDMP_ROLE={} needs [dev] coop_world (a scratch world); Co-op+ stays off", narrow(role));
                return 0;
            }
            log::info("Two copies: role {}, files cdmp.{}.*, the whole save folder in scratch world '{}'", narrow(role),
                      narrow(role), narrow(cfg.dev_coop_world));
            save_io::set_whole_folder(true);
            two_copy::keep_full_speed();
        }
        if (in_game && build_ok && (cfg.dev_save_io_log || !cfg.dev_coop_world.empty())) {
            save_io::install(cfg.dev_save_io_log);
            if (!cfg.dev_coop_world.empty()) save_io::set_dev_home(cfg.dev_coop_world);
        }
        if (in_game && build_ok && cfg.coop_enabled) {
            net::story_configure(cfg.coop_share_story, cfg.dev_story_slot);
            net::story_startup();
            net::quest_live_configure(cfg.coop_live_quests, cfg.dev_live_quests_any_world);
            net::prologue_configure(cfg.coop_solo_prologue, cfg.coop_prologue_missions);  // Co-op+ (2026-11-07)
            net::xp_share_configure(cfg.coop_shared_xp);  // Co-op+ shared XP (xp_share.h)
            net::slowmo_configure(cfg.coop_block_slowmo, cfg.dev_block_slowmo_always);
            game::set_stat_log(cfg.dev_stat_log);
        }
        log::info("Config: player '{}', peer {}:{}, {} Hz, net {}, co-op {}", cfg.player_name,
                  cfg.peer_host, cfg.peer_port, cfg.send_rate_hz, cfg.net_enabled ? "on" : "off",
                  cfg.coop_enabled ? "on" : "off");
        // Everything that changes behaviour, so a log sent by the other player
        // says how their game was set up.
        log::info("Config: net {} {} | coop delay {} ms, offset {:.2f} m, horse {}, equip {}, damage mirror {}, "
                  "map pin {}, minimap icon {:#x}, enemies target copy {}, guest hits {}, stagger {}, player react {} lethal {}",
                  cfg.net_transport_steam ? "steam" : cfg.net_transport_direct ? "direct" : "udp", cfg.net_role_auto ? "auto (host until a Steam join)" : cfg.net_role_host ? "host" : "guest", cfg.coop_delay_ms,
                  cfg.coop_offset_m, cfg.coop_horse, cfg.coop_equip, cfg.coop_damage, cfg.coop_map_pin,
                  cfg.coop_minimap_icon, cfg.coop_enemies_target_copy, cfg.coop_guest_hits, cfg.coop_guest_hit_stagger,
                  cfg.coop_player_hit_react, cfg.coop_player_hit_lethal);
        // Say what the room code will do, so a host can tell from the log alone
        // whether the mod is creating a room or looking one up.
        log::info("Config: room code '{}' -- {}", cfg.room_code,
                  cfg.room_code.empty()
                      ? (cfg.net_role_host ? "host: the mod will create a room and show the code"
                                           : "guest: none set; will fall back to [steam] peer_steamid")
                      : (cfg.net_role_host ? "host: unused, the mod creates the room itself"
                                           : "guest: will look this room up"));
        // Both switches that can ask for the relay, computed once here so this line and the
        // shared-loot warning below cannot disagree about what was requested.
        const bool relay_pref = cfg.coop_enemy_auto || cfg.dev_enemy_relay;
        log::info("Config: dev clone {} combat {} minimap cycle {} aggro {} piece4 {} | sniffer {} "
                  "record {} | overlay {}",
                  cfg.dev_clone_hotkeys, cfg.dev_combat_tests, cfg.dev_minimap_cycle, cfg.dev_aggro_test,
                  cfg.dev_piece4_test, cfg.sniffer_enabled, cfg.sniffer_record,
                  cfg.overlay_enabled);
        // The relay got its own line, because this one used to print dev_enemy_relay under the
        // label "enemy relay". That is the DEV switch (default 0); the real one is [coop]
        // enemy_auto. So a correctly configured player read "enemy relay false" and had no way
        // to tell it apart from the round-31 bug where the relay truly was not starting -- the
        // log showed false in both cases. Print the switch that decides, and print what is
        // actually running, which is the distinction that matters when the two disagree.
        log::info("Config: enemy relay wanted {} ([coop] enemy_auto={}, [dev] enemy_relay={}{})",
                  relay_pref ? "yes" : "no", cfg.coop_enemy_auto ? 1 : 0, cfg.dev_enemy_relay ? 1 : 0,
                  cfg.coop_suppress_lab ? ", suppressed by suppress_lab=1 (the one-PC lab)" : "");
        log::info("Config: time sync {}", cfg.coop_time_sync == 1   ? "send ours"
                                          : cfg.coop_time_sync == 2 ? "take the other player's"
                                          : cfg.coop_time_sync == 3 ? "auto (the host's time of day)"
                                                                    : "off");
        log::info("Config: enemy zone (guest) {} | {:.0f} m on, {:.0f} m off | restore {} state {} | fight wait {} s | "
                  "node events {} | lab {} ({:.0f} m)",
                  cfg.coop_suppress == 2 ? "ON" : cfg.coop_suppress == 1 ? "dry run" : "off",
                  cfg.coop_suppress_radius_m, cfg.coop_suppress_off_m, cfg.coop_suppress_restore,
                  cfg.coop_suppress_restore_state, cfg.coop_suppress_wait_fight_s, cfg.coop_suppress_node_events,
                  cfg.coop_suppress_lab, cfg.coop_suppress_lab_radius_m);
        // Boss safety (boss_safe.h): applied whether or not co-op is on, because
        // the guard has to be in place BEFORE the first boss fight, and the
        // default (on) is what protects the host's quest. A guest's lethal blow
        // on a stage boss kills it outside the host's script, which leaves the
        // host's quest stuck behind the arena barrier.
        net::boss_set_safe_mode(cfg.coop_boss_safe);
        {
            scaling::Settings sc = scaling_settings(cfg, dir);
            // Co-op+ (T40): the multipliers set in the F8 settings win over scaling.toml's 2-player values.
            const auto panel_value = [&](const wchar_t* key) {
                wchar_t buf[32] = {};
                GetPrivateProfileStringW(L"scaling", key, L"0", buf, 32, (own_file(dir, L".ini")).c_str());
                const float v = std::wcstof(buf, nullptr);
                return v >= 1.0f && v <= 4.0f ? v : 0.0f;
            };
            if (const float v = panel_value(L"panel_health_2p")) sc.health_2p = sc.health[2] = v;
            if (const float v = panel_value(L"panel_boss_health_2p")) sc.boss_health_2p = sc.boss_health[2] = v;
            net::scaling_configure(sc);
            settings::init(own_file(dir, L".ini"), cfg, sc.health[2] > 0 ? sc.health[2] : sc.health_2p,
                           sc.boss_health[2] > 0 ? sc.boss_health[2] : sc.boss_health_2p);
            net::scoreboard_init(own_file(dir, L"_scores.ini"));  // Co-op+ (T43)
            // Co-op+ (T31): first run?
            if (GetPrivateProfileIntW(L"general", L"onboarded", 0, (own_file(dir, L".ini")).c_str()) == 0) {
                g_onboard_ini = own_file(dir, L".ini");
            }
            // Co-op+ (T30): rebound hotkeys, and what is wrong with them.
            for (const auto& w : input::load_bindings(own_file(dir, L".ini"))) {
                log::warn("Keys: {}", w);
                g_compat_note += (g_compat_note.empty() ? std::string() : std::string("\n")) + "Keys: " + w;
            }
        }
        net::save_guard_configure(cfg.coop_save_guard);
        net::room_set_invite_only(cfg.net_join_invite_only);
        net::load_trace_set_mode(cfg.dev_load_trace);
        net::quest_events_enable(cfg.dev_quest_events_log);
        net::enemy_set_copies_allowed(cfg.dev_enemy_copies);
        net::enemy_set_keep_origin(cfg.dev_enemy_keep_origin);
        net::loot_set_test_original(cfg.dev_loot_test_original);
        net::coop_set_copy_disabled(cfg.dev_no_copy);
        net::revive_set_boss_wait(cfg.coop_boss_death_wait);
        net::activity_set_skip_test(cfg.dev_skip_test);
        if (cfg.dev_trace_op) net::sniffer_set_trace_op(static_cast<uint16_t>(cfg.dev_trace_op));
        net::activity_set_versus_test(static_cast<int>(cfg.dev_versus_test));
        net::activity_set_seat_test(cfg.dev_seat_test);
        net::weather_set_enabled(cfg.coop_weather_sync);
        net::weather_set_test(cfg.dev_weather_test);
        net::cinema_set_enabled(cfg.coop_stage_cinema);
        net::cinema_set_test(cfg.dev_cinema_test);
        net::activity_set_rps_test(cfg.dev_rps_test);
        net::activity_set_challenge_test(cfg.dev_challenge_test);
        net::duel_set_test(cfg.dev_duel_test);
        net::summary_set_test(cfg.dev_summary_test);
        net::hideseek_set_test(cfg.dev_hideseek_test);
        net::fishing_set_test(cfg.dev_fishing_test);
        if (cfg.dev_lag_probe) net::lag_probe_start();
        net::capture_set_pose_stream(cfg.coop_pose_stream);
        net::party_quests_set_test(cfg.dev_quest_test);
        net::hunt_set_test(cfg.dev_hunt_test);
        net::sync_set_test(cfg.dev_desync_test);
        net::revive_partner_set(cfg.coop_teammate_revive, cfg.dev_revive_test);
        net::lobby_set_shared_quest_credit(cfg.coop_shared_quest_credit);
        net::lobby_set_native_extra(cfg.dev_native_extra);
        net::guest_hits_set_finishes(cfg.coop_guest_finishes);
        net::aggro_set_copy_never_target(cfg.dev_copy_untargetable);
        net::activity_set_event_test(cfg.dev_event_test);
        net::enemy_set_zone_copy_test(cfg.dev_zone_copy_test);
        input::input_start();  // Co-op+: pads, input method, button icons (input/pad_input.h)
        input::set_sim(cfg.dev_pad_sim);
        game::main_menu_rb_test(cfg.dev_rb_test);
        if (cfg.dev_boss_hp) {
            net::boss_set_test_threshold(static_cast<int64_t>(cfg.dev_boss_hp) * 1000);
            log::info("Config: [dev] boss_hp={}: enemies with that much health count as bosses", cfg.dev_boss_hp);
        }
        if (cfg.dev_no_copy) log::info("Config: [dev] no_copy=1: the other player's character is never loaded");
        log::info("Config: who can join: {}", cfg.net_join_invite_only
                                                  ? "invited friends only ([net] join=invite)"
                                                  : "any Steam friend playing (\"Join Game\")");
        log::info("Config: boss safety {}", cfg.coop_boss_safe ? "ON (the host lands the killing blow)" : "OFF");
        // Shared loot is the guest's, and only means anything with co-op on.
        //
        // It also cannot work without the enemy relay: the twin is spawned from
        // the variant of the copy we helped kill, and that copy is only ever
        // known to the relay (enemy_relay.cpp fills g_copy_variant as it relays
        // each enemy). With enemy_auto off, zone_capable() is false, no copies
        // are tracked, and loot silently never fires -- the same "looks enabled,
        // does nothing" trap as an INI in the wrong encoding. So say plainly
        // which of the two switches is missing instead of reporting "ON".
        const bool loot_wanted = cfg.coop_guest_loot && !cfg.net_role_host;
        const bool relay_needed = cfg.coop_enemy_auto && !cfg.coop_suppress_lab;
        if (!loot_wanted) {
            // Name WHICH of the two reasons applies. The old message read
            // `guest_loot ? "off (this is the host)" : "off"`, which told a guest
            // who had deliberately left guest_loot=0 nothing, and would have told
            // a guest "this is the host" if the two ever disagreed. The role and
            // the setting are separate facts; report the one that decided.
            if (cfg.net_role_host) {
                log::info("Config: shared loot off (this is the host; the enemies are already yours)");
            } else {
                log::info("Config: shared loot off ([coop] guest_loot=0; set it to 1 to receive "
                          "your own drop when the host kills an enemy you helped with)");
            }
        } else if (!relay_needed) {
            // TWO reasons switch the relay off, and this message used to name only one of them.
            // With [coop] suppress_lab=1 -- the one-PC lab (PRUEBAS-COOP.md) -- relay_needed is
            // false while enemy_auto is still 1, and the old text told the player
            // "enemy_auto=0 ... set it to 1". Following that advice changes nothing: the lab
            // still suppresses the relay and the same warning comes back, so the player is sent
            // to look at a key that was already correct. Report the reason that actually decided.
            //
            // The lab suppresses the relay deliberately (native_suppress.cpp): on one PC the
            // same machine would be both host and guest, so the copies it made would be hidden
            // by its own zone. This warning is therefore EXPECTED during a lab run, not a fault.
            if (cfg.coop_enemy_auto) {
                log::info("Config: shared loot is ON, and the enemy relay is off because "
                          "[coop] suppress_lab=1 (the one-PC lab). That is expected: the lab works "
                          "alone and never copies enemies. With suppress_lab=0 this line would say "
                          "the relay is on.");
            } else {
                log::warn("Config: shared loot is ON but [coop] enemy_auto=0, so it can never fire: "
                          "the enemy relay is what tells the mod which creature a copy is. Set enemy_auto=1.");
            }
        } else {
            log::info("Config: shared loot (guest) ON (your own drop from the host's kills)");
        }
        if (in_game && cfg.coop_enabled) {
            net::enemy_set_auto(cfg.coop_enemy_auto && !cfg.coop_suppress_lab);
            net::enemy_set_watch_hidden(cfg.dev_enemy_relay);
            net::guest_hits_set_finish_arrow(cfg.coop_finish_arrow);
            net::puppet_set_rope_lookup(cfg.dev_rope_piece_lookup);
            net::puppet_minimap_set_enemy_offset(cfg.coop_enemy_minimap_icon);
            net::puppet_want_rope_bend(cfg.dev_rope_bend);
            if (cfg.coop_time_sync != 3) net::time_sync_set_mode(static_cast<net::TimeSyncMode>(cfg.coop_time_sync));
            net::time_sync_set_dev_key(cfg.dev_time_sync_key);
        }
        // Loot and the guest's enemy zone depend on the role; Co-op+ re-applies
        // them when a Steam join changes it.
        apply_role_settings(cfg, in_game, false);

        if (cfg.startup_delay_ms > 0 && stop_requested(cfg.startup_delay_ms)) return 0;

        LocalSource local(in_game);

        // The overlay only makes sense inside the game; a test host would
        // otherwise keep patched DXGI vtables pointing into an unloaded DLL.
        // cdmp_dx12_test opts in explicitly (and never unloads the mod).
        const bool overlay_allowed =
            in_game || GetEnvironmentVariableW(L"CDMP_TEST_OVERLAY", nullptr, 0) > 0;
        const bool overlay = overlay_allowed && cfg.overlay_enabled;
        const bool sniffer = in_game && cfg.sniffer_enabled;
        if (in_game && !cfg.overlay_enabled) log::info("Overlay disabled in cdmp.ini");
        if (in_game && !cfg.sniffer_enabled) log::info("Sniffer disabled in cdmp.ini");
        if (overlay || sniffer) {
            if (overlay) {
                render::hud_configure(
                    {cfg.overlay_self_marker, cfg.overlay_english, cfg.overlay_nameplates, build_problem});
            }
            g_late_thread = CreateThread(nullptr, 0, late_init,
                                         new LateInit{in_game, overlay, sniffer,
                                                      in_game && cfg.dev_clone_hotkeys,
                                                      cfg.dev_mirror_delay_ms, cfg.dev_mirror_offset_m,
                                                      in_game && cfg.coop_enabled,
                                                      cfg.coop_horse, cfg.coop_equip, cfg.coop_damage,
                                                      in_game && cfg.dev_combat_tests, cfg.sniffer_record,
                                                      dir, cfg.coop_minimap_icon, cfg.dev_minimap_cycle,
                                                      in_game && cfg.dev_aggro_test,
                                                      cfg.coop_enemies_target_copy,
                                                      in_game && cfg.coop_enabled &&
                                                          (cfg.coop_enemy_auto || cfg.dev_enemy_relay) &&
                                                          !cfg.coop_suppress_lab,
                                                      in_game && cfg.dev_piece4_test,
                                                      cfg.coop_guest_hits, cfg.coop_guest_hit_stagger,
                                                      cfg.coop_player_hit_react, cfg.coop_player_hit_lethal,
                                                      in_game && cfg.dev_attack_probe, cfg.coop_copy_attack_slots,
                                                      cfg.coop_map_pin_shape, cfg.coop_map_pin_colour},
                                         0, nullptr);
        }

        if (cfg.net_enabled && build_ok) run_coop(cfg, local, in_game);
        // Our Steam callbacks must be gone before the mod's code can be unloaded.
        net::join_shutdown();
    } catch (const std::exception& e) {
        log::error("Unhandled exception in worker: {}", e.what());
    } catch (...) {
        log::error("Unhandled non-standard exception in worker");
    }

    log::info("Worker exiting");
    return 0;
}

}  // namespace

void start(HMODULE self) {
    g_self = self;
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
}

void stop() {
    if (!g_thread) return;
    SetEvent(g_stop);
    WaitForSingleObject(g_thread, 10000);
    CloseHandle(g_thread);
    if (g_late_thread) {
        WaitForSingleObject(g_late_thread, 10000);
        CloseHandle(g_late_thread);
        g_late_thread = nullptr;
    }
    CloseHandle(g_stop);
    g_thread = nullptr;
    g_stop = nullptr;
    log::close();
}

}  // namespace cdmp
