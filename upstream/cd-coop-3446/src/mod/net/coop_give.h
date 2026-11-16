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
#include <string>
#include <vector>

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
// The answer: 0 cancel, 1 give, 2 discard, 3 put in the shared stash (guest), 4 offer it in a trade. Done on the UI
// thread by give_ui_tick.
void give_answer(int code);
// What the held-back discard's card offers: the friend is near (Give), we are the guest (the shared stash, B12).
bool give_offer_give();
bool give_offer_stash();

// UI thread, every frame of the pause menu (game/main_menu's update hook).
void give_ui_tick();

// B11 trade (2026-11-16): each player offers one stack ("Offer in a trade" on the same question), both see "Accept:
// your <item> for their <item>", and when both accepted each game gives its own offer to the other (the same give
// request, sent from the network loop; the offer was recorded by a dry run of the game's discard, nothing sent then).
// Activity type 31: u8 op | 1 offer: u16 item index | u64 count | u8 name length | name; 2 accept; 3 cancel.
struct TradeTick {
    std::vector<std::vector<uint8_t>> send;  // Activity type 31 payloads to send
    std::vector<std::string> toasts;         // "{}" = the friend's name (the names' own braces doubled)
    bool card = false;                       // show "Accept / Decline" now
    std::string mine, theirs;                // what the card names
};
TradeTick trade_tick(bool connected);
void trade_on_event(const uint8_t* data, size_t n);
void trade_answer(bool accept);

}  // namespace cdmp::net
