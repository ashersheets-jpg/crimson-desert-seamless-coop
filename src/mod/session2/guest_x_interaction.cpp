#include "mod/session2/guest_x_interaction.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <span>
#include <string>
#include <vector>

#include <safetyhook.hpp>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/guest.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/guest_seq_route.h"
#include "mod/session2/guest_x_interaction_logic.h"
#include "mod/session2/guest_x_veto_logic.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"
#include "mod/session2/stage_lookup_logic.h"

namespace cdmp::session2 {
namespace {

namespace p = p8p;
namespace off = game::build_2976;
using gxi::Act;
using gxi::Mode;
using gxi::Verdict;

// X's character and its manager now (the same chain guest_seq_route / guest_seq_play read): [X+0x18] = the actor,
// +0x60 its key, [[[actor+0x68]+0xA8]+0x18] its ClientSequencerStageManager.
constexpr uint32_t kMgr_Actor = 0x18, kActor_Key = 0x60, kActor_Comps = 0x68, kComps_Seq = 0xA8, kSeqComp_Mgr = 0x18;
// The instance's name, best effort ([[inst+0x48]+0x1B8], the chain guest_x_veto reads from a processor's stage) and
// its +0x30 (the stage's sequencer key there): logged to cross-check with GuestXVeto's SetInteraction lines.
constexpr uint32_t kInst_Data = 0x48, kData_Name = 0x1B8, kInst_Key = 0x30;
constexpr size_t kMaxX = 4;

uintptr_t g_base = 0;
std::atomic<bool> g_started{false}, g_a_on{false}, g_r_on{false}, g_f_on{false};
std::atomic<uint8_t> g_mode{0};
SafetyHookMid g_a_hook, g_r_hook, g_f_hook;
// The evaluator fix (2026-11-21): a redirected entry with no evaluator is accepted (mode 1) or counted (probe).
SafetyHookMid g_v_hook;
std::atomic<bool> g_v_on{false}, g_research{false}, g_v_fix{false};
std::atomic<uint64_t> g_v_seen{0}, g_v_accepted{0}, g_v_would{0}, g_v_own{0};
std::atomic<uint32_t> g_v_last_id{0};
// Gates P0..P4 (stage_lookup_logic.h kGateAt; research, read only): arrivals of the calls A followed, the last gate a
// call reached (F counts the refused ones by it), and what P0 / P1 read.
SafetyHookMid g_gate_hook[5];
std::atomic<bool> g_gates_on{false};
std::atomic<uint64_t> g_gate_seen[5]{}, g_refused_after[6]{}, g_busy_nonzero{0}, g_cond_minus1{0};
std::atomic<uint32_t> g_busy_last{0}, g_cond_last{0};
thread_local uint8_t t_gate = 0;  // 0: no gate reached yet; k + 1: gate Pk
// Early counters E0..E3 (every call of the prompt check, research): arrivals; E0 also the entry's stage id and whose
// player J it is (the local guest 0xA0100066 or another key).
SafetyHookMid g_early_hook[4];
std::atomic<bool> g_early_on{false};
std::atomic<uint64_t> g_early_seen[4]{}, g_early_own{0}, g_early_no_id{0};
std::atomic<uint32_t> g_early_last_id{0}, g_early_last_key{0};
gxi::XSlots<kMaxX> g_x;
thread_local gxi::Call t_call;
// P8 re-entry (s25/reentrada.md §1.4): the X managers noted belong to one remote conversation. XSlots is atomic: the
// first of its users (the prompt hook or guest_x_veto's note) that sees a new conversation clears it.
std::atomic<uint32_t> g_x_conv{0};
void x_conversation_check() noexcept {
    const uint32_t conv = guest_conversation_id();
    if (conv == g_x_conv.load(std::memory_order_relaxed) || g_x_conv.exchange(conv) == conv) return;
    if (conv >= 2) g_x.clear();
}

std::atomic<uint64_t> g_calls{0}, g_verdicts[static_cast<size_t>(Verdict::Count)] = {};
std::atomic<uint64_t> g_redirected{0}, g_would{0}, g_followed{0}, g_r_seen{0}, g_f_seen{0};
// Results at F of the calls A followed: [act class][bl 0, 1, 2, other]. Classes: 0 redirected, 1 would redirect
// (probe: the native lookup in M), 2 an entry of nobody known (in neither / dead in X / no X).
std::atomic<uint64_t> g_results[3][4] = {};
// At R: the native lookup found an instance ([0] redirected, [1] would, [2] other).
std::atomic<uint64_t> g_r_found[3] = {}, g_r_missing[3] = {};
std::atomic<uint64_t> g_x_states[static_cast<size_t>(gxi::XState::Count)] = {};

enum Kind : uint16_t { kEA, kEF, kEN, kER, kKindCount };
p::EventRing<256> g_ring;
p::WindowLimiter g_lim[kKindCount] = {{24, 5'000'000}, {24, 5'000'000}, {40, 5'000'000}, {40, 5'000'000}};
// Nodes (research): the StageChart interaction node's ADD / REMOVE on this client (interaction_share's 3732 sites).
constexpr uintptr_t kNodeAdd = 0x25D218C, kNodeRemove = 0x25D2368;
SafetyHookMid g_node_add_hook, g_node_rem_hook;
std::atomic<bool> g_nodes_on{false};
std::atomic<uintptr_t> g_last_m{0};
void xid_note(uint32_t id) noexcept;  // gx7 research, below
std::atomic<uint64_t> g_nodes_add{0}, g_nodes_rem{0}, g_nodes_from[3]{};
std::atomic<uintptr_t> g_ic_seen[8]{};
std::atomic<uint32_t> g_ic_next{0};
isl::IdBook<128> g_book;
std::atomic<uint64_t> g_limited{0}, g_lost{0}, g_lines{0};
constexpr uint64_t kMaxLines = 1500;
constexpr uint32_t kPerIdVerdict = 3;

// ---------------------------------------------------------------- hot path (no CRT, no allocation)

template <class T>
bool rd(uintptr_t a, T& v) noexcept {
    if (a < 0x10000) return false;
    __try {
        v = *reinterpret_cast<const volatile T*>(a);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
template <class T>
T rdv(uintptr_t a, T fallback = T{}) noexcept {
    T v{};
    return rd(a, v) ? v : fallback;
}
uintptr_t rdp(uintptr_t a) noexcept { return rdv<uintptr_t>(a); }

struct SehReader {
    template <class T>
    bool rd(uintptr_t a, T& v) const noexcept {
        return ::cdmp::session2::rd(a, v);
    }
};

uint32_t key_of_mgr(uintptr_t mgr) noexcept {
    const uintptr_t actor = mgr ? rdp(mgr + kMgr_Actor) : 0;
    return actor ? rdv<uint32_t>(actor + kActor_Key) : 0;
}

gxi::XCheck read_x(uintptr_t x) noexcept {
    gxi::XCheck c;
    c.x = x;
    c.want_vt = g_base + isl::kVtClientStageMgr;
    c.vt = rdp(x);
    if (c.vt != c.want_vt) return c;
    const uintptr_t actor = rdp(x + kMgr_Actor);
    c.key = actor ? rdv<uint32_t>(actor + kActor_Key) : 0;
    const uintptr_t comps = actor ? rdp(actor + kActor_Comps) : 0;
    const uintptr_t seq = comps ? rdp(comps + kComps_Seq) : 0;
    c.actor_mgr = seq ? rdp(seq + kSeqComp_Mgr) : 0;
    return c;
}

void emit(Kind k, p::Event& e) noexcept {
    e.kind = k;
    e.qpc_us = p8_now_us();
    e.tid = GetCurrentThreadId();
    if (!g_lim[k].take(e.qpc_us)) {
        g_limited.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!g_ring.push(e)) g_lost.fetch_add(1, std::memory_order_relaxed);
}

// The book's key: the stage id with the line kind and the verdict / result in the top byte.
uint64_t book_key(uint64_t id, uint8_t tag) noexcept { return (id & 0x00FFFFFFFFFFFFFFull) | static_cast<uint64_t>(tag) << 56; }

size_t act_class(Act a, Verdict v) noexcept {
    if (a == Act::Redirect) return 0;
    if (a == Act::Would) return 1;
    (void)v;
    return 2;
}

// A 0x2126A1F: RAX = the field's vf48 (M). See guest_x_interaction.h.
void on_a(safetyhook::Context& ctx) noexcept {
    if (!g_a_on.load(std::memory_order_acquire)) return;
    g_calls.fetch_add(1, std::memory_order_relaxed);
    gxi::Facts f;
    const uintptr_t field = rdp(ctx.rsp + isl::kRspField);
    f.client_field = field && rdp(field) == g_base + isl::kVtClientField;
    if (f.client_field) f.remote = guest_remote_live();
    f.m = ctx.rax;
    if (ctx.rax) g_last_m.store(ctx.rax, std::memory_order_relaxed);
    f.id = f.client_field && f.remote ? rdv<uint64_t>(ctx.r13 + isl::kEntry_StageId) : 0;
    uintptr_t x_inst = 0;
    uint32_t m_key = 0, x_key = 0;
    if (f.client_field && f.remote && f.m && (f.id & 0x1FFFFFFFull)) {
        const SehReader r;
        const isl::Lookup lm = isl::find_stage(r, f.m, f.id);
        f.m_read = lm.read_ok;
        f.in_m = lm.found;
        if (f.m_read && !f.in_m) {
            uintptr_t xs[kMaxX] = {};
            uint32_t keys[kMaxX] = {};
            x_conversation_check();
            f.x_known = g_x.snapshot(xs, keys, kMaxX);
            m_key = key_of_mgr(f.m);
            const uint32_t own = guest_seq_route_own_key();
            for (size_t i = 0; i < f.x_known; ++i) {
                const gxi::XCheck c = read_x(xs[i]);
                const gxi::XState s = gxi::check_x(c, f.m, m_key, own);
                g_x_states[static_cast<size_t>(s)].fetch_add(1, std::memory_order_relaxed);
                if (s != gxi::XState::Ok) continue;
                ++f.x_valid;
                const isl::Lookup lx = isl::find_stage(r, c.x, f.id);
                if (lx.read_ok) f.x_read = true;
                if (lx.found) {
                    f.x = c.x;
                    x_inst = lx.inst;
                    x_key = c.key;
                    break;
                }
                if (lx.dead) f.x_dead = true;
            }
        }
    }
    const Verdict v = gxi::decide(f);
    g_verdicts[static_cast<size_t>(v)].fetch_add(1, std::memory_order_relaxed);
    if (v == Verdict::NotClient || v == Verdict::NotRemote || v == Verdict::InM || v == Verdict::NoM ||
        v == Verdict::NoId) {
        return;
    }
    const Act a = gxi::act(static_cast<Mode>(g_mode.load(std::memory_order_relaxed)), v);
    if (a == Act::Redirect) {
        ctx.rax = f.x;  // mov rdi,rax ; mov [rbp+0x8E8],rax run next (relocated): the lookup and its lock use X
        g_redirected.fetch_add(1, std::memory_order_relaxed);
    } else if (a == Act::Would) {
        g_would.fetch_add(1, std::memory_order_relaxed);
    }
    // Follow the call to R and F (same thread, same entry and rsp).
    t_call.entry = ctx.r13;
    t_call.rsp = ctx.rsp;
    t_call.id = f.id;
    t_call.m = f.m;
    t_call.x = f.x;
    t_call.verdict = static_cast<uint8_t>(v);
    t_call.act = static_cast<uint8_t>(a);
    if (a == Act::Redirect && g_research.load(std::memory_order_relaxed)) xid_note(static_cast<uint32_t>(f.id));
    g_followed.fetch_add(1, std::memory_order_relaxed);
    if (!gxi::worth_a_line(v) || !g_book.take(book_key(f.id, static_cast<uint8_t>(0x10 | static_cast<uint8_t>(v))),
                                              kPerIdVerdict)) {
        return;
    }
    p::Event e;
    e.v[0] = static_cast<uint64_t>(v) | static_cast<uint64_t>(a) << 8;
    e.v[1] = ctx.r13;
    e.v[2] = f.id;
    e.v[3] = f.m;
    e.v[4] = f.x;
    e.v[5] = x_inst;
    e.v[6] = m_key | static_cast<uint64_t>(x_key) << 32;
    e.v[7] = f.x_known | static_cast<uint64_t>(f.x_valid) << 16 | static_cast<uint64_t>(f.m_read) << 32 |
             static_cast<uint64_t>(f.x_read) << 33 | static_cast<uint64_t>(f.x_dead) << 34;
    if (x_inst) {
        e.v[8] = rdv<uint64_t>(x_inst + kInst_Key);
        const uintptr_t data = rdp(x_inst + kInst_Data);
        const uintptr_t name = data ? rdp(data + kData_Name) : 0;
        for (size_t k = 0; name && k < 3 && rd(name + 8 * k, e.v[9 + k]); ++k) {
        }
    }
    emit(kEA, e);
}

// R 0x2126AAB: after the native lookup. RSI = the instance found (or 0), [rbp+0x8E8] = the manager it used.
thread_local uintptr_t t_r_found = 0, t_r_used = 0;  // per call, meaningful while t_call is set
thread_local bool t_r_set = false;

void on_r(safetyhook::Context& ctx) noexcept {
    if (!g_r_on.load(std::memory_order_acquire)) return;
    if (!t_call.same(ctx.r13, ctx.rsp)) return;
    g_r_seen.fetch_add(1, std::memory_order_relaxed);
    t_r_found = ctx.rsi;
    t_r_used = rdp(ctx.rbp + isl::kRbpMgrUsed);
    t_r_set = true;
    const size_t c = act_class(static_cast<Act>(t_call.act), static_cast<Verdict>(t_call.verdict));
    (ctx.rsi ? g_r_found[c] : g_r_missing[c]).fetch_add(1, std::memory_order_relaxed);
}

// The evaluator probe (research): entries WITH an evaluator, their +8's vtable (8 distinct, with an example).
struct EvalSeen {
    std::atomic<uintptr_t> vt{0};
    std::atomic<uintptr_t> ev{0}, entry{0};
    std::atomic<uint32_t> id{0};
    std::atomic<uint64_t> n{0};
};
EvalSeen g_eval_seen[8];
// gx7: X's sequencer ids (GuestSeqRoute's routed 2661s, GuestXInter's redirects) and the callers that look them up
// in M.
std::atomic<uint32_t> g_xids[32]{};
std::atomic<uint32_t> g_xid_next{0};
void xid_note(uint32_t id) noexcept {
    id &= 0x1FFFFFFFu;
    if (!id) return;
    for (const auto& s : g_xids) {
        if (s.load(std::memory_order_relaxed) == id) return;
    }
    g_xids[g_xid_next.fetch_add(1, std::memory_order_relaxed) % 32].store(id, std::memory_order_relaxed);
}
bool xid_has(uint32_t id) noexcept {
    id &= 0x1FFFFFFFu;
    if (!id) return false;
    for (const auto& s : g_xids) {
        if (s.load(std::memory_order_relaxed) == id) return true;
    }
    return false;
}
struct CallerSeen {
    std::atomic<uintptr_t> ret{0};
    std::atomic<uint64_t> n{0};
    std::atomic<uint32_t> id{0};
};
CallerSeen g_lookup_callers[16];
std::atomic<uint64_t> g_lookup_m_x{0}, g_lookup_x{0};
SafetyHookMid g_lookup_hook;
constexpr uintptr_t kLookupAt = 0x21C7370;
void on_lookup(safetyhook::Context& ctx) noexcept {
    const uintptr_t mgr = ctx.rcx;
    const uint32_t id = static_cast<uint32_t>(ctx.r8 & 0x1FFFFFFFull);
    if (!mgr || !xid_has(id)) return;
    if (mgr != g_last_m.load(std::memory_order_relaxed)) {
        g_lookup_x.fetch_add(1, std::memory_order_relaxed);  // in X (the redirect) or another manager
        return;
    }
    g_lookup_m_x.fetch_add(1, std::memory_order_relaxed);
    uintptr_t ret = 0;
    if (!game::fast_read(ctx.rsp, &ret, sizeof ret) || !ret) return;
    for (CallerSeen& c : g_lookup_callers) {
        const uintptr_t have = c.ret.load(std::memory_order_relaxed);
        if (have == ret) {
            c.n.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (have == 0) {
            uintptr_t expect = 0;
            if (c.ret.compare_exchange_strong(expect, ret) || expect == ret) {
                c.n.fetch_add(1, std::memory_order_relaxed);
                c.id.store(id, std::memory_order_relaxed);
                return;
            }
        }
    }
}
// gx6: whole entries (0x38 bytes) seen at the evaluator test, 8 distinct, logged once each.
struct EntryDump {
    std::atomic<uintptr_t> at{0};
    std::atomic<bool> ready{false}, said{false};
    uint8_t bytes[0x38] = {};
};
EntryDump g_entry_dump[8];
void entry_note(uintptr_t entry) noexcept {
    for (EntryDump& d : g_entry_dump) {
        const uintptr_t have = d.at.load(std::memory_order_relaxed);
        if (have == entry) return;
        if (have == 0) {
            uintptr_t expect = 0;
            if (!d.at.compare_exchange_strong(expect, entry)) {
                if (expect == entry) return;
                continue;
            }
            if (game::fast_read(entry, d.bytes, sizeof d.bytes)) d.ready.store(true, std::memory_order_release);
            return;
        }
    }
}
std::atomic<uint64_t> g_eval_calls{0}, g_eval_none{0};
void eval_note(uintptr_t entry, uintptr_t ev, uint32_t id) noexcept {
    uintptr_t vt = 0;
    if (!game::fast_read(ev, &vt, sizeof vt) || !vt) return;
    for (EvalSeen& s : g_eval_seen) {
        const uintptr_t have = s.vt.load(std::memory_order_relaxed);
        if (have == vt) {
            s.n.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (have == 0) {
            uintptr_t expect = 0;
            if (s.vt.compare_exchange_strong(expect, vt)) {
                s.ev.store(ev, std::memory_order_relaxed);
                s.entry.store(entry, std::memory_order_relaxed);
                s.id.store(id, std::memory_order_relaxed);
                s.n.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (expect == vt) {
                s.n.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
    }
}
// The host's side of the probe: every call of the check reaching 0x2360A4D on the host's client.
SafetyHookMid g_host_eval_hook;
void on_host_eval(safetyhook::Context& ctx) noexcept {
    g_eval_calls.fetch_add(1, std::memory_order_relaxed);
    uintptr_t ev = 0;
    uint64_t id = 0;
    if (!game::fast_read(ctx.r13 + 8, &ev, sizeof ev)) return;
    game::fast_read(ctx.r13 + isl::kEntry_StageId, &id, sizeof id);
    if (!ev) {
        g_eval_none.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    eval_note(ctx.r13, ev, static_cast<uint32_t>(id & 0x1FFFFFFFull));
    if (id & 0x1FFFFFFFull) entry_note(ctx.r13);
}
// gx8 (research, both roles): the player's interaction requests (pa::ClientInteractionActorComponent, vtable
// exe+0x59CF548): +0x180 0xAF1530 (2976's 2671 sender) and +0x188 0xAF0D60 (a thunk to the do-step sender). 24 calls
// kept: the component's actor key, r8 (the target) and its key, r9 (the index in its low 16 bits), the return address
// and the exe addresses on the stack above (a scan: stale values too).
struct ReqSeen {
    std::atomic<uint32_t> ready{0};
    std::atomic<bool> said{false};
    uint32_t which = 0, tid = 0, self_key = 0, target_key = 0;
    uintptr_t self = 0, r8 = 0, ret = 0;
    uint64_t r9 = 0, at_us = 0;
    uintptr_t stack[12] = {};
};
ReqSeen g_req[24];
std::atomic<uint32_t> g_req_n{0};
std::atomic<uint64_t> g_req_calls[2]{};
SafetyHookMid g_req_hook[2];
constexpr uintptr_t kReqAt[2] = {0xAF1530, 0xAF0D60};
constexpr uint8_t kReqBytes0[5] = {0x48, 0x89, 0x5C, 0x24, 0x10};  // mov [rsp+0x10],rbx
constexpr uint8_t kReqBytes1[5] = {0xE9, 0x7B, 0x83, 0xBE, 0x08};  // jmp exe+0x96D90E0
std::atomic<uint64_t> g_req_other{0};  // calls for a component whose actor is not a player character
template <int W>
void on_req(safetyhook::Context& ctx) noexcept {
    g_req_calls[W].fetch_add(1, std::memory_order_relaxed);
    uintptr_t actor = 0;
    uint32_t self_key = 0;
    if (ctx.rcx > 0x10000 && game::fast_read(ctx.rcx + 8, &actor, sizeof actor) && actor > 0x10000)
        game::fast_read(actor + kActor_Key, &self_key, sizeof self_key);
    if ((self_key >> 28) != 0xA) {
        g_req_other.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const uint32_t n = g_req_n.fetch_add(1, std::memory_order_relaxed);
    if (n >= 24) return;
    ReqSeen& r = g_req[n];
    r.which = W;
    r.tid = GetCurrentThreadId();
    r.self = ctx.rcx;
    r.self_key = self_key;
    r.r8 = ctx.r8;
    r.r9 = ctx.r9;
    r.at_us = p8_now_us();
    if (ctx.r8 > 0x10000) game::fast_read(ctx.r8 + kActor_Key, &r.target_key, sizeof r.target_key);
    game::fast_read(ctx.rsp, &r.ret, sizeof r.ret);
    size_t k = 0;
    for (uint32_t off = 8; off < 0x800 && k < 12; off += 8) {
        uintptr_t v = 0;
        if (!game::fast_read(ctx.rsp + off, &v, sizeof v)) break;
        if (v > g_base + 0x1000 && v < g_base + 0x10000000) r.stack[k++] = v;
    }
    r.ready.store(1, std::memory_order_release);
}
void req_install(const std::vector<gad::Section>& secs) {
    const bool a = p8_install_mid(g_base, secs, kReqAt[0], 5, kReqBytes0, &on_req<0>, g_req_hook[0], "GuestXInter",
                                  "the request sender +0x180 0xAF1530 (research)");
    const bool b = p8_install_mid(g_base, secs, kReqAt[1], 5, kReqBytes1, &on_req<1>, g_req_hook[1], "GuestXInter",
                                  "the do-step thunk +0x188 0xAF0D60 (research)");
    log::info("GuestXInter: the player's request senders {} / {} (3732 research, read only)", a ? "hooked" : "NOT hooked",
              b ? "hooked" : "NOT hooked");
}
void req_log() {
    for (ReqSeen& r : g_req) {
        if (!r.ready.load(std::memory_order_acquire) || r.said.exchange(true)) continue;
        std::string st;
        for (uintptr_t v : r.stack) {
            if (v) st += std::format(" exe+0x{:X}", v - g_base);
        }
        log::info("GuestXInter [request] {} at {} us, thread {} | component 0x{:X} of 0x{:08X} | r8 0x{:X} (key 0x{:08X}) | "
                  "r9 0x{:X} | ret exe+0x{:X} | stack:{}",
                  r.which ? "+0x188 do-step" : "+0x180 preemption step", r.at_us, r.tid, r.self, r.self_key, r.r8,
                  r.target_key, r.r9, r.ret ? r.ret - g_base : 0, st);
    }
}

DWORD WINAPI eval_log_thread(LPVOID) {
    uint64_t last = ~0ull;
    for (;;) {
        Sleep(10000);
        try {
            uint64_t h = g_eval_calls.load() ^ (g_eval_none.load() << 20);
            std::string list;
            for (const EvalSeen& s : g_eval_seen) {
                const uintptr_t vt = s.vt.load();
                if (!vt) continue;
                h ^= vt + s.n.load();
                list += std::format(" | vtable exe+0x{:X} x{} (e.g. stage 0x{:X}, entry 0x{:X}, evaluator 0x{:X})",
                                    vt - g_base, s.n.load(), s.id.load(), s.entry.load(), s.ev.load());
            }
            {
                std::string callers;
                for (const CallerSeen& c : g_lookup_callers) {
                    const uintptr_t r = c.ret.load();
                    if (!r) continue;
                    callers += std::format(" | exe+0x{:X} x{} (id 0x{:X})", r - g_base, c.n.load(), c.id.load());
                    h ^= r + c.n.load();
                }
                uint32_t nid = 0;
                for (const auto& s : g_xids) nid += s.load() ? 1 : 0;
                h ^= g_lookup_m_x.load() * 31 + g_lookup_x.load();
                if (g_lookup_hook)
                    log::info("GuestXInter [lookups] X's ids known {} | looked up in M {} (callers:{}) | in X or another "
                              "manager {}", nid, g_lookup_m_x.load(), callers.empty() ? " none" : callers,
                              g_lookup_x.load());
            }
            req_log();
            if (g_req_hook[0] || g_req_hook[1]) {
                const uint64_t rq = g_req_calls[0].load() * 1000003 + g_req_calls[1].load();
                static uint64_t last_rq = ~0ull;
                if (rq != last_rq) {
                    last_rq = rq;
                    log::info("GuestXInter [requests] +0x180 calls {} | +0x188 calls {} | of a non-player component {} | "
                              "player calls kept {}", g_req_calls[0].load(), g_req_calls[1].load(), g_req_other.load(),
                              g_req_n.load() < 24 ? g_req_n.load() : 24u);
                }
            }
            for (EntryDump& d : g_entry_dump) {
                if (!d.ready.load(std::memory_order_acquire) || d.said.exchange(true)) continue;
                std::string hex;
                for (size_t i = 0; i < sizeof d.bytes; ++i) hex += std::format("{}{:02X}", i && i % 8 == 0 ? " | " : (i ? " " : ""), d.bytes[i]);
                log::info("GuestXInter [entry bytes] 0x{:X}: {}", d.at.load(), hex);
            }
            if (h == last) continue;
            last = h;
            log::info("GuestXInter evaluators (3732 research): calls at 0x2360A4D {} | without an evaluator {}{}",
                      g_eval_calls.load(), g_eval_none.load(), list);
        } catch (...) {
        }
    }
}

// V 0x2360A4D (mov r10,[r13+8] ; test r10,r10): the entry's evaluator. A call A redirected whose entry has none is
// accepted: BL = 1, on to the tail every result takes (mode 1); probe counts it.
void on_v(safetyhook::Context& ctx) noexcept {
    if (!g_v_on.load(std::memory_order_acquire)) return;
    if (g_research.load(std::memory_order_relaxed)) {
        g_eval_calls.fetch_add(1, std::memory_order_relaxed);
        uintptr_t ev0 = 0;
        uint64_t id0 = 0;
        if (game::fast_read(ctx.r13 + 8, &ev0, sizeof ev0) && ev0) {
            game::fast_read(ctx.r13 + isl::kEntry_StageId, &id0, sizeof id0);
            eval_note(ctx.r13, ev0, static_cast<uint32_t>(id0 & 0x1FFFFFFFull));
        } else {
            g_eval_none.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (!t_call.same(ctx.r13, ctx.rsp)) return;
    if (static_cast<Act>(t_call.act) != Act::Redirect) return;
    g_v_seen.fetch_add(1, std::memory_order_relaxed);
    if (g_research.load(std::memory_order_relaxed)) entry_note(ctx.r13);
    uintptr_t ev = 0;
    if (!game::fast_read(ctx.r13 + 8, &ev, sizeof ev)) return;  // unreadable: the native path
    if (ev) {
        g_v_own.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_v_last_id.store(static_cast<uint32_t>(t_call.id & 0xFFFFFFFFu), std::memory_order_relaxed);
    if (static_cast<Mode>(g_mode.load(std::memory_order_relaxed)) != Mode::On || !g_v_fix.load(std::memory_order_relaxed)) {
        g_v_would.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_v_accepted.fetch_add(1, std::memory_order_relaxed);
    ctx.rbx = (ctx.rbx & ~0xFFull) | 1u;  // BL = 1: accepted
    ctx.rip = g_base + isl::kTailAt;
}

template <int K>
void on_gate(safetyhook::Context& ctx) noexcept {
    if (!g_gates_on.load(std::memory_order_acquire) || !t_call.same(ctx.r13, ctx.rsp)) return;
    g_gate_seen[K].fetch_add(1, std::memory_order_relaxed);
    t_gate = static_cast<uint8_t>(K + 1);
    if constexpr (K == 0) {
        uint32_t busy = 0;
        if (game::fast_read(ctx.rdi + isl::kMgrBusy, &busy, sizeof busy)) {
            g_busy_last.store(busy, std::memory_order_relaxed);
            if (busy) g_busy_nonzero.fetch_add(1, std::memory_order_relaxed);
        }
    } else if constexpr (K == 1) {
        uint32_t cond = 0;
        if (game::fast_read(ctx.r14 + 0x78, &cond, sizeof cond)) {
            g_cond_last.store(cond, std::memory_order_relaxed);
            if (cond == 0xFFFFFFFFu) g_cond_minus1.fetch_add(1, std::memory_order_relaxed);
        }
    }
}
using GateFn = void (*)(safetyhook::Context&) noexcept;
constexpr GateFn kGateFns[5] = {&on_gate<0>, &on_gate<1>, &on_gate<2>, &on_gate<3>, &on_gate<4>};

template <int K>
void on_early(safetyhook::Context& ctx) noexcept {
    if (!g_early_on.load(std::memory_order_acquire)) return;
    g_early_seen[K].fetch_add(1, std::memory_order_relaxed);
    if constexpr (K == 0) {
        uint64_t id = 0;
        if (game::fast_read(ctx.rcx + isl::kEntry_StageId, &id, sizeof id)) {
            if (id & 0x1FFFFFFFull) g_early_last_id.store(static_cast<uint32_t>(id & 0x1FFFFFFFull), std::memory_order_relaxed);
            else g_early_no_id.fetch_add(1, std::memory_order_relaxed);
        }
        uint32_t key = 0;
        if (game::fast_read(ctx.rdx + isl::kUser_Key, &key, sizeof key)) {
            g_early_last_key.store(key, std::memory_order_relaxed);
            if (key == 0xA0100066u) g_early_own.fetch_add(1, std::memory_order_relaxed);
        }
    }
}
constexpr GateFn kEarlyFns[4] = {&on_early<0>, &on_early<1>, &on_early<2>, &on_early<3>};

template <bool Add>
void on_node(safetyhook::Context& ctx) noexcept {
    if (!g_nodes_on.load(std::memory_order_acquire)) return;
    const uintptr_t ic = ctx.rcx;
    const uintptr_t scene = rdp(rdp(ctx.rbp + 0x858));
    const uint64_t id = scene ? rdv<uint64_t>(scene + 0x30) : 0;
    const uintptr_t mgr = scene ? rdp(scene + 0x28) : 0;
    const uint16_t idx = Add ? static_cast<uint16_t>(ctx.r8 & 0xFFFF) : rdv<uint16_t>(rdp(ctx.rbp + 0x850) + 0x38);
    uintptr_t xs[kMaxX] = {};
    uint32_t keys[kMaxX] = {};
    const size_t nx = g_x.snapshot(xs, keys, kMaxX);
    uint64_t from = 2;  // another manager
    if (mgr && mgr == g_last_m.load(std::memory_order_relaxed)) from = 0;
    for (size_t i = 0; i < nx; ++i) {
        if (mgr && mgr == xs[i]) from = 1;
    }
    (Add ? g_nodes_add : g_nodes_rem).fetch_add(1, std::memory_order_relaxed);
    g_nodes_from[from].fetch_add(1, std::memory_order_relaxed);
    if constexpr (Add) {
        if (ic) {
            bool have = false;
            for (const auto& s : g_ic_seen) have = have || s.load(std::memory_order_relaxed) == ic;
            if (!have) g_ic_seen[g_ic_next.fetch_add(1, std::memory_order_relaxed) % 8].store(ic, std::memory_order_relaxed);
        }
    }
    p::Event e;
    e.v[0] = ic;
    e.v[1] = idx;
    e.v[2] = id;
    e.v[3] = mgr;
    e.v[4] = from;
    e.v[5] = ic ? rdv<uint32_t>(ic + 0x98) : 0;
    emit(Add ? kEN : kER, e);
}
// Every 5 s (the diagnostics thread): the dynamic lists of the components the nodes touched, when they changed.
void dump_components() {
    static uint64_t last[8] = {};
    for (int k = 0; k < 8; ++k) {
        const uintptr_t ic = g_ic_seen[k].load(std::memory_order_relaxed);
        if (!ic) continue;
        const uintptr_t arr = rdp(ic + 0x90);
        const uint32_t n = rdv<uint32_t>(ic + 0x98);
        std::string list;
        uint64_t h = 0x100000001B3ull ^ n;
        for (uint32_t i = 0; arr && i < n && i < 16; ++i) {
            const uintptr_t en = arr + i * 0x38ull;
            const uint16_t idx = rdv<uint16_t>(en + 0x10);
            const uint64_t sid = rdv<uint64_t>(en + 0x20);
            h = (h * 1000003ull) ^ (static_cast<uint64_t>(idx) * 0x9E3779B97F4A7C15ull) ^ sid;
            list += std::format(" 0x{:X}@0x{:X}", idx, sid);
        }
        if (h == last[k]) continue;
        last[k] = h;
        if (g_lines.fetch_add(1) < kMaxLines)
            log::info("GuestXInter [component] 0x{:X}: {} dynamic entr(ies) (index@stage id):{}", ic, n, list);
    }
}

// F 0x2126D11: BL = the function's result for the call A followed.
void on_f(safetyhook::Context& ctx) noexcept {
    if (!g_f_on.load(std::memory_order_acquire)) return;
    if (!t_call.same(ctx.r13, ctx.rsp)) return;
    g_f_seen.fetch_add(1, std::memory_order_relaxed);
    const uint8_t bl = static_cast<uint8_t>(ctx.rbx & 0xFF);
    if (bl == 0) g_refused_after[t_gate <= 5 ? t_gate : 0].fetch_add(1, std::memory_order_relaxed);
    t_gate = 0;
    const auto a = static_cast<Act>(t_call.act);
    const auto v = static_cast<Verdict>(t_call.verdict);
    const size_t c = act_class(a, v);
    g_results[c][bl <= 2 ? bl : 3].fetch_add(1, std::memory_order_relaxed);
    const gxi::Call call = t_call;
    const uintptr_t found = t_r_set ? t_r_found : ~0ull, used = t_r_set ? t_r_used : 0;
    t_call = gxi::Call{};
    t_r_set = false;
    const auto tag = static_cast<uint8_t>(0x40u | static_cast<unsigned>(c) << 2 | (bl <= 2 ? bl : 3u));
    if (!g_book.take(book_key(call.id, tag), kPerIdVerdict)) {
        return;
    }
    p::Event e;
    e.v[0] = static_cast<uint64_t>(a) | static_cast<uint64_t>(bl) << 8 | static_cast<uint64_t>(v) << 16;
    e.v[1] = call.entry;
    e.v[2] = call.id;
    e.v[3] = used;
    e.v[4] = found;
    e.v[5] = call.m;
    e.v[6] = call.x;
    emit(kEF, e);
}

// ---------------------------------------------------------------- lines (diagnostics thread)

std::string line_of(const p::Event& e) {
    const uint64_t* v = e.v;
    const std::string when = p8_when(e.qpc_us);
    switch (e.kind) {
        case kEA: {
            const auto verdict = static_cast<Verdict>(v[0] & 0xFF);
            const auto a = static_cast<Act>((v[0] >> 8) & 0xFF);
            char name[32];
            gxv::stage_name(v + 9, 3, name, sizeof name);
            const char* what = a == Act::Redirect ? "REDIRECTED (RAX := X: the lookup at 0x2126A6A uses X)"
                               : a == Act::Would  ? "WOULD REDIRECT (probe: the native lookup in M refuses it)"
                                                  : "left native";
            return std::format(
                "GuestXInter [entry] {} T{} | stage id 0x{:X} of entry 0x{:X}: {} | M 0x{:X} (character 0x{:08X}; "
                "read {}) | X 0x{:X} (character 0x{:08X}) instance 0x{:X} (its +0x30 0x{:X}, name '{}') | X known {} "
                "valid {} (read {}{}) | {}",
                when, e.tid, v[2], v[1], gxi::verdict_name(verdict), v[3], static_cast<uint32_t>(v[6]),
                (v[7] >> 32) & 1 ? "ok" : "FAILED", v[4], static_cast<uint32_t>(v[6] >> 32), v[5], v[8], name,
                v[7] & 0xFFFF, (v[7] >> 16) & 0xFFFF, (v[7] >> 33) & 1 ? "ok" : "no", (v[7] >> 34) & 1 ? ", dead there" : "",
                what);
        }
        case kEN:
        case kER: {
            const char* from = v[4] == 0 ? "this client's own M" : v[4] == 1 ? "the host's X" : "another manager";
            return std::format("GuestXInter [node {}] {} T{} | component 0x{:X} index 0x{:X} | scene 0x{:X} of manager "
                               "0x{:X} ({}) | dynamic entries now {}",
                               e.kind == kEN ? "ADD" : "REMOVE", when, e.tid, v[0], v[1], v[2], v[3], from, v[5]);
        }
        case kEF: {
            const auto a = static_cast<Act>(v[0] & 0xFF);
            const auto bl = static_cast<uint8_t>((v[0] >> 8) & 0xFF);
            const auto verdict = static_cast<Verdict>((v[0] >> 16) & 0xFF);
            const bool r_seen = v[4] != ~0ull;
            return std::format(
                "GuestXInter [result] {} T{} | stage id 0x{:X} of entry 0x{:X} ({}; {}): the lookup used manager 0x{:X} "
                "({}) and {} | 0x2126920 returned {}",
                when, e.tid, v[2], v[1], gxi::verdict_name(verdict),
                a == Act::Redirect ? "REDIRECTED" : a == Act::Would ? "would redirect" : "native",
                v[3], v[3] == v[6] && v[6] ? "X" : v[3] == v[5] ? "M" : "?",
                !r_seen ? "R did not run" : v[4] ? std::format("found instance 0x{:X}", v[4]) : std::string("found nothing"),
                gxi::result_name(bl));
        }
        default:
            return std::format("GuestXInter: event {}?", e.kind);
    }
}

uint64_t g_tick_n = 0;  // diagnostics thread only

std::string results_of(size_t c) {
    return std::format("0:{} 1:{} 2:{} other:{} (native lookup found {} / nothing {})", g_results[c][0].load(),
                       g_results[c][1].load(), g_results[c][2].load(), g_results[c][3].load(), g_r_found[c].load(),
                       g_r_missing[c].load());
}

void summary() {
    std::string verdicts;
    for (size_t k = 0; k < static_cast<size_t>(Verdict::Count); ++k) {
        if (const uint64_t n = g_verdicts[k].load()) {
            verdicts += std::format(" {} {};", gxi::verdict_name(static_cast<Verdict>(k)), n);
        }
    }
    std::string xs;
    uintptr_t x[kMaxX] = {};
    uint32_t keys[kMaxX] = {};
    const size_t n = g_x.snapshot(x, keys, kMaxX);
    for (size_t i = 0; i < n; ++i) xs += std::format(" 0x{:X} (0x{:08X})", x[i], keys[i]);
    std::string states;
    for (size_t k = 0; k < static_cast<size_t>(gxi::XState::Count); ++k) {
        if (const uint64_t c = g_x_states[k].load()) {
            states += std::format(" {} {};", gxi::xstate_name(static_cast<gxi::XState>(k)), c);
        }
    }
    log::info("GuestXInter summary (guest_x_interaction_stage={}): hooks A {} R {} F {} | calls at A {} | verdicts:{} | "
              "REDIRECTED {}, would {} | results of the redirected: {} | of the would-redirect: {} | of the other "
              "entries of nobody known: {} | followed {} (R seen {}, F seen {}) | X known:{} (notes {}, replaced {}; "
              "checks:{}) | ids in the book {} (full {}) | limited {} lost {}",
              gxi::mode_name(static_cast<Mode>(g_mode.load())), g_a_on.load() ? "in" : "NOT in",
              g_r_on.load() ? "in" : "NOT in", g_f_on.load() ? "in" : "NOT in", g_calls.load(),
              verdicts.empty() ? " none" : verdicts, g_redirected.load(), g_would.load(), results_of(0), results_of(1),
              results_of(2), g_followed.load(), g_r_seen.load(), g_f_seen.load(), xs.empty() ? " none" : xs,
              g_x.notes(), g_x.replaced(), states.empty() ? " none" : states, g_book.used(), g_book.full(),
              g_limited.load(), g_lost.load());
    if (g_v_on.load())
        log::info("GuestXInter evaluator (guest_x_eval_fix={}): redirected calls at the evaluator test {} | the entry had "
                  "its own {} | none: ACCEPTED {}, would accept {} (refused as the game does) | last stage id 0x{:X}",
                  g_v_fix.load() ? 1 : 0, g_v_seen.load(), g_v_own.load(), g_v_accepted.load(), g_v_would.load(),
                  g_v_last_id.load());
    if (g_nodes_on.load())
        log::info("GuestXInter nodes (3732 research): ADD {} REMOVE {} | from this client's own M {}, the host's X {}, "
                  "another manager {}",
                  g_nodes_add.load(), g_nodes_rem.load(), g_nodes_from[0].load(), g_nodes_from[1].load(),
                  g_nodes_from[2].load());
    if (g_early_on.load())
        log::info("GuestXInter early (3732 research, every call of the prompt check): entered {} (the joined player's own "
                  "J 0xA0100066: {}; no stage id: {}; last id 0x{:X}, last J key 0x{:08X}) | past the stage-id / user "
                  "returns {} | past [rbp-0x80] (0x2299FB0) {} | past [rsp+0x50] (0x19A14B0) {} | at A {}",
                  g_early_seen[0].load(), g_early_own.load(), g_early_no_id.load(), g_early_last_id.load(),
                  g_early_last_key.load(), g_early_seen[1].load(), g_early_seen[2].load(), g_early_seen[3].load(),
                  g_calls.load());
    if (g_gates_on.load())
        log::info("GuestXInter gates (3732 research): reached P0 {} (instance +0x168 not 0: {}, last {}) P1 {} (cond id "
                  "-1: {}, last 0x{:X}) P2 {} P3 {} P4 {} | refused with no gate reached {}, after P0 {} (the instance's "
                  "+0x168, or its dead check 0x21CABF0 in X), after P1 {} (the player's condition 0x25B7F30), after P2 {} (the condition list), "
                  "after P3 {} (0x1C9FBB0), after P4 {} (later)",
                  g_gate_seen[0].load(), g_busy_nonzero.load(), g_busy_last.load(), g_gate_seen[1].load(),
                  g_cond_minus1.load(), g_cond_last.load(), g_gate_seen[2].load(), g_gate_seen[3].load(),
                  g_gate_seen[4].load(), g_refused_after[0].load(), g_refused_after[1].load(), g_refused_after[2].load(),
                  g_refused_after[3].load(), g_refused_after[4].load(), g_refused_after[5].load());
}

// ---------------------------------------------------------------- install

bool bytes_at(uintptr_t rva, std::span<const uint8_t> expected) {
    std::vector<uint8_t> actual(expected.size());
    return game::safe_read(g_base + rva, actual.data(), actual.size()) &&
           std::equal(actual.begin(), actual.end(), expected.begin());
}

bool context_ok(std::span<const gad::Section> secs, uintptr_t rva, std::span<const uint8_t> bytes, const char* what) {
    if (gad::in_real_code(secs, static_cast<uint32_t>(rva), static_cast<uint32_t>(bytes.size())) && bytes_at(rva, bytes)) {
        return true;
    }
    log::error("GuestXInter: {} (exe+0x{:X}, {} bytes) is not build 2976's .rsrc code; REFUSED", what, rva, bytes.size());
    return false;
}

DWORD WINAPI install_thread(LPVOID) {
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::vector<gad::Section> secs;
    if (g_base != off::kImageBase || !game::safe_read(g_base, &dos, sizeof dos) ||
        !game::safe_read(g_base + dos.e_lfanew, &nt, sizeof nt) || nt.FileHeader.TimeDateStamp != off::kPeTimestamp ||  /* Co-op+: ported to 3576 (stage_lookup_logic.h) */
        !p8_exe_sections(g_base, secs)) {
        log::error("GuestXInter: unsupported game build or unreadable sections; nothing installed");
        return 0;
    }
    for (int i = 0; i < 1200; ++i) {  // WorldSystem (up to 10 minutes), as the other guest modules
        uintptr_t world = 0;
        if (game::safe_read(g_base + off::kWorldSystemSlot, &world, sizeof world) && world) break;
        Sleep(500);
    }
    // R and F first (read only), then A (the decision): A's record is read by R and F.
    if (context_ok(secs, isl::kCtxLookup, isl::kCtxLookupBytes, "the lookup call 0x2126A5C") &&
        context_ok(secs, isl::kCtxA, isl::kCtxABytes, "the vf48 call 0x2126A14")) {
        if (p8_install_mid(g_base, secs, isl::kPointR, isl::kPatchR, isl::kBytesR, on_r, g_r_hook, "GuestXInter",
                           "R, after the lookup 0x2126AAB")) {
            g_r_on.store(true, std::memory_order_release);
        }
        if (p8_install_mid(g_base, secs, isl::kPointF, isl::kPatchF, isl::kBytesF, on_f, g_f_hook, "GuestXInter",
                           "F, the result 0x2126D11")) {
            g_f_on.store(true, std::memory_order_release);
        }
        // V only for research or guest_x_eval_fix (2026-11-21: the joined player's shop works through ShopTwin's own
        // entries, which an accepted host entry would hide; release builds leave the game's refusal untouched).
        if ((g_research.load() || g_v_fix.load()) &&
            context_ok(secs, isl::kTailAt, isl::kTailBytes, "the result's tail 0x2360B7C") &&
            p8_install_mid(g_base, secs, isl::kEvalAt, isl::kEvalPatch, isl::kEvalBytes, on_v, g_v_hook, "GuestXInter",
                           "V, the entry's evaluator test 0x2360A4D")) {
            g_v_on.store(true, std::memory_order_release);
        }
        log::info("GuestXInter: the evaluator test {} (a redirected entry without an evaluator is {})",
                  g_v_on.load() ? "hooked" : "NOT hooked",
                  static_cast<Mode>(g_mode.load()) == Mode::On && g_v_fix.load()
                      ? "accepted (guest_x_eval_fix=1)"
                      : "counted only (guest_x_eval_fix=0: refused as the game does)");
        if (g_research.load()) {
            static const uint8_t kLookupBytes[8] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x4C, 0x89, 0x44};
            p8_install_mid(g_base, secs, kLookupAt, 5, kLookupBytes, on_lookup, g_lookup_hook, "GuestXInter",
                           "the stage lookup's entry 0x21C7370 (research)");
            req_install(secs);
            CloseHandle(CreateThread(nullptr, 0, eval_log_thread, nullptr, 0, nullptr));
        }
        if (g_research.load()) {  // the gates (research, read only): all five or none
            const std::span<const uint8_t> bytes[5] = {isl::kGate0, isl::kGate1, isl::kGate2, isl::kGate3, isl::kGate4};
            int in = 0;
            for (int k = 0; k < 5; ++k) {
                if (p8_install_mid(g_base, secs, isl::kGateAt[k], isl::kGatePatch[k], bytes[k], kGateFns[k],
                                   g_gate_hook[k], "GuestXInter", "a gate between R and F"))
                    ++in;
            }
            if (in == 5) g_gates_on.store(true, std::memory_order_release);
            else for (SafetyHookMid& h : g_gate_hook) h = {};
            log::info("GuestXInter: the gates between R and F {} (3732 research, read only)",
                      in == 5 ? "hooked" : "NOT all hooked: off");
            const std::span<const uint8_t> eb[4] = {isl::kEarly0, isl::kEarly1, isl::kEarly2, isl::kEarly3};
            int ein = 0;
            for (int k = 0; k < 4; ++k) {
                if (p8_install_mid(g_base, secs, isl::kEarlyAt[k], isl::kEarlyPatch[k], eb[k], kEarlyFns[k],
                                   g_early_hook[k], "GuestXInter", "an early counter of the prompt check"))
                    ++ein;
            }
            if (ein == 4) g_early_on.store(true, std::memory_order_release);
            else for (SafetyHookMid& h : g_early_hook) h = {};
            log::info("GuestXInter: the early counters {} (3732 research, read only)",
                      ein == 4 ? "hooked" : "NOT all hooked: off");
            static const uint8_t kAddBytes[5] = {0xE8, 0xAF, 0xBE, 0xD8, 0xFF}, kRemBytes[5] = {0xE8, 0xA3, 0xC0, 0xD8, 0xFF};
            const bool na = p8_install_mid(g_base, secs, kNodeAdd, 5, kAddBytes, &on_node<true>, g_node_add_hook,
                                           "GuestXInter", "the node's ADD call");
            const bool nr = p8_install_mid(g_base, secs, kNodeRemove, 5, kRemBytes, &on_node<false>, g_node_rem_hook,
                                           "GuestXInter", "the node's REMOVE call");
            if (na && nr) g_nodes_on.store(true, std::memory_order_release);
            else {
                g_node_add_hook = {};
                g_node_rem_hook = {};
            }
            log::info("GuestXInter: the node hooks {} (3732 research, read only)", na && nr ? "hooked" : "NOT hooked: off");
        }
        if (p8_install_mid(g_base, secs, isl::kPointA, isl::kPatchA, isl::kBytesA, on_a, g_a_hook, "GuestXInter",
                           "A, the manager of the lookup 0x2126A1F")) {
            g_a_on.store(true, std::memory_order_release);
        }
    }
    log::info("GuestXInter (guest_x_interaction_stage={}): A 0x2126A1F {}, R 0x2126AAB {}, F 0x2126D11 {} | an entry "
              "whose stage id is not a live stage of M but is one of a known X (another character's "
              "ClientSequencerStageManager, vtable exe+0x{:X}): {}",
              gxi::mode_name(static_cast<Mode>(g_mode.load())), g_a_on.load() ? "hooked" : "REFUSED",
              g_r_on.load() ? "hooked" : "REFUSED", g_f_on.load() ? "hooked" : "REFUSED", isl::kVtClientStageMgr,
              static_cast<Mode>(g_mode.load()) == Mode::On ? "RAX := X (the prompt check looks it up in X)"
                                                           : "decided and counted only (probe)");
    return 0;
}

}  // namespace

// HOST, research (guest_x_research=1): the evaluator probe on the host's own client (see on_host_eval).
void guest_x_interaction_host_probe_start() {
    static std::atomic<bool> once{false};
    if (once.exchange(true)) return;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::vector<gad::Section> secs;
    if (!p8_exe_sections(g_base, secs)) {
        log::warn("GuestXInter host probe: sections unreadable; off");
        return;
    }
    const bool ok = p8_install_mid(g_base, secs, isl::kEvalAt, isl::kEvalPatch, isl::kEvalBytes, on_host_eval,
                                   g_host_eval_hook, "GuestXInter", "the host's evaluator probe 0x2360A4D");
    req_install(secs);
    CloseHandle(CreateThread(nullptr, 0, eval_log_thread, nullptr, 0, nullptr));
    log::info("GuestXInter host probe (3732 research, read only): the evaluators of the host's own entries {}",
              ok ? "hooked" : "NOT hooked");
}

void guest_x_interaction_start(const std::string& mode, const std::string& route_mode, bool research, bool eval_fix) {
    g_research.store(research);
    g_v_fix.store(eval_fix);
    bool bad = false;
    Mode m = gxi::parse_mode(mode, &bad);
    if (bad) log::warn("GuestXInter: guest_x_interaction_stage={} is not 0, probe or 1: off", mode);
    if (m != Mode::Off && gxv::parse_mode(route_mode) != gxv::Mode::On) {
        log::info("GuestXInter: guest_x_interaction_stage={} not installed: guest_seq_route is not 1, so no other "
                  "character's manager X exists on this client",
                  mode);
        m = Mode::Off;
    }
    g_mode.store(static_cast<uint8_t>(m));
    if (m == Mode::Off) return;
    if (g_started.exchange(true)) return;
    CloseHandle(CreateThread(nullptr, 0, install_thread, nullptr, 0, nullptr));
}

void guest_x_interaction_tick() noexcept {
    if (!g_started.load()) return;
    try {
        ++g_tick_n;
        p::Event e;
        while (g_ring.pop(e)) {
            if (e.kind >= kKindCount) continue;
            const bool loud = e.kind == kEA && static_cast<Act>((e.v[0] >> 8) & 0xFF) == Act::Redirect;
            if (loud) {
                log::warn("{}", line_of(e));  // every redirect (rate-limited at the source), outside the line budget
            } else if (g_lines.fetch_add(1) < kMaxLines) {
                log::info("{}", line_of(e));
            }
        }
        if (g_nodes_on.load()) dump_components();
        if (g_tick_n % 12 == 0) summary();
    } catch (...) {
    }
}

void guest_x_interaction_note_xseq(uint64_t seq) noexcept {
    if (g_research.load(std::memory_order_relaxed)) xid_note(static_cast<uint32_t>(seq));
}

void guest_x_interaction_note_x(uintptr_t x, uint32_t key) noexcept {
    if (!x || !g_started.load(std::memory_order_relaxed)) return;
    x_conversation_check();
    g_x.note(x, key);
}

size_t guest_x_interaction_known_x(uintptr_t* xs, size_t cap) noexcept {
    if (!xs || !cap || !g_started.load(std::memory_order_relaxed)) return 0;
    x_conversation_check();
    uint32_t keys[kMaxX];
    return g_x.snapshot(xs, keys, cap < kMaxX ? cap : kMaxX);
}

}  // namespace cdmp::session2
