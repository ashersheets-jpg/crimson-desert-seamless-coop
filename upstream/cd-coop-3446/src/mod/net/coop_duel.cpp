#include "mod/settings_menu.h"
#include "mod/net/coop_duel.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <format>
#include <mutex>

#include "mod/input/pad_input.h"
#include "mod/log.h"
#include "mod/net/coop_activity.h"
#include "mod/net/guest_hits.h"
#include "mod/net/local_capture.h"
#include "mod/net/player_hits.h"
#include "mod/net/remote_puppet.h"
#include "mod/net/wire_layout.h"

namespace cdmp::net {
namespace {

constexpr uint8_t kDuelType = 12;  // Activity event type (coop_activity.cpp Type)
enum Sub : uint8_t { kStart = 1, kHit = 2, kLost = 3 };
enum Why : uint8_t { kDown = 1, kOut = 2 };
enum Phase : uint8_t { kIdle, kWaitStart, kCountdown, kFight, kOver };

constexpr uint16_t kOpStartActionReq = 2431;
constexpr float kRadiusM = 20.0f;       // the duel ground
constexpr uint32_t kOutMs = 10000;      // out of it this long in a row: the round is lost
constexpr uint32_t kCountdownMs = 3000;
constexpr uint32_t kFightTitleMs = 1200;  // "Fight!" stays this long
constexpr uint32_t kOverMs = 6000;      // the result on screen, then the next round
constexpr uint32_t kImpactMs = 280;     // a swing lands this long after it starts
constexpr uint32_t kSwingGapMs = 220;   // one hit per swing: the same attack again within this is the same swing
constexpr float kReachM = 3.5f;
constexpr float kArcDeg = 75.0f;
constexpr double kFloor = 0.03;         // a round is lost at 3% health (nobody dies in a duel)
constexpr uint32_t kOurCharacterThere = 0xA010F001;

struct Swing {
    uint32_t hash = 0;
    double base = 0;              // HP on a defence-1.0 enemy (guest_hits)
    std::array<float, 3> pos{};
    float fx = 0, fz = 1;         // facing (unit, XZ)
    uint32_t at = 0;
};

struct State {
    Phase phase = kIdle;
    uint32_t round = 0;
    std::array<float, 3> centre{};
    float radius = kRadiusM;
    uint32_t since = 0;           // phase start
    uint32_t out_since = 0;       // 0 = inside
    int64_t start_hp = -1;        // our HP when the round started (x1000)
    int result = 0;               // the last round: 1 we won, 2 they did
    Why why = kDown;
    bool lost_sent = false;
    float partner_hp = -1;
    std::deque<Swing> swings;
    std::deque<std::vector<uint8_t>> hits_in;  // PlayerHit payloads for us, applied by the tick
    uint32_t last_swing_hash = 0, last_swing_at = 0;
    bool have_start = false;      // the duel ground of `round` is known
    bool reported = false;        // the round's result went to the challenge
    uint32_t next_round = 0;      // the inviter's latest round start (guest side), its ground:
    std::array<float, 3> next_centre{};
    float next_radius = kRadiusM;
} g;
std::mutex g_mutex;
std::atomic<bool> g_test{false};
std::atomic<bool> g_on{false};  // a duel challenge is on (read by the c2s hook)
std::atomic<uint32_t> g_attack_input_at{0};  // GetTickCount of the last attack button press (0 none)
constexpr uint32_t kInputToActionMs = 350;   // the swing's StartAction follows the press within this
constexpr double kUnknownBase = 15.0;        // an attack the damage tables do not know: a middling blow

// The attack buttons (the game's controls, progression.md): weak / strong attack and kick. Mouse left / F on a
// keyboard (right mouse aims or guards there), RB / RT / Y on a pad.
bool attack_input_down() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    const bool focused = pid == GetCurrentProcessId();
    if (focused && ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) || (GetAsyncKeyState('F') & 0x8000))) return true;
    return (input::pad_buttons() & (input::kPadRB | input::kPadRT | input::kPadY)) != 0 && !input::chord_held();
}

float dist_xz(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    const float dx = a[0] - b[0], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dz * dz);
}

void send(Sub sub, uint32_t round, const void* data, size_t n) {
    std::vector<uint8_t> b(5 + n);
    b[0] = sub;
    std::memcpy(b.data() + 1, &round, 4);
    if (n) std::memcpy(b.data() + 5, data, n);
    activity_send(kDuelType, b);
}

float u_float(const uint8_t* p) {
    float f;
    std::memcpy(&f, p, 4);
    return f;
}

}  // namespace

