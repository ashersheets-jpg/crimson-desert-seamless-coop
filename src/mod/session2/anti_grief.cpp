// Co-op+ (2026-11-09, build 1.0.0.3576): anti_grief.h.
#include "mod/session2/anti_grief.h"

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cstring>
#include <format>
#include <mutex>
#include <unordered_set>

#include <safetyhook.hpp>

#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/anti_grief_logic.h"

namespace cdmp::session2 {
namespace {

bool g_on = false;
std::mutex g_mu;
std::unordered_set<uint32_t> g_protected;  // guarded by g_mu
std::unordered_set<uint32_t> g_dialog, g_fought;  // NPCs with a dialog list / NPCs the host has hit (g_mu)
std::atomic<uint64_t> c_marked{0};
std::atomic<uint64_t> c_dropped{0}, c_hits_seen{0}, c_stripped{0}, c_freed{0};

// ---- crimes (tcB355 / tcB356: "Crime: Assault" / "Crime: Murder" on the guest for the host's quest bandits;
// build/decomp/notes_crime_coop.md, local). Every crime is registered by exe+0x2BCE6A0(wanted comp, criminal, victim,
// float3*, float3*, u8 type, i64 time, int, u8); comp+8 = the owning player. A hit comes from .tls$ 0xE3C54D0 (return
// address exe+0xE3C55B2), a kill from 0x2BCDDC0 (return address exe+0x2BCDE79); both skip when the attacker and the
// victim have the same user (actor+0xA0). tcB358: the world's NPCs have NO user (owner key 0x90100000), the HOST's
// hits and kills reach this function too, and the game's own test exe+0x2BCE2A0(comp, player, victim, float3*, u8 type,
// i64 time) then says "no crime" for the host (its battle state, its faction relation, its quest targets) but "crime"
// for the guest. So the guest is judged as the host would be: its hit / kill on an NPC is no crime when the host has
// fought that NPC or when the game's test says the HOST would not be punished for it. What an NPC's own AI reports
// (slot 44) is not touched.
constexpr uint32_t kHostCharacter = 0xA0100001;
constexpr uintptr_t kCrimeFn = 0x2BCE6A0, kCrimeGateFn = 0x2BCE2A0, kCrimeRetHit = 0xE3C55B2, kCrimeRetKill = 0x2BCDE79;
safetyhook::InlineHook g_crime;
bool g_crime_gate_ok = false;
std::atomic<uint64_t> c_crime_seen{0}, c_crime_skipped{0};

struct CrimeSeen {
    uint32_t owner = 0, criminal = 0, victim = 0;
    uintptr_t owner_user = 0, victim_user = 0;
    uint32_t victim_owner_key = 0;
    uint8_t battle = 0xFF;
    bool host_targeted = false;
    uint32_t targets = 0;
};
void read_crime(CrimeSeen& s, uintptr_t comp, uintptr_t criminal, uintptr_t victim) noexcept {
    __try {
        const uintptr_t owner = *reinterpret_cast<uintptr_t*>(comp + 8);
        if (owner) {
            s.owner = *reinterpret_cast<uint32_t*>(owner + 0x60);
            s.owner_user = *reinterpret_cast<uintptr_t*>(owner + 0xA0);
        }
        if (criminal) s.criminal = *reinterpret_cast<uint32_t*>(criminal + 0x60);
        if (!victim) return;
        s.victim = *reinterpret_cast<uint32_t*>(victim + 0x60);
        s.victim_owner_key = *reinterpret_cast<uint32_t*>(victim + 0x90);
        s.victim_user = *reinterpret_cast<uintptr_t*>(victim + 0xA0);
        const uintptr_t comps = *reinterpret_cast<uintptr_t*>(victim + 0x68);
        const uintptr_t ct = comps ? *reinterpret_cast<uintptr_t*>(comps + 0x90) : 0;  // CommonTarget
        if (!ct) return;
        s.battle = *reinterpret_cast<uint8_t*>(ct + 0x164);
        for (int i = 0; i < 3; ++i) {  // list 3 is not "in battle"
            const uintptr_t p = *reinterpret_cast<uintptr_t*>(ct + 0x98 + 0x10 * i);
            const uint32_t n = *reinterpret_cast<uint32_t*>(ct + 0xA0 + 0x10 * i);
            if (!p || n > 64) continue;
            s.targets += n;
            for (uint32_t k = 0; k < n; ++k) {
                if (*reinterpret_cast<uint32_t*>(p + 8ull * k) == kHostCharacter) s.host_targeted = true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
// Would the HOST be punished for this victim? The game's own test with the host's wanted component
// ([[host+0x68]+0x120]) and the host's character (the server's scoped actor lookup). 1 = crime, 0 = no crime,
// -1 = the host's character is not there.
int host_would_be_punished(uintptr_t victim, void* at, uint8_t type, int64_t time) noexcept {
    int r = -1;
    __try {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const uintptr_t mgr = *reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(base + 0x71C0610) + 0x50);
        uint64_t scope[8] = {};
        reinterpret_cast<void* (__fastcall*)(uintptr_t, uint64_t*, uint64_t)>(base + 0x2D11880)(mgr, scope,
                                                                                               kHostCharacter);
        const uintptr_t host = scope[1];
        if ((scope[2] & 0xFF) != 0 && host) {
            const uintptr_t comps = *reinterpret_cast<uintptr_t*>(host + 0x68);
            const uintptr_t wanted = comps ? *reinterpret_cast<uintptr_t*>(comps + 0x120) : 0;
            if (wanted && *reinterpret_cast<uintptr_t*>(wanted + 8) == host) {
                const uint64_t v = reinterpret_cast<uint64_t (__fastcall*)(uintptr_t, uintptr_t, uintptr_t, void*, uint8_t,
                                                                         int64_t)>(base + kCrimeGateFn)(
                    wanted, host, victim, at, type, time);
                r = (v & 0xFF) != 0 ? 1 : 0;
            }
        }
        reinterpret_cast<void (__fastcall*)(uint64_t*)>(base + 0x1618660)(scope);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r = -1;
    }
    return r;
}
bool host_fought(uint32_t npc) noexcept {
    try {
        std::lock_guard lk(g_mu);
        return g_fought.count(npc) != 0;
    } catch (...) {
        return false;
    }
}

void __fastcall crime_detour(uintptr_t comp, uintptr_t criminal, uintptr_t victim, void* at, void* dir, uint8_t type,
                             int64_t time, int a8, uint8_t a9) noexcept {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress()) - base;
    CrimeSeen s;
    read_crime(s, comp, criminal, victim);
    const bool guest = (s.owner >> 28) == 0xA && s.owner != kHostCharacter;
    bool skip = false;
    const char* why = "left to the game's own test";
    if (guest && victim && (s.victim >> 28) == 0xB && (ret == kCrimeRetHit || ret == kCrimeRetKill)) {
        const int host = g_crime_gate_ok ? host_would_be_punished(victim, at, type, time) : -1;
        if (host_fought(s.victim)) {
            skip = true;
            why = "skipped: the host has fought this NPC";
        } else if (host == 0) {
            skip = true;
            why = "skipped: the host would not be punished for this NPC either";
        } else if (host < 0 && s.battle == 1) {
            skip = true;
            why = "skipped: the NPC is in a fight (the host's character was not found)";
        } else if (host == 1) {
            why = "the host would be punished too: left to the game's own test";
        }
        if (skip && !g_on) {
            skip = false;
            why = "would be skipped (anti_grief off: registered)";
        }
    }
    if (guest || s.owner == kHostCharacter) {
        const uint64_t n = c_crime_seen.fetch_add(1) + 1;
        if (skip) c_crime_skipped.fetch_add(1);
        if (n <= 60 || n % 100 == 0)
            log::info("AntiGrief: crime type {} of {} 0x{:08X} (attacker 0x{:08X}) on 0x{:08X} from exe+0x{:X} ({}) | "
                      "victim owner key 0x{:08X} user {} | battle {} targets {} host among them {} | {}", type,
                      guest ? "the guest" : "the host", s.owner, s.criminal, s.victim, ret,
                      ret == kCrimeRetHit ? "a hit" : ret == kCrimeRetKill ? "a kill" : "another path",
                      s.victim_owner_key,
                      !s.victim_user ? "none" : s.victim_user == s.owner_user ? "the attacker's own" : "another",
                      s.battle, s.targets, s.host_targeted ? 1 : 0, why);
    }
    if (skip) return;
    g_crime.call<void>(comp, criminal, victim, at, dir, type, time, a8, a9);
}

}  // namespace

void anti_grief_start(bool on, bool guest) {
    g_on = on && !guest;
    log::info("AntiGrief {} (anti_grief={}{}): {}", g_on ? "ON" : "off", on ? 1 : 0, guest ? ", guest" : "",
              g_on ? "the guest's hits on the NPCs the host has talked to or traded with never reach the host's server"
                   : "the guest can hit any NPC");
    if (guest) return;
    // The crime register function is hooked on the host even with anti_grief off (then log only).
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    static const uint8_t kHead[16] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24,
                                      0x18, 0x4C};
    uint8_t got[16] = {};
    if (game::safe_read(base + kCrimeFn, got, sizeof got) && std::memcmp(got, kHead, sizeof got) == 0) {
        if (auto h = safetyhook::InlineHook::create(reinterpret_cast<void*>(base + kCrimeFn), crime_detour)) {
            g_crime = std::move(*h);
        }
    }
    static const uint8_t kGate[8] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C};
    g_crime_gate_ok = game::safe_read(base + kCrimeGateFn, got, 8) && std::memcmp(got, kGate, 8) == 0;
    log::info("AntiGrief: the crime register function {}{}", g_crime_gate_ok ? "" : "(its test NOT recognised) ",
              !g_crime ? "NOT hooked"
              : g_on ? "hooked: the guest's hits and kills on NPCs the host fights are no crime" : "hooked (log only)");
}

void anti_grief_host_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept {
    if (!g_on || size < 5 + 9) return;
    // Playtest 2026-11-10 (Borque: "enemies seem to not react to guest attacks"): some hostile NPCs have a dialog list
    // too (quest fights). An NPC the HOST attacks is fair game for both from then on.
    if (!server_to_client) {
        const uint16_t op = static_cast<uint16_t>(packet[0] | (packet[1] << 8));
        // tcB355 (2026-11-11): bandits get a dialog list too (their battle lines), so "has a dialog list" protected
        // enemies from the guest until the host hit each one. Now an NPC is protected only once the HOST has
        // interacted with it (InteractionDoStepReq 3073: u32 requester | u32 target | ..., a talk or a shop) AND it has
        // a dialog list; an NPC the host has fought is never protected again.
        if (op == 3073 && size >= 5 + 8) {
            try {
                uint32_t target = 0;
                std::memcpy(&target, packet + 5 + 4, 4);
                std::lock_guard lk(g_mu);
                if (g_dialog.count(target) && !g_fought.count(target) && g_protected.insert(target).second) {
                    const uint64_t n = c_marked.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (n <= 20) log::info("AntiGrief: NPC 0x{:08X} is protected: the host interacts with it", target);
                }
            } catch (...) {
            }
            return;
        }
        if (op != agl::kOpDoFrameEventReq) return;
        // Only the host's own CHARACTER counts as "the host fights it" (the attacker key is payload + 1). tcB413: with
        // the host 100 m away, every bandit the guest hit was marked as fought by the host: this tap also carries
        // frame events whose attacker is somebody else.
        if (size < 5 + 5 || agl::rd32(packet + 5 + 1) != kHostCharacter) return;
        try {
            const auto t = agl::targets(std::span<const uint8_t>(packet + 5, size - 5));
            std::lock_guard lk(g_mu);
            for (const uint32_t k : t) {
                if ((k >> 28) == 0xB) g_fought.insert(k);
                if (g_protected.erase(k)) {
                    const uint64_t n = c_freed.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (n <= 20) log::info("AntiGrief: NPC 0x{:08X} is no longer protected: the host fights it", k);
                }
            }
        } catch (...) {
        }
        return;
    }
    if (static_cast<uint16_t>(packet[0] | (packet[1] << 8)) != agl::kOpQuestDialogAck) return;
    const uint32_t npc = agl::dialog_npc(std::span<const uint8_t>(packet + 5, size - 5));
    if (!npc) return;
    try {
        std::lock_guard lk(g_mu);
        g_dialog.insert(npc);  // protected only once the host interacts with it (above)
    } catch (...) {
    }
}

std::vector<uint8_t> anti_grief_strip_user2_req(uint16_t opcode, std::span<const uint8_t> payload) noexcept {
    if (!g_on || opcode != agl::kOpDoFrameEventReq) return {};
    try {
        std::lock_guard lk(g_mu);
        const auto t = agl::targets(payload);
        size_t prot = 0;
        for (const uint32_t k : t) prot += g_protected.count(k) ? 1 : 0;
        if (!prot || prot == t.size()) return {};  // nothing to remove, or all: dropped whole
        size_t gone = 0;
        auto out = agl::without_targets(payload, [](uint32_t k) { return g_protected.count(k) != 0; }, &gone);
        const uint64_t n = c_stripped.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 10 || n % 50 == 0)
            log::info("AntiGrief: the guest's hit kept {} target(s), {} person(s) of the host's world taken out ({} so far)",
                      t.size() - gone, gone, n);
        return out;
    } catch (...) {
    }
    return {};
}

