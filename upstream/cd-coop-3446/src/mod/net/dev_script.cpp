#include "mod/net/dev_script.h"
#include "mod/input/pad_input.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#include "mod/game/actor_lookup.h"
#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/net/local_capture.h"
#include "mod/net/remote_puppet.h"
#include "mod/net/revive_prompt.h"
#include "mod/net/time_sync.h"
#include "mod/net/dev_watch.h"
#include "mod/net/dev_memscan.h"
#include "mod/net/quest_events.h"
#include "mod/net/coop_hunt.h"
#include "mod/net/enemy_relay.h"
#include "mod/net/troc_sniffer.h"
#include "mod/net/prologue_hold.h"
#include "mod/net/quest_advance.h"
#include "mod/net/guest_hits.h"

namespace cdmp::net {

// [dev] "npcs": what this client has loaded around our character (2026-11-11, ff1: a test needs to SEE whether
// enemies near a far guest exist, come closer and lose health; blind swings told nothing). Key, kind, distance, HP.
struct NpcRow {
    float d;
    uint32_t key, variant;
    float hp, hp_max;
    proto::Vec3 at;
};
// The loaded NPC actors between `from` and `radius` m of our character, nearest first.
static std::vector<NpcRow> npcs_near(float radius, float from, std::optional<proto::Vec3>& me, int& loaded) {
    using Row = NpcRow;
    std::vector<Row> rows;
    loaded = 0;
    me = game::actor_world_position(capture_own_player_key());
    if (!me) return rows;
    const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    for (const auto& [key, variant] : enemy_loaded_variants()) {
        if ((key >> 28) != 0xB) continue;
        ++loaded;
        const auto at = game::actor_world_position(key);
        if (!at) continue;
        const float dx = at->x - me->x, dy = at->y - me->y, dz = at->z - me->z;
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d > radius || d < from) continue;
        Row r{d, key, variant, -1.0f, -1.0f, *at};
        // health: actor -> components -> status -> stat container (as for our own character, player_reader.cpp)
        if (const auto actor = game::find_actor(key)) {
            const auto comps = game::read_ptr(*actor + game::build_3576::kActor_ComponentTable);
            const auto status = comps ? game::read_ptr(*comps + game::build_3576::kComponents_Status) : std::nullopt;
            const auto vt = status ? game::read_ptr(*status) : std::nullopt;
            if (status && vt && *vt == exe + game::build_3576::kVtStatusComponent) {
                const auto cont = game::read_ptr(*status + game::build_3576::kStatus_StatContainer);
                const auto entries = cont ? game::read_ptr(*cont + game::build_3576::kStatContainer_Entries) : std::nullopt;
                const auto count = cont ? game::read<uint32_t>(*cont + game::build_3576::kStatContainer_Count) : std::nullopt;
                if (entries && count && *count <= game::build_3576::kMaxStatEntries) {
                    for (uint32_t i = 0; i < *count; ++i) {
                        const uintptr_t e = *entries + static_cast<uintptr_t>(i) * game::build_3576::kStatEntry_Size;
                        const auto id = game::read<uint64_t>(e + game::build_3576::kStatEntry_Id);
                        if (!id || *id != game::build_3576::kStatId_Health) continue;
                        const auto cur = game::read<int64_t>(e + game::build_3576::kStatEntry_Current);
                        const auto max = game::read<int64_t>(e + game::build_3576::kStatEntry_Max);
                        if (cur && max) {
                            r.hp = static_cast<float>(*cur) / 1000.0f;
                            r.hp_max = static_cast<float>(*max) / 1000.0f;
                        }
                        break;
                    }
                }
            }
        }
        rows.push_back(r);
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.d < b.d; });
    return rows;
}

