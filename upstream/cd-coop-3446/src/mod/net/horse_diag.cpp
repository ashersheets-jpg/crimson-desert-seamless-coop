#include "mod/net/horse_diag.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstring>

#include <safetyhook.hpp>

#include "mod/hook_error.h"
#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/net/enemy_relay.h"

namespace cdmp::net {
namespace {
namespace off = game::build_3663;

constexpr uint32_t kCopyHorse = 0xB010F002;

struct Site {
    uintptr_t rva;
    uint8_t head[16];
    const char* name;
};
// ---- containment sites, 1.0.0.3446 ----------------------------------------
//
// All four are build-specific and were re-measured for 3446 on 2026-10-30, because the
// 3424 values read as garbage here (verified byte-for-byte against an independent read
// of the same executable on the guest's PC).
//
// Measured so far - ALL FOUR are now resolved:
//
//   kCcUpdate  exe+0x941D40 - three independent agreeing measurements:
//      1. its body touches the component struct the way read_state() reads it:
//         [rcx+0x08] actor, [rbx+0x40] state, [rax+0x30] restore, [rbx+0x88] layer,
//         [rbx+0xA8] pending;
//      2. it is slot 16 of the vtable of .?AVClientCharacterControlActorComponent@pa@@
//         at exe+0x59388D0, the same vtable the port already uses as kVtCharacterControl;
//      3. live: findptr located real instances in the running game and the object has
//         ALL SIX read_state fields populated, with +0x08 resolving to a
//         .?AVClientNormalInGameActor@pa@@ and actor+0x60 reading 0xB01017E1 (the 0xB0
//         prefix is the player actor key family).
//
//   kRestore   exe+0x948240 - two agreeing measurements:
//      1. it reads [this+0x08] (actor), [this+0x40] (state) and [state+0x30] (restore),
//         and touches [this+0x88] and [this+0x90] (the two layers) - the exact field set
//         on_restore walks;
//      2. its 16-byte prologue is byte-identical to the 3424 value. A prologue surviving
//         a patch is not something to rely on, but here it is corroboration rather than
//         the basis: the behaviour above is what identifies the function.
//
// Note the delta between kCcUpdate and kRestore CHANGED between builds (0x4B30 on 3424,
// 0x6500 here), which is one of several measured reasons a fixed delta cannot port these.
//
// The one method that DID work, and would have saved the whole session had it been tried
// first: diff the two builds directly. A copy of the old executable was sitting on disk at
// CD-ref/_scan/CrimsonDesert.exe (PE timestamp 0x6AE387F2, build 1.0.0.3424), so the
// addresses never had to be guessed from byte patterns at all.
//
//   kCcPost    exe+0x860D60 - the 3424 site is a MID-HOOK, not a function entry: 0x860D30
//         sits +0x110 into a body starting at 0x860C20. So the 16 "prologue" bytes
//         recorded in Site were never a prologue - they are an interior fragment, which is
//         why every attempt to find them elsewhere failed. 3424@0x860C20 and 3446@0x860C50
//         are the same code instruction for instruction, shifted by exactly 0x30, with
//         only branch displacements adjusted. The mid-hook is +0x110 into the body in both
//         builds and its 16 bytes are IDENTICAL.
//
//         This also corrects a wrong assumption: kCcPost was assumed to read the layer
//         chart (+0x110) because on_cc_post does. It does not. The mod reproduces that
//         logic in C++; the game need not read the field the same way, and kCcUpdate -
//         the function this pass follows - never touches +0x110 at all.
//
//   kPending   exe+0x232AF20 - also a mid-hook, into a body at exe+0x232AED0. Found
//         through the two `lea` references that install it rather than by any byte
//         pattern: on 3424 it is registered into a list at 0x163CEF8 and written to a
//         vtable slot at 0xB194D53; the 3446 code at 0x163DAA8 has the identical
//         surrounding structure (same [rbp+0x568] destination, same call, same two
//         `mov byte ptr [rsp+...], 0` lines after it) and the `lea rdx` in front of it
//         names the new address. The 30 instructions after the function entry match one
//         for one; only the five branch displacements differ.
//
//         Worth knowing: this function does NOT touch the ClientCharacterControl struct -
//         over 300 instructions it reads none of +0x80/+0x88/+0x90/+0xA8/+0x60. It is
//         installed into a vtable rather than invoked as a CC method. The hook still
//         reads ctx.rcx through read_key/read_state, so whether it observes what the
//         comment above it claims is NOT established - both readers are guarded, so a
//         wrong rcx fails safe, but it fails silently. Treat the PENDING log line as
//         unverified until a live run shows it printing sensible state for a watched key.
// 1.0.0.3576 (Co-op+): every site below moved again (CC, restore, animation, layer sample +0x370, post +0x2C0,
// pending +0xD50 with the action-layers template block); all heads unchanged. build/port3576.
// 1.0.0.3527 (Co-op+): every site below moved (CC +0xDF0, post +0xDD0, animation +0xCA0, pending +0x33B0 with
// the action layers); all heads unchanged. build/port3527/portmap.py.
// 1.0.0.3479 (Co-op+): CC update and restore -0x30 (CC update = vtable slot 16 again), pending
// -0x70 with the action layers; the post-update site did not move. All four heads unchanged.
constexpr Site kCcUpdate = {0x971190, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48,
                                       0x83, 0xEC, 0x20, 0x48}, "CC update"};
constexpr Site kCcPost = {0x88F300, {0x0F, 0x85, 0xB6, 0x00, 0x00, 0x00, 0x4C, 0x89, 0x6C, 0x24, 0x28, 0x80,
                                     0x7C, 0x24, 0x38, 0x00}, "CC post-update"};
constexpr Site kRestore = {0x9778B0, {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x20, 0x48, 0x89, 0x50, 0x10, 0x55,
                                      0x56, 0x57, 0x41, 0x54}, "restore actions"};
constexpr Site kPending = {0x23A2E30, {0x74, 0x51, 0x4C, 0x89, 0x3F, 0x44, 0x38, 0x3E, 0x74, 0x32, 0x48, 0xC7,
                                       0xC3, 0xFF, 0xFF, 0xFF}, "pending actions"};

// Co-op+ (3479): the CC's per-frame update (rcx = the component, xmm1 = frame time), called
// from the actor update at exe+0x99E891. kCcUpdate above is the component's vtable slot 16 (its
// "still loading?" query), not this. A bandit copy with a chart and no action crashed in here
// 0.2 s after loading (exe+0x944080 -> 0x948560 -> 0x25164ED reads [[layer+0x18]+0x28], the
// action, null). Same containment rule, for the copies it was written for (horse and enemies).
constexpr Site kCcTick = {0x9729C0, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
                                     0x41, 0x56, 0x41, 0x57}, "CC tick"};
