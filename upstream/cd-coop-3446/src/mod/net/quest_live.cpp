#include "mod/net/quest_live.h"

#include <windows.h>

#include <cstring>
#include <deque>
#include <format>
#include <mutex>
#include <unordered_map>

#include "mod/log.h"
#include "mod/net/session_summary.h"
#include "mod/net/coop_worlds.h"
#include "mod/net/party_quests.h"
#include "mod/net/quest_advance.h"
#include "mod/net/troc_sniffer.h"

namespace cdmp::net {
namespace {

constexpr uint16_t kOpMissionTrigger = 3106;    // MissionTriggerInOutReq: u32 mission, u32 user key, u8 in/out
constexpr uint16_t kOpDialogEnd = 3142;         // QuestDialogEndReq: u32 NPC actor, u32 dialog, ... (30 bytes)
constexpr uint16_t kOpMissionList = 2558;       // UpdateMissionListAck: the user key at offset 6, then missions
constexpr uint16_t kOpDialogList = 2180;        // SetQuestDialogAck: u8, u32 user, u32 NPC, [u32 n, n x u32 dialog]
// A step of ours finished (u32 mission key). Sent with the list's opcode; the partner's game finishes the same step
// through its quest server (quest_advance.h) if that step is under way there.
constexpr uint16_t kOpStepDone = kOpMissionList;
constexpr size_t kTriggerSize = 9, kDialogEndSize = 30, kStepDoneSize = 4;
constexpr uint8_t kVersion = 1, kFlagInWorld = 1;
constexpr uint8_t kUnderWay = 2, kDone = 5;
constexpr uint64_t kWaitForNpcMs = 60000;       // a dialog waits this long for our NPC that offers it
constexpr uint64_t kOursMs = 15000;             // a request we injected is not sent back for this long
constexpr uint64_t kResultMs = 15000;           // a step finishing this soon after an injection is "together"

struct Pending {
    uint16_t op;
    std::vector<uint8_t> payload;
    uint64_t until;
};
struct Injected {
    uint16_t op;
    std::vector<uint8_t> payload;  // as we injected it
    uint32_t mission;              // trigger: its mission; dialog: 0 (any step that finishes)
    uint64_t at;
};
struct Npc {
    uint32_t actor;
    uint64_t seen;
};

std::mutex g_m;
bool g_enabled = false, g_any_world = false;
bool g_connected = false, g_loading = false;
uint64_t g_loaded_at = 0;                             // when the last world load ended
std::string g_partner;
uint32_t g_user = 0;                                  // our user key, from our server's lists
std::unordered_map<uint32_t, Npc> g_npc_for_dialog;   // dialog -> our NPC offering it
std::unordered_map<uint32_t, uint8_t> g_state;        // our missions' last state (watched keys only)
std::deque<std::vector<uint8_t>> g_out;               // messages for the partner
std::deque<Pending> g_in;                             // the partner's requests, to inject into our server
std::deque<Injected> g_injected;
std::deque<std::pair<std::vector<uint8_t>, uint64_t>> g_sent;  // recently sent (op + payload): no repeats
uint64_t g_now = 0;
std::string g_toast, g_hud;
int g_steps_together = 0;

bool in_world() { return g_any_world || !worlds_active().empty(); }

std::string mission_label(uint32_t key) {
    const std::string n = party_quests_name(key);
    return n.empty() ? std::format("{:08X}", key) : "\"" + n + "\"";
}

bool injected_recently(uint16_t op, const uint8_t* p, size_t n) {
    for (const auto& i : g_injected) {
        if (i.op == op && i.payload.size() == n && std::memcmp(i.payload.data(), p, n) == 0 && g_now - i.at < kOursMs) {
            return true;
        }
    }
    return false;
}

// One of the partner's requests made ours: false while it cannot be yet (no user key, no NPC offering the dialog).
bool make_ours(Pending& r, uint32_t* mission) {
    *mission = 0;
    if (r.op == kOpMissionTrigger) {
        if (!g_user) return false;
        std::memcpy(r.payload.data() + 4, &g_user, 4);
        std::memcpy(mission, r.payload.data(), 4);
        return true;
    }
    uint32_t dialog;
    std::memcpy(&dialog, r.payload.data() + 4, 4);
    const auto it = g_npc_for_dialog.find(dialog);
    if (it == g_npc_for_dialog.end()) return false;
    std::memcpy(r.payload.data(), &it->second.actor, 4);
    return true;
}

void watch_mission_list(const uint8_t* packet, size_t size) {
    if (size < 10) return;
    uint32_t user;
    std::memcpy(&user, packet + 6, 4);
    if (user && user != g_user) {
        g_user = user;
        log::info("Live quests: our user key {:08X}", user);
    }
    // Records differ in length: walk the keys like party_quests.cpp (a named mission, a plausible state after it).
    const auto plausible = [](uint8_t s) { return s == 0 || s == 1 || s == kUnderWay || s == kDone || s == 0xFF; };
    for (size_t i = 10; i + 6 <= size; ++i) {
        uint32_t key;
        std::memcpy(&key, packet + i, 4);
        if (!plausible(packet[i + 4]) || !plausible(packet[i + 5]) || party_quests_name(key).empty()) continue;
        const uint8_t s = packet[i + 4];
        const auto old = g_state.find(key);
        if (old != g_state.end() && old->second == kUnderWay && s == kDone) {
            bool from_partner = false;
            for (const auto& inj : g_injected) {
                if (g_now - inj.at > kResultMs || (inj.mission && inj.mission != key)) continue;
                from_partner = true;
                ++g_steps_together;
                summary_note(SummaryStat::QuestStep);
                log::info("Live quests: {} finished here too (from {})", mission_label(key), g_partner);
                g_toast = "Co-op+\nQuest step done together with " + (g_partner.empty() ? "your friend" : g_partner) +
                          ": " + mission_label(key);
                break;
            }
            // Not while a save loads (its list is not steps finishing now).
            const bool settled = !g_loading && g_now - g_loaded_at > 10000;
            if (!from_partner && settled && g_connected && in_world() && g_out.size() < 64) {
                std::vector<uint8_t> m{kVersion, kFlagInWorld, static_cast<uint8_t>(kOpStepDone),
                                       static_cast<uint8_t>(kOpStepDone >> 8)};
                m.insert(m.end(), packet + i, packet + i + 4);
                g_out.push_back(std::move(m));
                log::info("Live quests: sending our finished step {}", mission_label(key));
            }
        }
        g_state[key] = s;
        i += 5;
    }
}

void watch_dialog_list(const uint8_t* packet, size_t size) {
    // u16 op | u8 | u16 len | u8 | u32 user | u32 NPC [| u32 n | n x u32 dialog]
    if (size < 14) return;
    uint32_t npc;
    std::memcpy(&npc, packet + 10, 4);
    std::erase_if(g_npc_for_dialog, [&](const auto& kv) { return kv.second.actor == npc; });
    if (size < 18) return;
    uint32_t n;
    std::memcpy(&n, packet + 14, 4);
    for (uint32_t k = 0; k < n && 18 + 4 * (k + 1) <= size; ++k) {
        uint32_t dialog;
        std::memcpy(&dialog, packet + 18 + 4 * k, 4);
        g_npc_for_dialog[dialog] = Npc{npc, g_now};
    }
}

}  // namespace

void quest_live_configure(bool enabled, bool any_world) {
    std::lock_guard lock(g_m);
    g_enabled = enabled;
    g_any_world = any_world;
    log::info("Live quests: {}{}", enabled ? "on (quest steps follow between players in a co-op world)" : "off",
              any_world ? " | [dev] outside co-op worlds too" : "");
}

void quest_live_observe_c2s(const uint8_t* packet, size_t size) {
    if (size < 5) return;
    uint16_t op;
    std::memcpy(&op, packet, 2);
    if (op != kOpMissionTrigger && op != kOpDialogEnd) return;
    const uint8_t* p = packet + 5;
    const size_t n = size - 5;
    if ((op == kOpMissionTrigger && n != kTriggerSize) || (op == kOpDialogEnd && n != kDialogEndSize)) return;
    std::lock_guard lock(g_m);
    if (!g_enabled || !g_connected || !in_world() || injected_recently(op, p, n)) return;
    std::vector<uint8_t> m{kVersion, kFlagInWorld, static_cast<uint8_t>(op), static_cast<uint8_t>(op >> 8)};
    m.insert(m.end(), p, p + n);
    for (const auto& [s, at] : g_sent) {
        if (s == m && g_now - at < 5000) return;  // the game repeats some requests
    }
    g_sent.emplace_back(m, g_now);
    while (g_sent.size() > 32) g_sent.pop_front();
    uint32_t key;
    std::memcpy(&key, p, 4);
    if (op == kOpMissionTrigger && key == 0) return;  // quest_advance's wake-up, not a step
    log::info("Live quests: sending our {} {}", op == kOpMissionTrigger ? "trigger for" : "dialog end, dialog",
              op == kOpMissionTrigger ? mission_label(key) : std::format("{:08X}", *reinterpret_cast<const uint32_t*>(p + 4)));
    if (g_out.size() < 64) g_out.push_back(std::move(m));
}

void quest_live_observe_s2c(const uint8_t* packet, size_t size) {
    if (size < 5) return;
    uint16_t op;
    std::memcpy(&op, packet, 2);
    if (op != kOpMissionList && op != kOpDialogList) return;
    std::lock_guard lock(g_m);
    if (!g_enabled) return;
    if (op == kOpMissionList) {
        watch_mission_list(packet, size);
    } else {
        watch_dialog_list(packet, size);
    }
}

void quest_live_tick(bool connected, const std::string& partner_name, uint64_t now_ms) {
    std::lock_guard lock(g_m);
    g_now = now_ms;
    g_connected = connected;
    if (connected) g_partner = partner_name;
    if (!g_enabled || !connected) {
        g_in.clear();
        g_hud.clear();
        return;
    }
    std::erase_if(g_injected, [&](const Injected& i) { return now_ms - i.at > kResultMs + kOursMs; });
    const bool loading = sniffer_world_loading();
    if (g_loading && !loading) g_loaded_at = now_ms;
    g_loading = loading;
    std::erase_if(g_npc_for_dialog, [&](const auto& kv) { return now_ms - kv.second.seen > 10 * 60000; });
    g_hud = g_steps_together ? std::format("Quest steps done together: {}", g_steps_together) : std::string();
    if (sniffer_world_loading() || !in_world()) return;
    for (auto it = g_in.begin(); it != g_in.end();) {
        uint32_t mission = 0;
        if (it->op == kOpStepDone) {
            // Only a step under way here: the routine would also start and finish one not started.
            std::memcpy(&mission, it->payload.data(), 4);
            const auto st = g_state.find(mission);
            if (st != g_state.end() && st->second == kDone) {
                it = g_in.erase(it);
            } else if (st != g_state.end() && st->second == kUnderWay && quest_advance_ready()) {
                quest_advance_request(mission);
                log::info("Live quests: {} finished {}; finishing it here", g_partner, mission_label(mission));
                g_injected.push_back(Injected{it->op, it->payload, mission, now_ms});
                it = g_in.erase(it);
            } else if (now_ms > it->until) {
                log::info("Live quests: {}'s finished step {} dropped: not under way here", g_partner,
                          mission_label(mission));
                it = g_in.erase(it);
            } else {
                ++it;  // maybe it starts here soon (the step before it is following too)
            }
            continue;
        }
        if (!make_ours(*it, &mission)) {
            if (now_ms > it->until) {
                uint32_t dialog;
                std::memcpy(&dialog, it->payload.data() + 4, 4);
                log::info("Live quests: {}'s dialog end (dialog {:08X}) dropped: no NPC near us offers it",
                          g_partner, dialog);
                it = g_in.erase(it);
            } else {
                ++it;
            }
            continue;
        }
        std::vector<uint8_t> pkt(5 + it->payload.size());
        std::memcpy(pkt.data(), &it->op, 2);
        const auto len = static_cast<uint16_t>(it->payload.size());
        std::memcpy(pkt.data() + 3, &len, 2);
        std::memcpy(pkt.data() + 5, it->payload.data(), it->payload.size());
        const bool ok = sniffer_inject_c2s(std::move(pkt));
        log::info("Live quests: {}'s {} into our game{}", g_partner,
                  it->op == kOpMissionTrigger ? "trigger for " + mission_label(mission) : std::string("dialog end"),
                  ok ? "" : ": NOT sent");
        if (ok) g_injected.push_back(Injected{it->op, it->payload, mission, now_ms});
        it = g_in.erase(it);
    }
}

void quest_live_on_message(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 4 || bytes[0] != kVersion) return;
    const uint16_t op = static_cast<uint16_t>(bytes[2] | (bytes[3] << 8));
    const size_t n = bytes.size() - 4;
    if (!((op == kOpMissionTrigger && n == kTriggerSize) || (op == kOpDialogEnd && n == kDialogEndSize) ||
          (op == kOpStepDone && n == kStepDoneSize))) {
        return;
    }
    std::lock_guard lock(g_m);
    if (!g_enabled) return;
    if (!(bytes[1] & kFlagInWorld) || !in_world()) {
        log::info("Live quests: a step from {} not taken (both players must be in a co-op world)", g_partner);
        return;
    }
    if (g_in.size() >= 64) return;
    g_in.push_back(Pending{op, std::vector<uint8_t>(bytes.begin() + 4, bytes.end()), g_now + kWaitForNpcMs});
}

bool quest_live_dev_any_world() {
    std::lock_guard lock(g_m);
    return g_enabled && g_any_world;
}

bool quest_live_enabled() {
    std::lock_guard lock(g_m);
    return g_enabled;
}

bool quest_live_can_advance(uint32_t mission) {
    std::lock_guard lock(g_m);
    if (!g_enabled || !g_connected || !in_world() || g_loading) return false;
    const auto state = g_state.find(mission);
    return state != g_state.end() && state->second == kUnderWay;
}

std::vector<std::vector<uint8_t>> quest_live_take_outgoing() {
    std::lock_guard lock(g_m);
    std::vector<std::vector<uint8_t>> out(g_out.begin(), g_out.end());
    g_out.clear();
    return out;
}

std::string quest_live_take_toast() {
    std::lock_guard lock(g_m);
    std::string t;
    t.swap(g_toast);
    return t;
}

std::string quest_live_hud_text() {
    std::lock_guard lock(g_m);
    return g_hud;
}

}  // namespace cdmp::net
