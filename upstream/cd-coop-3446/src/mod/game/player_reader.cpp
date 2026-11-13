#include "mod/game/player_reader.h"
#include "mod/log.h"
#include <string>
#include <format>
#include <atomic>

#include <windows.h>

#include <cmath>

#include "common/math3d.h"
#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"

namespace cdmp::game {
namespace off = build_3663;

namespace {

std::optional<proto::Vec3> read_vec3(uintptr_t addr) {
    float f[3];
    if (!safe_read(addr, f, sizeof f)) return std::nullopt;
    proto::Vec3 v{f[0], f[1], f[2]};
    if (!math::is_finite(v)) return std::nullopt;
    return v;
}

}  // namespace

bool PlayerReader::init() {
    base_ = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base_);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base_ + dos->e_lfanew);
    enabled_ = base_ == off::kImageBase && nt->FileHeader.TimeDateStamp == off::kPeTimestamp;
    if (!enabled_) last_failure_ = "unsupported game build";
    return enabled_;
}

std::nullopt_t PlayerReader::fail(const char* why) {
    last_failure_ = why;
    return std::nullopt;
}

std::optional<uintptr_t> PlayerReader::resolve_actor() {
    auto world = read_ptr(base_ + off::kWorldSystemSlot);
    if (!world) return fail("WorldSystem not created");
    auto manager = read_ptr(*world + off::kWorldSystem_ActorManager);
    if (!manager || !has_vtable(*manager, base_ + off::kVtClientActorManager))
        return fail("ClientActorManager missing");
    auto actor = read_ptr(*manager + off::kActorManager_ControlledActor);
    if (!actor) return fail("no controlled actor (not in world)");
    if (!has_vtable(*actor, base_ + off::kVtControlledActor))
        return fail("controlled actor has unexpected type");
    return actor;
}

uint32_t PlayerReader::controlled_key() {
    if (!enabled_) return 0;
    auto actor = resolve_actor();
    if (!actor) return 0;
    auto key = game::read<uint32_t>(*actor + off::kActor_Key);
    return key ? *key : 0;
}

std::optional<uintptr_t> PlayerReader::character_control() {
    if (!enabled_) return fail("unsupported game build");
    auto actor = resolve_actor();
    if (!actor) return std::nullopt;
    auto components = read_ptr(*actor + off::kActor_ComponentTable);
    if (!components) return fail("component table missing");
    auto control = read_ptr(*components + off::kComponents_CharacterControl);
    if (!control || !has_vtable(*control, base_ + off::kVtCharacterControl))
        return fail("CharacterControl component missing");
    return control;
}

std::optional<PlayerSnapshot> PlayerReader::read() {
    if (!enabled_) return fail("unsupported game build");

    auto actor = resolve_actor();
    if (!actor) return std::nullopt;

    auto components = read_ptr(*actor + off::kActor_ComponentTable);
    if (!components) return fail("component table missing");
    auto control = read_ptr(*components + off::kComponents_CharacterControl);
    if (!control || !has_vtable(*control, base_ + off::kVtCharacterControl))
        return fail("CharacterControl component missing");
    auto inner = read_ptr(*control + off::kControl_Inner);
    if (!inner) return fail("control inner missing");
    auto physics = read_ptr(*inner + off::kControlInner_Physics);
    if (!physics) return fail("physics object missing");
    // The vtable RVA is a code address this port could not re-derive offline
    // (kVtPhysicsObject == 0 means "unknown"), so it is only enforced when known.
    // The orthonormal-basis and static-position checks below are the real proof.
    if (off::kVtPhysicsObject != 0 && !has_vtable(*physics, base_ + off::kVtPhysicsObject)) {
        return fail("physics object missing");
    }

    auto local = read_vec3(*physics + off::kPhysics_Position);
    auto origin = read_vec3(base_ + off::kChunkOrigin);
    auto ax = read_vec3(*physics + off::kPhysics_AxisX);
    auto ay = read_vec3(*physics + off::kPhysics_AxisY);
    auto az = read_vec3(*physics + off::kPhysics_AxisZ);
    if (!local || !origin || !ax || !ay || !az) return fail("physics data unreadable");

    // The basis is a SEPARATE field from the position, and a bad basis must not
    // cost us the position.
    //
    // This used to return early on a non-orthonormal basis, which threw away a
    // perfectly good position read in the same call. In the first two-PC test the
    // host's log alternated "in world" and "physics basis not a rotation", and
    // every failure dropped that sample: the position the guest received stalled
    // on one value for tens of seconds at a time and jumped 10-20 m when it did
    // move ("Local: pos (-10606.81, 620.70, -3826.01) | check vs game global
    // 0.05 m" -- the read was correct to 5 cm and frozen). The guest's panel still
    // showed a distance, because the last good sample stayed on screen, and the
    // character was drawn nowhere useful.
    //
    // The tolerance is also widened to match camera_reader.cpp, which already
    // uses 0.05f for the same kind of game-provided basis: 0.02f is tight for
    // physics axes that have been through the game's own float math.
    const bool basis_ok = math::is_orthonormal(*ax, *ay, *az, 0.05f);

    PlayerSnapshot snap;
    snap.position = math::add(*local, *origin);
    snap.local_position = *local;
    snap.chunk_origin = *origin;
    // Without a usable basis we keep the position and leave the rotation at the
    // identity: the other player is then drawn unrotated rather than not drawn.
    snap.rotation = basis_ok ? math::quat_from_axes(*ax, *ay, *az) : math::Quat{};

    // The game keeps its own world-space copy; a large mismatch means our
    // chunk-origin math or chain is off (it lags by a frame, so allow some).
    if (auto global = read_vec3(base_ + off::kStaticPlayerPos)) {
        snap.static_pos_error = math::length(math::sub(snap.position, *global));
    }

    // While a save loads, the game parks the actor at (0, 1000, 0) for tens
    // of seconds before placing it. Don't broadcast that as a real position.
    if (std::fabs(snap.position.x) < off::kLoadingParkRadius &&
        std::fabs(snap.position.z) < off::kLoadingParkRadius &&
        std::fabs(snap.position.y - off::kLoadingParkHeight) < off::kLoadingParkRadius) {
        return fail("loading (actor parked at 0,1000,0)");
    }

    read_stats(*components, snap);
    last_failure_.clear();
    return snap;
}