// Co-op+ (2026-11-07, 3576, Ghidra + three guest crashes: xs3, ag3 at exe+0xA12E4E reading 0x115, after an enemy
// copy "came with no action"): exe+0xA128C0(rcx) processes a character's queued animation events; [rcx] is its CC.
// It walks the CC's layers and reads each layer's current action [layer+0x10] -> +0x111 / +0x115 without a null
// check on one path. Both crashes had L0 AND L1 with a chart and no action; the tick guard only looks at the MAIN
// layer, so once L0 got its action L1 crashed here. For copies only: while any layer has a chart and no current
// action, this function is skipped (its events wait: the count is not cleared), not the whole tick.
// CORRECTION (2026-11-07, full index): exe+0xA12E4E is NOT in this function but in exe+0xA12C00 (called by the CC
// tick); see copy_tick_would_fault(). This guard stays (it keeps waiting events from a copy without actions).
constexpr uint8_t kActionEventsHead[16] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
                                           0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55};

// Co-op+ (3479): the per-frame animation/movement update the CC calls on its +0xC8 object
// (exe+0x945402 -> 0x975B60(obj, frame time); obj+0x60 = the CC). For a copy with a chart and no
// action it reaches 0x976590, which dereferences the missing action (exe+0x976921: the second
// enemy-copy crash). A void function that already returns early in some states: skipped the same way.
constexpr Site kAnimUpdate = {0x9A55B0, {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x70, 0x18, 0x55,
                                         0x57, 0x41, 0x54, 0x41}, "animation update"};