// [dev] "killnear [radius]": KillCheatReq on the nearest living person (not a wild animal, 40+ max HP) within
// `radius` m; "lootlast": LootingDropFromDeadBodyReq (2491: u32 body | u8 remove | u8 send list) on that body. Both as
// our own requests. For the checklist's enemy-loot step (pp4-loot, 2026-11-11: the old trigger, "any hit the host's
// client reports kills its target", fired only when such a hit happened to come and failed twice in a row).
static std::atomic<uint32_t> g_last_killnear{0};
static std::vector<uint8_t> dev_c2s(uint16_t op, const std::vector<uint8_t>& payload) {  // header: op | slot 0 | length
    std::vector<uint8_t> pkt(5 + payload.size());
    const uint16_t len = static_cast<uint16_t>(payload.size());
    std::memcpy(pkt.data(), &op, 2);
    pkt[2] = 0;
    std::memcpy(pkt.data() + 3, &len, 2);
    if (!payload.empty()) std::memcpy(pkt.data() + 5, payload.data(), payload.size());
    return pkt;
}
static void kill_nearest_person(float radius) {
    std::optional<proto::Vec3> me;
    int loaded = 0;
    for (const NpcRow& r : npcs_near(radius, 0.0f, me, loaded)) {
        if (hunt_animal_name(r.variant) || r.hp_max < 40.0f || r.hp <= 0.0f) continue;
        std::vector<uint8_t> pl(8, 0);
        std::memcpy(pl.data(), &r.key, 4);
        const bool ok = sniffer_inject_c2s(dev_c2s(2210, pl));
        g_last_killnear = r.key;
        log::info("Dev script: killnear: KillCheatReq on 0x{:08X} (variant 0x{:08X}, {:.1f} m, hp {:.0f}/{:.0f}){}", r.key,
                  r.variant, r.d, r.hp, r.hp_max, ok ? "" : " NOT sent");
        return;
    }
    log::info("Dev script: killnear: no living person within {:.0f} m ({} NPC actors loaded)", radius, loaded);
}
static void loot_last_killed() {
    const uint32_t key = g_last_killnear.load();
    if (!key) {
        log::info("Dev script: lootlast: nothing was killed by killnear yet");
        return;
    }
    std::vector<uint8_t> pl = {0, 0, 0, 0, 0, 1};
    std::memcpy(pl.data(), &key, 4);
    const bool ok = sniffer_inject_c2s(dev_c2s(2491, pl));
    log::info("Dev script: lootlast: the body 0x{:08X} looted as our own request (2491){}", key, ok ? "" : " NOT sent");
}

static void log_npcs_near(float radius, float from) {
    using Row = NpcRow;
    std::optional<proto::Vec3> me;
    int loaded = 0;
    const std::vector<Row> rows = npcs_near(radius, from, me, loaded);
    if (!me) {
        log::info("Dev script: npcs: our own position is not known");
        return;
    }
    log::info("Dev script: npcs {:.0f}-{:.0f} m from ({:.1f}, {:.1f}, {:.1f}): {} of {} loaded NPC actors", from, radius,
              me->x, me->y, me->z, rows.size(), loaded);
    for (size_t i = 0; i < rows.size() && i < 14; ++i) {
        const Row& r = rows[i];
        const char* animal = hunt_animal_name(r.variant);
        log::info("Dev script: npc 0x{:08X} variant 0x{:08X}{}{} | {:.1f} m | hp {:.0f}/{:.0f} | at ({:.1f}, {:.1f}, {:.1f})",
                  r.key, r.variant, animal ? " " : "", animal ? animal : "", r.d, r.hp, r.hp_max, r.at.x, r.at.y, r.at.z);
    }
}
namespace {

struct Step {
    enum Kind { C2S, S2C, Wait, Teleport, Poke, Time, Watch, Day, Pad, KillGame, KillAll, MemScan, MemDiff, MemWatch, Advance, FScan, FFilter, FWatch, WaitFile, DScan, DFilter, DWatch, GSnap, GDiff, PtrScan, WatchStatus, Key, ClockScan, RideHorse, PrologueDone, GrantXp, GuestHit, DumpItemMove, TpActor, Npcs, KillNear, LootLast } kind;
    uintptr_t addr = 0;
    float to[3] = {};
    uint16_t op = 0;
    std::vector<uint8_t> payload;
    uint32_t wait_ms = 0;
};

std::string hex_of(const void* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t b = static_cast<const uint8_t*>(p)[i];
        s += d[b >> 4];
        s += d[b & 15];
    }
    return s;
}

void replace_all(std::string& s, const std::string& from, const std::string& to) {
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
}

bool parse_hex(const std::string& in, std::vector<uint8_t>& out) {
    std::string h;
    for (char c : in)
        if (std::isxdigit(static_cast<unsigned char>(c))) h += c;
        else if (!std::isspace(static_cast<unsigned char>(c))) return false;
    if (h.size() % 2) return false;
    for (size_t i = 0; i < h.size(); i += 2) out.push_back(static_cast<uint8_t>(std::stoul(h.substr(i, 2), nullptr, 16)));
    return true;
}

