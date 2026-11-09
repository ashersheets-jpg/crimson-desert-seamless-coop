#include "mod/net/dev_memscan.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <format>
#include <string>
#include <vector>

#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/log.h"
#include "mod/net/dev_watch.h"
#include "mod/net/local_capture.h"

namespace cdmp::net {
namespace {

constexpr int kWindow = 64;        // bytes kept on each side of a match
constexpr size_t kMaxMatches = 4000;
constexpr size_t kChunk = 1 << 20;

struct Match {
    uintptr_t addr;
    uint8_t before[kWindow * 2];
};
std::vector<Match> g_matches;
std::vector<uintptr_t> g_floats;
constexpr size_t kMaxFloats = 24u << 20;  // 24 M addresses (192 MB) at most

// Read/write memory a float scan looks at: committed, plain read/write (heap) or the exe's writable data.
bool scannable(const MEMORY_BASIC_INFORMATION& m) {
    if (m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    if (m.Type == MEM_PRIVATE) return m.Protect == PAGE_READWRITE;
    return m.Type == MEM_IMAGE && m.AllocationBase == GetModuleHandleW(nullptr) &&
           (m.Protect == PAGE_READWRITE || m.Protect == PAGE_WRITECOPY);
}

std::string where_of(uintptr_t a) {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    MEMORY_BASIC_INFORMATION m{};
    if (VirtualQuery(reinterpret_cast<void*>(a), &m, sizeof m) && m.AllocationBase == reinterpret_cast<void*>(base)) {
        char s[32];
        std::snprintf(s, sizeof s, " (exe+0x%llX)", static_cast<unsigned long long>(a - base));
        return s;
    }
    return {};
}

}  // namespace

void dev_memscan(const std::vector<uint8_t>& pattern) {
    g_matches.clear();
    if (pattern.empty() || pattern.size() > kWindow) return;
    std::vector<uint8_t> buf(kChunk + kWindow * 2);
    uint64_t scanned = 0;
    const DWORD t0 = GetTickCount();
    MEMORY_BASIC_INFORMATION mbi{};
    for (uintptr_t a = 0x10000; a < 0x7FFFFFFF0000ull && g_matches.size() < kMaxMatches;
         a = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
        if (!VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof mbi)) break;
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || mbi.Protect != PAGE_READWRITE) continue;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        for (size_t off = 0; off < mbi.RegionSize && g_matches.size() < kMaxMatches; off += kChunk) {
            const size_t n = std::min(kChunk, mbi.RegionSize - off);
            if (!game::safe_read(base + off, buf.data(), n)) break;
            scanned += n;
            for (size_t i = 0; i + pattern.size() <= n; i += 4) {
                if (std::memcmp(buf.data() + i, pattern.data(), pattern.size()) != 0) continue;
                Match m{base + off + i, {}};
                if (!game::safe_read(m.addr - kWindow, m.before, sizeof m.before)) continue;
                g_matches.push_back(m);
                if (g_matches.size() >= kMaxMatches) break;
            }
        }
    }
    log::info("Dev memscan: {} matches in {} MB of read/write memory ({} ms)", g_matches.size(), scanned >> 20,
              GetTickCount() - t0);
}

void dev_memdiff() {
    int changed = 0, lines = 0;
    for (size_t k = 0; k < g_matches.size(); ++k) {
        const Match& m = g_matches[k];
        uint8_t now[kWindow * 2];
        if (!game::safe_read(m.addr - kWindow, now, sizeof now)) {
            if (lines++ < 150) log::info("Dev memdiff: match {} at 0x{:X} is gone", k, m.addr);
            continue;
        }
        if (std::memcmp(now, m.before, sizeof now) == 0) continue;
        ++changed;
        std::string d;
        int n = 0;
        for (int i = 0; i < kWindow * 2; ++i) {
            if (now[i] == m.before[i]) continue;
            if (++n <= 24) {
                char s[40];
                std::snprintf(s, sizeof s, " %+d:%02x>%02x", i - kWindow, m.before[i], now[i]);
                d += s;
            }
        }
        if (lines++ < 150) {
            std::string ctx;
            for (int i = kWindow - 32; i < kWindow + 32; ++i) {
                char s[4];
                std::snprintf(s, sizeof s, "%02x", m.before[i]);
                ctx += s;
            }
            log::info("Dev memdiff: match {} at 0x{:X}: {} bytes changed{} | before -32..+32: {}", k, m.addr, n, d,
                      ctx);
        }
    }
    log::info("Dev memdiff: {} of {} matches changed", changed, g_matches.size());
}

