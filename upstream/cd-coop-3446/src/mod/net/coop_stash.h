#pragma once
// Co-op+ (B12): the shared stash. The HOST character's own warehouse is the stash: the host uses the game's
// warehouse screen as always; the guest sees it in the Co-op tab ("Shared stash") and takes items out of it from
// anywhere, and puts items in from its inventory (Discard -> "Put in the shared stash", net/coop_give).
//
// The host's game moves the items itself (the native's 3111 entries 0xC0DE0002 deposit / 0xC0DE0003 withdraw run the
// game's own cross-actor move exe+0x2DF04A0 between the guest's bag and the host character's WareHouse, so an item
// keeps its whole record; build/decomp/notes_shared_stash.md, local). This module only carries the list: the guest
// asks (activity kStash, op 1), the host reads its own client's WareHouse and answers in pages (op 2).
//
// Build 3576: the client holder = *(*(actor+0x68)+0xB8); its inventories at holder+0x18 (count u32 +0x20), each with
// slots at +0 (200 bytes each: item index u16 +8, sub u16 +0xA, count i64 +0x10), slot count i16 +8, index i16 +0x10;
// the WareHouse index at the static inventory manager +0xB2, its key = exe+0x5560E0(_, index)+8.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cdmp::net {

struct StashItem {
    uint16_t slot = 0, idx = 0xFFFF;
    uint32_t count = 0;
};

// Network loop (coop_activity's tick): the host answers asks; the guest asks while its stash page is open.
void stash_tick(bool connected, bool host, uint32_t now);
// Activity kStash payload (after the type byte). Records only (called under coop_activity's lock).
void stash_on_event(const uint8_t* p, size_t n);

// Guest, the Co-op tab's draw: the page is on screen (asks the host for the list every few seconds while it is).
void stash_page_shown();
// Guest: the last list (empty and known=false before the host's first answer).
std::vector<StashItem> stash_list(bool& known, uint32_t& age_ms);
// Guest: take that stack out of the stash (the whole stack). False when it could not be sent.
bool stash_withdraw(const StashItem& it);

// The display name of an item index ("" when unknown; the caller shows a fallback).
std::string stash_item_name(uint16_t idx);

}  // namespace cdmp::net
