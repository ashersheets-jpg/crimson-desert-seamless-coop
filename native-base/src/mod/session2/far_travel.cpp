// Co-op+ 2026-11-12, far travel ("Go to my friend" over kilometres): the one thing the layer cannot find out by itself.
//
// Testers: "teleporting to another player was ... very slow and buggy like being slowly dragged through the map": the
// layer moved a player with DoTeleportAck in 40 m hops (the client refuses farther targets: no navigation data there).
// The game's own far move is a PENDING RELOAD (local notes build/decomp/notes_far_teleport.md, game build 3576): the
// traveller's client sends ReloadStageStartReq 3074 with a free position (u32 0 | u32 FieldInfoKey | u32 0 | vec3 |
// quat | vec3 scale | u8 1: no waypoint, unlock or distance test with actor 0), the server stores the destination, and
// the client's loading machine (started by TeleportRevivePointAck 3071) asks for the load itself. The layer makes both
// packets on the traveller's own PC. What it needs from here is the FieldInfoKey of the field the players are in:
// the game's getter 0x555DE0(TS, u16 [TS+0x28]) -> [+8], asked for the HOST's character on the host's server.
//
// cdmp_field_info_key(): any thread of the layer; the answer is kept for 2 s. 0 = not known (not the host, no
// character in the world yet, another game build, or the getter faulted).
#include <windows.h>
#include <atomic>
#include <cstdint>
#include "mod/game/info_row.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

constexpr uintptr_t kFieldInfoGetter = 0x57B290;  // 3663: (const u16* index) -> record; FieldInfoKey = u32 [record+8]
constexpr uintptr_t kUserMainCharacter = 0xD0, kActorComponents = 0x68, kComponentsTransformSync = 0x1A0,
                    kTsFieldIndex = 0x28, kRecordFieldInfoKey = 0x08;

std::atomic<uint32_t> g_key{0};
std::atomic<uint64_t> g_asked_at{0};
std::atomic<uint32_t> c_asked{0}, c_faulted{0};
std::atomic<bool> g_said{false};

uint32_t ask(uintptr_t ts, uint16_t index) noexcept {
    __try {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        // 3663: the getter takes a pointer to the index and never answers null (game/info_row.h)
        (void)ts;
        const uintptr_t rec = game::info_row(base + kFieldInfoGetter, index);
        if (!rec) return 0;
        return *reinterpret_cast<uint32_t*>(rec + kRecordFieldInfoKey);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        c_faulted.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
}

uint32_t read_key() noexcept {
    uintptr_t user = 0, character = 0, comps = 0, ts = 0;
    uint16_t index = 0;
    if (!server_host_user(user) || !user) return 0;
    if (!game::fast_read(user + kUserMainCharacter, &character, sizeof character) || !character ||
        !game::fast_read(character + kActorComponents, &comps, sizeof comps) || !comps ||
        !game::fast_read(comps + kComponentsTransformSync, &ts, sizeof ts) || !ts ||
        !game::fast_read(ts + kTsFieldIndex, &index, sizeof index))
        return 0;
    c_asked.fetch_add(1, std::memory_order_relaxed);
    return ask(ts, index);
}

}  // namespace
}  // namespace cdmp::session2

extern "C" __declspec(dllexport) uint32_t cdmp_field_info_key() {
    using namespace cdmp::session2;
    if (!server_thread_id()) return 0;  // not the host (no server built here): the guest asks its host (nb5: a guarded
                                        // read faulted on the guest at every call)
    const uint64_t now = GetTickCount64();
    const uint64_t at = g_asked_at.load(std::memory_order_relaxed);
    if (at && now - at < 2000) return g_key.load(std::memory_order_relaxed);
    g_asked_at.store(now ? now : 1, std::memory_order_relaxed);
    const uint32_t key = read_key();
    const uint32_t before = g_key.exchange(key, std::memory_order_relaxed);
    if (key != before && (key || g_said.load())) {
        g_said.store(true);
        cdmp::log::info("FarTravel: the host's field is 0x{:08X} now (was 0x{:08X}) | asked {} time(s), faulted {}", key,
                        before, c_asked.load(), c_faulted.load());
    }
    return key;
}