void dev_memwatch(int rel, const std::vector<uint8_t>& value, int len, int hits, int nth) {
    int seen = 0;
    std::vector<uintptr_t> picks;
    for (size_t k = 0; k < g_matches.size() && !value.empty(); ++k) {
        uint8_t b[16] = {};
        if (value.size() > sizeof b || !game::safe_read(g_matches[k].addr + rel, b, value.size()) ||
            std::memcmp(b, value.data(), value.size()) != 0) {
            continue;
        }
        uint8_t ctx[32] = {};
        game::safe_read(g_matches[k].addr, ctx, sizeof ctx);
        std::string h;
        for (uint8_t c : ctx) {
            char s[4];
            std::snprintf(s, sizeof s, "%02x", c);
            h += s;
        }
        log::info("Dev memwatch: candidate #{} = match {} at 0x{:X}: {}{}", seen, k, g_matches[k].addr, h,
                  seen >= nth && picks.size() < 4 ? "  <- watched" : "");
        if (seen++ >= nth && picks.size() < 4) picks.push_back(g_matches[k].addr + rel);
    }
    if (!picks.empty()) {
        dev_watch_start_many(picks, len, hits);
    } else {
        log::info("Dev memwatch: no candidate #{} ({} seen)", nth, seen);
    }
}

void dev_fscan(float value) { dev_fscan_range(value, value); }

void dev_fscan_range(float lo, float hi) {
    const float value = lo;
    g_floats.clear();
    std::vector<uint8_t> buf(kChunk);
    uint64_t scanned = 0;
    const DWORD t0 = GetTickCount();
    MEMORY_BASIC_INFORMATION mbi{};
    for (uintptr_t a = 0x10000; a < 0x7FFFFFFF0000ull && g_floats.size() < kMaxFloats;
         a = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
        if (!VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof mbi)) break;
        if (!scannable(mbi)) continue;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        for (size_t off = 0; off < mbi.RegionSize && g_floats.size() < kMaxFloats; off += kChunk) {
            const size_t n = std::min(kChunk, mbi.RegionSize - off);
            if (!game::safe_read(base + off, buf.data(), n)) break;
            scanned += n;
            const auto* f = reinterpret_cast<const float*>(buf.data());
            for (size_t i = 0; i < n / 4; ++i) {
                if (f[i] >= lo && f[i] <= hi) g_floats.push_back(base + off + i * 4);
            }
        }
    }
    (void)value;
    log::info("Dev fscan: {} floats within [{}, {}] in {} MB ({} ms){}", g_floats.size(), lo, hi, scanned >> 20,
              GetTickCount() - t0, g_floats.size() >= kMaxFloats ? " - LIMIT reached" : "");
}

void dev_ffilter(float lo, float hi) {
    const DWORD t0 = GetTickCount();
    std::vector<uintptr_t> keep;
    std::vector<uint8_t> buf(kChunk);
    uintptr_t buf_at = 0;
    bool buf_ok = false;
    for (uintptr_t a : g_floats) {
        const uintptr_t chunk = a & ~(static_cast<uintptr_t>(kChunk) - 1);
        if (chunk != buf_at || !buf_ok) {
            buf_at = chunk;
            buf_ok = game::safe_read(chunk, buf.data(), kChunk);
        }
        float v = 0;
        if (buf_ok) {
            std::memcpy(&v, buf.data() + (a - chunk), 4);
        } else if (!game::safe_read(a, &v, 4)) {
            continue;
        }
        if (v >= lo && v <= hi) keep.push_back(a);
    }
    g_floats.swap(keep);
    log::info("Dev ffilter [{}, {}]: {} left ({} ms)", lo, hi, g_floats.size(), GetTickCount() - t0);
    if (g_floats.size() <= 400) {
        for (uintptr_t a : g_floats) {
            float v = 0;
            game::safe_read(a, &v, 4);
            log::info("Dev ffilter:   0x{:X} = {}{}", a, v, where_of(a));
        }
    }
}

void dev_fwatch(int hits) {
    std::vector<uintptr_t> picks(g_floats.begin(), g_floats.begin() + std::min<size_t>(4, g_floats.size()));
    if (picks.empty()) {
        log::info("Dev fwatch: nothing left to watch");
        return;
    }
    dev_watch_start_many(picks, 4, hits);
}

