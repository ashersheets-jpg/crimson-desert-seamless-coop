#pragma once
// Co-op+ dev: send hand-written Troc packets from a text file (research on one PC: dev stage/minigame requests,
// sequencer messages). Bound to Ctrl+Shift+F6 when [dev] script=<path> is set. Never used in normal play.
//
// File format, one packet per line ('#' starts a comment, blank lines skipped):
//   c2s <opcode> <hex payload>     (injected as if the client sent it)
//   s2c <opcode> <hex payload>     (injected as if the server sent it)
//   wait <ms>                      (pause before the next line, at most 10 s)
//   tp <x> <y> <z>                 (move our player there with the hopped teleport, revive_prompt.h)
//   poke <hex address> <hex bytes>  (write game memory: research only; the page must be writable)
//   time <hh:mm>                   (set our time of day, time_sync.h)
//   day <+N>                       (N days later, same hour)
//   watch <hex addr> [len] [hits]  (hardware write breakpoint, dev_watch.h; logs who writes it)
//   pad <hex buttons> <ms>         (a simulated pad press, input/pad_input.h; e.g. pad 0021 300 = View + D-pad Up)
//   killall                        (KillCheatReq on every loaded actor but wild animals: live quest research)
//   killfriend                     (KillCheatReq on the other player's character, its key from its load here)
//   killhorse                      (KillCheatReq on the horse the layer counts as ours, after a horse call)
//   advance <hex mission key>      (quest server advances that mission, quest_events.h)
//   waitfile <path>                (wait until a test driver creates the file)
//   fscan <float> / ffilter <lo> <hi> / fwatch <hits>   (dev_memscan.h)
//   memscan <hex> / memdiff / memwatch <rel> <hex bytes> <len> <hits> [nth]   (dev_memscan.h)
// Placeholders in the payload: {P} = our player key (u32), {POS} = our position (3 x f32), {U} = our user key (u32).
// Spaces inside the hex are ignored. Opcodes are decimal.

#include <cstdint>
#include <string>

namespace cdmp::net {

// Runs the file on a worker thread (so "wait" never blocks the network loop). Returns the number of packets queued,
// or -1 when the file cannot be read.
int dev_script_run(const std::string& path, uint32_t own_player, float x, float y, float z);

}  // namespace cdmp::net