std::atomic<bool> g_stat_log{false};
void set_stat_log(bool on) { g_stat_log = on; }

void PlayerReader::read_stats(uintptr_t components, PlayerSnapshot& out) {
    auto status = read_ptr(components + off::kComponents_Status);
    if (!status || !has_vtable(*status, base_ + off::kVtStatusComponent)) return;
    auto container = read_ptr(*status + off::kStatus_StatContainer);
    if (!container) return;
    auto entries = read_ptr(*container + off::kStatContainer_Entries);
    auto count = game::read<uint32_t>(*container + off::kStatContainer_Count);
    if (!entries || !count || *count > off::kMaxStatEntries) return;

    // [dev] stat_log: every stat of ours once a second (finding a stat's id, e.g. Spirit).
    if (g_stat_log.load(std::memory_order_relaxed)) {
        static ULONGLONG last = 0;
        if (const ULONGLONG now = GetTickCount64(); now - last >= 1000) {
            last = now;
            std::string line;
            for (uint32_t i = 0; i < *count && i < 64; ++i) {
                const uintptr_t e = *entries + static_cast<uintptr_t>(i) * off::kStatEntry_Size;
                auto id = game::read<uint64_t>(e + off::kStatEntry_Id);
                auto cur = game::read<int64_t>(e + off::kStatEntry_Current);
                auto max = game::read<int64_t>(e + off::kStatEntry_Max);
                if (id && cur && max) line += std::format(" {:X}={}/{}", *id, *cur, *max);
            }
            log::info("Stats:{}", line);
        }
    }
    // Look stats up by id: the entry order changes between patches.
    for (uint32_t i = 0; i < *count; ++i) {
        const uintptr_t e = *entries + static_cast<uintptr_t>(i) * off::kStatEntry_Size;
        auto id = game::read<uint64_t>(e + off::kStatEntry_Id);
        if (!id) continue;
        float* cur_out = nullptr;
        float* max_out = nullptr;
        if (*id == off::kStatId_Health) {
            cur_out = &out.health;
            max_out = &out.health_max;
        } else if (*id == off::kStatId_Stamina) {
            cur_out = &out.stamina;
            max_out = &out.stamina_max;
        } else {
            continue;
        }
        auto cur = game::read<int64_t>(e + off::kStatEntry_Current);
        auto max = game::read<int64_t>(e + off::kStatEntry_Max);
        if (!cur || !max || *max <= 0 || *cur < 0 || *cur > *max) continue;
        *cur_out = static_cast<float>(*cur) / 1000.0f;
        *max_out = static_cast<float>(*max) / 1000.0f;
    }
}

}  // namespace cdmp::game
