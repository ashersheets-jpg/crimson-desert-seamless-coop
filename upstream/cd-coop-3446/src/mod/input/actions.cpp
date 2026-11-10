#include "mod/input/actions.h"

#include <atomic>
#include <windows.h>

#include <cstring>

#include <cctype>
#include <format>

#include "mod/input/pad_input.h"
#include "mod/native_layer.h"

namespace cdmp::input {
namespace {

Binding kBindings[] = {  // Co-op+ (T30): not const, [keys] in cdmp.ini rebinds (load_bindings)
    {"Invite", VK_F1, true, false, kPadY, kPadY, true},
    {"Leave", VK_F2, true, false, kPadB, kPadB, true},
    {"Ping", VK_F3, true, false, kPadLeft, kPadLeft, true},
    {"GoToFriend", VK_F4, true, false, kPadRight, kPadRight, true},
    {"Challenge", VK_F5, false, true, kPadUp, kPadUp, true},
    {"Decline", VK_F6, false, true, kPadDown, kPadDown, true},
    {"Panel", VK_F8, false, false, kPadX, kPadX, true},
    {"Nameplates", VK_F8, true, false, kPadA, kPadA, true},
    {"ReviveNow", 'Q', false, false, kPadX, kPadX, false},
    {"ReviveCheckpoint", 'U', false, false, kPadY, kPadY, false},
    {"RpsScissors", 'U', false, false, kPadX, kPadX, false},
    {"RpsPaper", 'F', false, false, kPadY, kPadY, false},
    {"RpsRock", 'E', false, false, kPadB, kPadB, false},
    {"Skip", VK_ESCAPE, false, false, kPadStart | kPadB, kPadStart, false},
    {"Mash", 'E', false, false, kPadFace, kPadA, false},
    {"MenuPrev", VK_LEFT, false, false, kPadLeft, kPadLeft, false},
    {"MenuNext", VK_RIGHT, false, false, kPadRight, kPadRight, false},
    {"Resync", VK_F8, false, true, kPadRB, kPadRB, true},
    {"ReviveFriend", VK_F7, false, true, kPadLB, kPadLB, true},
    {"Summon", VK_F4, false, true, kPadR3, kPadR3, true},
    {"Chat", VK_F2, false, true, 0, 0, true},  // keyboard only (pads use the quick list on Ping)
    {"Help", VK_F1, false, true, kPadL3, kPadL3, true},  // Co-op+ (T31): the controls card
    {"MenuUp", 'W', false, false, kPadUp, kPadUp, false},
    {"MenuDown", 'S', false, false, kPadDown, kPadDown, false},
    {"MenuSelect", VK_SPACE, false, false, kPadA, kPadA, false},
    {"MenuBack", VK_ESCAPE, false, false, kPadB, kPadB, false},
    {"Interact", 'R', false, false, kPadA, kPadA, false},
};
static_assert(std::size(kBindings) == static_cast<size_t>(Action::Count));

bool key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

}  // namespace

const Binding& binding(Action a) { return kBindings[static_cast<size_t>(a)]; }

namespace {
std::atomic<uint64_t> g_triggered_until[static_cast<size_t>(Action::Count)];
}  // namespace

void trigger(Action a) {
    if (a < Action::Count) g_triggered_until[static_cast<size_t>(a)] = GetTickCount64() + 200;
}

bool action_down(Action a, bool focused) {
    if (a < Action::Count && GetTickCount64() < g_triggered_until[static_cast<size_t>(a)].load()) return true;
    const Binding& b = binding(a);
    if (focused && b.vk && key(b.vk) && key(VK_CONTROL) == b.ctrl && (key(VK_SHIFT) == b.shift || (!b.shift && !b.ctrl))) {
        return true;
    }
    const uint16_t pad = pad_buttons();
    if (!(pad & b.pad)) return false;
    return b.chord ? chord_held() : !chord_held();
}

namespace {

struct KeyName {
    const char* name;
    int vk;
};
constexpr KeyName kKeys[] = {
    {"F1", VK_F1}, {"F2", VK_F2}, {"F3", VK_F3}, {"F4", VK_F4}, {"F5", VK_F5}, {"F6", VK_F6}, {"F7", VK_F7},
    {"F8", VK_F8}, {"F9", VK_F9}, {"F10", VK_F10}, {"F11", VK_F11}, {"F12", VK_F12}, {"Esc", VK_ESCAPE},
    {"Tab", VK_TAB}, {"Space", VK_SPACE}, {"Enter", VK_RETURN}, {"Insert", VK_INSERT}, {"Delete", VK_DELETE},
    {"Home", VK_HOME}, {"End", VK_END}, {"PageUp", VK_PRIOR}, {"PageDown", VK_NEXT}, {"Left", VK_LEFT},
    {"Right", VK_RIGHT}, {"Up", VK_UP}, {"Down", VK_DOWN}, {"Backspace", VK_BACK},
};
struct PadName {
    const char* name;
    uint16_t bit;
};
constexpr PadName kPads[] = {
    {"A", kPadA}, {"B", kPadB}, {"X", kPadX}, {"Y", kPadY}, {"LB", kPadLB}, {"RB", kPadRB}, {"L3", kPadL3},
    {"R3", kPadR3}, {"Up", kPadUp}, {"Down", kPadDown}, {"Left", kPadLeft}, {"Right", kPadRight},
};

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s += c < 0x80 ? static_cast<char>(c) : '?';
    return s;
}

// "Ctrl+Shift+F1" -> vk + modifiers; false when not understood.
bool parse_key(const std::string& text, int& vk, bool& ctrl, bool& shift) {
    vk = 0;
    ctrl = shift = false;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t plus = text.find('+', start);
        std::string part = text.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        while (!part.empty() && part.front() == ' ') part.erase(part.begin());
        while (!part.empty() && part.back() == ' ') part.pop_back();
        const std::string l = lower(part);
        if (l == "ctrl" || l == "control") {
            ctrl = true;
        } else if (l == "shift") {
            shift = true;
        } else if (part.size() == 1 && std::isalnum(static_cast<unsigned char>(part[0]))) {
            vk = std::toupper(static_cast<unsigned char>(part[0]));
        } else {
            bool found = false;
            for (const auto& k : kKeys) {
                if (lower(k.name) == l) {
                    vk = k.vk;
                    found = true;
                }
            }
            if (!found) return false;
        }
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    return vk != 0;
}

std::string vk_text(int vk) {
    for (const auto& k : kKeys) {
        if (k.vk == vk) return k.name;
    }
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return std::string(1, static_cast<char>(vk));
    return "?";
}

}  // namespace

