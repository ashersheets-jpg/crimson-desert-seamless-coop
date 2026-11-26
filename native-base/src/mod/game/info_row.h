#pragma once
// The game's info-table getters (item, field, inventory, gimmick, ...: copies of one template per record type).
//   1.0.0.3840: `row* getter(table*, u32 index)`; the CALLER loads the table object from a global of its record type
//               and the getter answers NULL for an index out of range or a row it cannot load (the game's callers then
//               put a default record in its place). The records lost their 8-byte name pointer at the head (item key
//               +8 -> +0, gimmick key +0x10 -> +8, field info key +8 -> +0); the names are in the table's entries now.
//   1.0.0.3663 .. 3732: a POINTER to the u16 row index in rcx, the table read inside, a default record, never null.
//   up to 1.0.0.3576: the index itself in edx.
// One place for the call, so that the next change of this kind is one edit. Every caller is inside __try.
#include <cstdint>

namespace cdmp::game {

// getter = base + the getter's RVA; table_slot = base + the RVA of the global holding the table object. 0 = no row.
inline uintptr_t info_row(uintptr_t getter, uintptr_t table_slot, uint32_t index) noexcept {
    const uintptr_t table = *reinterpret_cast<const uintptr_t*>(table_slot);
    if (!table) return 0;
    return reinterpret_cast<uintptr_t (__fastcall*)(uintptr_t, uint32_t)>(getter)(table, static_cast<uint16_t>(index));
}

// 1.0.0.3840: a record's name object (the old `*(row + 0)` / `*(row + 8)`; its first field is the text), read the way
// the game inlines it: entry = *(table + 0x18) + index * 0x30, name = *(entry + 8), only while index < *(u32*)(table
// + 0x20). 0 = none.
inline uintptr_t info_name(uintptr_t table_slot, uint32_t index) noexcept {
    const uintptr_t table = *reinterpret_cast<const uintptr_t*>(table_slot);
    if (!table) return 0;
    const uint16_t i = static_cast<uint16_t>(index);
    if (i >= *reinterpret_cast<const uint32_t*>(table + 0x20)) return 0;
    const uintptr_t entries = *reinterpret_cast<const uintptr_t*>(table + 0x18);
    if (!entries) return 0;
    return *reinterpret_cast<const uintptr_t*>(entries + uintptr_t(i) * 0x30 + 8);
}

}  // namespace cdmp::game