std::vector<uintptr_t> g_doubles;

void dev_dscan(double lo, double hi) {
    g_doubles.clear();
    std::vector<uint8_t> buf(kChunk);
    uint64_t scanned = 0;
    const DWORD t0 = GetTickCount();
    MEMORY_BASIC_INFORMATION mbi{};
    for (uintptr_t a = 0x10000; a < 0x7FFFFFFF0000ull && g_doubles.size() < kMaxFloats;
         a = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
        if (!VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof mbi)) break;
        if (!scannable(mbi)) continue;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        for (size_t off = 0; off < mbi.RegionSize && g_doubles.size() < kMaxFloats; off += kChunk) {
            const size_t n = std::min(kChunk, mbi.RegionSize - off);
            if (!game::safe_read(base + off, buf.data(), n)) break;
            scanned += n;
            const auto* d = reinterpret_cast<const double*>(buf.data());
            for (size_t i = 0; i < n / 8; ++i) {
                if (d[i] >= lo && d[i] <= hi) g_doubles.push_back(base + off + i * 8);
            }
        }
    }
    log::info("Dev dscan: {} doubles within [{}, {}] in {} MB ({} ms)", g_doubles.size(), lo, hi, scanned >> 20,
              GetTickCount() - t0);
}

void dev_dfilter(double lo, double hi) {
    std::vector<uintptr_t> keep;
    std::vector<uint8_t> buf(kChunk);
    uintptr_t buf_at = 0;
    bool buf_ok = false;
    for (uintptr_t a : g_doubles) {
        const uintptr_t chunk = a & ~(static_cast<uintptr_t>(kChunk) - 1);
        if (chunk != buf_at || !buf_ok) {
            buf_at = chunk;
            buf_ok = game::safe_read(chunk, buf.data(), kChunk);
        }
        double v = 0;
        if (buf_ok) {
            std::memcpy(&v, buf.data() + (a - chunk), 8);
        } else if (!game::safe_read(a, &v, 8)) {
            continue;
        }
        if (v >= lo && v <= hi) keep.push_back(a);
    }
    g_doubles.swap(keep);
    log::info("Dev dfilter [{}, {}]: {} left", lo, hi, g_doubles.size());
    if (g_doubles.size() <= 400) {
        for (uintptr_t a : g_doubles) {
            double v = 0;
            game::safe_read(a, &v, 8);
            log::info("Dev dfilter:   0x{:X} = {}{}", a, v, where_of(a));
        }
    }
}

void dev_dwatch(int hits) {
    std::vector<uintptr_t> picks(g_doubles.begin(), g_doubles.begin() + std::min<size_t>(4, g_doubles.size()));
    if (picks.empty()) {
        log::info("Dev dwatch: nothing left to watch");
        return;
    }
    dev_watch_start_many(picks, 8, hits);
}

}  // namespace cdmp::net

