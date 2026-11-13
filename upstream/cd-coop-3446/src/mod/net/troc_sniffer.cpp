#include "mod/net/troc_sniffer.h"
#include "mod/native_layer.h"

#include <safetyhook.hpp>

#include <atomic>
#include <deque>

#include <share.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/byte_ring.h"
#include "mod/game/offsets.h"
#include "mod/game/rtti.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/game/game_errors.h"
#include "mod/net/revive_prompt.h"
#include "mod/net/slowmo.h"
#include "mod/net/local_capture.h"
#include "mod/net/loot_twin.h"
#include "mod/net/native_suppress.h"
#include "mod/net/remote_puppet.h"
#include "mod/net/weather_sync.h"

namespace cdmp::net {
namespace {
namespace off = game::build_3663;

enum class Dir : uint8_t { ClientToServer = 1, ServerToClient = 2 };

constexpr size_t kRingBytes = 64ull * 1024 * 1024;
constexpr uint16_t kMaxCopy = 32768;  // the game's packet buffers are 0x8000 bytes
constexpr size_t kPreviewBytes = 48;

// One record in the byte ring, followed by `copied` packet bytes. Records are
// 8-byte aligned; a record_len of 0 marks "wrap to the start of the ring".
#pragma pack(push, 1)
struct RecordHeader {
    uint32_t record_len;  // header + data, aligned
    uint32_t session;
    uint64_t t_us;
    uint32_t thread;
    uint16_t size;    // size the game reported
    uint16_t copied;  // bytes that follow (== size unless > kMaxCopy)
    Dir dir;
    uint8_t pad[7];
};
#pragma pack(pop)
static_assert(sizeof(RecordHeader) == 32);

// Shared by game threads (producers: any thread that posts a packet, plus the
// client NetworkThread) and our writer (consumer). Producers hold the SRW lock
// only for one memcpy; see ByteRing for the consumer protocol. If the writer
// falls behind, packets are dropped and counted, never blocking the game.
SRWLOCK g_lock = SRWLOCK_INIT;
ByteRing* g_ring = nullptr;
uint64_t g_dropped = 0;
uint64_t g_truncated = 0;
LARGE_INTEGER g_qpc_freq;

SafetyHookMid g_post_hook;
SafetyHookMid g_recv_hook;

// No C++ objects here: SEH and destructors don't mix.
bool guarded_copy(void* dst, const void* src, size_t n) {
    __try {
        std::memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint64_t now_us() {
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    const uint64_t f = static_cast<uint64_t>(g_qpc_freq.QuadPart);
    const uint64_t t = static_cast<uint64_t>(q.QuadPart);
    return t / f * 1000000ull + t % f * 1000000ull / f;  // no overflow for long uptimes
}

// Co-op+ low latency: the writer waits for this as well as for its 10 ms tick; set for every packet of ours, so our
// movement reaches co-op (and the fast lane) at once instead of on the writer's next tick (up to 10 ms later).
HANDLE g_c2s_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);

void capture(Dir dir, uint32_t session, const uint8_t* pkt, uint64_t rsp) {
    uint16_t size = 0;
    if (!guarded_copy(&size, reinterpret_cast<const void*>(rsp + off::kHookArgSizeStackOffset),
                      sizeof size) ||
        !pkt || size < 5) {
        return;
    }
    RecordHeader h{};
    h.session = session;
    h.t_us = now_us();
    h.thread = GetCurrentThreadId();
    h.size = size;
    h.copied = std::min(size, kMaxCopy);
    h.dir = dir;
    h.record_len = static_cast<uint32_t>((sizeof h + h.copied + 7) & ~size_t{7});

    AcquireSRWLockExclusive(&g_lock);
    if (auto at = g_ring->reserve(h.record_len)) {
        // If the copy faults, the record is simply not committed (a wrap that
        // already happened stays valid: the consumer skips the marker).
        if (guarded_copy(g_ring->at(*at) + sizeof h, pkt, h.copied)) {
            std::memcpy(g_ring->at(*at), &h, sizeof h);
            g_ring->commit(h.record_len);
            if (h.copied < h.size) ++g_truncated;
        }
    } else {
        ++g_dropped;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (dir == Dir::ClientToServer && g_c2s_wake) SetEvent(g_c2s_wake);
}

// ClientNet::PostPacket(ClientNet*, int* result, u32 session, const u8* packet,
// u16 size, u8 channel, u16 flags) (combate-coop.md §4.2). The channel and
// flags of real calls are recorded per opcode (read-only) so that a packet of
// ours can be posted the same way.
using PostPacketFn = void(__fastcall*)(void*, int*, uint32_t, const uint8_t*, uint16_t, uint8_t, uint16_t);
PostPacketFn g_post_fn = nullptr;
std::atomic<uint32_t> g_post_args[4096];  // bit31 seen | flags << 8 | channel
std::atomic<uint32_t> g_post_args_any{0};  // last seen for any opcode
SRWLOCK g_c2s_lock = SRWLOCK_INIT;
struct QueuedC2S {
    uint64_t ms = 0;
    std::vector<uint8_t> packet;
};
std::deque<QueuedC2S> g_c2s_queue;
// Our packets wait for the game's next real one; one that waited this long
// (a menu, a loading screen) names actors of a world that may be gone
// (pendiente-robustez.md §2.4): dropped.
constexpr uint64_t kMaxC2SWaitMs = 3000;
thread_local bool t_posting = false;

// The world's loading (the tunnel walk into the world): from the game's
// GameLoadingStartReq (C>S 2437, payload 0) to 1.5 s after its
// LoadingCompleteAck (S>C 2762), nothing of ours goes in either way: they
// wait in their queues (analisis-ia2/P5: the horse copy's action went in
// mid-load; both of Shadow's crashes were in the tunnel). At most 120 s.
// Co-op+: 3460 numbers from the live Troc table (were 2976-era 2437 and 2762).
constexpr uint16_t kOpGameLoadingStartReq = 2350, kOpLoadingCompleteAck = 2891;
constexpr uint64_t kAfterLoadingMs = 1500, kMaxLoadingMs = 120000;
std::atomic<bool> g_loading{false};
std::atomic<uint64_t> g_loading_start_ms{0}, g_loading_done_ms{0};
std::atomic<uint64_t> g_loadings{0};

bool world_loading() {
    if (!g_loading.load(std::memory_order_acquire)) return false;
    const uint64_t done = g_loading_done_ms.load(std::memory_order_acquire);
    const uint64_t start = g_loading_start_ms.load(std::memory_order_acquire);
    const uint64_t now = GetTickCount64();  // after the loads: never before them
    const bool over = done ? now >= done && now - done >= kAfterLoadingMs
                           : now >= start && now - start >= kMaxLoadingMs;
    if (over) g_loading.store(false, std::memory_order_release);
    return !over;
}

bool guarded_post(void* net, uint32_t session, const uint8_t* pkt, uint16_t size, uint8_t channel,
                  uint16_t flags) {
    __try {
        int result = 0;
        g_post_fn(net, &result, session, pkt, size, channel, flags);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool read_post_args(uint64_t rsp, uint8_t& channel, uint16_t& flags) {
    __try {
        channel = *reinterpret_cast<const uint8_t*>(rsp + 0x30);
        flags = *reinterpret_cast<const uint16_t*>(rsp + 0x38);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Co-op+ dev ([dev] trace_op): code addresses on the game's stack when it sends that opcode (first 3 times): who
// builds the message (research: weather, cutscenes).
std::atomic<uint16_t> g_trace_op{0};
std::atomic<int> g_traced{0};

void trace_stack(uint16_t op, uint64_t rsp) {
    if (g_traced.fetch_add(1) >= 3) return;
    uint64_t st[160] = {};
    if (!guarded_copy(st, reinterpret_cast<const void*>(rsp), sizeof st)) return;
    const auto base = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
    std::string code;
    for (int k = 0; k < 160; ++k) {
        if (st[k] > base + 0x1000 && st[k] < base + 0x6000000) code += std::format(" +0x{:X}:exe+0x{:X}", k * 8, st[k] - base);
    }
    log::info("Sniffer: trace of op {}:{}", op, code);
}

void on_post_packet(safetyhook::Context& ctx) {
    capture(Dir::ClientToServer, static_cast<uint32_t>(ctx.r8),
            reinterpret_cast<const uint8_t*>(ctx.r9), ctx.rsp);
    if (t_posting) return;
    // Looting a copy of the other player's enemy loots its twin (loot_twin.h).
    if (uint8_t head[5]; guarded_copy(head, reinterpret_cast<const void*>(ctx.r9), sizeof head)) {
        uint16_t len = 0;
        std::memcpy(&len, head + 3, 2);
        loot_rewrite_c2s(reinterpret_cast<uint8_t*>(ctx.r9), static_cast<uint16_t>(5 + len));
        puppet_scrub_c2s(reinterpret_cast<uint8_t*>(ctx.r9), static_cast<uint16_t>(5 + len));  // Co-op+ (T21)
    }
    uint8_t channel = 0;
    uint16_t flags = 0;
    uint16_t op = 0;
    if (!read_post_args(ctx.rsp, channel, flags) || !guarded_copy(&op, reinterpret_cast<const void*>(ctx.r9), 2)) {
        return;
    }
    if (op == g_trace_op.load(std::memory_order_relaxed)) trace_stack(op, ctx.rsp);
    const uint32_t packed = 0x80000000u | (static_cast<uint32_t>(flags) << 8) | channel;
    if (op < 4096) g_post_args[op].store(packed, std::memory_order_relaxed);
    g_post_args_any.store(packed, std::memory_order_relaxed);
    if (op == kOpGameLoadingStartReq) {
        g_loading_start_ms.store(GetTickCount64(), std::memory_order_release);
        g_loading_done_ms.store(0, std::memory_order_release);
        g_loading.store(true, std::memory_order_release);
        log::info("World: loading {} started (GameLoadingStartReq): our packets wait until it is over",
                  g_loadings.fetch_add(1, std::memory_order_relaxed) + 1);
    }

    // Our own packets go in before this real one, on the same thread (not
    // while the world loads: they wait, and those waiting too long go).
    if (world_loading()) return;
    if (!g_post_fn || !TryAcquireSRWLockExclusive(&g_c2s_lock)) return;
    std::deque<QueuedC2S> mine;
    mine.swap(g_c2s_queue);
    ReleaseSRWLockExclusive(&g_c2s_lock);
    const uint64_t now = GetTickCount64();
    for (auto& [queued_ms, pkt] : mine) {
        uint16_t pop = 0;
        std::memcpy(&pop, pkt.data(), 2);
        if (now - queued_ms > kMaxC2SWaitMs) {
            log::info("Inject C>S: opcode {} dropped, it waited {} ms", pop, now - queued_ms);
            continue;
        }
        const uint32_t a = pop < 4096 && g_post_args[pop].load() ? g_post_args[pop].load() : packed;
        t_posting = true;
        const bool ok = guarded_post(reinterpret_cast<void*>(ctx.rcx), static_cast<uint32_t>(ctx.r8),
                                     pkt.data(), static_cast<uint16_t>(pkt.size()),
                                     static_cast<uint8_t>(a & 0xFF), static_cast<uint16_t>((a >> 8) & 0xFFFF));
        t_posting = false;
        log::info("Inject C>S: opcode {} ({} bytes, channel {}, flags 0x{:X}) {}", pop, pkt.size(),
                  a & 0xFF, (a >> 8) & 0xFFFF, ok ? "posted" : "EXCEPTION");
        if (!ok) g_post_fn = nullptr;
    }
}

// ClientNet::OnRecvPacket(ClientNet*, NwSession*, ?, const u8* packet, u16 size).
using OnRecvPacketFn = void(__fastcall*)(void*, void*, uint64_t, const uint8_t*, uint16_t);
OnRecvPacketFn g_on_recv_fn = nullptr;
bool g_injection_enabled = false;
thread_local bool t_injecting = false;

// The opcodes this file matches against, resolved once at startup (see the check
// in install()). A game patch renumbers opcodes without renaming the handlers, so
// comparing against the header's frozen numbers silently stops matching; these
// carry the values the running game actually uses.
//
// Initialised to the header's numbers so that anything running before resolution
// -- and any build where resolution is unavailable -- behaves as it did before,
// rather than matching nothing at all.
uint16_t op_actor_load_ack = off::kOpActorLoadDataAck;
uint16_t op_remove_actor_ack = off::kOpRemoveActorAck;
uint16_t op_move_req = off::kOpMoveActorReq;
uint16_t op_move_ack = off::kOpMoveActorAck;
uint16_t op_start_action_req = off::kOpStartActionReq;
uint16_t op_start_action_ack = off::kOpStartActionAck;
uint16_t op_rotate_req = off::kOpRotateActorReq;
uint16_t op_rotate_ack = off::kOpRotateActorAck;

// SEH wrapper (no C++ objects): a malformed injected packet must not take the
// game down from inside its own packet code.
bool guarded_inject(void* net, void* session, uint64_t arg3, const uint8_t* pkt, uint16_t size) {
    __try {
        g_on_recv_fn(net, session, arg3, pkt, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A lone `ret` (as horse_diag's): resuming there returns from OnRecvPacket at
// once. The hook sits on its first instruction, so the stack is the caller's,
// and none of its four callers reads the result (fase5 §1.2, §9).
uint8_t* g_ret_stub = nullptr;
uint64_t g_skipped_injections = 0;

void on_recv_packet(safetyhook::Context& ctx) {
    capture(Dir::ServerToClient, 0, reinterpret_cast<const uint8_t*>(ctx.r9), ctx.rsp);
    if (t_injecting) return;
    // The end of a loading, seen even with injection off (the C>S gate needs it).
    if (uint16_t real = 0; guarded_copy(&real, reinterpret_cast<const void*>(ctx.r9), 2) &&
                           real == kOpLoadingCompleteAck && g_loading.load(std::memory_order_acquire)) {
        g_loading_done_ms.store(GetTickCount64(), std::memory_order_release);
        log::info("World: loading over (LoadingCompleteAck); our packets go in again in {} ms", kAfterLoadingMs);
    }
    if (!g_injection_enabled) return;
    const bool loading = world_loading();
    // Held meanwhile: overdue movement still goes (the queue would fill up and
    // turn away loads and removals, review of c787c08).
    if (loading) puppet_drop_stale_movement();

    // Experiment 4c: feed queued packets through the same function, on the
    // same thread, right before this real one. Re-entering hits this hook
    // again (and gets captured) but t_injecting stops the recursion.
    // At most this many per real packet: a flood from the other player must
    // not stall the game's network thread (the rest go in with the next ones).
    constexpr int kMaxPerCall = 64;
    std::vector<uint8_t> pkt;
    for (int taken = 0; !loading && taken < kMaxPerCall && puppet_take_injection(pkt); ++taken) {
        if (!suppress_allow_injection(pkt.data(), pkt.size())) {
            // A held native given back whose key the server removed meanwhile.
            if (g_skipped_injections++ < 20) log::info("Inject: a load given back skipped (the server removed it)");
            continue;
        }
        t_injecting = true;
        const bool ok = guarded_inject(reinterpret_cast<void*>(ctx.rcx),
                                       reinterpret_cast<void*>(ctx.rdx), ctx.r8, pkt.data(),
                                       static_cast<uint16_t>(pkt.size()));
        t_injecting = false;
        const uint16_t op = static_cast<uint16_t>(pkt[0] | (pkt[1] << 8));
        if (ok) {
            // Movement goes in at ~40 Hz; log only loads and removals. Compared
            // against the RESOLVED opcodes: the header's numbers are only a
            // fallback, and on a build that renumbered them a literal comparison
            // would silently stop matching and these lines would never print.
            if (op == op_actor_load_ack || op == op_remove_actor_ack) {
                log::info("Inject: delivered opcode {} ({} bytes) to ClientNet::OnRecvPacket", op,
                          pkt.size());
            } else if (op == op_start_action_ack && pkt.size() >= 9) {
                // The horse copy's actions (its first one matters: horse_diag).
                uint32_t key = 0;
                std::memcpy(&key, pkt.data() + 5, 4);
                static int horse_actions = 0;
                if (key == 0xB010F002 && horse_actions++ < 20) {
                    log::info("Inject: delivered StartActionAck of the horse copy ({} bytes)", pkt.size());
                }
            }
        } else {
            g_injection_enabled = false;
            log::error("Inject: EXCEPTION while delivering opcode {}; injection disabled", op);
        }
    }

    // Phase 5 (native_suppress.h): a zone native's load is held, not
    // delivered. Only real packets (ours set t_injecting), and only while
    // injection works: without it nothing could be given back.
    uint16_t size = 0;
    if (g_injection_enabled && g_ret_stub &&
        guarded_copy(&size, reinterpret_cast<const void*>(ctx.rsp + off::kHookArgSizeStackOffset), sizeof size) &&
        // The guest's loot twin (loot_twin.h): its load and messages never
        // show; its loot ones are renamed after the copy in place.
        (loot_filter_s2c(reinterpret_cast<uint8_t*>(ctx.r9), size) ||
         suppress_filter_s2c(reinterpret_cast<const uint8_t*>(ctx.r9), size) ||
         // Co-op+: our server's weather while we follow the host's (weather_sync.h).
         weather_filter_s2c(reinterpret_cast<const uint8_t*>(ctx.r9), size) ||
         // Co-op+: no Focus slow motion in co-op (slowmo.h).
         slowmo_filter_s2c(reinterpret_cast<const uint8_t*>(ctx.r9), size) ||
         // Co-op+: our own death notice once a copy was loaded (revive_prompt.h).
         revive_filter_s2c(reinterpret_cast<const uint8_t*>(ctx.r9), size))) {
        ctx.rip = reinterpret_cast<uintptr_t>(g_ret_stub);
    }
}

// ---------------------------------------------------------------- names

struct TrocInfo {
    std::string name;
    uint32_t kind = 0;
};
// Key: (opcode << 32) | kind. Opcodes are unique per build in practice, but
// keep the kind in the key so a Req and an Ack can never be confused.
std::map<uint64_t, TrocInfo> g_trocs;
std::map<uint16_t, std::string> g_name_by_opcode;

// Walks the static handler instances and reads (opcode, kind, RTTI name).
// Resolved at runtime because opcodes shift between game builds.
void load_troc_names(uintptr_t base) {
    for (uintptr_t a = base + off::kTrocInstancesBegin; a < base + off::kTrocInstancesEnd; a += 8) {
        auto vt = game::read_ptr(a);
        if (!vt || *vt < base || *vt >= base + 0x10000000) continue;
        const std::string decorated = game::rtti_name_of_vtable(*vt);
        if (decorated.rfind(".?AVTrocTr", 0) != 0) continue;
        auto kind = game::read<uint32_t>(a + off::kTroc_Kind);
        auto op = game::read<uint16_t>(a + off::kTroc_Opcode);
        if (!kind || !op) continue;
        TrocInfo info{game::short_class_name(decorated, "TrocTr"), *kind};
        g_name_by_opcode.emplace(*op, info.name);
        g_trocs[(static_cast<uint64_t>(*op) << 32) | *kind] = std::move(info);
        a += 0x80;  // skip the rest of this 0x90-byte instance
    }
}

const std::string& name_for(uint16_t op) {
    static const std::string unknown = "?";
    auto it = g_name_by_opcode.find(op);
    return it == g_name_by_opcode.end() ? unknown : it->second;
}

// ---------------------------------------------------------------- writer

// cdmp_packets_*.bin: "CDPK" u32 version u32 pe_timestamp, then records:
//   u64 t_us | u8 dir | u8 pad | u16 size | u16 copied | u16 pad | u32 session | u32 thread | data[copied]
// version 1: at most 1024 bytes per packet; version 2: whole packets (<= 32 KB).
struct BinHeader {
    char magic[4];
    uint32_t version;
    uint32_t pe_timestamp;
};

#pragma pack(push, 1)
struct BinRecord {
    uint64_t t_us;
    uint8_t dir;
    uint8_t pad8;
    uint16_t size;
    uint16_t copied;
    uint16_t pad16;
    uint32_t session;
    uint32_t thread;
};
#pragma pack(pop)

struct Writer {
    FILE* text = nullptr;
    FILE* bin = nullptr;
    uint64_t t0_us = 0;
    std::map<uint16_t, uint64_t> window_c2s, window_s2c, total_c2s, total_s2c;
    uint64_t reported_dropped = 0;
    uint64_t packets = 0;

    void write(const RecordHeader& h, const uint8_t* data) {
        if (!t0_us) t0_us = h.t_us;
        ++packets;
        const uint16_t op = static_cast<uint16_t>(data[0] | (data[1] << 8));
        const int slot = static_cast<int8_t>(data[2]);
        const uint16_t payload_len = static_cast<uint16_t>(data[3] | (data[4] << 8));
        const bool c2s = h.dir == Dir::ClientToServer;
        (c2s ? window_c2s : window_s2c)[op]++;
        (c2s ? total_c2s : total_s2c)[op]++;
        // Co-op first (low latency: our movement goes out from here), the recording after.
        if (h.copied == h.size) {
            capture_set_packet_time_us(h.t_us);
            if (c2s) capture_observe_c2s(data, h.copied);
            else capture_observe_s2c(data, h.copied);
        }
        if (!text) return;  // not recording

        // Hex preview through a lookup table: formatting used to be the
        // bottleneck that made v1 drop packets during loading bursts.
        static const char* kHex = "0123456789ABCDEF";
        char hex[kPreviewBytes * 3 + 1];
        const size_t n = std::min<size_t>(h.copied > 5 ? h.copied - 5 : 0, kPreviewBytes);
        for (size_t i = 0; i < n; ++i) {
            hex[i * 3] = kHex[data[5 + i] >> 4];
            hex[i * 3 + 1] = kHex[data[5 + i] & 15];
            hex[i * 3 + 2] = ' ';
        }
        hex[n * 3] = '\0';
        char line[512];
        const int len = std::snprintf(
            line, sizeof line, "%10.3f %s %-38s %5u slot %3d len %5u%s | %s\n",
            static_cast<double>(h.t_us - t0_us) / 1e6, c2s ? "C>S" : "S>C", name_for(op).c_str(),
            op, slot, payload_len, payload_len + 5u == h.size ? "" : " (size mismatch)", hex);
        if (len > 0) std::fwrite(line, 1, std::min<size_t>(len, sizeof line - 1), text);


        const BinRecord r{h.t_us, static_cast<uint8_t>(h.dir), 0, h.size, h.copied, 0, h.session,
                          h.thread};
        std::fwrite(&r, sizeof r, 1, bin);
        std::fwrite(data, 1, h.copied, bin);
    }

    static std::string top(const std::map<uint16_t, uint64_t>& counts, size_t k) {
        std::vector<std::pair<uint64_t, uint16_t>> v;
        uint64_t sum = 0;
        for (auto& [op, c] : counts) {
            v.emplace_back(c, op);
            sum += c;
        }
        std::sort(v.rbegin(), v.rend());
        std::string s = std::format("{} total", sum);
        for (size_t i = 0; i < v.size() && i < k; ++i)
            s += std::format(", {} x{}", name_for(v[i].second), v[i].first);
        return s;
    }

    void stats(uint64_t dropped, uint64_t truncated) {
        if (!text) {
            window_c2s.clear();
            window_s2c.clear();
            return;
        }
        std::fprintf(text, "# stats 10s | C>S %s\n", top(window_c2s, 8).c_str());
        std::fprintf(text, "# stats 10s | S>C %s\n", top(window_s2c, 8).c_str());
        if (dropped != reported_dropped) {
            std::fprintf(text, "# DROPPED %llu packets so far (writer too slow)\n",
                         static_cast<unsigned long long>(dropped));
            reported_dropped = dropped;
        }
        if (truncated) {
            std::fprintf(text, "# truncated %llu packets larger than %u bytes so far\n",
                         static_cast<unsigned long long>(truncated), kMaxCopy);
        }
        window_c2s.clear();
        window_s2c.clear();
        std::fflush(text);
        std::fflush(bin);
    }
};

struct ThreadArgs {
    std::wstring dir;
    HANDLE stop;
    uint32_t pe_timestamp;
    bool record;
};

DWORD WINAPI writer_thread(LPVOID p) {
    std::unique_ptr<ThreadArgs> args(static_cast<ThreadArgs*>(p));
    Writer w;
    // One pair of files per game session so a restart never overwrites a capture.
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t stamp[32];
    swprintf_s(stamp, L"%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour,
               st.wMinute, st.wSecond);
    const std::wstring stem = args->dir + L"cdmp_packets_" + stamp;
    if (args->record) {
        // Shared access so the capture can be read (e.g. by an analysis script) while the game runs.
        w.text = _wfsopen((stem + L".log").c_str(), L"w", _SH_DENYNO);
        w.bin = _wfsopen((stem + L".bin").c_str(), L"wb", _SH_DENYNO);
        if (!w.text || !w.bin) {
            log::error("Sniffer: cannot open output files");
            return 0;
        }
        std::setvbuf(w.text, nullptr, _IOFBF, 1 << 20);
        std::setvbuf(w.bin, nullptr, _IOFBF, 4 << 20);
        const BinHeader hdr{{'C', 'D', 'P', 'K'}, 2, args->pe_timestamp};
        std::fwrite(&hdr, sizeof hdr, 1, w.bin);
        std::fprintf(w.text, "# cdmp packet sniffer v2 | %zu message types known | t = seconds since first packet\n",
                     g_trocs.size());
        std::fprintf(w.text, "#       t dir name                                   opcode slot      len | payload preview\n");
    } else {
        log::info("Sniffer: not recording packets to disk ([sniffer] record=0)");
    }

    ULONGLONG last_stats = GetTickCount64(), last_flush = last_stats;
    for (;;) {
        // Co-op+: flush every 2 s, so a recording read while the game runs (or after it was killed) is complete.
        if (w.text && GetTickCount64() - last_flush >= 2000) {
            last_flush = GetTickCount64();
            std::fflush(w.text);
            std::fflush(w.bin);
        }
        AcquireSRWLockExclusive(&g_lock);
        const uint64_t head = g_ring->head();
        const uint64_t tail = g_ring->tail();  // only this thread moves the tail
        const uint64_t dropped = g_dropped;
        const uint64_t truncated = g_truncated;
        ReleaseSRWLockExclusive(&g_lock);

        // Outside the lock: producers never write into [tail, head).
        const uint64_t new_tail = g_ring->drain(tail, head, [&](const uint8_t* rec, uint32_t) {
            RecordHeader h;
            std::memcpy(&h, rec, sizeof h);
            w.write(h, rec + sizeof h);
        });
        AcquireSRWLockExclusive(&g_lock);
        g_ring->set_tail(new_tail);
        ReleaseSRWLockExclusive(&g_lock);

        capture_tick();
        puppet_tick();
        game::game_errors_drain();

        if (GetTickCount64() - last_stats >= 10000) {
            last_stats = GetTickCount64();
            w.stats(dropped, truncated);
        }
        const HANDLE waits[2] = {args->stop, g_c2s_wake};
        if (WaitForMultipleObjects(g_c2s_wake ? 2 : 1, waits, FALSE, 10) == WAIT_OBJECT_0) break;
    }
    if (w.text) {
        std::fprintf(w.text, "# session totals | %llu packets\n", static_cast<unsigned long long>(w.packets));
        std::fprintf(w.text, "# session totals | C>S %s\n", Writer::top(w.total_c2s, 30).c_str());
        std::fprintf(w.text, "# session totals | S>C %s\n", Writer::top(w.total_s2c, 30).c_str());
        std::fclose(w.text);
        std::fclose(w.bin);
    }
    return 0;
}

bool head_matches(uintptr_t addr, const uint8_t* expected, size_t n) {
    std::vector<uint8_t> actual(n);
    return game::safe_read(addr, actual.data(), n) && std::memcmp(actual.data(), expected, n) == 0;
}

}  // namespace

// Opcode of a Troc handler, looked up by its RTTI class name. The table is built
// once at startup from the live game, so this survives an opcode renumbering
// between builds; `fallback` keeps the old behaviour when it cannot be resolved.
//
// Defined OUTSIDE the anonymous namespace on purpose. troc_sniffer.h declares it
// as cdmp::net::opcode_for, and a definition inside the anonymous namespace makes
// an unrelated internal function of the same name -- two equally good candidates
// for every call, which the compiler rejects as ambiguous. It went unnoticed
// because nothing inside the anonymous namespace called it before.
uint16_t opcode_for(const char* name, uint16_t fallback) {
    if (!name || !*name || g_name_by_opcode.empty()) return fallback;
    // The map is name -> opcode indexed the other way, so walk it. It holds a few
    // thousand entries and this is called at most a handful of times, but cache
    // the answer per name to keep it off any hot path.
    static std::mutex cache_mutex;
    static std::map<std::string, uint16_t> cache;
    std::lock_guard lock(cache_mutex);
    if (const auto it = cache.find(name); it != cache.end()) return it->second;
    uint16_t found = fallback;
    for (const auto& [op, n] : g_name_by_opcode) {
        if (n == name) {
            found = op;
            break;
        }
    }
    cache.emplace(name, found);
    if (found != fallback) {
        log::info("Troc: {} is opcode {} this build (was {})", name, found, fallback);
    }
    return found;
}

// Outside the anonymous namespace for the same reason as opcode_for: the header
// declares it as cdmp::net::opcodes_resolved (Co-op+ fix: it was defined inside the
// anonymous namespace, so any caller failed to link; nobody had called it before).
bool opcodes_resolved() { return !g_name_by_opcode.empty(); }

bool sniffer_world_loading() { return world_loading(); }

bool sniffer_inject_c2s(std::vector<uint8_t> packet) {
    if (kNativeLayer) {
        // native_layer.h: our sniffer is off; the native base's cdmp.asi sends it through its own (cdmp_inject_c2s).
        using InjectFn = int (*)(const uint8_t*, size_t);
        static InjectFn fn = nullptr;
        if (!fn) {
            if (HMODULE m = GetModuleHandleW(L"cdmp.asi")) fn = reinterpret_cast<InjectFn>(GetProcAddress(m, "cdmp_inject_c2s"));
        }
        return fn && fn(packet.data(), packet.size()) != 0;
    }
    // While the world loads it would only wait and expire: the caller is told.
    if (!g_post_fn || packet.size() < 5 || packet.size() > 0x7F00 || world_loading()) return false;
    AcquireSRWLockExclusive(&g_c2s_lock);
    const bool ok = g_c2s_queue.size() < 16;
    if (ok) g_c2s_queue.push_back({GetTickCount64(), std::move(packet)});
    ReleaseSRWLockExclusive(&g_c2s_lock);
    return ok;
}

bool sniffer_install(const std::wstring& dir, HANDLE stop, bool record) {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (base != off::kImageBase || nt->FileHeader.TimeDateStamp != off::kPeTimestamp) {
        log::warn("Sniffer: unsupported game build, not installed");
        return false;
    }
    const uintptr_t post = base + off::kClientPostPacket;
    const uintptr_t recv = base + off::kClientOnRecvPacket;
    if (!head_matches(post, off::kClientPostPacketHead, sizeof off::kClientPostPacketHead) ||
        !head_matches(recv, off::kClientOnRecvPacketHead, sizeof off::kClientOnRecvPacketHead)) {
        log::error("Sniffer: function prologues don't match the expected bytes, not installed");
        return false;
    }

    load_troc_names(base);
    log::info("Sniffer: {} message types resolved from the game ({} opcodes)", g_trocs.size(),
              g_name_by_opcode.size());

    // Injection needs eight opcodes. Check them against the live table, and WHERE
    // THEY DISAGREE, take the live value rather than the header's.
    g_on_recv_fn = reinterpret_cast<OnRecvPacketFn>(recv);
    g_post_fn = reinterpret_cast<PostPacketFn>(post);
    //
    // This check used to compare the header's frozen numbers against the table and
    // disable injection on any mismatch -- fail-safe, but it also threw away the
    // answer it had just computed. On 3446 seven of the eight opcodes were
    // renumbered (RemoveActorAck 2516->3067, StartActionAck 2991->2552, ...), so
    // injection switched itself off and the host captured nothing at all, while the
    // log line "1175 message types resolved" right above suggested all was well.
    //
    // The table is built from the game's own RTTI names, which are stable across
    // patches, so a name that is present is authoritative and a renumbering is a
    // non-event. Only a name that is MISSING from the table is a real problem, and
    // that still disables injection.
    g_injection_enabled = true;
    {
        const std::pair<uint16_t, const char*> expected[] = {
            {off::kOpActorLoadDataAck, "ActorLoadDataAck"}, {off::kOpRemoveActorAck, "RemoveActorAck"},
            {off::kOpMoveActorReq, "MoveActorReq"},         {off::kOpMoveActorAck, "MoveActorAck"},
            {off::kOpStartActionReq, "StartActionReq"},     {off::kOpStartActionAck, "StartActionAck"},
            {off::kOpRotateActorReq, "RotateActorReq"},     {off::kOpRotateActorAck, "RotateActorAck"},
        };
        // `wanted`, not `name`: a local called `name` shadows the parameter of the
        // same name in opcode_for, which makes the call below ambiguous.
        for (const auto& [op, wanted] : expected) {
            const std::string& live = name_for(op);
            if (live == wanted) continue;             // header agrees with the game
            if (live == "?") {                        // the opcode is not in the table
                log::warn("Sniffer: {} is opcode {} in the header but no such opcode exists in this "
                          "game; injection disabled", wanted, op);
                g_injection_enabled = false;
                continue;
            }
            // The opcode moved. Re-resolve it by name and carry on.
            const uint16_t resolved = opcode_for(wanted, op);
            if (resolved == op || name_for(resolved) != wanted) {
                log::warn("Sniffer: {} was opcode {} but this game uses {} for '{}', and the name "
                          "could not be re-resolved; injection disabled", wanted, op, resolved, live);
                g_injection_enabled = false;
            }
        }
        // Hand the resolved values to the rest of the mod. Everything downstream
        // reads these, so this is the one place the renumbering has to be applied.
        if (g_injection_enabled) {
            op_move_req = opcode_for("MoveActorReq", off::kOpMoveActorReq);
            op_start_action_req = opcode_for("StartActionReq", off::kOpStartActionReq);
            op_rotate_req = opcode_for("RotateActorReq", off::kOpRotateActorReq);
            op_actor_load_ack = opcode_for("ActorLoadDataAck", off::kOpActorLoadDataAck);
            op_move_ack = opcode_for("MoveActorAck", off::kOpMoveActorAck);
            op_start_action_ack = opcode_for("StartActionAck", off::kOpStartActionAck);
            op_rotate_ack = opcode_for("RotateActorAck", off::kOpRotateActorAck);
            op_remove_actor_ack = opcode_for("RemoveActorAck", off::kOpRemoveActorAck);
            log::info("Sniffer: injection enabled on this build's opcodes "
                      "(MoveActorReq {} StartActionReq {} RotateActorReq {} / MoveActorAck {} "
                      "StartActionAck {} RotateActorAck {} RemoveActorAck {} ActorLoadDataAck {})",
                      op_move_req, op_start_action_req, op_rotate_req, op_move_ack, op_start_action_ack,
                      op_rotate_ack, op_remove_actor_ack, op_actor_load_ack);
        }
    }
    if (!g_injection_enabled) log::warn("Sniffer: opcode table mismatch, injection disabled");
    // The `ret` a held load resumes at (native_suppress.h).
    if (auto* stub = static_cast<uint8_t*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))) {
        stub[0] = 0xC3;
        DWORD old = 0;
        if (VirtualProtect(stub, 16, PAGE_EXECUTE_READ, &old)) {
            FlushInstructionCache(GetCurrentProcess(), stub, 16);
            g_ret_stub = stub;
        }
    }

    QueryPerformanceFrequency(&g_qpc_freq);
    auto* storage = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, kRingBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (storage) g_ring = new ByteRing(storage, kRingBytes);
    if (!g_ring) {
        log::error("Sniffer: cannot allocate the {} MB capture ring", kRingBytes >> 20);
        return false;
    }

    g_post_hook = safetyhook::create_mid(reinterpret_cast<void*>(post), on_post_packet);
    g_recv_hook = safetyhook::create_mid(reinterpret_cast<void*>(recv), on_recv_packet);
    if (!g_post_hook || !g_recv_hook) {
        log::error("Sniffer: mid-hook creation failed (post {}, recv {})",
                   static_cast<bool>(g_post_hook), static_cast<bool>(g_recv_hook));
        return false;
    }

    auto* args = new ThreadArgs{dir, stop, nt->FileHeader.TimeDateStamp, record};
    if (HANDLE t = CreateThread(nullptr, 0, writer_thread, args, 0, nullptr)) CloseHandle(t);
    log::info("Sniffer v2: hooked ClientNet::PostPacket (exe+0x{:X}) and ClientNet::OnRecvPacket "
              "(exe+0x{:X}); full packets up to {} bytes; writing cdmp_packets_<date-time>.log/.bin",
              off::kClientPostPacket, off::kClientOnRecvPacket, kMaxCopy);
    return true;
}

void sniffer_set_trace_op(uint16_t op) { g_trace_op = op; g_traced = 0; }

}  // namespace cdmp::net