bool chords_bound() {
    for (const auto& b : kBindings) {
        if (b.chord && b.pad) return true;
    }
    return false;
}

std::string key_text(Action a) {
    const Binding& b = binding(a);
    if (!b.vk) return "-";
    return std::string(b.ctrl ? "Ctrl+" : "") + (b.shift ? "Shift+" : "") + vk_text(b.vk);
}

std::vector<std::string> load_bindings(const std::wstring& ini_path) {
    std::vector<std::string> warnings;
    // Shared world (user 2026-11-10): no hidden key shortcuts. Everything is reached as in the game itself: hold
    // interact on the friend, or the menu's Co-op tab. The co-op hotkeys start unbound ([keys] can still bind them);
    // Chat keeps its key (typing needs one).
    if (kNativeLayer) {
        for (auto& b : kBindings) {
            if (!b.chord || b.name == "Chat") continue;
            b.vk = 0;
            b.ctrl = b.shift = false;
            b.pad = b.pad_shown = 0;
        }
    }
    for (auto& b : kBindings) {
        if (!b.chord) continue;  // only the co-op hotkeys; prompt keys (Q/U, RPS, Mash, arrows) follow the game's boxes
        const std::wstring name(b.name.begin(), b.name.end());
        wchar_t buf[64] = {};
        GetPrivateProfileStringW(L"keys", name.c_str(), L"", buf, 64, ini_path.c_str());
        if (buf[0]) {
            int vk;
            bool ctrl, shift;
            if (parse_key(narrow(buf), vk, ctrl, shift)) {
                b.vk = vk;
                b.ctrl = ctrl;
                b.shift = shift;
            } else {
                warnings.push_back(std::format("[keys] {}={} not understood (example: {}=Ctrl+F1)", b.name,
                                               narrow(buf), b.name));
            }
        }
        GetPrivateProfileStringW(L"keys", (name + L"_pad").c_str(), L"", buf, 64, ini_path.c_str());
        if (buf[0]) {
            bool found = false;
            for (const auto& p : kPads) {
                if (lower(p.name) == lower(narrow(buf))) {
                    b.pad = b.pad_shown = p.bit;
                    found = true;
                }
            }
            if (!found) warnings.push_back(std::format("[keys] {}_pad={} not understood (A B X Y LB RB L3 R3 Up Down Left Right)",
                                                       b.name, narrow(buf)));
        }
    }
    set_chord_modifier(chords_bound());
    // Conflicts among the co-op hotkeys, and plain keys the game likely uses.
    for (size_t i = 0; i < std::size(kBindings); ++i) {
        const Binding& a = kBindings[i];
        if (!a.chord || !a.vk) continue;
        for (size_t j = i + 1; j < std::size(kBindings); ++j) {
            const Binding& b = kBindings[j];
            if (b.chord && b.vk == a.vk && b.ctrl == a.ctrl && b.shift == a.shift) {
                warnings.push_back(std::format("{} and {} are both on {}", a.name, b.name, key_text(static_cast<Action>(i))));
            }
            if (b.chord && b.pad && b.pad == a.pad) {
                warnings.push_back(std::format("{} and {} are both on the same pad button", a.name, b.name));
            }
        }
        const bool fkey = a.vk >= VK_F1 && a.vk <= VK_F12;
        if (!a.ctrl && !a.shift && !fkey) {
            warnings.push_back(std::format("{} is on {} with no Ctrl/Shift: the game probably uses that key too", a.name,
                                           key_text(static_cast<Action>(i))));
        }
    }
    return warnings;
}

