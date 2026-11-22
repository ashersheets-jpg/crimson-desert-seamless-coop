// Co-op+ (2026-11-09, build 1.0.0.3576): shop_twin.h / shop_twin_logic.h.
#include "mod/session2/shop_twin.h"

#include <windows.h>

#include <atomic>
#include <cctype>
#include <format>
#include <string>
#include <mutex>
#include <unordered_map>

#include "common/troc_frame.h"
#include "common/troc_link.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/merchant_unhide_logic.h"
#include "mod/session2/server.h"
#include "mod/session2/shop_twin_logic.h"
#include "mod/session2/stage_lookup_logic.h"

namespace cdmp::session2 {
namespace {

bool g_on = false, g_guest = false;
std::atomic<bool> g_again{false};  // [session2] shop_twin_again
std::vector<uint32_t> g_watch_res;   // [session2] twin_watch (research, host): set once at start
std::atomic<bool> g_share_all{false};
uintptr_t g_base = 0;
std::mutex g_mu;
struct Twin {
    uint64_t twin = 0;
    bool initialised = false;  // its first 2920 (the initial state) given
    uint32_t root = 0;         // that 2920's root value (the answer to a branch 0 carries it)
    uint32_t state = 0;        // the type of the last 2920 it was given (stl::next_branch_type)
    // Prepared again after this client's manager lost it (again_check): its own 2380 and initial 2920 payloads, when
    // it was last prepared (GetTickCount64), checks in a row that found it missing, times prepared again.
    std::vector<uint8_t> prep, init;
    uint64_t prepared_ms = 0;
    uint8_t missing = 0;
    uint16_t again = 0;
};
std::unordered_map<uint64_t, Twin> g_twins;  // the host's scene id -> its twin
std::atomic<uint64_t> c_prepared{0}, c_deleted{0}, c_withheld{0}, c_answered{0}, c_branches{0}, c_again{0};
std::atomic<uint32_t> g_last_branch{0};

bool is_shop(uint32_t h) { return mul::is_shop_scene(h); }

// The shop dialog entries: the host's own per NPC (host: collected from its client's 3086s; guest: from state 17).
std::mutex g_dmu;
std::unordered_map<uint32_t, std::vector<stl::Dialog>> g_dialogs;
// dialog_redeliver (guest): the last 3086 payload this client got per NPC (as the host sent it), and the NPCs whose
// host choices changed after it (under g_dmu).
std::atomic<bool> g_redeliver{false};
std::unordered_map<uint32_t, std::vector<uint8_t>> g_last_list;
std::vector<uint32_t> g_redeliver_due;
std::atomic<uint64_t> c_redelivered{0}, c_redeliver_marked{0};
uint64_t g_dialogs_changed_ms = 0;
std::atomic<uint64_t> c_dialog_frames_out{0}, c_dialog_frames_in{0}, c_dialog_rewritten{0};

// This client's character key: the local user's main character [[[[base+0x71C0100]+0x30]+0x58]+0xD0]+0x60.
uint32_t own_key() {
    uintptr_t g = 0, g30 = 0, user = 0, ch = 0;
    uint32_t k = 0;
    if (game::safe_read(g_base + 0x724B350, &g, 8) && g && game::safe_read(g + 0x30, &g30, 8) && g30 &&
        game::safe_read(g30 + 0x58, &user, 8) && user && game::safe_read(user + 0xD0, &ch, 8) && ch)
        game::safe_read(ch + 0x60, &k, 4);
    return (k >> 28) == 0xA ? k : 0;
}

// This client's own ClientSequencerStageManager M: its character's [[[actor+0x68]+0xA8]+0x18], vtable checked.
uintptr_t own_manager() {
    uintptr_t g = 0, g30 = 0, user = 0, ch = 0, comps = 0, seq = 0, m = 0, vt = 0;
    if (!game::safe_read(g_base + 0x724B350, &g, 8) || !g || !game::safe_read(g + 0x30, &g30, 8) || !g30 ||
        !game::safe_read(g30 + 0x58, &user, 8) || !user || !game::safe_read(user + 0xD0, &ch, 8) || !ch ||
        !game::safe_read(ch + 0x68, &comps, 8) || !comps || !game::safe_read(comps + 0xA8, &seq, 8) || !seq ||
        !game::safe_read(seq + 0x18, &m, 8) || !m || !game::safe_read(m, &vt, 8))
        return 0;
    return vt == g_base + isl::kVtClientStageMgr ? m : 0;
}
struct SafeRd {
    template <class T>
    bool rd(uintptr_t a, T& v) const noexcept {
        return game::safe_read(a, &v, sizeof v);
    }
};

// Co-op+ 2026-11-21 (cs75p5): a load / far teleport of this client ends every scene of its own manager M, the twins
// too, while the host's scenes live on in X; nothing prepared the twins again. Every 2 s (the network thread, before a
// host packet): a twin whose id is not a live stage of M in two checks in a row, 8 s or more after it was last
// prepared, gets its own 2380 and initial 2920 again, queued after this packet. A shopkeeper not loaded here makes the
// client refuse the 2380 (a far teleport out of town): the first 10 tries come 8 s apart, then one every 30 s while
// the host's scene lives; a twin seen live again starts over. Under g_mu.
void again_check(std::vector<std::vector<uint8_t>>& after, uint64_t deleting) {
    if (!g_again.load(std::memory_order_relaxed)) return;
    const uint64_t now = GetTickCount64();
    static uint64_t next = 0;
    if (now < next) return;
    next = now + 2000;
    if (g_twins.empty()) return;
    const uintptr_t m = own_manager();
    if (!m) return;
    const SafeRd r;
    for (auto& [host_id, t] : g_twins) {
        if (host_id == deleting) continue;  // this packet deletes it (review F3)
        if (t.prep.empty() || t.init.empty() || now - t.prepared_ms < 8000) continue;
        const isl::Lookup l = isl::find_stage(r, m, t.twin);
        if (!l.read_ok || l.dead) continue;  // unreadable, or still being ended: not yet
        if (l.found) {
            t.missing = 0;
            t.again = 0;  // live: a later loss starts over
            continue;
        }
        if (++t.missing < 2) continue;
        t.missing = 0;
        if (t.again >= 10 && now - t.prepared_ms < 30000) continue;
        if (t.again < 0xFFFF) ++t.again;
        t.prepared_ms = now;
        t.state = 0;
        after.push_back(stl::frame(stl::kOpPrepare, t.prep));
        after.push_back(stl::frame(stl::kOpBranchState, t.init));
        if (c_again.fetch_add(1) < 40)
            log::info("ShopTwin [again] twin 0x{:X} (the host's scene 0x{:X}) is no live scene of this client's manager "
                      "any more (a load or a far teleport ends them): prepared again (try {})",
                      t.twin, host_id, t.again);
    }
}

}  // namespace

void shop_twin_set_watch(const std::string& hex_list) {
    std::vector<uint32_t> v;
    size_t i = 0;
    while (i < hex_list.size()) {
        while (i < hex_list.size() && !std::isxdigit(static_cast<unsigned char>(hex_list[i]))) ++i;
        size_t j = i;
        while (j < hex_list.size() && std::isxdigit(static_cast<unsigned char>(hex_list[j]))) ++j;
        if (j > i) {
            const std::string t = hex_list.substr(i, j - i);
            if (t.size() <= 8) v.push_back(static_cast<uint32_t>(std::stoul(t, nullptr, 16)));
        }
        i = j;
    }
    g_watch_res = v;
    if (!v.empty()) {
        std::string s;
        for (const uint32_t h : v) s += std::format(" 0x{:08X}", h);
        log::info("ShopTwin [watch] twin_watch (research): the host's scenes of{} are watched (their branches, every packet "
                  "naming them)", s);
    }
}

void shop_twin_set_again(bool on) {
    g_again = on;
    if (on) {
        log::info("ShopTwin [again] shop_twin_again=1: a twin this client's own manager lost (a load or far teleport ends "
                  "its scenes) is prepared again while the host's scene lives");
    }
}

void shop_twin_share_all(bool on) {
    g_share_all = on;
    log::info("ShopTwin [dialogs] dialog_share_all={}: {}", on ? 1 : 0,
              on ? "every dialog group the host's NPCs offer (not only the shop's) is shared with the guest"
                 : "only the shop choices are shared");
}

void shop_twin_redeliver(bool on) {
    g_redeliver = on;
    if (on) {
        log::info("ShopTwin [dialogs] dialog_redeliver=1: a dialog list this client already has gets the host's later "
                  "choices too (given to the client again)");
    }
}

void shop_twin_start(bool on, bool guest) {
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_on = on;
    g_guest = guest;
    log::info("ShopTwin {} (shop_twin={}): {}", on ? "ON" : "off", on ? 1 : 0,
              on ? "each shop scene of the host gets a twin for this client's character (id + 0x10000000) in its own "
                   "manager; the twin's requests never reach the host, branches and chart events answered locally"
                 : "the guest sees the host's shop scenes only (no shop of its own)");
}

void shop_twin_on_host(std::vector<uint8_t>& packet, std::vector<std::vector<uint8_t>>& before,
                       std::vector<std::vector<uint8_t>>& after) noexcept {
    if (!g_on || !g_guest) return;
    try {
        uint16_t op = 0;
        std::span<const uint8_t> pl;
        if (!stl::split(packet, op, pl)) return;
        // again_check (shop_twin_again) after this packet's own handling: a twin this packet deletes is not made again.
        struct AgainAfter {
            std::vector<std::vector<uint8_t>>& after;
            uint64_t deleting = 0;
            ~AgainAfter() {
                try {
                    std::lock_guard lk(g_mu);
                    again_check(after, deleting);
                } catch (...) {
                }
            }
        } again_after{after};
        if (op == stl::kOpDelete) again_after.deleting = stl::id_of_2292(pl);
        // dialog_redeliver: the lists whose host choices changed since the client got them, again, ahead of this packet.
        if (g_redeliver.load(std::memory_order_relaxed)) {
            std::lock_guard lk(g_dmu);
            for (uint32_t npc : g_redeliver_due) {
                const auto l = g_last_list.find(npc);
                const auto d = g_dialogs.find(npc);
                if (l == g_last_list.end() || d == g_dialogs.end()) continue;
                if (auto t = stl::with_shop_dialogs(l->second, d->second)) {
                    before.push_back(stl::frame(stl::kOpQuestDialogAck, *t));
                    const uint64_t n = c_redelivered.fetch_add(1) + 1;
                    if (n <= 20)
                        log::info("ShopTwin [dialogs] NPC 0x{:08X}'s list given to this client again with the host's {} "
                                  "choice(s) (first group 0x{:X} index 0x{:X}; re-deliveries {})",
                                  npc, d->second.size(), d->second[0].group, d->second[0].index, n);
                }
            }
            g_redeliver_due.clear();
        }
        if (op == stl::kOpQuestDialogAck) {  // the host's shop choices for this NPC added to the guest's own list
            uint32_t npc = 0;
            std::vector<stl::Dialog> mine;
            if (!stl::dialogs_of(pl, npc, mine, g_share_all)) return;
            if (g_redeliver.load(std::memory_order_relaxed)) {
                std::lock_guard lk(g_dmu);
                g_last_list[npc].assign(pl.begin(), pl.end());
            }
            std::vector<stl::Dialog> host;
            {
                std::lock_guard lk(g_dmu);
                auto it = g_dialogs.find(npc);
                if (it == g_dialogs.end()) return;
                host = it->second;
            }
            if (auto t = stl::with_shop_dialogs(pl, host)) {
                packet = stl::frame(stl::kOpQuestDialogAck, *t);
                const uint64_t n = c_dialog_rewritten.fetch_add(1) + 1;
                if (n <= 20)
                    log::info("ShopTwin [dialogs] the host's {} choice(s) added to this client's dialog list of NPC "
                              "0x{:08X} (first group 0x{:X} index 0x{:X})", host.size(), npc, host[0].group,
                              host[0].index);
            }
            return;
        }
        if (op == stl::kOpPrepare) {
            const uint32_t me = own_key();
            const auto t = stl::twin_prepare(pl, me, &is_shop);
            if (!t) return;
            const uint64_t host_id = stl::rd64(pl.data() + 9);
            {
                std::lock_guard lk(g_mu);
                Twin tw;
                tw.twin = stl::twin_of(host_id);
                tw.prep = *t;
                tw.prepared_ms = GetTickCount64();
                g_twins[host_id] = std::move(tw);
            }
            after.push_back(stl::frame(stl::kOpPrepare, *t));
            const uint64_t n = c_prepared.fetch_add(1) + 1;
            if (n <= 40)
                log::info("ShopTwin [prepared] the host's shop scene 0x{:X} (resource 0x{:08X}) -> twin 0x{:X} for "
                          "0x{:08X}", host_id, stl::rd32(pl.data() + stl::kHashAt), stl::twin_of(host_id), me);
        } else if (op == stl::kOpBranchState) {
            const uint64_t id = stl::id_of_2920(pl);
            std::lock_guard lk(g_mu);
            auto it = g_twins.find(id);
            if (it == g_twins.end() || it->second.initialised) return;  // only the initial state, never the host's branches
            it->second.initialised = true;
            it->second.root = stl::root_of_initial_2920(pl);
            it->second.init = stl::twin_branch_state(pl, own_key());
            after.push_back(stl::frame(stl::kOpBranchState, it->second.init));
        } else if (op == stl::kOpDelete) {
            const uint64_t id = stl::id_of_2292(pl);
            bool had = false;
            {
                std::lock_guard lk(g_mu);
                had = g_twins.erase(id) > 0;
            }
            if (!had) return;
            before.push_back(stl::frame(stl::kOpDelete, stl::twin_delete(pl, own_key())));
            c_deleted.fetch_add(1);
        }
    } catch (...) {
    }
}

bool shop_twin_on_out(std::span<const uint8_t> packet, std::vector<std::vector<uint8_t>>& answers) noexcept {
    if (!g_on) return false;
    try {
        uint16_t op = 0;
        std::span<const uint8_t> pl;
        if (!stl::split(packet, op, pl)) return false;
        const uint64_t id = stl::id_of_request(pl);
        if (!stl::is_twin_id(id)) return false;
        std::vector<std::vector<uint8_t>> a;
        uint32_t type = 0;
        {
            std::lock_guard lk(g_mu);
            Twin* tw = nullptr;
            for (auto& [h, t] : g_twins)
                if (t.twin == id) tw = &t;
            if (!tw) return false;
            a = stl::local_answers(op, pl, own_key(), tw->root, tw->state);
            type = tw->state;
        }
        c_withheld.fetch_add(1);
        if (!a.empty()) c_answered.fetch_add(1);
        if (op == stl::kOpBranchReq && pl.size() >= 12) {
            c_branches.fetch_add(1);
            g_last_branch.store(stl::rd32(pl.data() + 8));
            log::info("ShopTwin [branch] twin 0x{:X} -> branch {} answered locally with state {} (the host's scene is "
                      "untouched)", id, stl::rd32(pl.data() + 8), type);
            // Test harness aid: a flag file next to the exe once a twin's menu is up (the walker stops there).
            if (type == 2) {
                if (HANDLE f = CreateFileW(L"shop_twin_open.flag", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
                    f != INVALID_HANDLE_VALUE)
                    CloseHandle(f);
            }
        }
        for (auto& p : a) answers.push_back(std::move(p));
        return true;
    } catch (...) {
        return false;
    }
}

// Host: its own shop scenes (id -> resource) and the last branch its client asked, to log the server's transitions
// (the twin's table, stl::next_branch_type, comes from these) and to write the harness's stop flag at the menu.
std::unordered_map<uint64_t, uint32_t> g_host_scenes;
std::unordered_map<uint64_t, uint32_t> g_host_asked, g_host_state;
// twin_watch (research): the watched scenes' ids (under g_mu).
std::unordered_map<uint64_t, uint32_t> g_watch_ids;
std::atomic<uint32_t> g_watch_lines{0};
bool watched_res(uint32_t h) {
    for (const uint32_t w : g_watch_res) {
        if (w == h) return true;
    }
    return false;
}
void watch_log(uint16_t op, std::span<const uint8_t> pl, bool s2c) {
    if (g_watch_res.empty()) return;
    uint64_t hit = 0;
    {
        std::lock_guard lk(g_mu);
        if (g_watch_ids.empty()) return;
        for (const size_t off : {size_t{0}, size_t{1}, size_t{4}, size_t{5}, size_t{9}}) {
            if (pl.size() < off + 8) continue;
            const uint64_t v = stl::rd64(pl.data() + off);
            if (g_watch_ids.count(v)) {
                hit = v;
                break;
            }
        }
    }
    if (!hit || g_watch_lines.fetch_add(1) >= 600) return;
    std::string hex;
    for (size_t i = 0; i < pl.size() && i < 48; ++i) hex += std::format("{:02X}", pl[i]);
    log::info("ShopTwin [watch] {} op {} len {} | scene 0x{:X} | {}", s2c ? "S>C" : "C>S", op, pl.size(), hit, hex);
}

void host_scene_watch(uint16_t op, std::span<const uint8_t> pl, bool s2c) {
    if (s2c && op == stl::kOpPrepare && pl.size() >= stl::kPrepareFixed && pl[0] == 4 &&
        (mul::is_shop_scene(stl::rd32(pl.data() + stl::kHashAt)) || watched_res(stl::rd32(pl.data() + stl::kHashAt)))) {
        const uint32_t res = stl::rd32(pl.data() + stl::kHashAt);
        const uint64_t id = stl::rd64(pl.data() + 9);
        {
            std::lock_guard lk(g_mu);
            g_host_scenes[id] = res;
            if (watched_res(res)) g_watch_ids[id] = res;
        }
        if (watched_res(res))
            log::info("ShopTwin [watch] the host's scene 0x{:X} of resource 0x{:08X} is watched (its 2380: len {})", id, res,
                      pl.size());
        return;
    }
    if (!s2c && op == stl::kOpBranchReq && pl.size() >= 12) {
        std::lock_guard lk(g_mu);
        if (g_host_scenes.count(stl::rd64(pl.data()))) g_host_asked[stl::rd64(pl.data())] = stl::rd32(pl.data() + 8);
        return;
    }
    if (s2c && op == stl::kOpBranchState && pl.size() >= 21) {
        const uint64_t id = stl::id_of_2920(pl);
        uint32_t res = 0, asked = 0xFFFFFFFFu, prev = 0;
        {
            std::lock_guard lk(g_mu);
            auto it = g_host_scenes.find(id);
            if (it == g_host_scenes.end()) return;
            res = it->second;
            if (auto a = g_host_asked.find(id); a != g_host_asked.end()) {
                asked = a->second;
                g_host_asked.erase(a);
            }
            prev = g_host_state[id];
            g_host_state[id] = stl::rd32(pl.data() + 13);
        }
        const uint32_t type = stl::rd32(pl.data() + 13), n = stl::rd32(pl.data() + 17);
        if (asked != 0xFFFFFFFFu) {
            const bool ours = stl::next_branch_type(prev, asked) == type;
            log::info("ShopTwin [host scene] 0x{:X} resource 0x{:08X}: state {} branch {} -> type {} n {} (len {}){}", id,
                      res, prev, asked, type, n, pl.size(), ours ? "" : "  <-- NOT the twin's table");
        }
        if (type == 2) {
            if (HANDLE f = CreateFileW(L"shop_twin_open.flag", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
                f != INVALID_HANDLE_VALUE)
                CloseHandle(f);
        }
    }
}

void shop_twin_host_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept {
    if (!g_on || g_guest) return;
    try {
        uint16_t op = 0;
        std::span<const uint8_t> pl;
        if (!stl::split(std::span<const uint8_t>(packet, size), op, pl)) return;
        watch_log(op, pl, server_to_client);  // research (twin_watch)
        host_scene_watch(op, pl, server_to_client);
        if (!server_to_client || op != stl::kOpQuestDialogAck) return;
        uint32_t npc = 0;
        std::vector<stl::Dialog> d;
        if (!stl::dialogs_of(pl, npc, d, g_share_all) || d.empty()) return;  // only NPCs with shared choices
        std::lock_guard lk(g_dmu);
        auto& cur = g_dialogs[npc];
        if (cur != d) {
            cur = d;
            g_dialogs_changed_ms = GetTickCount64();
        }
    } catch (...) {
    }
}

void shop_twin_host_tick(troc::TrocLink& link) {
    if (!g_on || g_guest) return;
    try {
        static uint64_t last_sent = 0, sent_change = 0;
        const uint64_t now = GetTickCount64();
        std::vector<stl::NpcDialogs> v;
        uint64_t changed = 0;
        {
            std::lock_guard lk(g_dmu);
            changed = g_dialogs_changed_ms;
            if (g_dialogs.empty()) return;
            if (changed == sent_change && now - last_sent < 10000) return;
            for (const auto& [npc, d] : g_dialogs) v.push_back({npc, d});
        }
        if (!link.connected() || link.broken() || !server_user2_in_world()) return;
        const auto payload = stl::make_dialog_payload(v);
        if (link.push(troc::make(troc::kControlOpcode, -1, payload))) {
            last_sent = now;
            sent_change = changed;
            c_dialog_frames_out.fetch_add(1);
        }
    } catch (...) {
    }
}

bool shop_twin_guest_on_control(std::span<const uint8_t> payload) {
    if (payload.empty() || payload[0] != stl::kDialogState) return false;
    try {
        if (auto v = stl::parse_dialog_payload(payload)) {
            std::lock_guard lk(g_dmu);
            for (auto& e : *v) {
                // dialog_redeliver: a list the client already has, whose host choices are new or changed.
                if (g_redeliver.load(std::memory_order_relaxed) && g_last_list.count(e.npc)) {
                    const auto old = g_dialogs.find(e.npc);
                    if (old == g_dialogs.end() || old->second != e.dialogs) {
                        g_redeliver_due.push_back(e.npc);
                        c_redeliver_marked.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                g_dialogs[e.npc] = std::move(e.dialogs);
            }
            if (c_dialog_frames_in.fetch_add(1) == 0)
                log::info("ShopTwin [dialogs] first host frame: shop choices of {} NPC(s)", v->size());
        }
    } catch (...) {
    }
    return true;
}

std::string shop_twin_describe() {
    std::lock_guard lk(g_mu);
    return std::format("ShopTwin ({}): live twins {} | prepared {} (again after a load {}) deleted {} | requests "
                       "withheld {} (answered {}, branches {}, last branch {}) | dialogs: host frames out {} in {}, lists "
                       "rewritten {}, given again {} (marked {})",
                       g_on ? "on" : "off", g_twins.size(), c_prepared.load(), c_again.load(), c_deleted.load(),
                       c_withheld.load(),
                       c_answered.load(), c_branches.load(), g_last_branch.load(), c_dialog_frames_out.load(),
                       c_dialog_frames_in.load(), c_dialog_rewritten.load(), c_redelivered.load(),
                       c_redeliver_marked.load());
}

}  // namespace cdmp::session2