// Co-op+ (3479): a second per-frame CC update (exe+0x99A68A -> 0x94EA00(cc, frame time)); in a fight a
// bandit copy crashed in it (-> 0x7F1E50 -> 0x7F58D0 reads [[main layer]+0x10]+0x116, null).
constexpr Site kCcLate = {0x97D6D0, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
                                     0x41, 0x56, 0x41, 0x57}, "CC late update"};
// Co-op+ (2026-11-05, 3549): in a brand-new game the other player's copy never gets an action and a sixth function
// read the missing one (exe+0x96918B, in the function at 0x968F50 called from 0x9671E1; the CC is
// [[[[rcx+0x60]+8]+0x68]+0x40]: it then reads the CC's main layer and its +0x10 action). It answers a bool;
// skipped as "no".
constexpr Site kLayerSample = {0x997480, {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x48, 0x89, 0x68, 0x20, 0x56,
                                          0x57, 0x41, 0x54, 0x41}, "layer sample"};
SafetyHookMid g_cc_hook, g_post_hook, g_restore_hook, g_pending_hook, g_tick_hook, g_anim_hook, g_late_hook, g_sample_hook;
uint8_t* g_ret = nullptr;
bool g_contain = false;
std::atomic<uint32_t> g_watch_real{0};
std::atomic<int> g_lines{0};
constexpr int kMaxLines = 400;
std::atomic<uint64_t> g_skipped{0}, g_skipped_post{0}, g_skipped_tick{0}, g_skipped_anim{0}, g_skipped_late{0};
uint64_t g_t0 = 0;

uint64_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

struct Layer {
    uintptr_t a0, a8, a10, a18, chart;
};
struct State {
    uint32_t key = 0;
    Layer layer[2]{};
    uint8_t pending = 0;     // CC+0xA8
    uintptr_t pending_list = 0;  // CC+0x80
    uint8_t restore = 0xFF;  // [[CC+0x40]+0x30]
};

