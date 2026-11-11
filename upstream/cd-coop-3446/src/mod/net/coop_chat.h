#pragma once
// Co-op+ (roadmap T24): text chat and quick messages.
//
// Keyboard: the Chat action (Shift+F2) opens a line at the bottom ("Say: ..."); while it is open every key goes to
// it (the keyboard hook keeps them from the game): Enter sends, Esc cancels, Backspace deletes. Keyboard and pad: the
// Ping action (Ctrl+F3, pad View + D-pad Left) opens the quick list -- Go here, Danger, Loot here, Follow me, Wait,
// Help!, Thanks, Ready? -- MenuPrev / MenuNext choose (arrows, D-pad Left / Right), the Ping action again sends (the
// three "here" ones also put a pin on the other player's map, as Ctrl+F3 did), it closes by itself after 8 s.
// The other player sees the message as a notice and in a short chat log on the left (the last 4 lines, 15 s).
// Activity type 22: u8 kind (0 text, 1.. quick message) | text (UTF-8, at most kMaxText bytes).

#include <cstdint>
#include <string>
#include <vector>

namespace cdmp::net {

struct ChatView {
    std::string input;              // the open line ("" = closed); ends with a cursor
    std::string quick;              // the open quick list ("" = closed)
    std::vector<std::string> log;   // recent lines, oldest first
    std::string toast;              // a notice for a message that just came in
    bool ping = false;              // the chosen quick message is a "here" one: drop a map pin at our position now
};

// Network loop, every tick.
ChatView chat_tick(bool connected, const std::string& partner_name, bool focused, uint32_t now_ms);

// Activity type 22 from the other player.
void chat_on_event(const uint8_t* data, size_t n);

// Keyboard hook (its own thread): true = the key is ours (the chat line is open and the game is in front).
bool chat_hook_key(uint32_t vk, uint32_t scan, bool down);

// The quick list wants the arrow keys / D-pad Left-Right (the activity hook and pad code look at this).
bool chat_quick_open();
void chat_quick_step(int delta);

// Shared world (2026-11-10): the quick messages for the menu's Co-op tab (no hotkey): their names (nullptr past the
// last) and "send this one" (the next tick sends it, with its map pin for the "here" ones).
const char* chat_quick_name(int index);
void chat_send_quick(int index);

// A notice from the mod itself (no "name:" in front): shown to us / sent to the other player (activity type 22,
// kind 0xFE | text). The network loop's thread.
void chat_notice_local(const std::string& text);
void chat_send_notice(const std::string& text);

}  // namespace cdmp::net
