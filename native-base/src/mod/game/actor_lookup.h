#pragma once
// Finds an actor by its key in the client's actor manager and reads its
// rendered world position. Read-only, every hop guarded; returns nullopt
// whenever anything is missing (loading screens, actor not loaded yet).
// Recipe: docs/research/movimiento-remoto-cliente.md §7 (build 1.0.0.2976).

#include <cstdint>
#include <optional>

#include "common/protocol.h"

namespace cdmp::game {

std::optional<uintptr_t> find_actor(uint32_t key);
// The same lookup with fast reads (SEH-guarded, no VirtualQuery): for loops over many keys on a busy thread.
// Co-op+ 2026-11-24: find_actor's ~12 VirtualQuery-checked reads per key cost the host's network loop seconds
// while the game streams a town (Borque's two-PC log; LESSONS "A VirtualQuery in a game-thread hook").
std::optional<uintptr_t> find_actor_fast(uint32_t key) noexcept;

// World position of the actor's TransformSync (what is drawn), Y up.
std::optional<proto::Vec3> actor_world_position(uint32_t key);
// Its facing (TransformSync quaternion x, y, z, w); nullopt unless unit length.
std::optional<proto::Quat> actor_world_rotation(uint32_t key);

}  // namespace cdmp::game