bool read_key(uintptr_t cc, uint32_t& key) {
    __try {
        const auto actor = *reinterpret_cast<const uintptr_t*>(cc + 0x08);
        if (!actor) return false;
        key = *reinterpret_cast<const uint32_t*>(actor + off::kActor_Key);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool read_state(uintptr_t cc, State& s) {
    __try {
        for (int i = 0; i < 2; ++i) {
            const auto l = *reinterpret_cast<const uintptr_t*>(cc + 0x88 + 8 * i);
            if (!l) continue;
            s.layer[i] = {*reinterpret_cast<const uintptr_t*>(l + 0x00), *reinterpret_cast<const uintptr_t*>(l + 0x08),
                          *reinterpret_cast<const uintptr_t*>(l + 0x10), *reinterpret_cast<const uintptr_t*>(l + 0x18),
                          *reinterpret_cast<const uintptr_t*>(l + 0x110)};
        }
        s.pending = *reinterpret_cast<const uint8_t*>(cc + 0xA8);
        s.pending_list = *reinterpret_cast<const uintptr_t*>(cc + 0x80);
        const auto st = *reinterpret_cast<const uintptr_t*>(cc + 0x40);
        s.restore = st ? *reinterpret_cast<const uint8_t*>(st + 0x30) : 0xFE;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void read_restore_data(uintptr_t cc, uint8_t (&raw)[0x10], uint64_t& f8) {
    __try {
        const auto st = *reinterpret_cast<const uintptr_t*>(cc + 0x40);
        if (st) {
            std::memcpy(raw, reinterpret_cast<const void*>(st + 0x30), sizeof raw);
            f8 = *reinterpret_cast<const uint64_t*>(st + 0xF8);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Keys whose actor the containment may have to skip.
//
// The other player's character belongs here and was missing: kPuppetPlayer and
// kPuppetBody are built by remote_puppet.cpp and are exactly the actors this file
// exists to protect, so leaving them out meant the guard could never fire for the one
// case that has actually crashed the host. Found by an external review of this file
// (30/10/2026) that read is_copy() against the key list in the puppet code.
//
// The body is included too, not just the player: both are copied actors driven by
// replayed packets rather than by the local world, which is what makes them the ones
// that can arrive with a layer that has a chart and no action.
inline constexpr uint32_t kPuppetPlayerKey = 0xA010F001;
inline constexpr uint32_t kPuppetBodyKey = 0xB010F001;
bool is_copy(uint32_t key) {
    return key == kCopyHorse || is_enemy_copy_key(key) || key == kPuppetPlayerKey || key == kPuppetBodyKey;
}

bool watched(uint32_t key) {
    return is_copy(key) || (key && key == g_watch_real.load(std::memory_order_relaxed));
}

const char* who(uint32_t key) {
    if (key == kCopyHorse) return "COPY";
    if (is_enemy_copy_key(key)) return "ENEMY COPY";
    if (key == kPuppetPlayerKey || key == kPuppetBodyKey) return "OTHER PLAYER";
    return "real";
}

// Same "shape" = same nullness of every field (pointers themselves change).
uint32_t shape(const State& s) {
    uint32_t v = 0;
    for (int i = 0; i < 2; ++i) {
        const Layer& l = s.layer[i];
        v = v << 5 | (l.a0 != 0) | (l.a8 != 0) << 1 | (l.a10 != 0) << 2 | (l.a18 != 0) << 3 | (l.chart != 0) << 4;
    }
    return v << 10 | (s.pending != 0) << 9 | (s.pending_list != 0) << 8 | s.restore;
}

bool take_line() { return g_lines.fetch_add(1, std::memory_order_relaxed) < kMaxLines; }

void log_state(const char* where, const State& s) {
    if (!take_line()) return;
    log::info("HorseDiag +{} ms {} {} 0x{:08X}: L0 a0={} a8={} cur(+10)={} play(+18)={} chart={} | "
              "L1 cur={} play={} chart={} | pending(+A8)={} list(+80)={} restoreByte={}",
              now_ms() - g_t0, where, who(s.key), s.key, s.layer[0].a0 != 0, s.layer[0].a8 != 0,
              s.layer[0].a10 != 0, s.layer[0].a18 != 0, s.layer[0].chart != 0, s.layer[1].a10 != 0,
              s.layer[1].a18 != 0, s.layer[1].chart != 0, s.pending, s.pending_list != 0, s.restore);
}

// A layer with its own chart but nothing playing: the update crashes on it.
// Both layers: an enemy copy ("Wolf Tracker") got its layer-0 action and
// crashed on layer 1 (live test 2026-10-04).
bool layer_without_action(const State& s) {
    for (const Layer& l : s.layer) {
        if (l.chart && (!l.a10 || !l.a18)) return true;
    }
    return false;
}

// Last shape seen per watched actor (2 slots: copy, real).
std::atomic<uint32_t> g_last_shape[3] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};

void on_cc_update(safetyhook::Context& ctx) {
    uint32_t key = 0;
    if (!read_key(ctx.rcx, key) || !watched(key)) return;
    State s;
    s.key = key;
    if (!read_state(ctx.rcx, s)) return;
    const uint32_t sh = shape(s);
    auto& last = g_last_shape[key == kCopyHorse ? 0 : is_enemy_copy_key(key) ? 2 : 1];
    if (last.exchange(sh, std::memory_order_relaxed) != sh) log_state("update", s);
    // Containment: a copy whose main layer has a chart but no current action
    // crashed three different functions of this update (INFORME §5).
    // Co-op+ (3479): this function is the CC's "still loading?" query (vtable slot 16), not its
    // update. For an enemy copy the game's own answer is the right one: it says "loading" until the
    // copy's movement method has its data, and only then may it finish (forcing "done" crashed it in
    // the load-complete path, forcing "loading" left it there for ever). Not touched for them.
    if (g_contain && is_copy(key) && !is_enemy_copy_key(key) && layer_without_action(s)) {
        g_skipped.fetch_add(1, std::memory_order_relaxed);
        // Co-op+ (3460): this is also the component's "still loading?" query -- the actor
        // loader's prepare step (exe+0x5B0CD0) calls it as vf 0x80 and keeps the actor at load
        // state 8 while it returns true. Returning through the bare `ret` left al = the low
        // byte of the vtable pointer (non-zero), so a copy that had no action yet could never
        // finish loading, and it gets no action until it has loaded. Answer what the function
        // itself answers when it has nothing to do: false (exe+0x9428E4 `xor al, al`).
        ctx.rax = 0;
        ctx.rip = reinterpret_cast<uintptr_t>(g_ret);
    }
}

// Co-op+ (3479): what the crashing code reads -- the MAIN layer (index byte [[cc+0x30]+0x1B] into the
// layers at cc+0x88) and its current (+0x10) and playing (+0x18) action. Empty for a copy between
// loading and its first relayed action, and again for a moment between actions. True when the code
// would dereference null. For every copy: Co-op+ (2026-11-05, 3549) the OTHER PLAYER's copy crashed the CC tick the
// same way (exe+0x944E70 -> 0x949350 -> 0x251A1AD reads [action]+0x28) 1.5 s after it was loaded at the start of a
// new game; skipping a frame it could not run is all this does (the copy waits for its action, as horses do).
bool copy_main_layer_empty(uintptr_t cc) {
    uint32_t key = 0;
    if (!read_key(cc, key) || !is_copy(key)) return false;
    uintptr_t info = 0, layer = 0, cur = 0, play = 0;
    uint8_t idx = 0;
    if (!game::safe_read(cc + 0x30, &info, 8) || !info || !game::safe_read(info + 0x1B, &idx, 1) || idx > 7) return false;
    if (!game::safe_read(cc + 0x88 + 8ull * idx, &layer, 8)) return false;
    if (!layer) return true;
    if (!game::safe_read(layer + 0x10, &cur, 8) || !game::safe_read(layer + 0x18, &play, 8)) return false;
    return !cur || !play;
}

// Any of a copy's first four layers with a chart (+0x110) and no current action (+0x10). Guarded reads.
bool copy_layer_waiting(uintptr_t cc) {
    uint32_t key = 0;
    if (!read_key(cc, key) || !is_copy(key)) return false;
    for (int i = 0; i < 4; ++i) {
        uintptr_t layer = 0, chart = 0, cur = 0;
        if (!game::safe_read(cc + 0x88 + 8ull * i, &layer, 8) || !layer) continue;
        if (!game::safe_read(layer + 0x110, &chart, 8) || !game::safe_read(layer + 0x10, &cur, 8)) return false;
        if (chart && !cur) return true;
    }
    return false;
}

std::atomic<uint64_t> g_skipped_events{0};
SafetyHookMid g_events_hook;
void on_action_events(safetyhook::Context& ctx) {
    uintptr_t cc = 0;
    if (g_contain && game::safe_read(ctx.rcx, &cc, 8) && cc && copy_layer_waiting(cc)) {
        const uint64_t n = ++g_skipped_events;
        if (n <= 5 || n % 500 == 0) log::info("HorseDiag: a copy's animation events wait: a layer has no action yet ({})", n);
        ctx.rip = reinterpret_cast<uintptr_t>(g_ret);
    }
}

// Co-op+ (2026-11-07, the decomp pass; guest crash xs2 at exe+0x2519762 reading 0x28): in the action playback
// exe+0x2518850 the game copies an action record's frame number, [[rec + 0x28] + 0x66], into the playback slot BEFORE
// its own `if (rec != 0)` three instructions later -- a game bug that a copy whose record is not there yet (a re-sent
// enemy copy) reaches. At exe+0x251975D (`mov rax, [rsp+0x48]` = rec): with rec 0 the slot's frame word [r14 + 0x38]
// is set to 0 and the two loads are skipped (to exe+0x251976F); with a record, nothing changes. For any actor: the
// path only differs where the game would crash.
constexpr uint8_t kPlaybackFrameCode[18] = {0x48, 0x8B, 0x44, 0x24, 0x48, 0x48, 0x8B, 0x48, 0x28,
                                            0x0F, 0xB7, 0x41, 0x66, 0x66, 0x41, 0x89, 0x46, 0x38};
std::atomic<uint64_t> g_null_records{0};
SafetyHookMid g_playback_hook;
uintptr_t g_playback_resume = 0;
void on_playback_frame(safetyhook::Context& ctx) {
    uintptr_t rec = 0;
    if (!game::safe_read(ctx.rsp + 0x48, &rec, 8) || rec) return;
    uint16_t zero = 0;
    std::memcpy(reinterpret_cast<void*>(ctx.r14 + 0x38), &zero, sizeof zero);
    ctx.rip = g_playback_resume;
    const uint64_t n = ++g_null_records;
    if (n <= 5 || n % 500 == 0) log::info("HorseDiag: an action playback with no record skipped its frame read ({})", n);
}

// Co-op+ (2026-11-07, full Ghidra index + Borque's crash at exe+0xA12E4E reading 0x115 WITH the events guard working):
// that address is not in 0xA128C0 (the events function guarded below) but in exe+0xA12C00, which the CC tick calls
// (0x9451E0 -> 0xA12C00). Its loop over layers 0 and 1, once per layer start: layer = [cc+0x88+8i]; if its flag byte
// [layer+0x108] has no bit 2, and either bit 1 or the layer's queued entry ([cc+0x80] + i*0x78: int != 0 and the
// three shorts at +0x20/+0x22/+0x24 != -1) is set, it sets bit 2 and reads [action+0x115] with action = [layer+0x10]
// -- no null check. The chart is not looked at, so the "chart and no action" test missed it, and the tick guard only
// looked at the MAIN layer. True exactly when that read would hit a null action (for copies).
bool copy_tick_would_fault(uintptr_t cc) {
    uint32_t key = 0;
    if (!read_key(cc, key) || !is_copy(key)) return false;
    uintptr_t entries = 0;
    if (!game::safe_read(cc + 0x80, &entries, 8)) return false;
    for (int i = 0; i < 2; ++i) {
        uintptr_t layer = 0, action = 0;
        uint8_t flags = 0;
        if (!game::safe_read(cc + 0x88 + 8ull * i, &layer, 8) || !layer) return true;  // the game reads [layer] too
        if (!game::safe_read(layer + 0x108, &flags, 1)) return false;
        if (flags & 2) continue;
        if (!(flags & 1)) {
            if (!entries) continue;
            struct Entry {
                int32_t n;
                uint8_t pad[0x1C];
                int16_t a, b, c;
            } e{};
            if (!game::safe_read(entries + 0x78ull * i, &e, sizeof e)) return false;
            if (e.n == 0 || e.a == -1 || e.b == -1 || e.c == -1) continue;
        }
        if (!game::safe_read(layer + 0x10, &action, 8)) return false;
        if (!action) return true;
    }
    return false;
}
std::atomic<uint64_t> g_skipped_tick_layer{0};

void on_cc_tick(safetyhook::Context& ctx) {
    if (g_contain && !copy_main_layer_empty(ctx.rcx) && copy_tick_would_fault(ctx.rcx)) {
        const uint64_t n = ++g_skipped_tick_layer;
        if (n <= 5 || n % 500 == 0) log::info("HorseDiag: a copy's tick waits: a starting layer has no action yet ({})", n);
        ctx.rip = reinterpret_cast<uintptr_t>(g_ret);
        return;
    }
    if (g_contain && copy_main_layer_empty(ctx.rcx)) {
        g_skipped_tick.fetch_add(1, std::memory_order_relaxed);
        ctx.rip = reinterpret_cast<uintptr_t>(g_ret);
    }
}

void on_cc_late(safetyhook::Context& ctx) {
    if (g_contain && copy_main_layer_empty(ctx.rcx)) {
        g_skipped_late.fetch_add(1, std::memory_order_relaxed);
        ctx.rip = reinterpret_cast<uintptr_t>(g_ret);
    }
}

void on_anim_update(safetyhook::Context& ctx) {
    uintptr_t cc = 0;
    if (g_contain && game::safe_read(ctx.rcx + 0x60, &cc, 8) && cc && copy_main_layer_empty(cc)) {
        g_skipped_anim.fetch_add(1, std::memory_order_relaxed);
        ctx.rip = reinterpret_cast<uintptr_t>(g_ret);
    }
}

std::atomic<uint64_t> g_skipped_sample{0};
void on_layer_sample(safetyhook::Context& ctx) {
    if (!g_contain) return;
    // The function's own way there (exe+0x96914B..0x969173): [[[[rcx+0x60]+8]+0x68]+0x40] is the CC.
    uintptr_t p = ctx.rcx;
    for (const uintptr_t off : {0x60, 0x08, 0x68, 0x40}) {
        if (!game::safe_read(p + off, &p, 8) || !p) return;
    }
    const uintptr_t cc = p;
    if (copy_main_layer_empty(cc)) {
        g_skipped_sample.fetch_add(1, std::memory_order_relaxed);
        ctx.rax = 0;
        ctx.rip = reinterpret_cast<uintptr_t>(g_ret);
    }
}

void on_cc_post(safetyhook::Context& ctx) {
    if (!g_contain) return;
    uint32_t key = 0;
    if (!read_key(ctx.rcx, key) || !is_copy(key)) return;
    State s;
    if (!read_state(ctx.rcx, s)) return;
    if (layer_without_action(s)) {
        g_skipped_post.fetch_add(1, std::memory_order_relaxed);
        ctx.rip = reinterpret_cast<uintptr_t>(g_ret);
    }
}

void on_restore(safetyhook::Context& ctx) {
    uint32_t key = 0;
    if (!read_key(ctx.rcx, key) || !watched(key)) return;
    State s;
    s.key = key;
    if (!read_state(ctx.rcx, s)) return;
    log_state("RESTORE(0x891241)", s);
    // The restore data: bytes +0x30..+0x40 and the u64 at +0xF8 of [CC+0x40].
    uint8_t raw[0x10] = {};
    uint64_t f8 = 0;
    read_restore_data(ctx.rcx, raw, f8);
    if (take_line()) {
        log::info("HorseDiag   restore data [CC+0x40]+0x30: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} "
                  "{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} | +0xF8 0x{:X}",
                  raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7], raw[8], raw[9], raw[10],
                  raw[11], raw[12], raw[13], raw[14], raw[15], f8);
    }
}

void on_pending(safetyhook::Context& ctx) {
    uint32_t key = 0;
    if (!read_key(ctx.rcx, key) || !watched(key)) return;
    State s;
    s.key = key;
    if (!read_state(ctx.rcx, s)) return;
    if (s.pending) log_state("PENDING(0x239A7F0)", s);
}

bool hook(const Site& site, SafetyHookMid& out, safetyhook::MidHookFn fn) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint8_t head[16];
    if (!game::safe_read(base + site.rva, head, sizeof head) || std::memcmp(head, site.head, sizeof head) != 0) {
        log::warn("HorseDiag: {} (exe+0x{:X}) not recognised", site.name, site.rva);
        return false;
    }
    auto h = safetyhook::MidHook::create(reinterpret_cast<void*>(base + site.rva), fn);
    if (!h) {
        // Co-op+ (T06): the reason, for the rare launch where a hook does not take (1 in 80, 2026-11-03).
        log::warn("HorseDiag: {} hook failed (safetyhook error {}.{})", site.name,
                  static_cast<int>(h.error().type), hook_failure_detail(h.error()));
        return false;
    }
    out = std::move(*h);
    return true;
}

}  // namespace

bool horse_diag_install(bool contain) {
    if (g_cc_hook) return true;
    g_t0 = now_ms();
    // Co-op+ (T28 found): written once. A retry (mod.cpp, T06) wrote into the page made read-only by the first try
    // and crashed the game (compat-a, 2026-11-03: exception writing g_ret 2 s after a failed first try).
    if (!g_ret) {
        uint8_t* ret = static_cast<uint8_t*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!ret) return false;
        ret[0] = 0xC3;
        DWORD old = 0;
        VirtualProtect(ret, 16, PAGE_EXECUTE_READ, &old);
        FlushInstructionCache(GetCurrentProcess(), ret, 16);
        g_ret = ret;
    }
    const bool ok = hook(kCcUpdate, g_cc_hook, on_cc_update) && hook(kRestore, g_restore_hook, on_restore) &&
                    hook(kPending, g_pending_hook, on_pending);
    if (!ok) {
        g_cc_hook = {};
        g_restore_hook = {};
        g_pending_hook = {};
        return false;
    }
    // If the post-update hook is asked for and does not install, the containment is
    // incomplete, and the caller must not be told it is armed.
    //
    // This returned true unconditionally, so a request for containment that only got
    // three of its four hooks was reported as success: mod.cpp reads the return value
    // as `contained` and enables the horse and enemy copies on the strength of it.
    // Those copies then run without the post-update pass that exists to catch a layer
    // with a chart and no action -- the exact state the containment was written for.
    // Found by an external review of this file (30/10/2026).
    //
    // Partial installs are now torn down rather than left half-armed, because a
    // containment that fires on some paths and not others is worse than none: it makes
    // the failure intermittent instead of reproducible.
    if (contain && (!hook(kCcPost, g_post_hook, on_cc_post) || !hook(kCcTick, g_tick_hook, on_cc_tick) ||
                    !hook(kAnimUpdate, g_anim_hook, on_anim_update) || !hook(kCcLate, g_late_hook, on_cc_late))) {
        g_post_hook = {};
        g_tick_hook = {};
        g_anim_hook = {};
        g_late_hook = {};
        g_cc_hook = {};
        g_restore_hook = {};
        g_pending_hook = {};
        log::warn("HorseDiag: post-update or tick hook not installed; containment refused (all hooks uninstalled)");
        return false;
    }
    // Optional (not part of the all-or-nothing set above): without it a fresh character's copy can still crash.
    if (contain && !g_sample_hook && !hook(kLayerSample, g_sample_hook, on_layer_sample)) {
        log::warn("HorseDiag: layer-sample guard not installed (a brand-new game's copy may crash)");
    }
    // Co-op+ (2026-11-07): the animation-events guard (copy_layer_waiting; optional like the sample guard).
    if (contain && !g_events_hook) {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        uint8_t head[sizeof kActionEventsHead] = {};
        if (game::safe_read(base + 0xA427E0, head, sizeof head) && std::memcmp(head, kActionEventsHead, sizeof head) == 0) {
            g_events_hook = safetyhook::create_mid(reinterpret_cast<void*>(base + 0xA427E0), on_action_events);
        }
        log::info("HorseDiag: animation-events guard {}", g_events_hook ? "installed" : "NOT installed (code not as expected)");
    }
    // Co-op+ (2026-11-07): the action-playback null-record guard (on_playback_frame), only on these exact bytes.
    if (contain && !g_playback_hook) {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        uint8_t code[sizeof kPlaybackFrameCode] = {};
        if (game::safe_read(base + 0x258B13D, code, sizeof code) && std::memcmp(code, kPlaybackFrameCode, sizeof code) == 0) {
            g_playback_resume = base + 0x258B14F;
            g_playback_hook = safetyhook::create_mid(reinterpret_cast<void*>(base + 0x258B13D), on_playback_frame);
        }
        log::info("HorseDiag: action-playback guard {}", g_playback_hook ? "installed" : "NOT installed (code not as expected)");
    }
    g_contain = contain;
    log::info("HorseDiag: installed (containment {})", g_contain ? "on" : "off");
    return true;
}

void horse_diag_watch(uint32_t key) {
    g_watch_real = key;
    log::info("HorseDiag: watching the copy 0x{:08X} and the real horse 0x{:08X}", kCopyHorse, key);
}

void horse_diag_tick() {
    static uint64_t logged = 0, logged_post = 0, last = 0;
    const uint64_t a = g_skipped.load(std::memory_order_relaxed),
                   b = g_skipped_post.load(std::memory_order_relaxed) + g_skipped_tick.load(std::memory_order_relaxed) +
                       g_skipped_anim.load(std::memory_order_relaxed) + g_skipped_late.load(std::memory_order_relaxed) +
                       g_skipped_sample.load(std::memory_order_relaxed);
    const uint64_t t = now_ms();
    if ((a != logged || b != logged_post) && t - last >= 2000) {
        log::info("HorseDiag: copy's update skipped {} times (post-update {}) while it has no action", a, b);
        logged = a;
        logged_post = b;
        last = t;
    }
}

}  // namespace cdmp::net