void duel_set_test(bool on) { g_test = on; }

void duel_observe_c2s(const uint8_t* packet, size_t size) {
    if (!g_on || size != 5 + wire::kActionReqSize) return;
    uint16_t op;
    std::memcpy(&op, packet, 2);
    if (op != kOpStartActionReq) return;
    const uint8_t* p = packet + 5;
    uint32_t key, hash;
    std::memcpy(&key, p, 4);
    std::memcpy(&hash, p + 4, 4);  // the action (the same hash the attack's frame events name)
    if (!key || key != capture_own_player_key()) return;
    double base = guest_hits_action_damage(hash);
    if (base <= 0) {
        // Not in the tables (attacks are learnt from fights against enemies): a swing when the player just pressed
        // an attack button.
        const uint32_t pressed = g_attack_input_at.load();
        const bool swing = attack_input_down() || (pressed && GetTickCount() - pressed < kInputToActionMs);
        if (!swing || !hash) return;
        base = kUnknownBase;
    }
    // Facing from the actor's quaternion (x, y, z, w): yaw about +Y, forward = (-sin yaw, -cos yaw) on XZ
    // (measured 2026-11-02: facing +X is (0, -0.717, 0, 0.697), facing +Z is (0, 1, 0, 0.019)).
    const float qy = u_float(p + wire::kActionRot + 4), qw = u_float(p + wire::kActionRot + 12);
    const float yaw = 2.0f * std::atan2(qy, qw);
    Swing s;
    s.hash = hash;
    s.base = base;
    for (int i = 0; i < 3; ++i) s.pos[i] = u_float(p + wire::kActionPos + 4 * i);
    s.fx = -std::sin(yaw);
    s.fz = -std::cos(yaw);
    s.at = GetTickCount();
    if (!std::isfinite(s.fx) || !std::isfinite(s.fz)) return;
    std::lock_guard lock(g_mutex);
    if (g.phase != kFight) return;
    if (hash == g.last_swing_hash && s.at - g.last_swing_at < kSwingGapMs) return;
    g.last_swing_hash = hash, g.last_swing_at = s.at;
    if (g.swings.size() < 8) g.swings.push_back(s);
}

void duel_on_event(const uint8_t* d, size_t n) {
    if (n < 5) return;
    uint32_t round;
    std::memcpy(&round, d + 1, 4);
    std::lock_guard lock(g_mutex);
    switch (d[0]) {
        case kStart:
            if (n >= 5 + 16 && round > g.next_round && !g_test) {
                g.next_round = round;
                for (int i = 0; i < 3; ++i) g.next_centre[i] = u_float(d + 5 + 4 * i);
                g.next_radius = std::clamp(u_float(d + 17), 8.0f, 60.0f);
            }
            break;
        case kHit:
            if (round == g.round && g.phase == kFight && g.hits_in.size() < 8) {
                g.hits_in.emplace_back(d + 5, d + n);
            }
            break;
        case kLost:
            if (round == g.round && (g.phase == kFight || g.phase == kCountdown) && !g_test) {
                g.phase = kOver;
                g.result = 1;
                g.why = n >= 6 && d[5] == kOut ? kOut : kDown;
                g.since = GetTickCount();
                g.lost_sent = true;  // nothing for us to send
            }
            break;
        default: break;
    }
}

