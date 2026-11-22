#pragma once
// Co-op+ B20 (2026-11-22, research, [session2] voice_probe=1, default off; host AND guest): do the other player's
// VOICE sounds (effort / hurt shouts) play on this game? Voices are action frame events
// (pa::ClientFrameEventVoiceSound, 3576 0x79A160 -> 3732 0x7C72A0), the same frame-event path as the swing sounds
// (ClientFrameEventSoundEvent, 3576 0x795B20 -> 3732 0x7C2C60) that cs42a heard for the friend; both processors take
// a context whose first field is the actor (3732: mov r12, [rdi] after rdi = the 5th argument). Inline hooks count,
// per character (the other player / this game's own player), each processor's calls and the Wwise posts
// (PlayEvent, 3663 0x3BD12D0 -> 3732 0x3BD5820) made inside them, with how many the sound manager accepted
// (non-zero). Each detour calls the game's code unchanged. A line every 10 s for 10 minutes.
namespace cdmp::session2 {
void voice_probe_start(bool guest);
}  // namespace cdmp::session2
