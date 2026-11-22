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
std::vector<uint32_t> g_extra_res;   // [session2] twin_extra (research, guest): set once at start
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
    // twin_extra (B14): the scene's resource and a 2752 held for the client's StageBranchStateDelayEndReq.
    uint32_t res = 0;
    std::vector<std::vector<uint8_t>> later;
    // twin_extra_chart=3: this client's minigame opponent for the scene (its 2810) and whether the grip was given.
    uint32_t opponent = 0;
    bool caught = false;
};
std::unordered_map<uint64_t, Twin> g_twins;  // the host's scene id -> its twin
// 2026-11-21 (cs86a): the host's initial 2752 of a scene that has no twin YET (its prepare comes later: the observer
// copy holds a PrepareSequencerAck until its actors are announced, the state goes at once; 21 of 31 host scenes in
// cs86a, every shop among them). Given to the twin right after its prepare. Under g_mu; at most kMaxEarly.
std::unordered_map<uint64_t, std::vector<uint8_t>> g_early;
constexpr size_t kMaxEarly = 512;
std::atomic<uint64_t> c_early_kept{0}, c_early_used{0};
// [session2] early_state_x (guest, research): such an early initial state is also delivered again right after its own
// scene's prepare (the host's scene copies in X start as on the host's client).
std::atomic<bool> g_early_x{false};
// [session2] twin_extra_only (guest, research, B14 hypothesis (d)): 1 = a twin_extra scene's own prepare is not given to
// the game (only the twin): the scene's people bind to the twin alone; 2 = the twin's prepare goes BEFORE the host's.
std::atomic<int> g_extra_only{0};
std::atomic<uint64_t> c_extra_only{0};
// [session2] twin_extra_chart (guest, research, B14, cs92): 1 = a twin_extra scene's chart events in its match state
// (kChartState, the table's state 7) go on to the host's server under the host's scene id (the server runs the catch
// there), the server's 2105 for them comes back to the twin; 2 = every chart event of such a twin; 3 = nothing goes
// on, the twin's answer to the match's first chart event carries a grip made here (stl::catch_result). Under g_mu.
std::atomic<int> g_extra_chart{0};
constexpr uint32_t kChartState = 7;
constexpr uint64_t kChartMaxAgeMs = 5000;
constexpr size_t kMaxForwardedCharts = 64;
std::vector<stl::ForwardedChart> g_fwd_charts;
std::atomic<uint64_t> c_chart_out{0}, c_chart_back{0};
std::atomic<uint64_t> c_early_x{0};
std::atomic<uint64_t> c_prepared{0}, c_deleted{0}, c_withheld{0}, c_answered{0}, c_branches{0}, c_again{0};
std::atomic<uint32_t> g_last_branch{0};

bool is_shop(uint32_t h) { return mul::is_shop_scene(h); }
bool is_extra(uint32_t h) {
    for (const uint32_t r : g_extra_res) {
        if (r == h) return true;
    }
    return false;
}
bool is_twin_res(uint32_t h) { return is_shop(h) || is_extra(h); }
std::atomic<uint64_t> c_table{0}, c_table_unknown{0}, c_held{0}, c_mg_out{0}, c_mg_in{0};

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

void shop_twin_set_extra(const std::string& hex_list) {
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
    g_extra_res = v;
    for (const uint32_t h : v) {
        log::info("ShopTwin [extra] twin_extra (research): the host's scenes of resource 0x{:08X} get a twin too ({})", h,
                  stl::chart_of(h).empty() ? "NO answer table for it: its branches go back to the start"
                                           : "answered from its table");
    }
}

void shop_twin_set_extra_only(int mode) {
    g_extra_only = mode == 1 || mode == 2 ? mode : 0;
    if (g_extra_only.load())
        log::info("ShopTwin [extra only] twin_extra_only={} (research): a twin_extra scene of the host's is {}",
                  g_extra_only.load(), g_extra_only.load() == 1 ? "given to the game only as its twin"
                                                                 : "prepared after its twin");
}

