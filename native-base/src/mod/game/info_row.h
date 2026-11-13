#pragma once
// 1.0.0.3663: the game's info-table getters (item, field, ... : 127 copies of one template) take a POINTER to the u16
// row index in rcx and answer a default record, never null, when the index is out of range. Up to 1.0.0.3576 they
// took the index itself in edx. One place for the call, so that the next change of this kind is one edit.
#include <cstdint>

namespace cdmp::game {

inline uintptr_t info_row(uintptr_t getter, uint32_t index) noexcept {
    const uint16_t i = static_cast<uint16_t>(index);
    return reinterpret_cast<uintptr_t (__fastcall*)(const uint16_t*)>(getter)(&i);
}

}  // namespace cdmp::game
