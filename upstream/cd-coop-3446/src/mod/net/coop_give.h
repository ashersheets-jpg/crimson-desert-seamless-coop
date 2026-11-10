#pragma once
// Co-op+ (B11): giving an item to the friend from the game's own inventory.
//
// The inventory's Discard on an item, with the friend standing close, asks first: "Give to <name>", "Discard" or
// "Cancel" (the player interaction menu's card, net/coop_activity). Give sends the item to the friend through the
// native's give route (MoveItemInventoryToOtherInventoryReq with the give entry: the host's game discards from the
// giver and adds to the receiver, PROGRESS.md tcB293). Discard runs the game's own discard as if we were not there.
//
// Build 3576 (build/decomp/notes_inventory_menu.md): the two discard handlers of
// UIGamePlayControlRootInventoryEquipment, exe+0xEF6560 (bag) and exe+0xEF68F0 (the second list), both
// (root, i64 count), and ClientDiscard exe+0xAC9210(holder, int* err, actor, u16 inventory, i16 slot, i64 count).

#include <cstdint>

namespace cdmp::net {

// Hooks the three functions once (refused when their first bytes are not this build's). True when hooked.
bool give_install();
// [coop] item_sharing: the give / stash card and the stash page only when on (set at start-up).
void give_set_enabled(bool on);
bool give_enabled();

// Network loop: are we linked, are we the host, is the friend within reach.
void give_set_state(bool linked, bool host, bool friend_near);

// Network loop: true once when a discard was held back and the question has to be shown.
bool give_take_question();
// The answer: 0 cancel, 1 give, 2 discard, 3 put in the shared stash (guest). Done on the UI thread by give_ui_tick.
void give_answer(int code);
// What the held-back discard's card offers: the friend is near (Give), we are the guest (the shared stash, B12).
bool give_offer_give();
bool give_offer_stash();

// UI thread, every frame of the pause menu (game/main_menu's update hook).
void give_ui_tick();

}  // namespace cdmp::net