void shop_twin_set_extra_chart(int mode) {
    g_extra_chart = mode >= 1 && mode <= 3 ? mode : 0;
    if (g_extra_chart.load() == 3)
        log::info("ShopTwin [extra chart] twin_extra_chart=3 (research, B14): the twin's answer to the match's first "
                  "chart event (7/0) carries a grip made here (CatchActionResultAck, this client's character and its "
                  "minigame opponent); nothing goes to the server");
    else if (g_extra_chart.load())
        log::info("ShopTwin [extra chart] twin_extra_chart={} (research, B14): a twin_extra scene's chart events {} go "
                  "to the host's server under the host's scene id (the server runs the table's catch), its answers back "
                  "to the twin",
                  g_extra_chart.load(), g_extra_chart.load() == 1 ? std::format("in state {}", kChartState) : "(all)");
}

void shop_twin_set_early_x(bool on) {
    g_early_x = on;
    if (on)
        log::info("ShopTwin [early state x] early_state_x=1 (research): a host scene's initial state that comes before "
                  "its prepare is given to the game again right after the prepare");
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
        if (op == stl::kOpChartEventAck && g_extra_chart.load(std::memory_order_relaxed)) {
            // twin_extra_chart: the server's answer to a chart event this client sent on for a twin goes to the twin.
            const std::span<uint8_t> mpl(packet.data() + stl::kHeader, packet.size() - stl::kHeader);
            uint64_t tid = 0;
            {
                std::lock_guard lk(g_mu);
                if (!g_fwd_charts.empty())
                    tid = stl::chart_ack_to_twin(op, mpl, g_fwd_charts, GetTickCount64(), kChartMaxAgeMs);
            }
            if (tid && c_chart_back.fetch_add(1) < 40)
                log::info("ShopTwin [extra chart] the server's UpdateStageChartEventAck (actor 0x{:08X}) for scene 0x{:X} "
                          "given to the game as its twin 0x{:X}", stl::rd32(mpl.data() + 1), tid - stl::kTwinIdBase, tid);
            return;
        }
        if (op == stl::kOpPrepare) {
            // early_state_x: the scene's own initial state that came before this prepare, again right after it.
            const uint64_t pid = pl.size() >= 17 ? stl::rd64(pl.data() + 9) : 0;
            if (pid && g_early_x.load(std::memory_order_relaxed)) {
                std::vector<uint8_t> replay;
                {
                    std::lock_guard lk(g_mu);
                    if (const auto e = g_early.find(pid); e != g_early.end()) replay = e->second;
                }
                if (!replay.empty()) {
                    after.push_back(stl::frame(stl::kOpBranchState, replay));
                    if (c_early_x.fetch_add(1) < 40)
                        log::info("ShopTwin [early state x] the host's scene 0x{:X} (resource 0x{:08X}) sent its initial "
                                  "state before its prepare: given to the game again right after the prepare", pid,
                                  pl.size() >= stl::kHashAt + 4 ? stl::rd32(pl.data() + stl::kHashAt) : 0);
                }
            }
            const uint32_t me = own_key();
            const auto t = stl::twin_prepare(pl, me, &is_twin_res);
            if (!t) {
                if (pid && g_early_x.load(std::memory_order_relaxed)) {
                    std::lock_guard lk(g_mu);
                    g_early.erase(pid);
                }
                return;
            }
            const uint64_t host_id = stl::rd64(pl.data() + 9);
            const uint32_t res = stl::rd32(pl.data() + stl::kHashAt);
            std::vector<uint8_t> early_init;  // the twin's initial state from a 2752 that came before this prepare
            {
                std::lock_guard lk(g_mu);
                Twin tw;
                tw.twin = stl::twin_of(host_id);
                tw.prep = *t;
                tw.prepared_ms = GetTickCount64();
                tw.res = res;
                if (const auto e = g_early.find(host_id); e != g_early.end()) {
                    tw.initialised = true;
                    tw.root = stl::root_of_initial_2920(e->second);
                    tw.init = stl::twin_branch_state(e->second, me);
                    early_init = tw.init;
                    g_early.erase(e);
                }
                g_twins[host_id] = std::move(tw);
            }
            const int only = is_extra(res) ? g_extra_only.load(std::memory_order_relaxed) : 0;
            // twin_extra_only=2: the twin (and its early state) ahead of the host's own prepare of the scene.
            std::vector<std::vector<uint8_t>>& into = only == 2 ? before : after;
            into.push_back(stl::frame(stl::kOpPrepare, *t));
            if (only == 1) {
                packet.clear();  // twin_extra_only=1: the host's own copy is not made here (guest.cpp skips an empty packet)
            }
            if (only && c_extra_only.fetch_add(1) < 20)
                log::info("ShopTwin [extra only] the host's scene 0x{:X} (resource 0x{:08X}): {}", host_id, res,
                          only == 1 ? std::format("NOT given to the game, only its twin 0x{:X}", stl::twin_of(host_id))
                                    : std::format("its twin 0x{:X} prepared BEFORE it", stl::twin_of(host_id)));
            if (!early_init.empty()) {
                into.push_back(stl::frame(stl::kOpBranchState, early_init));
                if (c_early_used.fetch_add(1) < 40)
                    log::info("ShopTwin [early state] the host's scene 0x{:X} sent its initial state before its prepare: "
                              "given to twin 0x{:X} right after the twin's prepare", host_id, stl::twin_of(host_id));
            }
            const uint64_t n = c_prepared.fetch_add(1) + 1;
            if (n <= 40 || !is_shop(res))
                log::info("ShopTwin [prepared] the host's {} scene 0x{:X} (resource 0x{:08X}) -> twin 0x{:X} for "
                          "0x{:08X}", is_shop(res) ? "shop" : "twin_extra", host_id, res, stl::twin_of(host_id), me);
        } else if (op == stl::kOpBranchState) {
            const uint64_t id = stl::id_of_2920(pl);
            std::lock_guard lk(g_mu);
            auto it = g_twins.find(id);
            if (it == g_twins.end()) {
                // No twin yet: an initial state is kept for a twin its prepare may still make (cs86a).
                if (stl::root_of_initial_2920(pl) && !stl::is_twin_id(id)) {
                    if (g_early.size() >= kMaxEarly) g_early.clear();
                    g_early[id].assign(pl.begin(), pl.end());
                    c_early_kept.fetch_add(1);
                }
                return;
            }
            if (it->second.initialised) return;  // only the initial state, never the host's branches
            it->second.initialised = true;
            it->second.root = stl::root_of_initial_2920(pl);
            it->second.init = stl::twin_branch_state(pl, own_key());
            after.push_back(stl::frame(stl::kOpBranchState, it->second.init));
        } else if (op == stl::kOpSpawnMiniGameCharacterAck && !g_extra_res.empty() && packet.size() > stl::kHeader) {
            // twin_extra: this client's own opponent spawn names the host's scene; its game knows the twin.
            const std::span<uint8_t> mpl(packet.data() + stl::kHeader, packet.size() - stl::kHeader);
            std::lock_guard lk(g_mu);
            const uint64_t tid = stl::minigame_ack_to_twin(op, mpl, own_key(), [](uint64_t h) {
                const auto it = g_twins.find(h);
                return it != g_twins.end() && is_extra(it->second.res);
            });
            if (tid) {  // twin_extra_chart=3: the opponent for the grip made at the match's first chart event
                const auto it = g_twins.find(tid - stl::kTwinIdBase);
                if (it != g_twins.end()) {
                    it->second.opponent = stl::minigame_opponent(mpl);
                    it->second.caught = false;
                }
            }
            if (tid && c_mg_in.fetch_add(1) < 20)
                log::info("ShopTwin [minigame] the server's SpawnMiniGameCharacterAck for this client names the host's "
                          "scene 0x{:X}: given to the game as its twin 0x{:X}", tid - stl::kTwinIdBase, tid);
        } else if (op == stl::kOpDelete) {
            const uint64_t id = stl::id_of_2292(pl);
            bool had = false;
            {
                std::lock_guard lk(g_mu);
                had = g_twins.erase(id) > 0;
                g_early.erase(id);
            }
            if (!had) return;
            before.push_back(stl::frame(stl::kOpDelete, stl::twin_delete(pl, own_key())));
            c_deleted.fetch_add(1);
        }
    } catch (...) {
    }
}