std::vector<uint8_t> make_c2s(uint16_t op, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> pkt(5 + payload.size());
    const uint16_t len = static_cast<uint16_t>(payload.size());
    std::memcpy(pkt.data(), &op, 2);
    pkt[2] = 0;
    std::memcpy(pkt.data() + 3, &len, 2);
    if (!payload.empty()) std::memcpy(pkt.data() + 5, payload.data(), payload.size());
    return pkt;
}

void key_tap(BYTE vk, uint32_t hold_ms) {
    const auto scan = static_cast<BYTE>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    keybd_event(vk, scan, 0, 0);
    Sleep(hold_ms);
    keybd_event(vk, scan, KEYEVENTF_KEYUP, 0);
}

float flat_distance(const proto::Vec3& a, const proto::Vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

// Co-op+ test kit (user 2026-11-06: "the horse walks up nearby: look around, find it, walk to it and get on"). After
// the script called the horse (a key step before this one), wait until our horse is in the world and has stopped
// coming, step to its side (a teleport ~1.8 m from it, on our side), press the mount key and check our Moves now name
// the horse as their parent. Up to 3 tries, each from where the horse stands then. Logs "ridehorse: MOUNTED" or why not.
void ride_horse(uint32_t timeout_s, BYTE mount_vk) {
    const uint64_t until = GetTickCount64() + uint64_t{timeout_s} * 1000;
    const uint32_t own = capture_own_player_key();
    uint32_t horse = 0;
    proto::Vec3 last{};
    int still = 0, logged = 0;
    while (GetTickCount64() < until) {  // 1. find it, wait until it stands (it walks up after the call)
        horse = capture_horse_present();
        const auto me = game::actor_world_position(own);
        const auto at = horse ? game::actor_world_position(horse) : std::nullopt;
        if (at && me) {
            const float moved = flat_distance(*at, last), away = flat_distance(*at, *me);
            last = *at;
            still = moved < 0.15f ? still + 1 : 0;  // 4 x 250 ms almost still
            if (logged++ % 4 == 0) {
                log::info("Dev script: ridehorse: horse 0x{:08X} {:.1f} m away, moving {:.2f} m / 250 ms", horse, away,
                          moved);
            }
            if (still >= 4 && away < 80.0f) break;
        } else if (logged++ % 8 == 0) {
            log::info("Dev script: ridehorse: no horse of ours in the world yet (template {}, us {})",
                      horse ? "yes" : "no", me ? "found" : "not found");
        }
        Sleep(250);
    }
    if (!horse) {
        log::warn("Dev script: ridehorse: NO HORSE (it never came; was it called?)");
        return;
    }
    for (int attempt = 1; attempt <= 3; ++attempt) {  // 2. go to it and get on
        const auto me = game::actor_world_position(own);
        const auto at = game::actor_world_position(horse);
        if (!me || !at) break;
        const float away = flat_distance(*at, *me);
        if (away > 2.5f) {
            const float k = 1.8f / std::max(away, 0.01f);
            const float x = at->x + (me->x - at->x) * k, z = at->z + (me->z - at->z) * k;
            const bool ok = revive_teleport_to(x, at->y + 0.2f, z);
            log::info("Dev script: ridehorse: try {}: stepping next to the horse ({:.1f} m away){}", attempt, away,
                      ok ? "" : " (teleport NOT done)");
            Sleep(1500);
        }
        key_tap(mount_vk, 120);
        for (int i = 0; i < 40 && !capture_riding(); ++i) Sleep(100);  // the mount animation takes ~2 s
        if (capture_riding()) {
            log::info("Dev script: ridehorse: MOUNTED (try {})", attempt);
            return;
        }
        log::info("Dev script: ridehorse: try {}: not riding after key 0x{:02X}", attempt, mount_vk);
    }
    log::warn("Dev script: ridehorse: NOT MOUNTED after 3 tries");
}

}  // namespace

