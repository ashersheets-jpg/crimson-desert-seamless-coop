#include "mod/session2/guest_save_tunnel.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/sha256.h"
#include "common/troc_frame.h"
#include "common/troc_link.h"
#include "mod/log.h"
#include "mod/session2/coop_world_logic.h"
#include "mod/session2/guest_save_host.h"

namespace cdmp::session2 {
namespace {

using gsw::AckResult;
using gsw::Kind;

std::atomic<uint64_t> g_own_id{0}, g_peer_id{0};
std::atomic<bool> g_installed{false};

// Network thread only.
gsw::Assembler g_up;            // the upload in progress
gsw::Offer g_offer{};           // its offer
std::string g_offer_guest;      // its guest id (folder name source)
std::string g_guest;            // the guest of the last accepted offer ("" = none yet: nothing is sent back)
uint32_t g_guest_slot = 0;      // its N
std::deque<std::vector<uint8_t>> g_replies;  // payloads (SAVE_ACK) for the next tick
gsw::ResultSender g_sender;
gsw::Outgoing g_out;            // the chunks of the generation in flight
uint32_t g_next_xfer = 0;
uint32_t g_link_epoch = 0;
bool g_link_seen = false;
std::vector<uint8_t> g_last_status;
uint64_t g_status_ms = 0;
uint8_t g_status_mode_logged = 0xFF;
bool g_speaks_logged = false;

// Co-op+ 2026-11-12, the co-op world's character (coop_world_logic.h). Network thread only, as everything above: the
// upload is finished in guest_save_tunnel_on_frame and the gate is asked from server.cpp's inject() for user 2's
// LoginUserToFieldServerReq 2535, which session2.cpp's offer() calls on the same thread (the mirror thread only
// injects the host's movement Req, never a 2535).
// In a world with a marker: the checked upload, kept (never staged) for the gate's fallback only.
struct KeptUpload {
    bool have = false;
    uint32_t xfer = 0;
    gsw::Offer offer{};
    std::string guest;
    std::vector<uint8_t> lobby, save;
};
KeptUpload g_cw_upload;
struct CoopWorldGate {
    uint64_t open_until_ms = 0;  // the gate is open without a look until then (a decision just made, or a failure)
    uint64_t wait_since_ms = 0;  // user 2's login waits since then (0 = it does not)
    uint64_t next_look_ms = 0;   // while it waits: the next look at the disk
    uint64_t waits = 0;          // looks that ended in a wait (the wait line's pace)
    std::optional<cwl::SlotSample> sample;  // the host's slot as it was read last
    uint64_t sample_ms = 0;
    bool logged = false;         // the last "stays" line: its stage and line (one line per change, not per look)
    cwl::Stage logged_stage = cwl::Stage::Today;
    std::string logged_root;
};
CoopWorldGate g_cw;
constexpr uint64_t kCoopWorldOpenMs = 1000;    // after a decision: a Req offered again does not ask again at once
constexpr uint64_t kCoopWorldStaleMs = 5000;   // a wait nobody came back to (the held Req is gone): forgotten

// Server thread -> network thread.
std::mutex g_exp_mu;
std::deque<GuestSaveExport> g_exports;  // under g_exp_mu
std::atomic<uint64_t> g_guest_final_ms{0}, g_host_final_ms{0};

std::atomic<uint64_t> c_offers{0}, c_offers_ok{0}, c_offers_refused{0}, c_chunks_in{0}, c_bytes_in{0}, c_malformed{0},
    c_stale{0}, c_cancels{0}, c_mailbox_staged{0}, c_results_started{0}, c_results_delivered{0}, c_results_failed{0},
    c_chunks_out{0}, c_bytes_out{0}, c_status_out{0}, c_exports_in{0}, c_exports_old{0};
// The co-op world's character: uploads kept aside, and what the gate staged.
std::atomic<uint64_t> c_cw_kept{0}, c_cw_seeded{0}, c_cw_reseeded{0}, c_cw_mailbox{0}, c_cw_prev{0}, c_cw_fallback{0};

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

uint64_t now64() { return GetTickCount64(); }

// ---------------------------------------------------------------- files (the mod's own mailbox)

bool read_all(const std::wstring& path, std::vector<uint8_t>& out, uint64_t max, std::string& why) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        why = std::format("cannot open '{}' (error {})", narrow(path), GetLastError());
        return false;
    }
    LARGE_INTEGER li{};
    bool ok = GetFileSizeEx(h, &li) != FALSE && static_cast<uint64_t>(li.QuadPart) <= max;
    if (ok) {
        out.resize(static_cast<size_t>(li.QuadPart));
        DWORD got = 0;
        ok = out.empty() || (ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size());
    }
    CloseHandle(h);
    if (!ok) {
        out.clear();
        why = std::format("cannot read '{}' whole (at most {} bytes)", narrow(path), max);
    }
    return ok;
}

bool write_all(const std::wstring& path, std::string_view text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = text.empty() || (WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &put, nullptr) &&
                                     put == text.size());
    return CloseHandle(h) && ok;
}