void shop_twin_rewrite_out(std::vector<uint8_t>& packet) noexcept {
    if (!g_on || !g_guest || g_extra_res.empty() || packet.size() <= stl::kHeader) return;
    try {
        const uint16_t op = static_cast<uint16_t>(packet[0] | (packet[1] << 8));
        if (op == stl::kOpChartEventReq) {
            // twin_extra_chart: the twin's chart event in its match state goes to the host's scene on the server.
            const int mode = g_extra_chart.load(std::memory_order_relaxed);
            const std::span<uint8_t> cpl(packet.data() + stl::kHeader, packet.size() - stl::kHeader);
            if (!mode || mode == 3 || cpl.size() != 17 || !stl::is_twin_id(stl::rd64(cpl.data()))) return;
            const uint64_t host = stl::rd64(cpl.data()) - stl::kTwinIdBase;
            uint32_t state = 0;
            {
                std::lock_guard lk(g_mu);
                const auto it = g_twins.find(host);
                if (it == g_twins.end() || !is_extra(it->second.res)) return;
                state = it->second.state;
                if (mode == 1 && state != kChartState) return;
                if (stl::chart_req_to_host(op, cpl) != host) return;
                stl::ForwardedChart f;
                f.host = host;
                std::memcpy(f.ev, cpl.data() + 8, sizeof f.ev);
                f.at_ms = GetTickCount64();
                if (g_fwd_charts.size() >= kMaxForwardedCharts) g_fwd_charts.erase(g_fwd_charts.begin());
                g_fwd_charts.push_back(f);
            }
            if (c_chart_out.fetch_add(1) < 40)
                log::info("ShopTwin [extra chart] twin 0x{:X} (state {}): chart event {}/{} ({}) sent to the host's server "
                          "as its scene 0x{:X}", stl::twin_of(host), state, stl::rd32(cpl.data() + 8),
                          stl::rd32(cpl.data() + 12), cpl[16], host);
            return;
        }
        if (op != stl::kOpSpawnMiniGameCharacterReq && op != stl::kOpUnspawnMiniGameCharacterReq) return;
        const std::span<uint8_t> pl(packet.data() + stl::kHeader, packet.size() - stl::kHeader);
        if (pl.size() < 9 || !stl::is_twin_id(stl::rd64(pl.data() + 1))) return;
        {
            std::lock_guard lk(g_mu);
            const auto it = g_twins.find(stl::rd64(pl.data() + 1) - stl::kTwinIdBase);
            if (it == g_twins.end() || !is_extra(it->second.res)) return;  // only a twin_extra scene's
        }
        const uint64_t host = stl::minigame_req_to_host(op, pl);
        if (host && c_mg_out.fetch_add(1) < 20)
            log::info("ShopTwin [minigame] {} names twin 0x{:X}: sent to the host's server with its scene 0x{:X}",
                      op == stl::kOpSpawnMiniGameCharacterReq ? "SpawnMiniGameCharacterReq" : "UnspawnMiniGameCharacterReq",
                      host + stl::kTwinIdBase, host);
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
        uint32_t type = 0, caught_opp = 0;
        {
            std::lock_guard lk(g_mu);
            Twin* tw = nullptr;
            for (auto& [h, t] : g_twins)
                if (t.twin == id) tw = &t;
            if (!tw) return false;
            const auto chart = stl::chart_of(tw->res);
            if (is_extra(tw->res) && !chart.empty() && op == stl::kOpBranchReq) {
                // twin_extra (B14): the scene's own table of the server's answers.
                const uint32_t from = tw->state;
                auto r = stl::table_answers(chart, pl, own_key(), tw->root, tw->state);
                // A state still held (its 2654 never came) goes first: the game sees every step, in order.
                for (auto& f : tw->later) a.push_back(std::move(f));
                tw->later.clear();
                for (auto& f : r.now) a.push_back(std::move(f));
                const bool held = !r.later.empty();
                if (held) tw->later = std::move(r.later);
                c_table.fetch_add(1);
                if (!r.known) c_table_unknown.fetch_add(1);
                log::info("ShopTwin [table] twin 0x{:X} (resource 0x{:08X}) state {} branch {} -> {}{}{}", id, tw->res,
                          from, pl.size() >= 12 ? stl::rd32(pl.data() + 8) : 0, r.next,
                          held ? " (its state held for the client's StageBranchStateDelayEndReq)" : "",
                          r.known ? "" : "  <-- NOT in the table: the scene back to its start");
            } else if (is_extra(tw->res) && op == stl::kOpBranchDelayEndReq) {
                if (!tw->later.empty()) {
                    a = std::move(tw->later);
                    tw->later.clear();
                    c_held.fetch_add(1);
                    log::info("ShopTwin [table] twin 0x{:X}: the client's StageBranchStateDelayEndReq -> its held state "
                              "{}", id, tw->state);
                }
            } else if (is_extra(tw->res) && chart.empty() && op == stl::kOpBranchReq) {
                // no table for this resource: back to the start (never the shop's rule for a scene that is no shop)
                static constexpr stl::ChartStep kNone[] = {{0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, {}}};
                auto r = stl::table_answers(kNone, pl, own_key(), tw->root, tw->state);
                a = std::move(r.now);
                c_table_unknown.fetch_add(1);
                log::info("ShopTwin [table] twin 0x{:X} (resource 0x{:08X}) has no answer table: branch {} -> back to "
                          "the start", id, tw->res, pl.size() >= 12 ? stl::rd32(pl.data() + 8) : 0);
            } else {
                a = stl::local_answers(op, pl, own_key(), tw->root, tw->state);
                // twin_extra_chart=3: the match's first chart event (7/0) gets the grip the server would have run.
                if (is_extra(tw->res) && op == stl::kOpChartEventReq && pl.size() == 17 &&
                    g_extra_chart.load(std::memory_order_relaxed) == 3 && tw->state == kChartState && !tw->caught &&
                    tw->opponent && stl::rd32(pl.data() + 8) == kChartState && stl::rd32(pl.data() + 12) == 0) {
                    if (const uint32_t me = own_key()) {
                        a.insert(a.begin(), stl::catch_result(me, tw->opponent));
                        tw->caught = true;
                        caught_opp = tw->opponent;
                    }
                }
            }
            type = tw->state;
        }
        if (caught_opp)
            log::info("ShopTwin [extra chart] twin 0x{:X}: the match's first chart event answered with a grip made here "
                      "(CatchActionResultAck: this client's character and its opponent 0x{:08X})", id, caught_opp);
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
                       "rewritten {}, given again {} (marked {}) | twin_extra: table answers {} (not in a table {}, held "
                       "states given {}), minigame ids out {} in {}, chart events to the host's server {} answers back "
                       "{} | initial states before their prepare: kept {}, given to a twin {}, again to the game {}",
                       g_on ? "on" : "off", g_twins.size(), c_prepared.load(), c_again.load(), c_deleted.load(),
                       c_withheld.load(),
                       c_answered.load(), c_branches.load(), g_last_branch.load(), c_dialog_frames_out.load(),
                       c_dialog_frames_in.load(), c_dialog_rewritten.load(), c_redelivered.load(),
                       c_redeliver_marked.load(), c_table.load(), c_table_unknown.load(), c_held.load(), c_mg_out.load(),
                       c_mg_in.load(), c_chart_out.load(), c_chart_back.load(), c_early_kept.load(), c_early_used.load(),
                       c_early_x.load());
}

}  // namespace cdmp::session2