int dev_script_run(const std::string& path, uint32_t own_player, float x, float y, float z) {
    std::ifstream f(path);
    if (!f) {
        log::warn("Dev script: cannot read {}", path);
        return -1;
    }
    const uint32_t user = capture_own_user_key();
    const float pos[3] = {x, y, z};
    std::vector<Step> steps;
    std::string line;
    int lineno = 0;
    while (std::getline(f, line)) {
        ++lineno;
        if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        std::istringstream in(line);
        std::string kind;
        if (!(in >> kind)) continue;
        Step st{};
        if (kind == "wait") {
            in >> st.wait_ms;
            st.kind = Step::Wait;
            st.wait_ms = std::min<uint32_t>(st.wait_ms, 10000);
            steps.push_back(st);
            continue;
        }
        if (kind == "watch") {
            std::string a;
            int len = 4, hits = 8;
            in >> a >> len >> hits;
            st.kind = Step::Watch;
            st.addr = static_cast<uintptr_t>(std::stoull(a, nullptr, 16));
            st.to[0] = static_cast<float>(len ? len : 4);
            st.to[1] = static_cast<float>(hits ? hits : 8);
            steps.push_back(st);
            continue;
        }
        if (kind == "pad") {  // pad <hex buttons (input/pad_input.h)> <ms>: a simulated pad press
            std::string b;
            in >> b >> st.wait_ms;
            st.kind = Step::Pad;
            st.addr = static_cast<uintptr_t>(std::stoul(b, nullptr, 16));
            steps.push_back(st);
            continue;
        }
        if (kind == "killgame") {  // killgame: KillCheatReq on a loaded wild animal (coop_hunt.h)
            st.kind = Step::KillGame;
            steps.push_back(st);
            continue;
        }
        if (kind == "memscan") {  // memscan <hex pattern>: find it in read/write memory (dev_memscan.h)
            std::string h;
            in >> h;
            st.kind = Step::MemScan;
            if (!parse_hex(h, st.payload)) continue;
            steps.push_back(st);
            continue;
        }
        if (kind == "advance") {  // advance <hex mission key>: the quest server advances it (quest_events.h)
            std::string k;
            in >> k;
            st.kind = Step::Advance;
            st.addr = static_cast<uintptr_t>(std::stoul(k, nullptr, 16));
            steps.push_back(st);
            continue;
        }
        if (kind == "waitfile") {  // waitfile <path>: wait (up to 5 min) until a test driver creates it, then delete it
            std::string f;
            std::getline(in >> std::ws, f);
            st.kind = Step::WaitFile;
            st.payload.assign(f.begin(), f.end());
            steps.push_back(st);
            continue;
        }
        if (kind == "dscan" || kind == "dfilter") {  // dscan / dfilter <lo> <hi>: the same for doubles
            in >> st.to[0] >> st.to[1];
            st.kind = kind == "dscan" ? Step::DScan : Step::DFilter;
            steps.push_back(st);
            continue;
        }
        if (kind == "dwatch") {
            in >> st.wait_ms;
            st.kind = Step::DWatch;
            steps.push_back(st);
            continue;
        }
        if (kind == "fscan") {  // fscan <float> [hi]: every float equal to it, or within [float, hi] (dev_memscan.h)
            in >> st.to[0];
            if (!(in >> st.to[1])) st.to[1] = st.to[0];
            st.kind = Step::FScan;
            steps.push_back(st);
            continue;
        }
        if (kind == "ffilter") {  // ffilter <lo> <hi>: keep those now within [lo, hi]
            in >> st.to[0] >> st.to[1];
            st.kind = Step::FFilter;
            steps.push_back(st);
            continue;
        }
        if (kind == "fwatch") {  // fwatch <hits>: hardware write watch on the first four left
            in >> st.wait_ms;
            st.kind = Step::FWatch;
            steps.push_back(st);
            continue;
        }
        if (kind == "guesthit") {  // guesthit <hex attack hash> <frame>: a blow of ours on the nearest enemy copy
            std::string h;
            in >> h >> st.wait_ms;
            st.kind = Step::GuestHit;
            st.addr = static_cast<uintptr_t>(std::stoull(h, nullptr, 16));
            steps.push_back(st);
            continue;
        }
        if (kind == "grantxp") {  // grantxp <sub-level key> <amount>: our server adds experience (quest_advance.h)
            st.kind = Step::GrantXp;
            uint32_t key = 0;
            uint64_t amount = 0;
            in >> key >> amount;
            st.addr = key;
            st.wait_ms = static_cast<uint32_t>(amount);
            steps.push_back(st);
            continue;
        }
        if (kind == "prologuedone") {  // prologuedone: as the Co-op page's "My prologue is done" (prologue_hold.h)
            st.kind = Step::PrologueDone;
            steps.push_back(st);
            continue;
        }
        if (kind == "ridehorse") {  // ridehorse [timeout s] [hex mount vk]: find our called horse, go to it, get on
            st.kind = Step::RideHorse;
            std::string vk;
            if (!(in >> st.wait_ms)) st.wait_ms = 30;
            st.addr = (in >> vk) ? static_cast<uintptr_t>(std::stoull(vk, nullptr, 16)) : 0x45;  // E (Ride on 3479)
            steps.push_back(st);
            continue;
        }
        if (kind == "key") {  // key <hex vk> <hold ms>: a key press from inside the game (as coop_worlds.cpp sends)
            std::string a;
            in >> a >> st.wait_ms;
            st.kind = Step::Key;
            st.addr = static_cast<uintptr_t>(std::stoull(a, nullptr, 16));
            steps.push_back(st);
            continue;
        }
        if (kind == "watchstatus") {  // watchstatus <hex offset> <len> <hits>: our status component (dev_memscan.h)
            std::string a;
            int len = 1, hits = 6;
            in >> a >> len >> hits;
            st.kind = Step::WatchStatus;
            st.addr = static_cast<uintptr_t>(std::stoull(a, nullptr, 16));
            st.to[0] = static_cast<float>(len ? len : 1);
            st.to[1] = static_cast<float>(hits ? hits : 6);
            steps.push_back(st);
            continue;
        }
        if (kind == "ptrscan") {  // ptrscan <hex actor key>: globals pointing at that actor (dev_memscan.h)
            std::string a;
            in >> a;
            st.kind = Step::PtrScan;
            st.addr = static_cast<uintptr_t>(std::stoull(a, nullptr, 16));
            steps.push_back(st);
            continue;
        }
        if (kind == "clockscan") {  // clockscan: values that run like the game's clock (dev_memscan.h)
            st.kind = Step::ClockScan;
            steps.push_back(st);
            continue;
        }
        if (kind == "gsnap" || kind == "gdiff") {  // gsnap / gdiff N: the exe's .data, before / after (dev_memscan.h)
            st.kind = kind == "gsnap" ? Step::GSnap : Step::GDiff;
            in >> st.wait_ms;
            steps.push_back(st);
            continue;
        }
        if (kind == "memdiff") {  // memdiff: which bytes around the memscan matches changed since
            st.kind = Step::MemDiff;
            steps.push_back(st);
            continue;
        }
        if (kind == "memwatch") {  // memwatch <rel> <hex value> <len> <hits> [nth]: hardware watch on a match
            std::string v;
            int rel = 0, len = 1, hits = 4, nth = 0;
            in >> rel >> v >> len >> hits >> nth;
            if (!parse_hex(v, st.payload)) continue;
            st.kind = Step::MemWatch;
            st.to[0] = static_cast<float>(rel);
            st.to[1] = static_cast<float>(len);
            st.to[2] = static_cast<float>(hits);
            st.wait_ms = static_cast<uint32_t>(nth);
            steps.push_back(st);
            continue;
        }
        if (kind == "trade3111") {  // trade3111 <item hex> <count> <receiver hex> <inventory hex> <entry> [slot hex]
            std::string item, count, recv, inv, entry, slot = "FFFF";
            in >> item >> count >> recv >> inv >> entry;
            if (in >> slot) {}
            try {
                st.kind = Step::C2S;
                st.op = 3111;
                st.addr = 0x3111;  // marks the giver key to fill in at run time
                const uint32_t it = static_cast<uint32_t>(std::stoul(item, nullptr, 16));
                const uint64_t n = std::stoull(count);
                const uint32_t rv = static_cast<uint32_t>(std::stoul(recv, nullptr, 16));
                const uint16_t iv = static_cast<uint16_t>(std::stoul(inv, nullptr, 16));
                const uint32_t en = static_cast<uint32_t>(std::stoul(entry));
                const uint16_t sl = static_cast<uint16_t>(std::stoul(slot, nullptr, 16));
                const uint16_t b = 0xFFFF;
                st.payload.resize(30);
                std::memcpy(&st.payload[0], &it, 4);
                std::memcpy(&st.payload[4], &b, 2);
                std::memcpy(&st.payload[6], &n, 8);
                // giver @14 (filled at run time), receiver @18
                std::memcpy(&st.payload[18], &rv, 4);
                std::memcpy(&st.payload[22], &iv, 2);
                std::memcpy(&st.payload[24], &sl, 2);
                std::memcpy(&st.payload[26], &en, 4);
            } catch (...) {
                log::warn("Dev script: line {} needs trade3111 <item hex> <count> <receiver hex> <inventory hex> <entry> "
                          "[slot hex]", lineno);
                continue;
            }
            steps.push_back(std::move(st));
            continue;
        }
        if (kind == "dumpitemmove") {  // dumpitemmove: B11, 3111's item-move definitions (read only)
            st.kind = Step::DumpItemMove;
            steps.push_back(st);
            continue;
        }
        if (kind == "killall") {  // killall: KillCheatReq on every loaded actor but animals (live quest research)
            st.kind = Step::KillAll;
            steps.push_back(st);
            continue;
        }
        if (kind == "day") {
            st.kind = Step::Day;
            in >> st.to[0];
            steps.push_back(st);
            continue;
        }
        if (kind == "time") {
            char colon = 0;
            if (!(in >> st.to[0] >> colon >> st.to[1]) || colon != ':') {
                log::warn("Dev script: line {} needs time hh:mm", lineno);
                continue;
            }
            st.kind = Step::Time;
            steps.push_back(st);
            continue;
        }
        if (kind == "poke") {
            std::string a, rest;
            in >> a;
            std::getline(in, rest);
            st.kind = Step::Poke;
            st.addr = static_cast<uintptr_t>(std::stoull(a, nullptr, 16));
            if (!st.addr || !parse_hex(rest, st.payload) || st.payload.empty()) {
                log::warn("Dev script: line {} needs poke <hex address> <hex bytes>", lineno);
                continue;
            }
            steps.push_back(std::move(st));
            continue;
        }
        if (kind == "killnear" || kind == "lootlast") {
            st.kind = kind == "killnear" ? Step::KillNear : Step::LootLast;
            float r = 15.0f;
            in >> r;
            st.to[0] = r > 0 ? r : 15.0f;
            steps.push_back(std::move(st));
            continue;
        }
        if (kind == "npcs") {  // npcs [radius m, default 40]: the loaded NPC actors near us, nearest first (log only)
            st.kind = Step::Npcs;
            float r = 40.0f, from = 0.0f;  // npcs [radius] [from]: only those at least `from` m away
            in >> r >> from;
            st.to[0] = r > 0 ? r : 40.0f;
            st.to[1] = from > 0 ? from : 0.0f;
            steps.push_back(std::move(st));
            continue;
        }
        if (kind == "tpkill" || kind == "tpactor") {  // tpactor <hex key>: 1 m beside a loaded actor; tpkill: beside
            st.kind = Step::TpActor;                    // the animal the last killgame killed
            st.addr = 0;
            std::string h;
            if (kind == "tpactor" && (!(in >> h) || (st.addr = std::strtoull(h.c_str(), nullptr, 16)) == 0)) {
                log::warn("Dev script: line {} needs tpactor <hex actor key>", lineno);
                continue;
            }
            steps.push_back(st);
            continue;
        }
        if (kind == "tp" || kind == "tprel") {  // tprel dx dy dz: from where we stood when the script started
            st.kind = Step::Teleport;
            if (!(in >> st.to[0] >> st.to[1] >> st.to[2])) {
                log::warn("Dev script: line {} needs {} x y z", lineno, kind);
                continue;
            }
            if (kind == "tprel") {
                for (int i = 0; i < 3; ++i) st.to[i] += pos[i];
            }
            steps.push_back(st);
            continue;
        }
        unsigned op = 0;
        if ((kind != "c2s" && kind != "s2c") || !(in >> op)) {
            log::warn("Dev script: line {} not understood", lineno);
            continue;
        }
        std::string rest;
        std::getline(in, rest);
        replace_all(rest, "{P}", hex_of(&own_player, 4));
        replace_all(rest, "{U}", hex_of(&user, 4));
        replace_all(rest, "{POS}", hex_of(pos, 12));
        if (!parse_hex(rest, st.payload)) {
            log::warn("Dev script: line {} has a bad hex payload", lineno);
            continue;
        }
        st.kind = kind == "c2s" ? Step::C2S : Step::S2C;
        st.op = static_cast<uint16_t>(op);
        steps.push_back(std::move(st));
    }
    int packets = 0;
    for (const auto& st : steps) packets += st.kind != Step::Wait;
    log::info("Dev script: {} packets from {}", packets, path);
    std::thread([steps = std::move(steps)] {
        for (const auto& st : steps) {
            if (st.kind == Step::Wait) {
                Sleep(st.wait_ms);
            } else if (st.kind == Step::Watch) {
                dev_watch_start(st.addr, static_cast<int>(st.to[0]), static_cast<int>(st.to[1]));
            } else if (st.kind == Step::Pad) {
                input::dev_pad_press(static_cast<uint16_t>(st.addr), st.wait_ms ? st.wait_ms : 150);
                log::info("Dev script: pad 0x{:04X} for {} ms", st.addr, st.wait_ms);
                Sleep(st.wait_ms ? st.wait_ms : 150);
            } else if (st.kind == Step::MemScan) {
                dev_memscan(st.payload);
            } else if (st.kind == Step::Advance) {
                quest_events_request_advance(static_cast<uint32_t>(st.addr));
                log::info("Dev script: advance {:08X}", st.addr);
            } else if (st.kind == Step::WaitFile) {
                const std::string f(st.payload.begin(), st.payload.end());
                int i = 0;
                for (; i < 3000 && GetFileAttributesA(f.c_str()) == INVALID_FILE_ATTRIBUTES; ++i) Sleep(100);
                DeleteFileA(f.c_str());
                log::info("Dev script: waitfile {} {}", f, i < 3000 ? "found" : "timed out");
            } else if (st.kind == Step::DScan) {
                dev_dscan(st.to[0], st.to[1]);
            } else if (st.kind == Step::DFilter) {
                dev_dfilter(st.to[0], st.to[1]);
            } else if (st.kind == Step::DWatch) {
                dev_dwatch(static_cast<int>(st.wait_ms ? st.wait_ms : 6));
            } else if (st.kind == Step::FScan) {
                dev_fscan_range(st.to[0], st.to[1]);
            } else if (st.kind == Step::FFilter) {
                dev_ffilter(st.to[0], st.to[1]);
            } else if (st.kind == Step::FWatch) {
                dev_fwatch(static_cast<int>(st.wait_ms ? st.wait_ms : 8));
            } else if (st.kind == Step::Key) {
                const auto vk = static_cast<BYTE>(st.addr);
                const auto scan = static_cast<BYTE>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
                keybd_event(vk, scan, 0, 0);
                Sleep(st.wait_ms ? st.wait_ms : 60);
                keybd_event(vk, scan, KEYEVENTF_KEYUP, 0);
                log::info("Dev script: key 0x{:02X} ({} ms)", vk, st.wait_ms);
            } else if (st.kind == Step::GuestHit) {
                const bool ok = guest_hits_dev_hit(static_cast<uint32_t>(st.addr), st.wait_ms);
                log::info("Dev script: guesthit {:08X}/{}{}", st.addr, st.wait_ms, ok ? "" : " - no enemy copy within 150 m");
            } else if (st.kind == Step::GrantXp) {
                const bool ok = quest_advance_grant_xp(static_cast<uint32_t>(st.addr), st.wait_ms);
                log::info("Dev script: grantxp {} +{}{}", st.addr, st.wait_ms, ok ? " (queued)" : " NOT queued");
            } else if (st.kind == Step::PrologueDone) {
                prologue_done_now();
                log::info("Dev script: prologue done");
            } else if (st.kind == Step::RideHorse) {
                ride_horse(std::min<uint32_t>(st.wait_ms, 120), static_cast<BYTE>(st.addr));
            } else if (st.kind == Step::WatchStatus) {
                dev_watch_status(static_cast<int>(st.addr), static_cast<int>(st.to[0]), static_cast<int>(st.to[1]));
            } else if (st.kind == Step::PtrScan) {
                dev_ptrscan(static_cast<uint32_t>(st.addr));
            } else if (st.kind == Step::ClockScan) {
                dev_clockscan();
            } else if (st.kind == Step::DumpItemMove) {
                dev_dump_item_move();
            } else if (st.kind == Step::GSnap) {
                dev_gsnap();
            } else if (st.kind == Step::GDiff) {
                dev_gdiff(static_cast<int>(st.wait_ms));
            } else if (st.kind == Step::MemDiff) {
                dev_memdiff();
            } else if (st.kind == Step::MemWatch) {
                dev_memwatch(static_cast<int>(st.to[0]), st.payload, static_cast<int>(st.to[1]),
                             static_cast<int>(st.to[2]), static_cast<int>(st.wait_ms));
            } else if (st.kind == Step::KillGame) {
                hunt_dev_kill();
            } else if (st.kind == Step::KillAll) {
                int sent = 0;
                for (const auto& [key, variant] : enemy_loaded_variants()) {
                    if (hunt_animal_name(variant) || (key & 0xFF000000) != 0xB0000000 || sent >= 40) continue;
                    std::vector<uint8_t> pay(8, 0);  // KillCheatReq: u32 key | u32 option (0 = at once)
                    std::memcpy(pay.data(), &key, 4);
                    sent += sniffer_inject_c2s(make_c2s(2210, pay)) ? 1 : 0;
                    log::info("Dev script: KillCheatReq on 0x{:08X} (variant {})", key, variant);
                    Sleep(150);
                }
                log::info("Dev script: killall sent {}", sent);
            } else if (st.kind == Step::Day) {
                time_sync_dev_add_days(static_cast<int>(st.to[0]));
            } else if (st.kind == Step::Time) {
                time_sync_dev_set(static_cast<int>(st.to[0]), static_cast<int>(st.to[1]));
            } else if (st.kind == Step::Poke) {
                MEMORY_BASIC_INFORMATION mbi{};
                const bool ok = VirtualQuery(reinterpret_cast<void*>(st.addr), &mbi, sizeof mbi) &&
                                mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE));
                if (ok) std::memcpy(reinterpret_cast<void*>(st.addr), st.payload.data(), st.payload.size());
                log::info("Dev script: poke 0x{:X} ({} bytes){}", st.addr, st.payload.size(), ok ? "" : " REFUSED (not writable)");
            } else if (st.kind == Step::KillNear) {
                kill_nearest_person(st.to[0]);
            } else if (st.kind == Step::LootLast) {
                loot_last_killed();
            } else if (st.kind == Step::Npcs) {
                log_npcs_near(st.to[0], st.to[1]);
            } else if (st.kind == Step::TpActor) {
                const uint32_t key = st.addr ? static_cast<uint32_t>(st.addr) : hunt_dev_last_kill();
                const auto at = key ? game::actor_world_position(key) : std::nullopt;
                if (at) revive_teleport_to(at->x + 1.0f, at->y + 0.5f, at->z);
                log::info("Dev script: teleport beside actor 0x{:08X}: {}", key,
                          at ? std::format("({:.1f}, {:.1f}, {:.1f})", at->x, at->y, at->z) : std::string("NOT loaded"));
            } else if (st.kind == Step::Teleport) {
                revive_teleport_to(st.to[0], st.to[1], st.to[2]);
                log::info("Dev script: teleport to ({:.0f}, {:.0f}, {:.0f})", st.to[0], st.to[1], st.to[2]);
            } else if (st.kind == Step::C2S) {
                std::vector<uint8_t> pl = st.payload;
                if (st.addr == 0x3111 && pl.size() == 30) {  // trade3111: the giver = our character, now
                    const uint32_t me = capture_own_player_key();
                    std::memcpy(&pl[14], &me, 4);
                    uint32_t rv = 0;
                    std::memcpy(&rv, &pl[18], 4);
                    if (!rv) std::memcpy(&pl[18], &me, 4);  // receiver 0 = ourselves (the single-player move)
                    log::info("Dev script: trade3111 giver 0x{:08X} receiver 0x{:08X}", me, rv ? rv : me);
                }
                const bool ok = sniffer_inject_c2s(make_c2s(st.op, pl));
                log::info("Dev script: c2s {} ({} bytes){}", st.op, st.payload.size(), ok ? "" : " NOT sent");
            } else {
                puppet_inject_s2c_batch({{st.op, st.payload}});
                log::info("Dev script: s2c {} ({} bytes)", st.op, st.payload.size());
            }
        }
    }).detach();
    return packets;
}

}  // namespace cdmp::net
