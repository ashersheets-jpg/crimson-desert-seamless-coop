// Co-op+ (2026-11-08, build 1.0.0.3576): npc_category.h / npc_category_logic.h.
#include "mod/session2/npc_category.h"

#include <windows.h>

#include <safetyhook.hpp>

#include <atomic>
#include <cstring>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/troc_frame.h"
#include "common/troc_link.h"
#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/npc_category_logic.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

namespace off = game::build_2976;

constexpr uintptr_t kEvaluate = 0x24752E0;  // ConditionData_CheckQuestDialogCategory, vf+0xA8 (vtable 0x5CFB9F0)
constexpr uint8_t kEvaluateHead[15] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
                                       0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20};
constexpr uintptr_t kLocalUserRoot = 0x71C0100;  // [[[root]+0x30]+0x58] = the local user, its +0x50 the character
constexpr uint64_t kSendEveryMs = 5000, kGuestStaleMs = 15000, kReadEveryMs = 1000;

bool g_on = false, g_guest = false;
uintptr_t g_base = 0;
SafetyHookInline g_hook;

SRWLOCK g_lock = SRWLOCK_INIT;
std::unordered_map<uint32_t, uint8_t> g_host;  // the host's NPC key -> category
uint64_t g_host_ms = 0;                         // when it was last filled (GetTickCount64)

std::atomic<uint64_t> c_calls{0}, c_host{0}, c_own{0}, c_native{0}, c_changed{0}, c_frames_in{0}, c_frames_bad{0},
    c_frames_out{0}, c_frames_refused{0};
std::atomic<uint32_t> g_last_size{0};

template <class T>
bool rd(uintptr_t a, T& v) {
    return a && game::safe_read(a, &v, sizeof v);
}

uint32_t key_of(uintptr_t actor) {
    uint32_t k = 0;
    rd(actor + off::kActor_Key, k);
    return k;
}

// The table of a player character: [[actor+0x68]+0xF8] (the SetQuestDialogAck handler 0xD2E520 and 0x24752E0's
// main character path; for a player's character the main character is itself).
uintptr_t table_of(uintptr_t actor) {
    uintptr_t comps = 0, tab = 0;
    if (!rd(actor + 0x68, comps) || !rd(comps + 0xF8, tab)) return 0;
    return tab;
}

// 0x1C358E0's lookup: buckets +0x68, count +0x6C, bucket array +0x78 (0x100 bytes each: u32 n, then n pairs
// {u32 key, u32 index} from +8), entries [[+0x80] + index*8] (+4 key, category +0x18 / +0x19).
bool lookup(uintptr_t tab, uint32_t key, uint8_t& cat) {
    uint32_t buckets = 0, count = 0;
    uintptr_t bk = 0, ents = 0;
    if (!rd(tab + 0x68, buckets) || !rd(tab + 0x6C, count) || !buckets || !count || !rd(tab + 0x78, bk) ||
        !rd(tab + 0x80, ents) || !bk || !ents)
        return false;
    const uintptr_t b = bk + static_cast<uintptr_t>(key % buckets) * 0x100;
    uint32_t n = 0;
    if (!rd(b, n)) return false;
    for (uint32_t i = 0; i < n && i < 31; ++i) {
        uint32_t k = 0, ix = 0;
        if (!rd(b + 8 + 8ull * i, k) || k != key || !rd(b + 12 + 8ull * i, ix)) continue;
        uintptr_t e = 0;
        uint32_t ek = 0;
        uint8_t c18 = 0, c19 = 0;
        if (!rd(ents + 8ull * ix, e) || !rd(e + 4, ek) || ek != key || !rd(e + 0x18, c18) || !rd(e + 0x19, c19))
            return false;
        cat = ncl::effective(c18, c19);
        return true;
    }
    return false;
}

