#include "mod/session2/frame_probe.h"

#include <windows.h>

#include <safetyhook.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/p8_probes.h"

namespace cdmp::session2 {
namespace {

namespace g2976 = game::build_2976;

// 1.0.0.3732 (build/decomp/notes_gimmick_handoff.md section 3.2): ClientGimmickActorComponent's frame-event runner.
// Entry: mov [rsp+8],rbx | mov [rsp+0x10],rsi | mov [rsp+0x18],rdi (three 5-byte instructions; the patch takes the
// first). rcx = the component ([rcx+8] = the gimmick actor), r8 = the local player ([[r8+0xA0]+0x60] = its user); its
// test at +0x6E: cmp [gimmick+0x90],eax; jne -> a silent return.
constexpr uintptr_t kRunner = 0xA0CB80;
constexpr uint8_t kRunnerHead[15] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
                                     0x48, 0x89, 0x7C, 0x24, 0x18};
constexpr uint32_t kComp_Actor = 0x08, kActor_Key = 0x60, kActor_Controller = 0x90, kPlayer_User = 0xA0,
                   kUser_Key = 0x60;
// 3732 opcodes (notes section 0 / 3.3): the summon at a cast, and the bite.
constexpr uint16_t kOpSummon = 3010, kOpDoStep = 2526;
constexpr size_t kHeader = 5;

struct Slot {
    std::atomic<uint32_t> key{0};
    std::atomic<uint32_t> calls{0}, refused{0}, ctl{0}, local{0};
};
constexpr size_t kSlots = 1024;
Slot g_tab[kSlots];
std::atomic<uint64_t> g_calls{0}, g_refused{0}, g_unread{0}, g_full{0};
std::atomic<int> g_lines{0};
constexpr int kMaxLines = 30;
SafetyHookMid g_mid;
std::atomic<bool> g_on{false}, g_server_on{false}, g_guest{false};
std::atomic<uint64_t> g_srv[2][2] = {};  // [op 0 summon / 1 bite][0 host's client / 1 user 2]
std::atomic<int> g_srv_lines{0};
constexpr int kMaxSrvLines = 40;

Slot* slot_for(uint32_t key) noexcept {
    const size_t h = (static_cast<size_t>(key) * 2654435761u) & (kSlots - 1);
    for (size_t i = 0; i < 64; ++i) {
        Slot& s = g_tab[(h + i) & (kSlots - 1)];
        uint32_t k = s.key.load(std::memory_order_relaxed);
        if (k == key) return &s;
        if (k == 0) {
            if (s.key.compare_exchange_strong(k, key, std::memory_order_relaxed) || k == key) return &s;
        }
    }
    return nullptr;
}

void runner_mid(safetyhook::Context& ctx) {
    g_calls.fetch_add(1, std::memory_order_relaxed);
    const auto gimmick = game::fast_ptr(ctx.rcx + kComp_Actor);
    const auto user = ctx.r8 ? game::fast_ptr(ctx.r8 + kPlayer_User) : std::nullopt;
    const auto key = gimmick ? game::fast<uint32_t>(*gimmick + kActor_Key) : std::nullopt;
    const auto ctl = gimmick ? game::fast<uint32_t>(*gimmick + kActor_Controller) : std::nullopt;
    const auto local = user ? game::fast<uint32_t>(*user + kUser_Key) : std::nullopt;
    if (!key || !ctl) {
        g_unread.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const uint32_t loc = local ? *local : 0;
    const bool refuse = *ctl != loc;
    if (refuse) g_refused.fetch_add(1, std::memory_order_relaxed);
    Slot* s = slot_for(*key);
    if (!s) {
        g_full.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    s->calls.fetch_add(1, std::memory_order_relaxed);
    s->ctl.store(*ctl, std::memory_order_relaxed);
    s->local.store(loc, std::memory_order_relaxed);
    if (refuse && s->refused.fetch_add(1, std::memory_order_relaxed) == 0 &&
        g_lines.fetch_add(1, std::memory_order_relaxed) < kMaxLines) {
        log::info("FrameProbe [{}]: gimmick 0x{:08X}: its frame events stop here (controller 0x{:08X}, this game's user "
                  "0x{:08X}): first refusal of this key",
                  g_guest.load() ? "guest" : "host", *key, *ctl, loc);
    }
}

DWORD WINAPI worker(void*) {
    for (;;) {
        Sleep(30000);
        std::vector<std::pair<uint32_t, uint32_t>> top;  // (refused or calls, slot index)
        size_t keys = 0;
        for (size_t i = 0; i < kSlots; ++i) {
            if (!g_tab[i].key.load(std::memory_order_relaxed)) continue;
            ++keys;
            const uint32_t r = g_tab[i].refused.load(std::memory_order_relaxed);
            const uint32_t c = g_tab[i].calls.load(std::memory_order_relaxed);
            top.emplace_back(r ? r : c, static_cast<uint32_t>(i));
        }
        std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::string list;
        for (size_t n = 0; n < top.size() && n < 8; ++n) {
            const Slot& s = g_tab[top[n].second];
            list += std::format("{}0x{:08X} {}/{} (ctl 0x{:08X})", list.empty() ? "" : ", ", s.key.load(),
                                s.refused.load(), s.calls.load(), s.ctl.load());
        }
        std::string srv;
        if (g_server_on.load()) {
            srv = std::format(" | server receive: summon 2594 host's client {} user 2 {}, bite 2304 host's client {} "
                              "user 2 {}",
                              g_srv[0][0].load(), g_srv[0][1].load(), g_srv[1][0].load(), g_srv[1][1].load());
        }
        log::info("FrameProbe summary [{}]: frame-event runs {} (would stop {}) on {} gimmicks | unread {} table full {} "
                  "| most (stopped/runs): {}{}",
                  g_guest.load() ? "guest" : "host", g_calls.load(), g_refused.load(), keys, g_unread.load(),
                  g_full.load(), list.empty() ? "none" : list, srv);
    }
}

}  // namespace

void frame_probe_start(uint32_t on, bool guest) {
    if (!on) return;
    if (g_on.exchange(true)) return;
    g_guest.store(guest);
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    std::vector<gad::Section> secs;
    if (base != g2976::kImageBase || !game::safe_read(base, &dos, sizeof dos) ||
        !game::safe_read(base + dos.e_lfanew, &nt, sizeof nt) || nt.FileHeader.TimeDateStamp != g2976::kPeTimestamp ||
        !p8_exe_sections(base, secs)) {
        log::error("FrameProbe: not game 1.0.0.3732 (or its sections unreadable); frame_probe stays off");
        return;
    }
    if (!p8_install_mid(base, secs, kRunner, 5, kRunnerHead, &runner_mid, g_mid, "FrameProbe",
                        "the client's gimmick frame-event runner 0xA0CB80 (entry)")) {
        return;
    }
    if (!guest) g_server_on.store(true);
    if (HANDLE h = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr)) CloseHandle(h);
    log::info("FrameProbe ON ({}, frame_probe=1, log only): every run of 0xA0CB80 (a gimmick's frame events on this "
              "game) counted per gimmick, with whether its controller check would stop it{}; a summary every 30 s",
              guest ? "guest" : "host",
              guest ? "" : "; at the server's receive every summon 2594 and bite 2304 by origin");
}

void frame_probe_server_ingress(uint16_t op, uintptr_t packet, uint16_t size, bool from_user2) noexcept {
    if (!g_server_on.load(std::memory_order_relaxed) || (op != kOpSummon && op != kOpDoStep)) return;
    const int which = op == kOpSummon ? 0 : 1;
    g_srv[which][from_user2 ? 1 : 0].fetch_add(1, std::memory_order_relaxed);
    if (g_srv_lines.fetch_add(1, std::memory_order_relaxed) >= kMaxSrvLines) return;
    uint32_t key = 0;
    if (packet && size >= kHeader + 4) {
        if (const auto k = game::fast<uint32_t>(packet + kHeader)) key = *k;
    }
    log::info("FrameProbe server: {} from {} for actor 0x{:08X} ({} bytes)", which == 0 ? "summon 2594" : "bite 2304",
              from_user2 ? "user 2" : "the host's client", key, size);
}

}  // namespace cdmp::session2