void strip_unbound_hints(std::string& s) {
    for (size_t from = 0;;) {
        const size_t at = s.find('{', from);
        if (at == std::string::npos) break;
        const size_t tok_end = s.find('}', at);
        if (tok_end == std::string::npos) break;
        const Action a = action_by_name(std::string_view(s).substr(at + 1, tok_end - at - 1));
        if (a == Action::Count || binding(a).vk || binding(a).pad) {
            from = at + 1;
            continue;
        }
        const size_t open = s.rfind('(', at), close = s.find(')', at);
        if (open != std::string::npos && close != std::string::npos && s.find(')', open) == close) {
            const size_t semi = s.rfind(';', at);
            if (semi != std::string::npos && semi > open) s.erase(semi, close - semi);  // keeps "(first to 2"
            else s.erase(open, close - open + 1);
            from = open;
        } else {
            size_t end = tok_end + 1;
            const std::string_view rest(s.data() + end, s.size() - end);
            for (const char* tail : {" to accept", " accept", " decline", " stops", " ends", " withdraws", " go",
                                     " follow them", " send"}) {
                if (rest.rfind(tail, 0) == 0) {
                    end += std::strlen(tail);
                    break;
                }
            }
            s.erase(at, end - at);
            from = at;
        }
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == ':')) s.pop_back();
}

Action action_by_name(std::string_view name) {
    for (size_t i = 0; i < std::size(kBindings); ++i) {
        if (kBindings[i].name == name) return static_cast<Action>(i);
    }
    return Action::Count;
}

}  // namespace cdmp::input