DuelTick duel_tick(bool on, bool inviter, bool connected, std::optional<std::array<float, 3>> me,
                   std::optional<std::array<float, 3>> partner, const std::string& who, uint32_t now) {
    DuelTick out;
    now = GetTickCount();  // one clock with duel_hud and the c2s hook (the caller's is another)
    g_on = on;
    // Attack presses (rising edge), for duel_observe_c2s.
    static bool attack_was = false;
    const bool attack = on && attack_input_down();
    if (attack && !attack_was) g_attack_input_at = now;
    attack_was = attack;
    // Where our swings may land: what our screen shows of them (the copy, drawn ~0.3-0.4 s late) and where they
    // last said they are. Co-op+ (2026-11-05 playtest: 40 of 49 swings missed at 0.4-2.5 m because the late copy
    // was behind us): a swing that reaches either counts.
    std::optional<std::array<float, 3>> reported = partner;
    if (auto pp = puppet_position()) partner = std::array<float, 3>{pp->x, pp->y, pp->z};
    std::vector<std::vector<uint8_t>> hits_in;
    std::vector<Swing> swings;
    std::optional<std::pair<Sub, std::vector<uint8_t>>> send_it;
    int64_t heal = 0;
    uint32_t round = 0;
    {
        std::lock_guard lock(g_mutex);
        if (!on || (!connected && !g_test)) {
            if (g.phase != kIdle) log::info("Duel: over");
            g = State{};
            return out;
        }
        const int64_t hp = hits_own_hp(), max = hits_own_hp_max();
        switch (g.phase) {
            case kIdle:
            case kWaitStart: {
                if (g.phase == kIdle) {
                    g.phase = kWaitStart;
                    g.round = 1;
                    g.since = now;
                    g.have_start = false;
                }
                if ((inviter || g_test) && !g.have_start) {
                    // We place the duel ground: half way between us (or here, when they are not drawn).
                    if (!me) break;
                    g.centre = *me;
                    if (partner && dist_xz(*me, *partner) < 60.0f) {
                        for (int i = 0; i < 3; ++i) g.centre[i] = ((*me)[i] + (*partner)[i]) * 0.5f;
                    }
                    g.radius = settings::duel_ring_m();  // Co-op+ (T42): sent below, so both use it
                    std::vector<uint8_t> b(16);
                    std::memcpy(b.data(), g.centre.data(), 12);
                    std::memcpy(b.data() + 12, &g.radius, 4);
                    send_it = std::make_pair(kStart, std::move(b));
                    g.have_start = true;
                }
                if (!g.have_start && g.next_round == g.round) {
                    g.centre = g.next_centre;
                    g.radius = g.next_radius;
                    g.have_start = true;
                } else if (!g.have_start && g.next_round > g.round) {
                    g.round = g.next_round;  // they are a round ahead (we missed one): catch up
                    break;
                }
                if (!g.have_start) {
                    out.banner = std::format("Duel vs {}: waiting for {}", who, who);
                    break;
                }
                g.phase = kCountdown;
                g.since = now;
                g.out_since = 0;
                g.start_hp = -1;  // taken when the fight starts (a heal from the last round lands meanwhile)
                g.lost_sent = false;
                g.reported = false;
                g.result = 0;
                g.swings.clear();
                g.hits_in.clear();
                log::info("Duel: round {} at ({:.1f}, {:.1f}, {:.1f}), radius {:.0f} m", g.round, g.centre[0],
                          g.centre[1], g.centre[2], g.radius);
                break;
            }
            case kCountdown:
                if (now - g.since >= kCountdownMs) {
                    g.phase = kFight;
                    g.since = now;
                    g.start_hp = hp;
                    log::info("Duel: fight (round {}, our hp {:.1f})", g.round, hp / 1000.0);
                }
                break;
            case kFight: {
                swings.assign(g.swings.begin(), g.swings.end());
                g.swings.clear();
                hits_in.assign(g.hits_in.begin(), g.hits_in.end());
                g.hits_in.clear();
                // Out of the duel ground too long, or brought down: the round is theirs.
                const bool outside = me && dist_xz(*me, g.centre) > g.radius;
                if (!outside) g.out_since = 0;
                else if (!g.out_since) g.out_since = now;
                std::optional<Why> lost;
                if (g.out_since && now - g.out_since >= kOutMs) lost = kOut;
                if (hp >= 0 && max > 0 && hp <= static_cast<int64_t>(max * kFloor) + 500) lost = kDown;
                if (lost) {
                    g.phase = kOver;
                    g.result = 2;
                    g.why = *lost;
                    g.since = now;
                    const uint8_t why = *lost;
                    send_it = std::make_pair(kLost, std::vector<uint8_t>{why});
                    log::info("Duel: we lost round {} ({})", g.round, *lost == kOut ? "out of the duel ground" : "down");
                }
                break;
            }
            case kOver:
                if (now - g.since >= kOverMs) {
                    // Back to the health we started the round with, then the next round.
                    if (g.start_hp > 0 && hp >= 0 && hp < g.start_hp) heal = g.start_hp - hp;
                    g.round += 1;
                    g.phase = kWaitStart;
                    g.since = now;
                    g.have_start = false;
                }
                break;
        }
        round = g.round;
    }
    if (send_it) send(send_it->first, round, send_it->second.data(), send_it->second.size());
    if (heal > 0) {
        hits_vary_own_hp(heal);
        log::info("Duel: healed back {:.1f} hp", heal / 1000.0);
    }
    // Our swings that reach them.
    for (const Swing& s : swings) {
        if (!partner && !reported) break;
        // Judged at the impact: where they are now, from where we are now.
        const auto from = me.value_or(s.pos);
        // ([dev] duel_test: the echo copy always stands at our side, never in front: any direction.)
        const float arc = g_test ? 180.0f : kArcDeg;
        float d = 1e9f, cosang = -1.0f;
        bool hit = false;
        for (const auto& at : {partner, reported}) {
            if (!at) continue;
            const float dx = (*at)[0] - from[0], dz = (*at)[2] - from[2];
            const float dd = std::sqrt(dx * dx + dz * dz);
            const float dy = std::fabs((*at)[1] - from[1]);
            const float ca = dd > 0.01f ? (dx * s.fx + dz * s.fz) / dd : 1.0f;
            const bool h = dd <= kReachM && dy < 2.0f && ca >= std::cos(arc * 3.14159265f / 180.0f);
            if (h || dd < d) {
                d = dd;
                cosang = ca;
            }
            hit = hit || h;
        }
        log::info("Duel: our swing 0x{:08X} (base {:.1f}): {:.1f} m, {:.0f} deg -> {}", s.hash, s.base, d,
                  std::acos(std::clamp(cosang, -1.0f, 1.0f)) * 57.2958f, hit ? "HIT" : "miss");
        if (!hit) continue;
        const auto payload = hits_make_pvp(s.hash, static_cast<int64_t>(s.base * 1000));
        send(kHit, round, payload.data(), payload.size());
    }
    // Their swings on us.
    for (auto& h : hits_in) {
        const int64_t max = hits_own_hp_max();
        if (h.size() < 16 || max <= 0) continue;
        int64_t base;
        std::memcpy(&base, h.data() + 8, 8);
        // 4% of our health for the lightest blow up to 10% for the heaviest: 10..25 hits a round.
        const double b = std::clamp(base / 1000.0, 0.0, 40.0);
        const auto damage = static_cast<int64_t>(static_cast<double>(max) * (0.04 + b / 40.0 * 0.06));
        std::memcpy(h.data() + 8, &damage, 8);
        hits_on_pvp(h, static_cast<int64_t>(max * kFloor));
    }
    // What to say.
    std::lock_guard lock(g_mutex);
    if (g.phase == kOver && g.result && !g.reported) {
        g.reported = true;
        out.round_result = g.result;
    }
    if (g.phase == kFight && g.out_since) {
        const int left = static_cast<int>((kOutMs - std::min(kOutMs, now - g.out_since) + 999) / 1000);
        out.banner = std::format("Duel vs {}: round {}   (out of the duel ground: {} s)", who, g.round, left);
    } else if (g.phase == kFight) {
        out.banner = std::format("Duel vs {}: round {}", who, g.round);
    }
    return out;
}

