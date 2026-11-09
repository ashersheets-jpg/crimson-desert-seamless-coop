// Co-op+ (2026-11-09, build 1.0.0.3576): shop_twin.h / shop_twin_logic.h.
#include "mod/session2/shop_twin.h"

#include <windows.h>

#include <atomic>
#include <format>
#include <mutex>
#include <unordered_map>

#include "common/troc_frame.h"
#include "common/troc_link.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/merchant_unhide_logic.h"
#include "mod/session2/server.h"
#include "mod/session2/shop_twin_logic.h"

namespace cdmp::session2 {
namespace {

bool g_on = false, g_guest = false;
uintptr_t g_base = 0;
std::mutex g_mu;
struct Twin {
    uint64_t twin = 0;
    bool initialised = false;  // its first 2920 (the initial state) given
    uint32_t root = 0;         // that 2920's root value (the answer to a branch 0 carries it)
    uint32_t state = 0;        // the type of the last 2920 it was given (stl::next_branch_type)
};
std::unordered_map<uint64_t, Twin> g_twins;  // the host's scene id -> its twin
std::atomic<uint64_t> c_prepared{0}, c_deleted{0}, c_withheld{0}, c_answered{0}, c_branches{0};
std::atomic<uint32_t> g_last_branch{0};

bool is_shop(uint32_t h) { return mul::is_shop_scene(h); }

// The shop dialog entries: the host's own per NPC (host: collected from its client's 3086s; guest: from state 17).
std::mutex g_dmu;
std::unordered_map<uint32_t, std::vector<stl::Dialog>> g_dialogs;
uint64_t g_dialogs_changed_ms = 0;
std::atomic<uint64_t> c_dialog_frames_out{0}, c_dialog_frames_in{0}, c_dialog_rewritten{0};

// This client's character key: the local user's main character [[[[base+0x71C0100]+0x30]+0x58]+0xD0]+0x60.
uint32_t own_key() {
    uintptr_t g = 0, g30 = 0, user = 0, ch = 0;
    uint32_t k = 0;
    if (game::safe_read(g_base + 0x71C0100, &g, 8) && g && game::safe_read(g + 0x30, &g30, 8) && g30 &&
        game::safe_read(g30 + 0x58, &user, 8) && user && game::safe_read(user + 0xD0, &ch, 8) && ch)
        game::safe_read(ch + 0x60, &k, 4);
    return (k >> 28) == 0xA ? k : 0;
}

}  // namespace

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
        if (op == stl::kOpQuestDialogAck) {  // the host's shop choices for this NPC added to the guest's own list
            uint32_t npc = 0;
            std::vector<stl::Dialog> mine;
            if (!stl::shop_dialogs_of(pl, npc, mine)) return;
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
                    log::info("ShopTwin [dialogs] the host's {} shop choice(s) added to this client's dialog list of "
                              "NPC 0x{:08X}", host.size(), npc);
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
                g_twins[host_id] = Twin{stl::twin_of(host_id), false};
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
            after.push_back(stl::frame(stl::kOpBranchState, stl::twin_branch_state(pl, own_key())));
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

void shop_twin_host_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept {
    if (!g_on || g_guest || !server_to_client) return;
    try {
        uint16_t op = 0;
        std::span<const uint8_t> pl;
        if (!stl::split(std::span<const uint8_t>(packet, size), op, pl) || op != stl::kOpQuestDialogAck) return;
        uint32_t npc = 0;
        std::vector<stl::Dialog> d;
        if (!stl::shop_dialogs_of(pl, npc, d) || d.empty()) return;  // only NPCs with shop choices
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
            for (auto& e : *v) g_dialogs[e.npc] = std::move(e.dialogs);
            if (c_dialog_frames_in.fetch_add(1) == 0)
                log::info("ShopTwin [dialogs] first host frame: shop choices of {} NPC(s)", v->size());
        }
    } catch (...) {
    }
    return true;
}

std::string shop_twin_describe() {
    std::lock_guard lk(g_mu);
    return std::format("ShopTwin ({}): live twins {} | prepared {} deleted {} | requests withheld {} (answered {}, "
                       "branches {}, last branch {})",
                       g_on ? "on" : "off", g_twins.size(), c_prepared.load(), c_deleted.load(), c_withheld.load(),
                       c_answered.load(), c_branches.load(), g_last_branch.load());
}

}  // namespace cdmp::session2
