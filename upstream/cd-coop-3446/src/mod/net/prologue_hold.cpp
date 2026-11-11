#include "mod/net/prologue_hold.h"

#include <windows.h>
#include <shlobj.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <thread>

#include "common/game_save.h"
#include "mod/log.h"
#include "mod/native_layer.h"
#include "mod/save_io.h"

namespace cdmp::net {
namespace {

namespace fs = std::filesystem;

constexpr uint64_t kCheckMs = 30000;  // the save is read again this often while in the world
constexpr uint64_t kSendMs = 15000;   // our state goes out again this often (and at once when it changes)

std::atomic<bool> g_enabled{true};
std::atomic<uint32_t> g_threshold{60};
std::atomic<bool> g_held{false}, g_ours{false}, g_done_now{false};

std::mutex g_m;
// under g_m:
std::optional<uint32_t> g_done;  // completed missions in our newest save (nullopt: not read / unreadable)
bool g_no_save = false;          // the save folder in use has no save at all (a brand-new game)
bool g_reading = false;
uint64_t g_last_check = 0;
uint32_t g_last_gen = 0;
uint32_t g_known_gen = ~0u;  // the load whose save was read last (~0: none yet; co-op waits until the first)
bool g_theirs = false, g_theirs_known = false;
bool g_was_connected = false, g_sent_ours = false;
uint64_t g_last_send = 0;
std::vector<std::vector<uint8_t>> g_out;
std::string g_line;

fs::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) out = p;
    CoTaskMemFree(p);
    return out;
}

// The save folder in use: the active co-op world's (save_io), else the game's own.
fs::path save_root() {
    const std::wstring w = save_io::world();
    if (!w.empty()) return fs::path(save_io::world_folder(w));
    return known_folder(FOLDERID_LocalAppData) / L"Pearl Abyss" / L"CD" / L"save";
}

// Background thread: the newest save under the root (account folders, then slots), its completed missions.
void read_newest(uint32_t generation) {
    const fs::path root = save_root();
    std::error_code ec;
    fs::path best;
    fs::file_time_type best_t{};
    for (const auto& user : fs::directory_iterator(root, ec)) {
        if (!user.is_directory(ec)) continue;
        for (const auto& slot : fs::directory_iterator(user.path(), ec)) {
            const fs::path s = slot.path() / L"save.save";
            if (!fs::is_regular_file(s, ec)) continue;
            const auto t = fs::last_write_time(s, ec);
            if (!ec && (best.empty() || t > best_t)) {
                best = s;
                best_t = t;
            }
        }
    }
    std::optional<uint32_t> done;
    const bool none = best.empty();
    if (!none) {
        std::ifstream f(best, std::ios::binary);
        const save::Bytes file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::string why;
        if (const auto d = save::decode(file, &why)) {
            if (const auto c = save::save_story_count(d->blob)) done = c->done;
        }
    }
    std::lock_guard lock(g_m);
    g_reading = false;
    if (done != g_done || none != g_no_save) {
        log::info("Prologue: our newest save {} (the solo prologue lasts below {} completed missions)",
                  none   ? std::string("does not exist yet (a new game)")
                  : done ? std::format("has {} completed missions", *done)
                         : std::string("could not be read"),
                  g_threshold.load());
    }
    g_done = done;
    g_no_save = none;
    g_known_gen = generation;
}

void send_locked(bool ours) {
    std::vector<uint8_t> b{kMsgPrologue, static_cast<uint8_t>(ours ? 1 : 0)};
    const uint32_t d = g_done.value_or(0);
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(d >> (8 * i)));
    g_out.push_back(std::move(b));
}

}  // namespace

void prologue_configure(bool enabled, uint32_t threshold) {
    // Shared world (2026-11-11, a tester's "text in the middle" that nothing in the menu removes): there both players
    // are in the host's world from the first second, so nobody plays a prologue "solo first". The hold only kept the
    // co-op features off and its line on screen for any player whose own save has few completed missions, and the
    // merged Co-op tab has no "My prologue is done" row.
    if (kNativeLayer && enabled) {
        g_enabled = false;
        g_threshold = threshold;
        log::info("Prologue: shared world: no solo prologue, co-op is on from the start");
        return;
    }
    g_enabled = enabled;
    g_threshold = threshold;
    log::info("Prologue: {}", enabled ? std::format("played solo (co-op waits below {} completed missions)", threshold)
                                      : std::string("played together ([coop] solo_prologue=0)"));
}