bool anti_grief_drop_user2_req(uint16_t opcode, std::span<const uint8_t> payload) noexcept {
    if (!g_on || opcode != agl::kOpDoFrameEventReq) return false;
    try {
        c_hits_seen.fetch_add(1, std::memory_order_relaxed);
        const auto t = agl::targets(payload);
        std::lock_guard lk(g_mu);
        size_t prot = 0;
        for (const uint32_t k : t) prot += g_protected.count(k) ? 1 : 0;
        if (prot && prot < t.size()) return false;  // anti_grief_strip_user2_req keeps the others
        for (const uint32_t k : t) {
            if (g_protected.count(k)) {
                const uint64_t n = c_dropped.fetch_add(1, std::memory_order_relaxed) + 1;
                if (n <= 10 || n % 50 == 0)
                    log::info("AntiGrief: the guest's hit on NPC 0x{:08X} (a person of the host's world) dropped ({} so "
                              "far)", k, n);
                return true;
            }
        }
    } catch (...) {
    }
    return false;
}

std::string anti_grief_describe() {
    std::lock_guard lk(g_mu);
    return std::format("AntiGrief: {} | protected NPCs {} | the guest's hits seen {} dropped {} trimmed {} | freed by the host's hits {} | crimes seen {} skipped for the guest {}",
                       g_on ? "on" : "off", g_protected.size(), c_hits_seen.load(), c_dropped.load(), c_stripped.load(),
                       c_freed.load(), c_crime_seen.load(), c_crime_skipped.load());
}

}  // namespace cdmp::session2