// The mailbox folder and its parents (%LOCALAPPDATA%\cdmp\guest_saves\<guest>).
bool make_dirs(const std::wstring& dir) {
    for (size_t at = dir.find(L'\\', 3); at != std::wstring::npos; at = dir.find(L'\\', at + 1)) {
        CreateDirectoryW(dir.substr(0, at).c_str(), nullptr);
    }
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

std::vector<uint32_t> list_generations(const std::wstring& dir) {
    std::vector<uint32_t> g;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileExW((dir + L"\\gen*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (f == INVALID_HANDLE_VALUE) return g;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (const auto n = gsv::parse_generation(fd.cFileName)) g.push_back(*n);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return g;
}

uint32_t read_delivered(const std::wstring& mailbox) {
    std::vector<uint8_t> bytes;
    std::string why;
    if (!read_all(mailbox + L"\\delivered.txt", bytes, 4096, why)) return 0;
    const auto kv = gsw::parse_key_values(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    const auto n = gsw::kv_u64(kv, "delivered");
    return n && *n <= 0xFFFFFFFFull ? static_cast<uint32_t>(*n) : 0;
}

void write_delivered(const std::wstring& mailbox, uint32_t gen, const std::string& guest) {
    if (!make_dirs(mailbox)) return;
    const std::string text = std::format("delivered={}\nguest={}\n", gen, guest);
    if (!write_all(mailbox + L"\\delivered.txt", text)) {
        log::warn("Session2 guest save link: cannot write '{}\\delivered.txt' (error {})", narrow(mailbox),
                  GetLastError());
    }
}

// A mailbox generation, whole and checked: its manifest's sizes and SHA-256, the game's header check. Fills `g`.
bool load_generation(const std::wstring& mailbox, uint32_t gen, std::vector<uint8_t>& lobby, std::vector<uint8_t>& save,
                     gsw::Generation& g, std::string& why) {
    const std::wstring dir = mailbox + std::format(L"\\gen{}", gen);
    std::vector<uint8_t> manifest;
    if (!read_all(dir + L"\\manifest.txt", manifest, 64 * 1024, why)) return false;
    const auto kv =
        gsw::parse_key_values(std::string_view(reinterpret_cast<const char*>(manifest.data()), manifest.size()));
    const auto files = gsw::kv_files(kv);
    if (!files) {
        why = std::format("gen{}: its manifest has no sizes / SHA-256 for both files", gen);
        return false;
    }
    if (!read_all(dir + L"\\lobby.save", lobby, gsv::kMaxFileSize, why) ||
        !read_all(dir + L"\\save.save", save, gsv::kMaxFileSize, why)) {
        return false;
    }
    if (gsw::files_of(lobby, save) != *files) {
        why = std::format("gen{}: the files are not the ones its manifest names (size or SHA-256)", gen);
        return false;
    }
    if (gsw::check_pair(lobby, save, why) != AckResult::Ok) return false;
    g = {};
    g.gen = gen;
    g.files = *files;
    // Without guest_slot (before session 25 slot 0 was not written): the current guest's slot, with no line (the
    // guest's script never applies a generation without one).
    const auto slot = gsw::kv_u64(kv, "guest_slot");
    g.slot = slot && *slot <= 0xFFFFFFFFull ? static_cast<uint32_t>(*slot) : g_guest_slot;
    g.root = gsw::kv_root(kv).value_or(sha256::Digest{});
    g.base = gsw::kv_base(kv).value_or(sha256::Digest{});
    g.time_unix = gsw::kv_u64(kv, "time_unix").value_or(0);
    g.reason = gsw::reason_from_text(gsw::kv_get(kv, "reason").value_or(""));
    return true;
}

// A mailbox generation's line (its manifest only: the files are checked when it is staged). False = unreadable.
bool read_line(const std::wstring& mailbox, uint32_t gen, gsw::LineGen& out) {
    std::vector<uint8_t> manifest;
    std::string why;
    if (!read_all(mailbox + std::format(L"\\gen{}\\manifest.txt", gen), manifest, 64 * 1024, why)) return false;
    const auto kv =
        gsw::parse_key_values(std::string_view(reinterpret_cast<const char*>(manifest.data()), manifest.size()));
    const auto files = gsw::kv_files(kv);
    if (!files) return false;
    out = {};
    out.gen = gen;
    out.save = (*files)[1].sha;
    out.root = gsw::kv_root(kv).value_or(sha256::Digest{});
    return true;
}

// ---------------------------------------------------------------- the co-op world's character: its files
// (coop_world_logic.h.) The mod's own reads and writes in the world's folder, the save folder's stand-in: none of
// these paths is under the real save folder, so the save redirect's hooks pass them through unchanged.

// Off = coop_world_character=0 or own-save mode is not 1 (nothing is read); Absent = a world without a marker (the
// behaviour before); Invalid = a marker that cannot be read or that this build does not understand.
enum class MarkerRead : uint8_t { Off, Absent, Invalid, Ok };

MarkerRead read_marker(cwl::Marker& out, std::wstring* path_out = nullptr) {
    out = cwl::Marker{};
    const CoopWorldPaths paths = guest_save_coop_world_paths();
    if (paths.world.empty()) return MarkerRead::Off;
    const std::wstring path = paths.world + L"\\" + cwl::kMarkerFile;
    if (path_out) *path_out = path;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const DWORD e = GetLastError();
        return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? MarkerRead::Absent : MarkerRead::Invalid;
    }
    std::vector<uint8_t> bytes;
    std::string why;
    if (!read_all(path, bytes, 64 * 1024, why)) return MarkerRead::Invalid;
    const auto m = cwl::parse_marker(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!m) return MarkerRead::Invalid;
    out = *m;
    return MarkerRead::Ok;
}

// The marker rewritten: a temporary file renamed over it (a reader never sees half of it).
bool write_marker(const cwl::Marker& m, std::string& why) {
    const CoopWorldPaths paths = guest_save_coop_world_paths();
    if (paths.world.empty()) {
        why = "the co-op world's character is off";
        return false;
    }
    const std::wstring path = paths.world + L"\\" + cwl::kMarkerFile, tmp = path + L".tmp";
    const std::string text = cwl::marker_text(m);
    if (!write_all(tmp, text) || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        why = std::format("cannot write '{}' (error {})", narrow(path), GetLastError());
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

bool file_there(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// The slot folders of the world's account folder (the host's saves; the guest's 204 too), for cwl::pick_host_slot:
// per slot number its two files, save.save's last write, and whether a slot<N>_new / slot<N>_backup folder is there.
std::vector<gsw::SlotSeen> list_host_slots(const std::wstring& account_dir) {
    std::vector<gsw::SlotSeen> list;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileExW((account_dir + L"\\slot*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                0);
    if (f == INVALID_HANDLE_VALUE) return list;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            continue;
        }
        const auto d = gsw::parse_slot_dir(fd.cFileName);
        if (!d) continue;
        size_t at = 0;
        while (at < list.size() && list[at].slot != d->slot) ++at;
        if (at == list.size()) {
            list.push_back(gsw::SlotSeen{});
            list[at].slot = d->slot;
        }
        gsw::SlotSeen& s = list[at];
        if (d->kind == gsw::DirKind::New) s.has_new = true;
        if (d->kind == gsw::DirKind::Backup) s.has_backup = true;
        if (d->kind != gsw::DirKind::Plain) continue;
        const std::wstring dir = account_dir + L"\\" + fd.cFileName;
        s.has_lobby = file_there(dir + L"\\lobby.save");
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (GetFileAttributesExW((dir + L"\\save.save").c_str(), GetFileExInfoStandard, &a) &&
            !(a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            s.has_save = true;
            s.save_time = (static_cast<uint64_t>(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return list;
}

// Both files of a slot folder, whole and checked as the game would load them (header, sizes).
bool read_pair(const std::wstring& dir, std::vector<uint8_t>& lobby, std::vector<uint8_t>& save, std::string& why) {
    if (!read_all(dir + L"\\lobby.save", lobby, gsv::kMaxFileSize, why) ||
        !read_all(dir + L"\\save.save", save, gsv::kMaxFileSize, why)) {
        return false;
    }
    return gsw::check_pair(lobby, save, why) == AckResult::Ok;
}

// ---------------------------------------------------------------- the upload

void reply(uint32_t xfer, AckResult r, uint32_t gen) {
    g_replies.push_back(gsw::encode_ack(gsw::kStateAck, gsw::Ack{xfer, static_cast<uint8_t>(r), gen}));
}

void refuse(uint32_t xfer, AckResult r, const std::string& why) {
    c_offers_refused.fetch_add(1);
    reply(xfer, r, 0);
    log::warn("Session2 guest save link: the guest's upload xfer {} is REFUSED ({}): {}; SAVE_ACK {}", xfer,
              gsw::ack_result_name(static_cast<uint8_t>(r)), why, static_cast<unsigned>(r));
    g_up.reset();
}

std::string short_sha(const sha256::Digest& d) { return sha256::hex(d).substr(0, 16); }

void on_offer(const gsw::Offer& o, uint32_t now_ms) {
    c_offers.fetch_add(1);
    const uint64_t peer = g_peer_id.load();
    log::info("Session2 guest save link: SAVE_OFFER xfer {} from guest {} (connected peer {}) | its slot{} | base gen {} "
              "| lobby {} B sha256 {}... | save {} B sha256 {}...",
              o.xfer, o.guest_id, peer ? std::to_string(peer) : std::string("unknown (UDP)"), o.slot, o.base_gen,
              o.files[0].size, short_sha(o.files[0].sha), o.files[1].size, short_sha(o.files[1].sha));
    if (g_up.active()) log::warn("Session2 guest save link: the upload xfer {} in progress is replaced", g_up.xfer());
    if (guest_save_mode() == gsv::Mode::Off) {
        return refuse(o.xfer, AckResult::Off, "guest_own_save=0 on the host (or forced off): user 2 plays a clone");
    }
    if (const gsv::SlotState s = guest_save_state(); s.disabled || s.poisoned) {
        return refuse(o.xfer, AckResult::Off, s.poisoned ? "the guest's save is POISONED in this run" : "own-save mode failed in this run");
    }
    if (!gsw::offer_slot_ok(o.slot)) {
        return refuse(o.xfer, AckResult::Slot, std::format("slot {} is not a slot a Continue loads (0-2, 100-108)", o.slot));
    }
    if (peer && o.guest_id && o.guest_id != peer) {
        return refuse(o.xfer, AckResult::Identity,
                      std::format("the offer says SteamID64 {}, the Steam session talks to {}", o.guest_id, peer));
    }
    if (const AckResult r = g_up.begin(o.xfer, o.files); r != AckResult::Ok) {
        return refuse(o.xfer, r, "a size is 0 or above 16 MB");
    }
    g_offer = o;
    g_offer_guest = peer ? std::to_string(peer) : o.guest_id ? std::to_string(o.guest_id) : std::string("local-peer");
    (void)now_ms;
}

// The newest generation the guest never acknowledged (nor applied) goes to it again.
void resume_undelivered(const std::wstring& mailbox, uint32_t base_gen) {
    const uint32_t delivered = read_delivered(mailbox);
    g_sender.set_delivered(std::max(delivered, base_gen));
    // Review item 6: new exports are numbered above what the guest has, even after the mailbox was emptied.
    guest_save_set_generation_floor(g_sender.delivered());
    const auto gens = list_generations(mailbox);
    const auto newest = gsw::newest_above(gens, g_sender.delivered());
    if (!newest) return;
    std::vector<uint8_t> lobby, save;
    gsw::Generation g;
    std::string why;
    if (!load_generation(mailbox, *newest, lobby, save, g, why)) {
        log::warn("Session2 guest save link: mailbox gen{} was never acknowledged by the guest but cannot be sent: {}",
                  *newest, why);
        return;
    }
    if (g_sender.resume(g)) {
        log::info("Session2 guest save link: mailbox gen{} (delivered {}, the guest's base gen {}) was never acknowledged: "
                  "it goes to the guest again",
                  g.gen, delivered, base_gen);
    }
}

// The staging of a checked upload, as it was before the co-op world's character: a mailbox generation that continues
// the same save replaces it, then begin / write / ready. Ok = staged (staged_gen: the mailbox generation staged
// instead, 0 = the upload itself); anything else is the SAVE_ACK result and `why`. Used by finish_upload (a world
// without a marker) and by the co-op world gate's fallback.
AckResult stage_upload(uint32_t xfer, const gsw::Offer& offer, const std::string& guest, std::vector<uint8_t> lobby,
                       std::vector<uint8_t> save, uint32_t& staged_gen, std::string& why) {
    std::string source = std::format("the guest's upload (xfer {}, its slot{}, guest {})", xfer, offer.slot, guest);
    staged_gen = 0;
    // §3.5.6 as fixed in session 25 (gsw::find_continuation): a mailbox generation replaces the upload only when it
    // continues THE SAME save (the upload is its line's root or one of its generations) and is newer: the guest's
    // progress never goes back, and a guest that comes with another save plays that save.
    const sha256::Digest& offered = offer.files[1].sha;
    gsw::Continuation line;
    line.root = offered;
    const std::wstring mailbox = guest_save_mailbox_folder(guest);
    if (!mailbox.empty()) {
        std::vector<gsw::LineGen> lines;
        std::string without;
        for (const uint32_t gen : list_generations(mailbox)) {
            gsw::LineGen lg;
            if (!read_line(mailbox, gen, lg)) continue;
            if (gsw::digest_zero(lg.root)) without += std::format(" gen{}", gen);
            lines.push_back(lg);
        }
        line = gsw::find_continuation(lines, offered);
        if (!without.empty()) {
            log::info("Session2 guest save link: mailbox generations without a line (an older build):{}: never staged "
                      "instead of an upload",
                      without);
        }
        if (line.newer) {
            std::vector<uint8_t> ml, ms;
            gsw::Generation g;
            std::string mwhy;
            if (load_generation(mailbox, *line.newer, ml, ms, g, mwhy) && g.root == line.root) {
                lobby = std::move(ml);
                save = std::move(ms);
                staged_gen = *line.newer;
                c_mailbox_staged.fetch_add(1);
                source = std::format("mailbox gen{} of guest {} (the same save as its upload xfer {}, newer: line {}...)",
                                     *line.newer, guest, xfer, short_sha(line.root));
                log::warn("Session2 guest save link: the upload is {} of line {}... and the mailbox '{}' has its gen{}: "
                          "that one is staged INSTEAD of the upload (§3.5.6: the same save never goes back)",
                          line.at ? std::format("gen{}", line.at) : std::string("the root"), short_sha(line.root),
                          narrow(mailbox), *line.newer);
            } else {
                log::warn("Session2 guest save link: mailbox gen{} continues the upload's save but cannot be used ({}); "
                          "the upload is staged",
                          *line.newer, mwhy.empty() ? std::string("its line changed on disk") : mwhy);
            }
        } else if (line.found) {
            log::info("Session2 guest save link: the upload is {} of line {}... (nothing newer in the mailbox): it is "
                      "staged",
                      line.at ? std::format("gen{}", line.at) : std::string("the root"), short_sha(line.root));
        } else {
            log::info("Session2 guest save link: no mailbox generation continues the upload's save ({} B sha256 "
                      "{}...; {} generation(s) of other saves): it is staged as a new line",
                      offer.files[1].size, short_sha(offered), lines.size());
        }
    }
    const std::string root_hex = sha256::hex(line.root);
    if (!guest_save_incoming_begin(why)) {
        const gsv::SlotState s = guest_save_state();
        return s.disabled || s.poisoned || guest_save_mode() == gsv::Mode::Off ? AckResult::Off : AckResult::Io;
    }
    if (!guest_save_incoming_write(gsv::FileKind::Lobby, lobby, why) ||
        !guest_save_incoming_write(gsv::FileKind::Save, save, why)) {
        return AckResult::Io;
    }
    // The base: the file the guest offered (a member of the line: the apply script needs it once the generation it
    // came from is gone from the guest's incoming folder, review of 12f8dfb item 1).
    const GuestSaveLabel label{guest, offer.slot, root_hex, sha256::hex(offered)};
    if (!guest_save_incoming_ready(source, label, why)) return AckResult::Header;
    return AckResult::Ok;
}

void finish_upload() {
    const uint32_t xfer = g_up.xfer();
    std::string why;
    if (const AckResult v = g_up.verify(why); v != AckResult::Ok) return refuse(xfer, v, why);
    std::vector<uint8_t> lobby = g_up.file(Kind::Lobby), save = g_up.file(Kind::Save);
    uint32_t staged_gen = 0;
    // Co-op+ 2026-11-12 (coop_world_logic.h): in a world with a marker the friend never plays the save it uploads.
    // The upload is still checked and acknowledged (the guest's client goes on as before) and its guest id and slot
    // are adopted, but nothing is staged here: user 2's login gate (guest_save_coop_world_gate) stages this world's
    // own character. The checked files are kept in memory only for that gate's fallback.
    cwl::Marker marker;
    std::wstring marker_path;
    const MarkerRead world = guest_save_mode() == gsv::Mode::On ? read_marker(marker, &marker_path) : MarkerRead::Off;
    if (world == MarkerRead::Invalid) {
        log::error("Session2 guest save (co-op world): the world's marker '{}' cannot be read or is not one this "
                   "build understands: the upload xfer {} is staged as before (the friend brings its own character)",
                   narrow(marker_path), xfer);
    }
    const bool kept = world == MarkerRead::Ok;
    if (kept) {
        if (const gsv::SlotState s = guest_save_state(); s.disabled || s.poisoned) {
            return refuse(xfer, AckResult::Off,
                          s.poisoned ? "the guest's save is POISONED in this run" : "own-save mode failed in this run");
        }
        c_cw_kept.fetch_add(1);
        log::info("Session2 guest save (co-op world): marker {}{}: the upload xfer {} of guest {} (its slot{}; lobby {} "
                  "B sha256 {}..., save {} B sha256 {}...) is checked and kept aside, NOT staged: user 2 plays this "
                  "world's own character, staged at its login",
                  cwl::character_name(marker.character),
                  marker.character == cwl::Character::World
                      ? std::format(" (line {}...)", marker.lineage_root.substr(0, 16))
                      : std::string(),
                  xfer, g_offer_guest, g_offer.slot, lobby.size(), short_sha(g_offer.files[0].sha), save.size(),
                  short_sha(g_offer.files[1].sha));
        g_cw_upload = KeptUpload{true, xfer, g_offer, g_offer_guest, std::move(lobby), std::move(save)};
    } else {
        g_cw_upload = KeptUpload{};
        const AckResult r = stage_upload(xfer, g_offer, g_offer_guest, std::move(lobby), std::move(save), staged_gen, why);
        if (r != AckResult::Ok) return refuse(xfer, r, why);
    }
    if (g_guest != g_offer_guest) {
        if (!g_guest.empty()) log::warn("Session2 guest save link: the guest changed ({} -> {})", g_guest, g_offer_guest);
        g_sender = gsw::ResultSender{};
        g_out = {};
    }
    g_guest = g_offer_guest;
    g_guest_slot = g_offer.slot;
    guest_save_set_guest_id(g_guest);
    reply(xfer, AckResult::Ok, staged_gen);
    c_offers_ok.fetch_add(1);
    if (kept) {
        log::info("Session2 guest save link: upload xfer {} VERIFIED and KEPT ASIDE (co-op world) for guest {} (its "
                  "slot{}); SAVE_ACK ok | user 2's login stages this world's own character",
                  xfer, g_guest, g_guest_slot);
    } else {
        log::info("Session2 guest save link: upload xfer {} VERIFIED and STAGED for guest {} (its slot{}){}; SAVE_ACK ok | {}",
                  xfer, g_guest, g_guest_slot,
                  staged_gen ? std::format(" from mailbox gen{}", staged_gen) : std::string(),
                  guest_save_mode() == gsv::Mode::On ? "user 2's next lobby load moves it in as slot204"
                                                     : "probe: nothing moves in (user 2 plays a clone)");
    }
    g_up.reset();
    const std::wstring mailbox = guest_save_mailbox_folder(g_offer_guest);
    if (!mailbox.empty()) resume_undelivered(mailbox, g_offer.base_gen);
}

void on_chunk(const gsw::Chunk& c) {
    c_chunks_in.fetch_add(1);
    c_bytes_in.fetch_add(c.data.size());
    AckResult err = AckResult::Ok;
    switch (g_up.add(c, err)) {
        case gsw::Assembler::Step::Ignored: c_stale.fetch_add(1); return;
        case gsw::Assembler::Step::Error:
            reply(c.xfer, err, 0);
            c_offers_refused.fetch_add(1);
            log::error("Session2 guest save link: the guest's upload xfer {} BROKE: chunk {} at offset {} ({} B) out of "
                       "order or past its size; SAVE_ACK {}",
                       c.xfer, gsw::kind_name(c.kind), c.offset, c.data.size(), gsw::ack_result_name(static_cast<uint8_t>(err)));
            g_up.reset();
            return;
        case gsw::Assembler::Step::Need: return;
        case gsw::Assembler::Step::Complete: finish_upload(); return;
    }
}

// ---------------------------------------------------------------- the download

void on_result_ack(const gsw::Ack& a) {
    const auto v = g_sender.on_ack(a);
    switch (v) {
        case gsw::ResultSender::AckVerdict::Delivered:
            c_results_delivered.fetch_add(1);
            g_out = {};
            write_delivered(guest_save_mailbox_folder(g_guest), a.gen, g_guest);
            guest_save_set_generation_floor(a.gen);
            log::info("Session2 guest save link: the guest STORED gen{} (SAVE_RESULT_ACK ok, xfer {})", a.gen, a.xfer);
            return;
        case gsw::ResultSender::AckVerdict::Retry:
            c_results_failed.fetch_add(1);
            g_out = {};
            log::warn("Session2 guest save link: the guest could not store gen{} (SAVE_RESULT_ACK {}, xfer {}): sent "
                      "again",
                      a.gen, gsw::ack_result_name(a.result), a.xfer);
            return;
        case gsw::ResultSender::AckVerdict::GaveUp:
            c_results_failed.fetch_add(1);
            g_out = {};
            log::error("Session2 guest save link: the guest could not store the generation in {} attempts (last "
                       "SAVE_RESULT_ACK {}): it stays in the mailbox and goes again on the next connection",
                       gsw::kMaxResultAttempts, gsw::ack_result_name(a.result));
            return;
        case gsw::ResultSender::AckVerdict::Stale: c_stale.fetch_add(1); return;
    }
}

void take_exports(uint64_t now) {
    std::deque<GuestSaveExport> exports;
    {
        std::lock_guard lock(g_exp_mu);
        exports.swap(g_exports);
    }
    for (const GuestSaveExport& e : exports) {
        c_exports_in.fetch_add(1);
        gsw::Generation g;
        g.gen = e.generation;
        g.slot = e.guest_slot.value_or(g_guest_slot);
        g.root = sha256::parse_hex(e.lineage_root).value_or(sha256::Digest{});
        g.base = sha256::parse_hex(e.lineage_base).value_or(sha256::Digest{});
        if (!e.guest.empty() && e.guest != g_guest) {
            // The live pair is another guest's (an upload of this one is staged, not moved in yet): its mailbox keeps
            // it for its own guest (review of 12f8dfb, item 4).
            log::warn("Session2 guest save link: gen{} belongs to guest {}, not to the connected guest {}: not sent (it "
                      "stays in its mailbox)",
                      e.generation, e.guest, g_guest.empty() ? std::string("none") : g_guest);
            continue;
        }
        g.time_unix = e.time_unix;
        g.reason = gsw::live_reason(e.reason, now, g_host_final_ms.load(), g_guest_final_ms.load());
        const auto ls = sha256::parse_hex(e.lobby_sha256);
        const auto ss = sha256::parse_hex(e.save_sha256);
        if (!ls || !ss || !e.lobby_size || !e.save_size) {
            log::error("Session2 guest save link: export gen{} has no usable sizes / SHA-256: not sent", e.generation);
            continue;
        }
        g.files[0] = {static_cast<uint32_t>(e.lobby_size), *ls};
        g.files[1] = {static_cast<uint32_t>(e.save_size), *ss};
        switch (g_sender.on_export(g)) {
            case gsw::ResultSender::Export::Queued:
                log::info("Session2 guest save link: gen{} ({}) queued for the guest{}", g.gen, gsw::reason_name(g.reason),
                          g_guest.empty() ? " (no guest identified by an upload yet: it waits)" : "");
                break;
            case gsw::ResultSender::Export::Superseded:
                g_out = {};
                log::info("Session2 guest save link: gen{} ({}) replaces the generation in flight (sent from the start)",
                          g.gen, gsw::reason_name(g.reason));
                break;
            case gsw::ResultSender::Export::Old:
                c_exports_old.fetch_add(1);
                log::warn("Session2 guest save link: gen{} is NOT sent: the guest already has gen{} or newer (delivered / its base "
                          "gen); it stays in the mailbox",
                          g.gen, g_sender.delivered());
                break;
        }
    }
}

void send_results(troc::TrocLink& link, uint64_t now) {
    if (g_guest.empty()) return;
    if (const auto v = g_sender.check_timeout(now)) {
        g_out = {};
        log::warn("Session2 guest save link: no SAVE_RESULT_ACK {} s after the last chunk ({})",
                  gsw::kResultAckTimeoutMs / 1000,
                  *v == gsw::ResultSender::AckVerdict::Retry ? "sent again" : "parked until the next connection");
    }
    if (!g_sender.in_flight()) {
        const uint32_t xfer = ++g_next_xfer;
        if (const auto g = g_sender.start(xfer, now)) {
            const std::wstring mailbox = guest_save_mailbox_folder(g_guest);
            std::vector<uint8_t> lobby, save;
            std::string why;
            gsw::Generation disk;
            if (mailbox.empty() || !load_generation(mailbox, g->gen, lobby, save, disk, why) || disk.files != g->files) {
                if (why.empty()) why = "its files on disk are not the exported ones";
                log::error("Session2 guest save link: gen{} cannot be sent: {}", g->gen, why);
                on_result_ack(gsw::Ack{xfer, static_cast<uint8_t>(AckResult::Io), 0});
                return;
            }
            const gsw::Result r{xfer, g->gen, g->slot, g->reason, g->time_unix, g->files, g->root, g->base};
            if (!link.push(gsw::frame(gsw::encode_result(r)))) {
                g_sender.on_link_lost();
                return;
            }
            c_results_started.fetch_add(1);
            g_out = gsw::Outgoing(xfer, std::move(lobby), std::move(save));
            log::info("Session2 guest save link: SAVE_RESULT gen{} -> the guest (xfer {}, its slot{}, line {}, {}; lobby {} "
                      "B, save {} B in {} chunks, paced under {} KB of tunnel backlog; attempt {})",
                      r.gen, xfer, r.slot, gsw::digest_zero(r.root) ? std::string("none") : short_sha(r.root) + "...",
                      gsw::reason_name(r.reason), r.files[0].size, r.files[1].size,
                      gsw::Outgoing::chunk_count(r.files[0].size, r.files[1].size), gsw::kDownloadBacklog / 1024,
                      g_sender.attempts());
        }
    }
    while (g_sender.sending() && link.backlog_bytes() < gsw::kDownloadBacklog) {
        std::vector<uint8_t> payload;
        if (!g_out.next(payload)) {
            g_sender.all_sent(now);
            log::info("Session2 guest save link: all {} chunks of gen{} are in the tunnel; waiting for the guest's "
                      "SAVE_RESULT_ACK",
                      g_out.chunks(), g_sender.current() ? g_sender.current()->gen : 0);
            break;
        }
        if (!link.push(gsw::frame(payload))) {
            g_sender.on_link_lost();
            g_out = {};
            break;
        }
        c_chunks_out.fetch_add(1);
        c_bytes_out.fetch_add(payload.size() - gsw::kChunkHeader);
    }
}

void send_status(troc::TrocLink& link, uint64_t now) {
    gsw::Status s;
    s.mode = static_cast<uint8_t>(gsw::host_mode(guest_save_mode(), guest_save_state()));
    s.guest_slot = gsv::kGuestSlot;
    s.last_error = guest_save_last_error();
    s.host_id = g_own_id.load();
    s.last_gen = guest_save_last_generation();
    std::vector<uint8_t> p = gsw::encode_status(s);
    if (p == g_last_status && now - g_status_ms < 5000) return;
    if (!link.push(gsw::frame(p))) return;
    c_status_out.fetch_add(1);
    g_status_ms = now;
    g_last_status = std::move(p);
    if (s.mode != g_status_mode_logged) {
        g_status_mode_logged = s.mode;
        log::info("Session2 guest save link: SAVE_STATUS to the guest: mode {} = {} | slot {} | last game error 0x{:X} | "
                  "host {} | last gen {}",
                  s.mode, gsw::host_mode_name(s.mode), s.guest_slot, s.last_error, s.host_id, s.last_gen);
    }
}

void on_new_connection() {
    g_sender.on_link_lost();
    g_out = {};
    if (g_up.active()) {
        log::warn("Session2 guest save link: the upload xfer {} of the previous connection is dropped ({} of {} B)",
                  g_up.xfer(), g_up.received(), g_up.total());
        g_up.reset();
    }
    g_replies.clear();
    g_last_status.clear();
    g_speaks_logged = false;
}

void on_export(const GuestSaveExport& e) noexcept {
    try {
        std::lock_guard lock(g_exp_mu);
        if (g_exports.size() < 16) g_exports.push_back(e);
    } catch (...) {
    }
}

// ---------------------------------------------------------------- the co-op world's character: the gate
// (coop_world_logic.h.) What user 2 loads in a world with a marker, decided when its LoginUserToFieldServerReq 2535
// is about to be handed to the server. Network thread.

// The guest the staged pair is for: the one whose upload was accepted on this connection, else the Steam peer, else
// the configured id.
std::string coop_world_guest() {
    if (!g_guest.empty()) return g_guest;
    const uint64_t peer = g_peer_id.load();
    return peer ? std::to_string(peer) : guest_save_guest_id();
}

std::string pair_text(const std::vector<uint8_t>& lobby, const std::vector<uint8_t>& save) {
    return std::format("lobby {} B sha256 {}..., save {} B sha256 {}...", lobby.size(), short_sha(sha256::of(lobby)),
                       save.size(), short_sha(sha256::of(save)));
}

// A pair of the world's line into the staging folder (begin / write / ready). Its line and its base are both
// `root_hex`, never the friend's own save: the apply script on its PC never puts this character into a solo slot.
// False (why) = the staging itself failed (the mode is off or failed in this run, or the folder cannot be written).
bool stage_world_pair(const std::string& source, const std::vector<uint8_t>& lobby, const std::vector<uint8_t>& save,
                      const std::string& root_hex, std::string& why) {
    if (!guest_save_incoming_begin(why)) return false;
    if (!guest_save_incoming_write(gsv::FileKind::Lobby, lobby, why) ||
        !guest_save_incoming_write(gsv::FileKind::Save, save, why)) {
        return false;
    }
    GuestSaveLabel label;
    label.guest = coop_world_guest();
    if (!g_guest.empty()) label.guest_slot = g_guest_slot;
    label.lineage_root = root_hex;
    label.lineage_base = root_hex;
    return guest_save_incoming_ready(source, label, why);
}

// The newest generation of the line `root` in one mailbox, by the manifests (gsw::find_continuation with the line's
// root as the offered save: `newer` is the line's newest generation; `at` only when a generation is the root itself).
std::optional<uint32_t> newest_of_line(const std::wstring& mailbox, const sha256::Digest& root) {
    std::vector<gsw::LineGen> lines;
    for (const uint32_t gen : list_generations(mailbox)) {
        gsw::LineGen lg;
        if (read_line(mailbox, gen, lg)) lines.push_back(lg);
    }
    const gsw::Continuation c = gsw::find_continuation(lines, root);
    if (!c.found || c.root != root) return std::nullopt;
    if (c.newer) return c.newer;
    if (c.at) return c.at;
    return std::nullopt;
}

struct WorldGen {
    std::string guest;     // whose mailbox it is in
    std::wstring mailbox;
    gsw::Generation info{};
    std::vector<uint8_t> lobby, save;
};

// The world's character in the mailbox of the guest the marker names and, when another guest is connected, in that
// one's too (its sessions exported there): the newest generation of the line, loaded whole and checked
// (load_generation); of two, the later one by its manifest's time. `notes` = what could not be used.
std::optional<WorldGen> find_world_generation(const cwl::Marker& marker, const sha256::Digest& root, std::string& notes) {
    std::vector<std::string> ids;
    if (!marker.guest.empty()) ids.push_back(marker.guest);
    const std::string connected = coop_world_guest();
    if (!connected.empty() && connected != marker.guest) ids.push_back(connected);
    std::optional<WorldGen> best;
    for (const std::string& id : ids) {
        const std::wstring mailbox = guest_save_mailbox_folder(id);
        if (mailbox.empty()) continue;
        const std::optional<uint32_t> gen = newest_of_line(mailbox, root);
        if (!gen) continue;
        WorldGen w;
        std::string why;
        if (!load_generation(mailbox, *gen, w.lobby, w.save, w.info, why) || w.info.root != root) {
            notes += std::format(" [mailbox gen{} of guest {} is of this line but cannot be used: {}]", *gen, id,
                                 why.empty() ? std::string("its line changed on disk") : why);
            continue;
        }
        w.guest = id;
        w.mailbox = mailbox;
        if (!best || w.info.time_unix > best->info.time_unix) best = std::move(w);
    }
    return best;
}

struct HostSeed {
    uint32_t slot = 0;
    std::vector<uint8_t> lobby, save;
    cwl::SlotSample sample;
};

// True = the host's slot (cwl::pick_host_slot) was read twice, at least cwl::kStableMs apart, and gave the same
// sizes and SHA-256 both times: `seed` holds the second read's bytes. False: `why` says what is still missing.
bool host_slot_stable(const std::wstring& account_dir, uint64_t now, HostSeed& seed, std::string& why) {
    CoopWorldGate& g = g_cw;
    if (g.sample && now - g.sample_ms < cwl::kStableMs) {
        why = std::format("the host's slot{} was read once; it is read again {} ms after that", g.sample->slot,
                          cwl::kStableMs);
        return false;
    }
    const std::vector<gsw::SlotSeen> seen = list_host_slots(account_dir);
    const std::optional<uint32_t> slot = cwl::pick_host_slot(seen);
    if (!slot) {
        g.sample.reset();
        why = std::format("the host has no complete save slot in this world yet ({} slot number(s) in '{}'; a slot "
                          "that is being written is skipped)",
                          seen.size(), narrow(account_dir));
        return false;
    }
    if (!read_pair(account_dir + std::format(L"\\slot{}", *slot), seed.lobby, seed.save, why)) {
        g.sample.reset();
        return false;
    }
    if (seed.save.size() < cwl::kMinSeedSaveBytes) {
        // fs1a (2026-11-12): a new game's first save is a 5180-byte stub written at world entry, before the story's
        // first quest exists; the friend seeded from it had no quest at all. The first real save (565 KB) came 63 s
        // later, in another slot.
        g.sample.reset();
        why = std::format("the host's newest slot{} is only the new game's first stub ({} B, a real save is over {} "
                          "KB): its first real save is waited for",
                          *slot, seed.save.size(), cwl::kMinSeedSaveBytes / 1024);
        return false;
    }
    seed.slot = *slot;
    seed.sample = cwl::SlotSample{*slot, seed.lobby.size(), seed.save.size(), sha256::of(seed.lobby),
                                  sha256::of(seed.save)};
    const cwl::Stable stable = cwl::judge_stable(g.sample, g.sample_ms, seed.sample, now);
    if (stable == cwl::Stable::Yes) return true;
    why = stable == cwl::Stable::Changed
              ? std::format("the host's slot{} changed between two reads (a save in progress): read again", *slot)
              : std::format("the host's slot{} was read once (lobby {} B, save {} B); it must read the same again", *slot,
                            seed.lobby.size(), seed.save.size());
    if (stable != cwl::Stable::TooSoon) {
        g.sample = seed.sample;
        g.sample_ms = now;
    }
    return false;
}

bool gate_open(uint64_t now, uint64_t for_ms) {
    g_cw.wait_since_ms = 0;
    g_cw.waits = 0;
    g_cw.open_until_ms = now + for_ms;
    return true;
}

// A hard failure: the gate opens and user 2's login goes on with the behaviour before the co-op world's character
// (the friend's own upload, staged now if one was kept). Never a wait without an end.
bool gate_fallback(uint64_t now, const std::string& reason) {
    g_cw.sample.reset();
    g_cw.logged = false;
    c_cw_fallback.fetch_add(1);
    std::string what;
    if (g_cw_upload.have) {
        uint32_t staged_gen = 0;
        std::string why;
        const AckResult r = stage_upload(g_cw_upload.xfer, g_cw_upload.offer, g_cw_upload.guest, g_cw_upload.lobby,
                                         g_cw_upload.save, staged_gen, why);
        what = r == AckResult::Ok
                   ? std::format("the friend's own upload (xfer {}, guest {}) is staged instead{}", g_cw_upload.xfer,
                                 g_cw_upload.guest,
                                 staged_gen ? std::format(", from its mailbox gen{}", staged_gen) : std::string())
                   : std::format("the friend's own upload (xfer {}) could not be staged either: {}", g_cw_upload.xfer,
                                 why);
    } else {
        what = "no upload of the friend is kept, so nothing is staged (its login may fail on the game's side)";
    }
    log::error("Session2 guest save (co-op world): FAILED: {} | user 2's login goes on as before the co-op world's "
               "character: {} | the gate stays open for {} s",
               reason, what, cwl::kGaveUpMs / 1000);
    return gate_open(now, cwl::kGaveUpMs);
}

// True = user 2's 2535 may go to the server now (what it loads is staged or live, or this is not a marker world);
// false = not yet (asked again at the Req's next offer; the disk is looked at every cwl::kGateLookMs).
bool coop_world_gate(uint64_t now) {
    if (guest_save_mode() != gsv::Mode::On) return true;
    CoopWorldGate& g = g_cw;
    if (now < g.open_until_ms) return true;
    if (g.wait_since_ms && now > g.next_look_ms + kCoopWorldStaleMs) {
        // The Req that waited never came back (its conversation ended): the next login starts its own wait.
        g.wait_since_ms = 0;
        g.waits = 0;
        g.sample.reset();
    }
    if (g.wait_since_ms && now < g.next_look_ms) return false;
    cwl::Marker marker;
    std::wstring marker_path;
    const MarkerRead found = read_marker(marker, &marker_path);
    if (found == MarkerRead::Off || found == MarkerRead::Absent) return gate_open(now, kCoopWorldOpenMs);
    if (found == MarkerRead::Invalid) {
        return gate_fallback(now, std::format("the world's marker '{}' cannot be read or is not one this build "
                                              "understands",
                                              narrow(marker_path)));
    }
    const CoopWorldPaths paths = guest_save_coop_world_paths();
    const bool world = marker.character == cwl::Character::World;
    const std::string marker_says = world ? std::format("world (line {}..., made for guest {})",
                                                        marker.lineage_root.substr(0, 16),
                                                        marker.guest.empty() ? std::string("?") : marker.guest)
                                          : std::string("pending");
    cwl::StageInputs in;
    in.enabled = true;
    in.marker = marker.character;
    const gsv::SlotState state = guest_save_state();
    GuestSaveLabel staged, live;
    guest_save_labels(staged, live);
    in.staged_world = world && state.staged && staged.lineage_root == marker.lineage_root;
    in.live_world = world && state.live && live.lineage_root == marker.lineage_root;
    in.timed_out = g.wait_since_ms && now - g.wait_since_ms >= cwl::kHostSlotWaitMs;
    std::optional<WorldGen> gen;
    std::string notes;
    if (world && !in.staged_world && !in.live_world) {
        if (const auto root = sha256::parse_hex(marker.lineage_root)) gen = find_world_generation(marker, *root, notes);
        in.mailbox = gen.has_value();
        in.prev = !paths.prev_slot.empty() && file_there(paths.prev_slot + L"\\lobby.save") &&
                  file_there(paths.prev_slot + L"\\save.save");
    }
    HostSeed seed;
    std::string host_why;
    bool host_asked = false;
    for (int step = 0; step < 4; ++step) {
        cwl::Stage stage = cwl::decide_stage(in);
        if ((stage == cwl::Stage::Wait || stage == cwl::Stage::Fallback) && !host_asked) {
            // Only now: the host's slot is read only when nothing else is there to stage.
            host_asked = true;
            in.host_stable = host_slot_stable(paths.account, now, seed, host_why);
            stage = cwl::decide_stage(in);
        }
        switch (stage) {
            case cwl::Stage::Today: return gate_open(now, kCoopWorldOpenMs);
            case cwl::Stage::KeepStaged:
            case cwl::Stage::KeepLive:
                if (!g.logged || g.logged_stage != stage || g.logged_root != marker.lineage_root) {
                    g.logged = true;
                    g.logged_stage = stage;
                    g.logged_root = marker.lineage_root;
                    log::info("Session2 guest save (co-op world): marker {}: {}: nothing new is staged, user 2's login "
                              "goes on",
                              marker_says,
                              stage == cwl::Stage::KeepStaged
                                  ? "a pair of this line is staged already (user 2's next lobby load moves it in)"
                                  : "this line is live in the guest slot in this run (user 2 loads it again)");
                }
                return gate_open(now, kCoopWorldOpenMs);
            case cwl::Stage::Wait: {
                if (!g.wait_since_ms) g.wait_since_ms = now;
                g.next_look_ms = now + cwl::kGateLookMs;
                if (g.waits++ % 20 == 0) {
                    log::info("Session2 guest save (co-op world): marker {}: user 2's login waits: {}{} ({} s so far, "
                              "{} s at most)",
                              marker_says, host_why, notes, (now - g.wait_since_ms) / 1000,
                              cwl::kHostSlotWaitMs / 1000);
                }
                return false;
            }
            case cwl::Stage::Fallback:
                return gate_fallback(now, std::format("marker {}: no complete save slot of the host's that reads the "
                                                      "same twice in {} s (last: {}){}",
                                                      marker_says, cwl::kHostSlotWaitMs / 1000, host_why, notes));
            case cwl::Stage::SeedFromHost:
            case cwl::Stage::ReseedFromHost: {
                const bool again = stage == cwl::Stage::ReseedFromHost;
                const std::string root = sha256::hex(seed.sample.save);
                const std::string from = std::format("the host's own slot{} in '{}'", seed.slot, narrow(paths.account));
                std::string why;
                if (!stage_world_pair(std::format("{} (a copy; the co-op world's new line {}...)", from, root.substr(0, 16)),
                                      seed.lobby, seed.save, root, why)) {
                    return gate_fallback(now, std::format("marker {}: the copy of {} could not be staged: {}", marker_says,
                                                          from, why));
                }
                cwl::Marker next;
                next.character = cwl::Character::World;
                next.guest = coop_world_guest();
                next.lineage_root = root;
                std::string marker_why;
                const bool written = write_marker(next, marker_why);
                g.sample.reset();
                g.logged = false;
                (again ? c_cw_reseeded : c_cw_seeded).fetch_add(1);
                const std::string text =
                    std::format("staged a copy of {} ({}), read the same twice, as a NEW line {}... (root and base) for "
                                "guest {}; marker {}",
                                from, pair_text(seed.lobby, seed.save), root.substr(0, 16), next.guest,
                                written ? "rewritten: user2_character=world" : "NOT rewritten");
                if (again) {
                    log::warn("Session2 guest save (co-op world): marker {}: neither a mailbox generation of that line "
                              "nor last session's guest slot ('{}') is there{}: the friend's character of this world is "
                              "LOST and made anew: {}",
                              marker_says, narrow(paths.prev_slot), notes, text);
                } else {
                    log::info("Session2 guest save (co-op world): marker {}: the friend gets a new character, the "
                              "host's save: {}",
                              marker_says, text);
                }
                if (!written) {
                    log::error("Session2 guest save (co-op world): the marker could not be rewritten ({}): the staged "
                               "pair is played now, but the next session will not find this line under the marker "
                               "(its saves stay in the mailbox as line {}...)",
                               marker_why, root.substr(0, 16));
                    return gate_open(now, cwl::kGaveUpMs);
                }
                return gate_open(now, kCoopWorldOpenMs);
            }
            case cwl::Stage::Mailbox: {
                std::string why;
                const std::string from = std::format("mailbox gen{} of guest {} ('{}')", gen->info.gen, gen->guest,
                                                     narrow(gen->mailbox));
                if (!stage_world_pair(std::format("{} (the co-op world's character, line {}...)", from,
                                                  marker.lineage_root.substr(0, 16)),
                                      gen->lobby, gen->save, marker.lineage_root, why)) {
                    return gate_fallback(now, std::format("marker {}: {} could not be staged: {}", marker_says, from, why));
                }
                g.logged = false;
                c_cw_mailbox.fetch_add(1);
                log::info("Session2 guest save (co-op world): marker {}: staged the newest generation of that line, {} "
                          "({}){}: user 2 comes back as this world's character",
                          marker_says, from, pair_text(gen->lobby, gen->save), notes);
                return gate_open(now, kCoopWorldOpenMs);
            }
            case cwl::Stage::Prev: {
                std::vector<uint8_t> lobby, save;
                std::string why;
                if (!read_pair(paths.prev_slot, lobby, save, why)) {
                    notes += std::format(" [last session's guest slot '{}' cannot be used: {}]", narrow(paths.prev_slot),
                                         why);
                    in.prev = false;
                    continue;
                }
                const std::string from = std::format("last session's guest slot '{}'", narrow(paths.prev_slot));
                if (!stage_world_pair(std::format("{} (the co-op world's character, line {}...)", from,
                                                  marker.lineage_root.substr(0, 16)),
                                      lobby, save, marker.lineage_root, why)) {
                    return gate_fallback(now, std::format("marker {}: {} could not be staged: {}", marker_says, from, why));
                }
                g.logged = false;
                c_cw_prev.fetch_add(1);
                log::info("Session2 guest save (co-op world): marker {}: no mailbox generation of that line{}: staged {} "
                          "({}): user 2 comes back as this world's character",
                          marker_says, notes, from, pair_text(lobby, save));
                return gate_open(now, kCoopWorldOpenMs);
            }
        }
    }
    return gate_fallback(now, std::format("marker {}: no decision could be made{}", marker_says, notes));
}

}  // namespace

void guest_save_set_net_ids(uint64_t own, uint64_t peer) {
    g_own_id.store(own, std::memory_order_relaxed);
    g_peer_id.store(peer, std::memory_order_relaxed);
}
uint64_t guest_save_own_net_id() { return g_own_id.load(std::memory_order_relaxed); }
uint64_t guest_save_peer_net_id() { return g_peer_id.load(std::memory_order_relaxed); }

void guest_save_tunnel_install() {
    if (guest_save_mode() == gsv::Mode::Off || g_installed.exchange(true)) return;
    g_next_xfer = static_cast<uint32_t>(GetTickCount64()) << 8;  // never the xfer of an earlier run's last transfer
    guest_save_set_result_handler(&on_export);
    log::info("Session2 guest save (P8 S25, phase B): the tunnel speaks states 6-11 | upload: SAVE_OFFER + SAVE_CHUNK "
              "({} B at most) checked (SHA-256, the game's header check) and staged in order, before the guest's 2490 / "
              "3093 | download: each export of slot204 as SAVE_RESULT + chunks, paced under {} KB of tunnel backlog, "
              "to a guest that announced caps 0x{:02X} | policy: 2910 (slot 100-108) / 3037 {}, 2711 / 2178 {} | link lost: {}",
              gsw::kMaxChunk, gsw::kDownloadBacklog / 1024, gsw::kCapsGuestSave,
              guest_save_mode() == gsv::Mode::On ? "to the server once the guest's slot is live" : "blocked (probe)",
              guest_save_mode() == gsv::Mode::On ? "blocked" : "as before (probe)",
              guest_save_mode() == gsv::Mode::On ? "one SaveToFileCurrentPlayerReq 2910 (slot 100, 'Co-op') before user 2's 2001 (15 s at most)"
                                                 : "nothing (probe)");
    // Co-op+ 2026-11-12 (coop_world_logic.h): what this world's marker says at the start (a read only; this is the
    // late-init thread, before the tunnel carries anything).
    cwl::Marker marker;
    std::wstring marker_path;
    switch (read_marker(marker, &marker_path)) {
        case MarkerRead::Off:
            log::info("Session2 guest save (co-op world): off ([session2] coop_world_character=0, or own-save mode is "
                      "not 1): no marker is read; the friend brings its own character, as before");
            break;
        case MarkerRead::Absent:
            log::info("Session2 guest save (co-op world): coop_world_character=1 | no marker '{}': this world is not a "
                      "new co-op save of this build (a copy of the solo progress, or an older world): the friend "
                      "brings its own character, as before",
                      narrow(marker_path));
            break;
        case MarkerRead::Invalid:
            log::error("Session2 guest save (co-op world): coop_world_character=1 | the marker '{}' cannot be read or "
                       "is not one this build understands: the friend brings its own character, as before",
                       narrow(marker_path));
            break;
        case MarkerRead::Ok:
            log::info("Session2 guest save (co-op world): coop_world_character=1 | marker '{}': user2_character={}{} | "
                      "the friend's upload is checked but not staged; its login stages {}",
                      narrow(marker_path), cwl::character_name(marker.character),
                      marker.character == cwl::Character::World
                          ? std::format(", line {}..., made for guest {}", marker.lineage_root.substr(0, 16),
                                        marker.guest.empty() ? std::string("?") : marker.guest)
                          : std::string(),
                      marker.character == cwl::Character::World
                          ? "the newest mailbox generation of that line (else last session's guest slot, else a copy "
                            "of the host's slot again)"
                          : "a copy of the host's own complete save slot, read the same twice (a new line)");
            break;
    }
}

bool guest_save_coop_world_gate() {
    try {
        return coop_world_gate(now64());
    } catch (...) {
        // Only the mod's own steps throw (no memory): the login goes on, never a wait without an end.
        return true;
    }
}

bool guest_save_tunnel_on_frame(const std::vector<uint8_t>& packet, uint32_t now_ms) {
    const auto payload = gsw::save_payload(packet);
    if (!payload) return false;
    try {
        const std::span<const uint8_t> p = *payload;
        switch (p[0]) {
            case gsw::kStateOffer:
                if (const auto o = gsw::decode_offer(p)) {
                    on_offer(*o, now_ms);
                } else {
                    c_malformed.fetch_add(1);
                }
                break;
            case gsw::kStateChunk:
                if (const auto c = gsw::decode_chunk(p)) {
                    on_chunk(*c);
                } else {
                    c_malformed.fetch_add(1);
                }
                break;
            case gsw::kStateAck:
                if (const auto a = gsw::decode_ack(p, gsw::kStateAck)) {
                    c_cancels.fetch_add(1);
                    log::warn("Session2 guest save link: the guest gave up its upload xfer {} ({}): it falls back (no "
                              "own save on its side in this session)",
                              a->xfer, gsw::ack_result_name(a->result));
                } else {
                    c_malformed.fetch_add(1);
                }
                break;
            case gsw::kStateResultAck:
                if (const auto a = gsw::decode_ack(p, gsw::kStateResultAck)) {
                    on_result_ack(*a);
                } else {
                    c_malformed.fetch_add(1);
                }
                break;
            default: c_malformed.fetch_add(1); break;  // 9 / 10 only go the other way
        }
    } catch (...) {
        g_up.reset();
    }
    return true;  // never a server Req
}

void guest_save_tunnel_tick(troc::TrocLink& link, uint32_t now_ms) {
    (void)now_ms;
    if (!g_installed.load(std::memory_order_relaxed)) {
        // guest_own_save=0 here: only the refusals (SAVE_ACK "own save off") go back, so a guest in mode 1 does not
        // hold its 3093 for 30 s.
        if (g_replies.empty()) return;
        try {
            if (link.connected() && !link.broken()) {
                while (!g_replies.empty() && link.push(gsw::frame(g_replies.front()))) g_replies.pop_front();
            }
            if (g_replies.size() > 64) g_replies.clear();
        } catch (...) {
        }
        return;
    }
    try {
        const uint64_t now = now64();
        const uint32_t le = link.session_epoch();
        if (!g_link_seen || le != g_link_epoch) {
            if (g_link_seen) on_new_connection();
            g_link_seen = true;
            g_link_epoch = le;
        }
        take_exports(now);
        if (!link.connected() || link.broken()) return;
        while (!g_replies.empty()) {
            if (!link.push(gsw::frame(g_replies.front()))) break;
            g_replies.pop_front();
        }
        if ((link.peer_caps() & gsw::kCapsGuestSave) == 0) return;
        if (!g_speaks_logged) {
            g_speaks_logged = true;
            log::info("Session2 guest save link: the guest announced caps 0x{:02X} on this connection (it speaks states "
                      "6-11): SAVE_STATUS and SAVE_RESULT go to it",
                      link.peer_caps());
        }
        send_status(link, now);
        send_results(link, now);
    } catch (...) {
    }
}

gsw::ReqPolicy guest_save_req_policy(uint16_t opcode, std::span<const uint8_t> payload) {
    return gsw::req_policy(guest_save_mode(), guest_save_state(), opcode, payload);
}

void guest_save_tunnel_note_req(uint16_t opcode, std::span<const uint8_t> payload) {
    // The guest's final save (a 2910 named gsw::kFinalSaveName): the next export's reason is "final".
    if (opcode == gsw::kOpSaveToFileCurrentPlayerReq && gsw::is_final_save_req(payload)) g_guest_final_ms.store(now64());
}

bool guest_save_final_save_wanted() {
    return guest_save_mode() == gsv::Mode::On && gsv::slot_healthy(guest_save_state());
}

void guest_save_tunnel_note_final_injected() { g_host_final_ms.store(now64()); }

std::string guest_save_tunnel_describe() {
    if (!g_installed.load()) return {};
    const uint64_t cw_staged = c_cw_seeded.load() + c_cw_reseeded.load() + c_cw_mailbox.load() + c_cw_prev.load();
    const std::string cw = c_cw_kept.load() || cw_staged || c_cw_fallback.load()
                               ? std::format(" | co-op world character: uploads kept aside {}, staged from the host's "
                                             "slot {} (again {}), from the mailbox {}, from last session's slot {}, "
                                             "fallbacks {}",
                                             c_cw_kept.load(), c_cw_seeded.load(), c_cw_reseeded.load(),
                                             c_cw_mailbox.load(), c_cw_prev.load(), c_cw_fallback.load())
                               : std::string();
    return std::format("guest save link: offers {} (ok {}, refused {}, from the mailbox {}), chunks in {} ({} KB), "
                       "malformed {}, stale {}, guest gave up {} | results started {}, delivered {}, failed {}, chunks "
                       "out {} ({} KB), status {} | exports seen {} (not sent: older than the guest's {})",
                       c_offers.load(), c_offers_ok.load(), c_offers_refused.load(), c_mailbox_staged.load(),
                       c_chunks_in.load(), c_bytes_in.load() / 1024, c_malformed.load(), c_stale.load(), c_cancels.load(),
                       c_results_started.load(), c_results_delivered.load(), c_results_failed.load(),
                       c_chunks_out.load(), c_bytes_out.load() / 1024, c_status_out.load(), c_exports_in.load(),
                       c_exports_old.load()) +
           cw;
}

}  // namespace cdmp::session2