void prologue_tick(const PrologueInput& in) {
    static bool new_run_before = false;  // a New Co-op Save run starts the prologue again
    if (in.new_save_run && !new_run_before && g_done_now.exchange(false)) log::info("Prologue: a new game: solo again");
    new_run_before = in.new_save_run;
    if (in.riding && g_enabled && !g_done_now.exchange(true)) {
        log::info("Prologue: we have ridden 0x{:08X} for 5 s (no riding in the prologue): our prologue is done",
                  in.riding);
    }
    std::lock_guard lock(g_m);
    if (in.in_world && !g_reading && (in.generation != g_last_gen || in.now_ms - g_last_check >= kCheckMs)) {
        g_last_gen = in.generation;
        g_last_check = in.now_ms;
        g_reading = true;
        std::thread(read_newest, in.generation).detach();
    }
    bool ours = false;
    if (g_enabled && !g_done_now && in.in_world) {
        if (g_known_gen == ~0u) ours = true;  // no save read yet this game (a moment): nothing slips in first
        else if (g_done) ours = *g_done < g_threshold.load();
        else ours = g_no_save || in.new_save_run;
    } else if (g_enabled && !g_done_now) {
        ours = g_ours.load();  // at the title or loading: keep what we had
    }
    if (ours != g_ours.exchange(ours)) {
        log::info("Prologue: {}", ours ? "we are in the prologue: co-op waits until it is done" : "our prologue is done");
        g_sent_ours = false;
    }
    if (in.connected && !g_was_connected) g_sent_ours = false;
    if (!in.connected) {
        g_theirs = false;
        g_theirs_known = false;
    }
    g_was_connected = in.connected;
    if (in.connected && (!g_sent_ours || in.now_ms - g_last_send >= kSendMs)) {
        send_locked(ours);
        g_sent_ours = true;
        g_last_send = in.now_ms;
    }
    const bool held = in.connected && g_enabled && (ours || g_theirs);
    if (held != g_held.exchange(held)) {
        log::info("Prologue: co-op {}", held ? "waits (one of us is in the prologue; each plays it solo)"
                                             : "is on (no prologue in the way)");
    }
    const std::string who = in.partner_name.empty() ? std::string("your friend") : in.partner_name;
    if (!held) g_line.clear();
    else if (ours && g_theirs) g_line = "Co-op starts after the prologue: you each play it on your own";
    else if (ours) g_line = "Co-op starts after your prologue: " + who + " is waiting";
    else g_line = "Co-op starts once " + who + " finishes the prologue";
}

bool prologue_held() { return g_held.load(); }
bool prologue_ours() { return g_ours.load(); }

void prologue_done_now() {
    if (!g_done_now.exchange(true)) log::info("Prologue: our prologue is done (dev script)");
}

std::string prologue_line() {
    std::lock_guard lock(g_m);
    return g_line;
}

std::vector<std::vector<uint8_t>> prologue_take_outgoing() {
    std::lock_guard lock(g_m);
    std::vector<std::vector<uint8_t>> out;
    out.swap(g_out);
    return out;
}

void prologue_on_message(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 2 || bytes[0] != kMsgPrologue) return;
    std::lock_guard lock(g_m);
    const bool theirs = bytes[1] != 0;
    uint32_t done = 0;
    if (bytes.size() >= 6) std::memcpy(&done, bytes.data() + 2, 4);
    if (!g_theirs_known || theirs != g_theirs) {
        log::info("Prologue: the other player is {} ({} completed missions)",
                  theirs ? "in the prologue" : "past the prologue", done);
    }
    g_theirs = theirs;
    g_theirs_known = true;
}

bool prologue_activity_allowed(uint8_t type) {
    // 16 menu, 19 wanted notice, 21 world event notice, 22 chat, 23 kill feed, 25 scoreboard title (coop_activity.cpp)
    return type == 16 || type == 19 || type == 21 || type == 22 || type == 23 || type == 25;
}

}  // namespace cdmp::net