// ---- gsnap / gdiff (2026-11-05): the exe's whole .data section, before and after -------------------------------
namespace cdmp::net {
namespace {
std::vector<uint8_t> g_gsnap;
uintptr_t g_gdata = 0;
size_t g_gsize = 0;

bool data_section(uintptr_t& at, size_t& size) {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s) {
        if (std::memcmp(s->Name, ".data\0\0", 6) == 0) {
            at = base + s->VirtualAddress;
            size = s->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

void read_pages(uintptr_t at, size_t size, std::vector<uint8_t>& out) {
    out.assign(size, 0);
    for (size_t off = 0; off < size; off += 0x1000) {
        const size_t n = std::min<size_t>(0x1000, size - off);
        game::safe_read(at + off, out.data() + off, n);  // an unreadable page stays zero
    }
}
}  // namespace

// Co-op+ dev (2026-11-06, low latency): where the game keeps its client clock (the one its MoveActorReq stamps use and
// remote actors are drawn against). Every writable section of the exe (their names are decoys that change every
// build) is read twice ~500 ms apart; every 4-aligned u64 / u32 that advanced like a millisecond clock (and every u64
// like a microsecond one) is logged with its distance to our estimate of the game clock (capture_game_clock_ms).
void dev_clockscan() {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    struct Sec {
        uintptr_t at;
        size_t size;
        std::vector<uint8_t> a, b;
    };
    std::vector<Sec> secs;
    const IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s) {
        if (s->Characteristics & IMAGE_SCN_MEM_WRITE) secs.push_back({base + s->VirtualAddress, s->Misc.VirtualSize, {}, {}});
    }
    const auto t1 = std::chrono::steady_clock::now();
    const uint64_t est1 = capture_game_clock_ms();
    for (auto& x : secs) read_pages(x.at, x.size, x.a);
    Sleep(500);
    const auto t2 = std::chrono::steady_clock::now();
    const uint64_t est2 = capture_game_clock_ms();
    for (auto& x : secs) read_pages(x.at, x.size, x.b);
    const double dt = std::chrono::duration<double, std::milli>(t2 - t1).count();
    struct Hit {
        int64_t dist;
        std::string line;
    };
    std::vector<Hit> hits;
    for (const auto& x : secs) {
        for (size_t i = 0; i + 8 <= x.size; i += 4) {
            uint64_t a, b;
            std::memcpy(&a, x.a.data() + i, 8);
            std::memcpy(&b, x.b.data() + i, 8);
            if (b <= a) continue;
            const double d = static_cast<double>(b - a);
            const char* kind = nullptr;
            int64_t dist = 0;
            if (d > dt * 0.8 && d < dt * 1.2) {
                kind = "u64 ms";
                dist = static_cast<int64_t>(b) - static_cast<int64_t>(est2);
            } else if (d > dt * 800 && d < dt * 1200) {
                kind = "u64 us";
                dist = static_cast<int64_t>(b / 1000) - static_cast<int64_t>(est2);
            } else {
                const uint32_t a32 = static_cast<uint32_t>(a), b32 = static_cast<uint32_t>(b);
                const double d32 = static_cast<double>(b32 - a32);
                if (b32 > a32 && d32 > dt * 0.8 && d32 < dt * 1.2) {
                    kind = "u32 ms";
                    dist = static_cast<int64_t>(b32) - static_cast<int64_t>(est2 & 0xFFFFFFFFull);
                }
            }
            if (!kind) continue;
            hits.push_back({std::llabs(dist), std::format("exe+0x{:X} {} value {} (advanced {:.0f} in {:.0f} ms) | minus our "
                                                          "estimate: {} ms",
                                                          x.at - base + i, kind, b, d, dt, dist)});
        }
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& l, const Hit& r) { return l.dist < r.dist; });
    log::info("Dev clockscan: {} clock-like values in {} writable sections (our estimate {} -> {})", hits.size(),
              secs.size(), est1, est2);
    for (size_t i = 0; i < hits.size() && i < 40; ++i) log::info("Dev clockscan: {}", hits[i].line);
}

void dev_gsnap() {
    if (!data_section(g_gdata, g_gsize)) {
        log::warn("Dev gsnap: no .data section");
        return;
    }
    read_pages(g_gdata, g_gsize, g_gsnap);
    log::info("Dev gsnap: {} bytes of .data at exe+0x{:X}", g_gsize,
              g_gdata - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
}

void dev_gdiff(int tag) {
    if (g_gsnap.empty()) {
        log::warn("Dev gdiff: no gsnap");
        return;
    }
    std::vector<uint8_t> now;
    read_pages(g_gdata, g_gsize, now);
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring path(exe);
    path = path.substr(0, path.find_last_of(L"\/") + 1) + L"cdmp_gdiff_" + std::to_wstring(tag) + L".txt";
    FILE* f = _wfopen(path.c_str(), L"w");
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    size_t changed = 0;
    for (size_t i = 0; i < g_gsize; ++i) {
        if (now[i] == g_gsnap[i]) continue;
        ++changed;
        if (f) std::fprintf(f, "%llX %02X %02X\n", static_cast<unsigned long long>(g_gdata - base + i), g_gsnap[i], now[i]);
    }
    if (f) std::fclose(f);
    log::info("Dev gdiff {}: {} bytes of .data changed since gsnap (file cdmp_gdiff_{}.txt)", tag, changed, tag);
    g_gsnap.swap(now);  // the next gdiff is against this moment
}

}  // namespace cdmp::net

// ---- ptrscan (2026-11-05): which globals point at an actor (or its component table) -----------------------------
#include "mod/game/actor_lookup.h"
#include "mod/game/offsets.h"
namespace cdmp::net {

void dev_ptrscan(uint32_t key) {
    const auto actor = game::find_actor(key);
    if (!actor) {
        log::info("Dev ptrscan: actor 0x{:08X} not loaded", key);
        return;
    }
    const auto table = game::read_ptr(*actor + game::build_3576::kActor_ComponentTable);
    uintptr_t at = 0;
    size_t size = 0;
    if (!data_section(at, size)) return;
    std::vector<uint8_t> d;
    read_pages(at, size, d);
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    int n = 0;
    std::string hits;
    for (size_t i = 0; i + 8 <= size; i += 8) {
        uint64_t v = 0;
        std::memcpy(&v, d.data() + i, 8);
        const char* what = v == *actor ? "actor" : (table && v == *table) ? "components" : nullptr;
        if (!what) continue;
        if (++n <= 60) {
            char s[64];
            std::snprintf(s, sizeof s, " %llX(%s)", static_cast<unsigned long long>(at - base + i), what);
            hits += s;
        }
    }
    log::info("Dev ptrscan 0x{:08X}: actor 0x{:X}, {} slot(s) in .data:{}", key, *actor, n, hits);
}

}  // namespace cdmp::net

// ---- watchstatus (2026-11-05): hardware watch on our own status component + offset ----------------------------
#include "mod/net/dev_watch.h"
#include "mod/net/local_capture.h"
namespace cdmp::net {

void dev_watch_status(int offset, int len, int hits) {
    const uint32_t own = capture_own_player_key();
    const auto actor = own ? game::find_actor(own) : std::nullopt;
    const auto table = actor ? game::read_ptr(*actor + game::build_3576::kActor_ComponentTable) : std::nullopt;
    const auto status = table ? game::read_ptr(*table + game::build_3576::kComponents_Status) : std::nullopt;
    if (!status) {
        log::warn("Dev watchstatus: our status component not found");
        return;
    }
    log::info("Dev watchstatus: status component 0x{:X} (+0x{:X})", *status, offset);
    dev_watch_start(*status + offset, len, hits);
}

}  // namespace cdmp::net

namespace cdmp::net {
// B11 research (2026-11-09): the item-move definitions 3111's worker 0x2E0B2B0 reads through 0x5560E0.
void dev_dump_item_move() {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uintptr_t table = 0;
    uint32_t count = 0;
    if (!game::safe_read(base + 0x71C1060, &table, sizeof table) || !table ||
        !game::safe_read(table + 8, &count, sizeof count)) {
        log::warn("Dev script: dumpitemmove: the table [exe+0x71C1060] is not readable");
        return;
    }
    uintptr_t defs = 0;
    game::safe_read(table + 0x58, &defs, sizeof defs);
    log::info("Dev script: dumpitemmove: table 0x{:X}, {} definition(s), array 0x{:X}", table, count, defs);
    if (!defs) return;
    int lines = 0;
    for (uint32_t i = 0; i < count && i < 4096 && lines < 400; ++i) {
        uintptr_t def = 0;
        if (!game::safe_read(defs + static_cast<uintptr_t>(i) * 8, &def, sizeof def) || !def) continue;
        uintptr_t entries = 0;
        uint32_t n = 0;
        if (!game::safe_read(def + 0x38, &entries, sizeof entries) || !game::safe_read(def + 0x40, &n, sizeof n)) continue;
        uint16_t key = 0;
        game::safe_read(def, &key, sizeof key);
        log::info("Dev script: dumpitemmove: def {} (0x{:X}) key? 0x{:04X}: {} entr{}", i, def, key, n, n == 1 ? "y" : "ies");
        ++lines;
        for (uint32_t e = 0; e < n && e < 64 && lines < 400; ++e) {
            uint8_t rec[0xA0] = {};
            if (!game::safe_read(entries + static_cast<uintptr_t>(e) * 0xA0, rec, sizeof rec)) break;
            uint16_t a = 0, b = 0;
            uint64_t cond = 0;
            std::memcpy(&a, rec + 2, 2);
            std::memcpy(&b, rec + 4, 2);
            std::memcpy(&cond, rec + 0x78, 8);
            std::string hex;
            for (int k = 0; k < 0x20; ++k) hex += std::format("{:02X}", rec[k]);
            log::info("Dev script: dumpitemmove:   def {} entry {}: kind {} inventories 0x{:04X} -> 0x{:04X} condition "
                      "0x{:X} | {}", i, e, rec[0], a, b, cond, hex);
            ++lines;
        }
    }
}
}  // namespace cdmp::net