DuelHud duel_hud() {
    DuelHud h;
    std::lock_guard lock(g_mutex);
    if (g.phase == kIdle || (g.phase == kWaitStart && !g.have_start)) return h;
    const uint32_t now = GetTickCount();
    h.active = true;
    h.centre = g.centre;
    h.radius = g.radius;
    const uint32_t t = now - g.since;
    if (g.phase == kCountdown) {
        const uint32_t left = (kCountdownMs - std::min(kCountdownMs, t) + 999) / 1000;
        h.title = std::to_string(std::max<uint32_t>(left, 1));
        h.subtitle = g.round == 1 ? "Stay within the duel ground" : std::format("Round {}", g.round);
        h.title_color = 0xFF82C8E8;  // gold (ABGR)
        h.title_alpha = 1.0f - static_cast<float>(t % 1000) / 1600.0f;
    } else if (g.phase == kFight && t < kFightTitleMs) {
        h.title = "Fight!";
        h.title_color = 0xFF82C8E8;
        h.title_alpha = 1.0f - static_cast<float>(t) / kFightTitleMs;
    } else if (g.phase == kOver) {
        const bool won = g.result == 1;
        h.title = won ? "Victory" : "Defeat";
        h.subtitle = won ? (g.why == kOut ? "Your opponent fled the duel ground" : "Your opponent yields")
                         : (g.why == kOut ? "You left the duel ground" : "You yield");
        h.title_color = won ? 0xFF82C8E8 : 0xFF2830C4;  // gold / the death screen's red
        h.title_alpha = t < kOverMs - 1000 ? 1.0f : std::max(0.0f, (kOverMs - t) / 1000.0f);
    }
    if (g.phase == kFight && g.out_since) {
        h.out_left_s = std::max(0.0f, (kOutMs - static_cast<float>(now - g.out_since)) / 1000.0f);
        if (h.title.empty()) {
            h.title = "Return to the duel ground";
            h.subtitle = std::format("{:.0f} s, or the round is forfeit", std::ceil(h.out_left_s));
            h.title_color = 0xFF2830C4;
            h.title_alpha = 1.0f;
        }
    }
    return h;
}

}  // namespace cdmp::net