bool host_category(uint32_t key, uint8_t& cat, uint64_t now) {
    AcquireSRWLockShared(&g_lock);
    bool ok = false;
    if (!g_guest || now - g_host_ms <= kGuestStaleMs) {
        if (auto it = g_host.find(key); it != g_host.end()) {
            cat = it->second;
            ok = true;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return ok;
}

// The guest's character as the subject? Guest: any player character on this client. Host: user 2's character
// (its key from the server's MainPlayerDataStartAck), on any thread (the server judges it; the host's client never
// asks for it).
bool is_guest_subject(uintptr_t subj) {
    const uint32_t k = key_of(subj);
    if ((k >> 28) != 0xA) return false;
    if (g_guest) return true;
    const uint32_t u2 = server_user2_player_key();
    return u2 && k == u2;
}

uint8_t __fastcall detour(uintptr_t obj, uintptr_t param, uintptr_t a3, uintptr_t a4) {
    const uint8_t native = g_hook.unsafe_call<uint8_t>(obj, param, a3, a4);
    c_calls.fetch_add(1, std::memory_order_relaxed);
    // The subject as 0x24752E0 takes it: a compare parameter of type 5 (byte +8) has it at +0x830, the NPC at +0x828.
    uint8_t type = 0;
    uintptr_t subj = 0, npc = 0;
    if (!rd(param + 8, type) || type != 5 || !rd(param + 0x830, subj) || !rd(param + 0x828, npc) || !subj || !npc ||
        !is_guest_subject(subj))
        return native;
    const uint32_t nkey = key_of(npc);
    uint8_t want = 0, own = 0, host = 0;
    rd(obj + 0x18, want);
    const uintptr_t tab = table_of(subj);
    const bool own_present = tab && lookup(tab, nkey, own);
    const bool host_present = host_category(nkey, host, GetTickCount64());
    const ncl::Decision d = ncl::decide(native, own_present, own, host_present, host, want);
    (d.source == ncl::Source::Host ? c_host : d.source == ncl::Source::Own ? c_own : c_native)
        .fetch_add(1, std::memory_order_relaxed);
    if (d.result != native) c_changed.fetch_add(1, std::memory_order_relaxed);
    return d.result;
}

uintptr_t local_character() {
    uintptr_t g = 0, g30 = 0, user = 0, ch = 0;
    if (!rd(g_base + kLocalUserRoot, g) || !rd(g + 0x30, g30) || !rd(g30 + 0x58, user) || !rd(user + 0x50, ch))
        return 0;
    return ch;
}

// The host's own client table, read as a whole.
std::vector<ncl::Entry> read_local_table() {
    std::vector<ncl::Entry> out;
    const uintptr_t tab = table_of(local_character());
    uint32_t count = 0;
    uintptr_t ents = 0;
    if (!tab || !rd(tab + 0x6C, count) || !rd(tab + 0x80, ents) || !ents) return out;
    if (count > ncl::kMaxEntries) count = ncl::kMaxEntries;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t e = 0;
        uint32_t k = 0;
        uint8_t c18 = 0, c19 = 0;
        if (!rd(ents + 8ull * i, e) || !e || !rd(e + 4, k) || !rd(e + 0x18, c18) || !rd(e + 0x19, c19)) continue;
        if ((k >> 28) != 0xB) continue;  // NPCs only
        out.push_back({k, ncl::effective(c18, c19)});
    }
    return out;
}

void store(const std::vector<ncl::Entry>& v) {
    std::unordered_map<uint32_t, uint8_t> m;
    m.reserve(v.size());
    for (const auto& e : v) m[e.key] = e.cat;
    AcquireSRWLockExclusive(&g_lock);
    g_host.swap(m);
    g_host_ms = GetTickCount64();
    ReleaseSRWLockExclusive(&g_lock);
    g_last_size.store(static_cast<uint32_t>(v.size()), std::memory_order_relaxed);
}

}  // namespace

void npc_category_install(bool on, bool guest) {
    g_guest = guest;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!on) {
        log::info("NpcCategory off (npc_category_share=0): the guest's NPC quest-dialog categories stay its own (the "
                  "Greet is refused to the guest for nearly every NPC)");
        return;
    }
    uint8_t h[sizeof kEvaluateHead] = {};
    if (!game::safe_read(g_base + kEvaluate, h, sizeof h) || std::memcmp(h, kEvaluateHead, sizeof h) != 0) {
        log::warn("NpcCategory NOT installed: CheckQuestDialogCategory's evaluate 0x{:X} has other bytes (another game "
                  "build?)",
                  kEvaluate);
        return;
    }
    g_hook = safetyhook::create_inline(reinterpret_cast<void*>(g_base + kEvaluate), reinterpret_cast<void*>(&detour));
    g_on = static_cast<bool>(g_hook);
    log::info("NpcCategory {} (npc_category_share=1, {}): CheckQuestDialogCategory 0x{:X} judges the guest's character "
              "with the host's NPC category where its own is missing or 0 (the NPCs' stage functions set it only on "
              "the host's client){}",
              g_on ? "ON" : "NOT installed (hook failed)", guest ? "guest" : "host", kEvaluate,
              guest ? "; the host's table arrives as tunnel control state 16" : "; this table goes to the guest as tunnel "
                                                                                 "control state 16");
}

void npc_category_host_tick(troc::TrocLink& link, uint32_t /*now_ms*/) {
    if (!g_on || g_guest) return;
    try {
        static uint64_t last_read = 0, last_sent = 0, last_fp = 0, last_log = 0;
        const uint64_t now = GetTickCount64();
        if (now - last_read < kReadEveryMs) return;
        last_read = now;
        const std::vector<ncl::Entry> v = read_local_table();
        store(v);
        const uint64_t fp = ncl::fingerprint(v);
        if (link.connected() && !link.broken() && server_user2_in_world() && (fp != last_fp || now - last_sent >= kSendEveryMs)) {
            const auto payload = ncl::make_payload(v);
            const auto pkt = troc::make(troc::kControlOpcode, -1, payload);
            if (link.push(pkt)) {
                c_frames_out.fetch_add(1, std::memory_order_relaxed);
                last_sent = now;
                last_fp = fp;
            } else {
                c_frames_refused.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (now - last_log >= 60000) {
            last_log = now;
            log::info("{}", npc_category_describe());
        }
    } catch (...) {
    }
}

bool npc_category_guest_on_control(std::span<const uint8_t> payload) {
    if (payload.empty() || payload[0] != ncl::kState) return false;
    try {
        if (auto v = ncl::parse_payload(payload)) {
            store(*v);
            const uint64_t n = c_frames_in.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n == 1 || n % 60 == 0) log::info("{}", npc_category_describe());
        } else {
            c_frames_bad.fetch_add(1, std::memory_order_relaxed);
        }
    } catch (...) {
    }
    return true;
}

std::string npc_category_describe() {
    return std::format("NpcCategory ({}{}): host table {} NPCs | the guest's character judged {} times: host category {}, "
                       "own kept {}, native {} (answers changed {}) | frames out {} (refused {}) in {} (bad {})",
                       g_on ? "on" : "off", g_guest ? ", guest" : ", host", g_last_size.load(), c_calls.load() ?
                       c_host.load() + c_own.load() + c_native.load() : 0, c_host.load(), c_own.load(), c_native.load(),
                       c_changed.load(), c_frames_out.load(), c_frames_refused.load(), c_frames_in.load(),
                       c_frames_bad.load());
}

}  // namespace cdmp::session2
