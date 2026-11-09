#include "mod/render/game_ui.h"

#include <windows.h>

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <string_view>

#include "mod/input/actions.h"
#include "mod/input/pad_input.h"
#include "mod/log.h"

namespace cdmp::render::ui {
namespace {

ImFont* g_serif = nullptr;
ImFont* g_serif_bold = nullptr;

constexpr ImU32 kGold = IM_COL32(232, 200, 130, 255);
constexpr ImU32 kText = IM_COL32(232, 228, 220, 255);
constexpr ImU32 kRule = IM_COL32(205, 195, 175, 150);
// Co-op+ (T34): the other player's colour and mark (a diamond for the host, a round mark for the guest).
ImU32 g_partner = kGold;
bool g_partner_round = false;

void player_mark(ImDrawList* dl, float x, float y, float r, ImU32 col) {
    if (g_partner_round) dl->AddCircleFilled(ImVec2(x, y), r * 0.85f, col, 16);
    else dl->AddQuadFilled(ImVec2(x, y - r), ImVec2(x + r, y), ImVec2(x, y + r), ImVec2(x - r, y), col);
}
constexpr ImU32 kCap = IM_COL32(214, 210, 202, 240);
constexpr ImU32 kCapText = IM_COL32(28, 26, 24, 255);

ImU32 with_alpha(ImU32 c, float a) {
    const auto base = static_cast<float>((c >> IM_COL32_A_SHIFT) & 0xFF);
    const auto na = static_cast<ImU32>(std::clamp(base * a, 0.0f, 255.0f));
    return (c & ~IM_COL32_A_MASK) | (na << IM_COL32_A_SHIFT);
}

ImFont* font_or_default(ImFont* f) { return f ? f : ImGui::GetFont(); }

float text_w(ImFont* f, float size, std::string_view s) {
    return font_or_default(f)->CalcTextSizeA(size, FLT_MAX, 0.0f, s.data(), s.data() + s.size()).x;
}

void text(ImDrawList* dl, ImFont* f, float size, float x, float y, ImU32 c, std::string_view s, bool shadow = true) {
    if (shadow) {
        dl->AddText(font_or_default(f), size, ImVec2(x + 1, y + 1), with_alpha(IM_COL32(0, 0, 0, 200), 1.0f), s.data(),
                    s.data() + s.size());
    }
    dl->AddText(font_or_default(f), size, ImVec2(x, y), c, s.data(), s.data() + s.size());
}

// A horizontal band that fades to nothing at both ends (the game's dialog background).
void faded_rect(ImDrawList* dl, float x0, float y0, float x1, float y1, ImU32 c) {
    const float w = x1 - x0, edge = w * 0.22f;
    const ImU32 clear = with_alpha(c, 0.0f);
    dl->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(x0 + edge, y1), clear, c, c, clear);
    dl->AddRectFilled(ImVec2(x0 + edge, y0), ImVec2(x1 - edge, y1), c);
    dl->AddRectFilledMultiColor(ImVec2(x1 - edge, y0), ImVec2(x1, y1), c, clear, clear, c);
}

// ---------------------------------------------------------------- button icons

float cap(ImDrawList* dl, float x, float y, float h, std::string_view label) {
    const float fs = h * 0.62f;
    const float w = std::max(h, text_w(g_serif_bold, fs, label) + h * 0.5f);
    if (dl) {
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), kCap, h * 0.18f);
        dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(90, 86, 80, 255), h * 0.18f, 1.0f);
        text(dl, g_serif_bold, fs, x + (w - text_w(g_serif_bold, fs, label)) * 0.5f, y + (h - fs) * 0.5f - 1, kCapText,
             label, false);
    }
    return w;
}

float pill(ImDrawList* dl, float x, float y, float h, std::string_view label, ImU32 bg) {
    const float fs = h * 0.5f;
    const float w = text_w(g_serif_bold, fs, label) + h * 0.6f;
    if (dl) {
        dl->AddRectFilled(ImVec2(x, y + h * 0.12f), ImVec2(x + w, y + h * 0.88f), bg, h * 0.38f);
        text(dl, g_serif_bold, fs, x + h * 0.3f, y + (h - fs) * 0.5f - 1, IM_COL32(240, 240, 240, 255), label, false);
    }
    return w;
}

float dpad(ImDrawList* dl, float x, float y, float h, uint16_t arm) {
    if (dl) {
        const float c = h * 0.5f, t = h * 0.16f, l = h * 0.46f;
        const ImVec2 m(x + c, y + c);
        const ImU32 off = IM_COL32(110, 110, 110, 255), on = IM_COL32(245, 245, 245, 255);
        auto bar = [&](float dx, float dy, uint16_t bit) {
            const ImVec2 a(m.x + std::min(0.0f, dx) - (dx == 0 ? t : 0), m.y + std::min(0.0f, dy) - (dy == 0 ? t : 0));
            const ImVec2 b(m.x + std::max(0.0f, dx) + (dx == 0 ? t : 0), m.y + std::max(0.0f, dy) + (dy == 0 ? t : 0));
            dl->AddRectFilled(a, b, (arm & bit) ? on : off, t * 0.4f);
        };
        bar(0, -l, input::kPadUp);
        bar(0, l, input::kPadDown);
        bar(-l, 0, input::kPadLeft);
        bar(l, 0, input::kPadRight);
    }
    return h;
}

float face(ImDrawList* dl, float x, float y, float h, uint16_t bit, bool playstation) {
    if (!dl) return h;
    const ImVec2 c(x + h * 0.5f, y + h * 0.5f);
    const float r = h * 0.48f;
    if (!playstation) {
        ImU32 col = IM_COL32(96, 176, 72, 255);
        const char* l = "A";
        if (bit == input::kPadB) col = IM_COL32(210, 64, 58, 255), l = "B";
        if (bit == input::kPadX) col = IM_COL32(64, 128, 216, 255), l = "X";
        if (bit == input::kPadY) col = IM_COL32(222, 182, 40, 255), l = "Y";
        dl->AddCircleFilled(c, r, IM_COL32(30, 30, 30, 240));
        dl->AddCircle(c, r * 0.86f, col, 0, h * 0.09f);
        const float fs = h * 0.62f;
        text(dl, g_serif_bold, fs, c.x - text_w(g_serif_bold, fs, l) * 0.5f, c.y - fs * 0.52f, col, l, false);
        return h;
    }
    dl->AddCircleFilled(c, r, IM_COL32(32, 32, 36, 240));
    const float s = h * 0.24f, w = h * 0.08f;
    if (bit == input::kPadA) {  // cross
        const ImU32 col = IM_COL32(126, 178, 232, 255);
        dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, w);
        dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), col, w);
    } else if (bit == input::kPadB) {  // circle
        dl->AddCircle(c, s, IM_COL32(232, 92, 92, 255), 0, w);
    } else if (bit == input::kPadX) {  // square
        dl->AddRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), IM_COL32(226, 138, 186, 255), 0.0f, w);
    } else {  // triangle
        dl->AddTriangle(ImVec2(c.x, c.y - s * 1.1f), ImVec2(c.x - s * 1.1f, c.y + s * 0.8f),
                        ImVec2(c.x + s * 1.1f, c.y + s * 0.8f), IM_COL32(100, 200, 160, 255), w);
    }
    return h;
}

// One pad button in the family's look.
float pad_button(ImDrawList* dl, float x, float y, float h, uint16_t bit, bool ps) {
    if (bit & input::kPadFace) return face(dl, x, y, h, bit, ps);
    if (bit & input::kPadDpad) return dpad(dl, x, y, h, bit);
    const ImU32 bg = IM_COL32(60, 60, 64, 240);
    if (bit == input::kPadBack) return pill(dl, x, y, h, ps ? "CREATE" : "VIEW", bg);
    if (bit == input::kPadTouchpad) return pill(dl, x, y, h, "TOUCHPAD", bg);
    if (bit == input::kPadStart) return pill(dl, x, y, h, ps ? "OPTIONS" : "MENU", bg);
    if (bit == input::kPadLB) return pill(dl, x, y, h, ps ? "L1" : "LB", bg);
    if (bit == input::kPadRB) return pill(dl, x, y, h, ps ? "R1" : "RB", bg);
    if (bit == input::kPadR3) return pill(dl, x, y, h, ps ? "R3" : "RS", bg);
    if (bit == input::kPadL3) return pill(dl, x, y, h, ps ? "L3" : "LS", bg);
    return pill(dl, x, y, h, "?", bg);
}

float plus(ImDrawList* dl, float x, float y, float h) {
    const float fs = h * 0.6f;
    if (dl) text(dl, g_serif, fs, x + 2, y + (h - fs) * 0.5f - 1, kText, "+");
    return text_w(g_serif, fs, "+") + 4;
}

// Co-op+: the controls card draws one device's buttons whatever was used last (guide_card).
int g_glyph_method = -1;  // -1 = the current input method

// The action's button(s) for the current input method.
float action_glyph(ImDrawList* dl, float x, float y, float h, input::Action a) {
    const auto& b = input::binding(a);
    const auto m = g_glyph_method >= 0 ? static_cast<input::Method>(g_glyph_method) : input::method();
    float w = 0;
    if (m == input::Method::Keyboard) {
        auto key_name = [](int vk) -> std::string {
            if (vk >= VK_F1 && vk <= VK_F12) return "F" + std::to_string(vk - VK_F1 + 1);
            if (vk == 0x1B) return "Esc";
            if (vk == VK_LEFT) return "<";
            if (vk == VK_RIGHT) return ">";
            return std::string(1, static_cast<char>(vk));
        };
        if (b.ctrl) w += cap(dl, x + w, y, h, "Ctrl"), w += plus(dl, x + w, y, h);
        if (b.shift) w += cap(dl, x + w, y, h, "Shift"), w += plus(dl, x + w, y, h);
        return w + cap(dl, x + w, y, h, key_name(b.vk));
    }
    const bool ps = m == input::Method::PlayStation;
    if (b.chord) {
        w += pad_button(dl, x + w, y, h, ps ? input::kPadTouchpad : input::kPadBack, ps);
        w += plus(dl, x + w, y, h);
    }
    return w + pad_button(dl, x + w, y, h, b.pad_shown, ps);
}

}  // namespace

void load_fonts() {
    ImGuiIO& io = ImGui::GetIO();
    const char* dir = "C:\\Windows\\Fonts\\";
    for (const char* name : {"pala.ttf", "georgia.ttf", "constan.ttf"}) {
        const std::string p = std::string(dir) + name;
        if (!std::filesystem::exists(p)) continue;
        g_serif = io.Fonts->AddFontFromFileTTF(p.c_str(), 20.0f);
        const std::string bold = std::string(dir) + (std::string_view(name) == "pala.ttf" ? "palab.ttf" : name);
        if (std::filesystem::exists(bold)) g_serif_bold = io.Fonts->AddFontFromFileTTF(bold.c_str(), 20.0f);
        if (g_serif) {
            io.FontDefault = g_serif;
            log::info("Overlay: game-style font {}", name);
            break;
        }
    }
    if (!g_serif_bold) g_serif_bold = g_serif;
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 2.0f;
    s.WindowBorderSize = 0.0f;
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0.04f, 0.035f, 0.03f, 0.78f);
    s.Colors[ImGuiCol_Text] = ImVec4(0.91f, 0.89f, 0.86f, 1.0f);
    s.Colors[ImGuiCol_Separator] = ImVec4(0.80f, 0.76f, 0.68f, 0.45f);
    s.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.70f, 0.16f, 0.13f, 1.0f);
}

ImFont* serif() { return g_serif; }
ImFont* serif_bold() { return g_serif_bold; }

float rich_text(ImDrawList* dl, float x, float y, float size, const std::string& s, unsigned int color, ImFont* f) {
    if (!f) f = g_serif;
    float w = 0;
    size_t at = 0;
    const unsigned int plain = color;
    while (at < s.size()) {
        const size_t open = s.find('{', at);
        const size_t close = open == std::string::npos ? std::string::npos : s.find('}', open);
        const size_t lit_end = close == std::string::npos ? s.size() : open;
        if (lit_end > at) {
            const std::string_view lit(s.data() + at, lit_end - at);
            if (dl) text(dl, f, size, x + w, y, color, lit);
            w += text_w(f, size, lit);
        }
        if (close == std::string::npos) break;
        const std::string_view token(s.data() + open + 1, close - open - 1);
        const auto a = input::action_by_name(token);
        if (token == "*") {  // {*}: the text that follows in gold (a chosen item), until the next {*}
            color = color == plain ? ((kGold & ~IM_COL32_A_MASK) | (plain & IM_COL32_A_MASK)) : plain;
        } else if (a != input::Action::Count) {
            const float h = size * 1.15f;
            w += action_glyph(dl, x + w, y + (size - h) * 0.5f, h, a) + size * 0.25f;
        } else {
            const std::string_view raw(s.data() + open, close - open + 1);
            if (dl) text(dl, f, size, x + w, y, color, raw);
            w += text_w(f, size, raw);
        }
        at = close + 1;
    }
    return w;
}

void band(float sw, float y, const std::string& title, const std::string& body, float alpha) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float k = std::max(0.6f, sw / 1920.0f);
    const float ts = 26.0f * k, bs = 20.0f * k;
    const float tw = title.empty() ? 0.0f : text_w(g_serif_bold, ts, title);
    const float bw = rich_text(nullptr, 0, 0, bs, body, kText);
    const float w = std::max({tw, bw, 360.0f * k}) + 260.0f * k;
    const float h = (title.empty() ? 0.0f : ts + 10 * k) + bs + 34 * k;
    const float x0 = sw * 0.5f - w * 0.5f, x1 = sw * 0.5f + w * 0.5f;
    faded_rect(dl, x0, y, x1, y + h, with_alpha(IM_COL32(8, 7, 6, 215), alpha));
    faded_rect(dl, x0, y, x1, y + 1.0f, with_alpha(kRule, alpha));
    faded_rect(dl, x0, y + h - 1.0f, x1, y + h, with_alpha(kRule, alpha));
    float ty = y + 16 * k;
    if (!title.empty()) {
        text(dl, g_serif_bold, ts, sw * 0.5f - tw * 0.5f, ty, with_alpha(kGold, alpha), title);
        ty += ts + 10 * k;
    }
    rich_text(dl, sw * 0.5f - bw * 0.5f, ty, bs, body, with_alpha(kText, alpha));
}

void guide_card(float sw, float sh, const std::string& title, const std::vector<std::string>& lines,
                const std::string& footer, float alpha) {
    // A controller plugged in: its buttons (user 2026-11-03), else the keys.
    g_glyph_method = static_cast<int>(input::connected_pad());
    struct Restore {
        ~Restore() { g_glyph_method = -1; }
    } restore;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float k = std::max(0.6f, sh / 1080.0f);
    const float ts = 34.0f * k, ls = 21.0f * k, fs = 17.0f * k, gap = ls * 0.62f;
    float w = text_w(g_serif_bold, ts, title);
    for (const auto& l : lines) w = std::max(w, rich_text(nullptr, 0, 0, ls, l, kText));
    w = std::max(w, rich_text(nullptr, 0, 0, fs, footer, kText)) + 120.0f * k;
    const float h = ts + 30 * k + static_cast<float>(lines.size()) * (ls + gap) + fs + 44 * k;
    const float x0 = sw * 0.5f - w * 0.5f, y0 = sh * 0.5f - h * 0.5f;
    faded_rect(dl, x0, y0, x0 + w, y0 + h, with_alpha(IM_COL32(8, 7, 6, 228), alpha));
    faded_rect(dl, x0, y0, x0 + w, y0 + 1.0f, with_alpha(kRule, alpha));
    faded_rect(dl, x0, y0 + h - 1.0f, x0 + w, y0 + h, with_alpha(kRule, alpha));
    float y = y0 + 20 * k;
    text(dl, g_serif_bold, ts, sw * 0.5f - text_w(g_serif_bold, ts, title) * 0.5f, y, with_alpha(kGold, alpha), title);
    y += ts + 6 * k;
    faded_rect(dl, sw * 0.5f - 200 * k, y, sw * 0.5f + 200 * k, y + 1.0f, with_alpha(kRule, alpha));
    y += 18 * k;
    for (const auto& l : lines) {
        rich_text(dl, x0 + 60 * k, y, ls, l, with_alpha(kText, alpha));
        y += ls + gap;
    }
    y += 6 * k;
    const float fw = rich_text(nullptr, 0, 0, fs, footer, kText);
    rich_text(dl, sw * 0.5f - fw * 0.5f, y, fs, footer, with_alpha(IM_COL32(200, 192, 176, 255), alpha * 0.9f));
}

void summary_card(float sw, float sh, const std::string& title, const std::string& subtitle, const std::string& partner,
                  const std::vector<SummaryLine>& rows, const std::string& footer, float alpha) {
    g_glyph_method = static_cast<int>(input::connected_pad());
    struct Restore {
        ~Restore() { g_glyph_method = -1; }
    } restore;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float k = std::max(0.6f, sh / 1080.0f);
    const float ts = 40.0f * k, ss = 20.0f * k, hs = 19.0f * k, ls = 22.0f * k, fs = 17.0f * k;
    const float row_h = ls + 16.0f * k;
    // Columns: the labels on the left, then "You" and the friend, each centred on its column.
    float label_w = 0;
    for (const auto& r : rows) label_w = std::max(label_w, text_w(g_serif, ls, r.label));
    const float col_w = std::max({150.0f * k, text_w(g_serif_bold, hs, partner) + 30.0f * k});
    const float inner = std::max({label_w + 2 * col_w + 40.0f * k, text_w(g_serif_bold, ts, title) + 40.0f * k,
                                  text_w(g_serif, ss, subtitle) + 40.0f * k, 560.0f * k});
    const float w = inner + 200.0f * k;
    const float h = ts + ss + 70.0f * k + hs + 18.0f * k + static_cast<float>(rows.size()) * row_h + fs + 50.0f * k;
    const float x0 = sw * 0.5f - w * 0.5f, y0 = sh * 0.46f - h * 0.5f;
    const float left = sw * 0.5f - inner * 0.5f, right = sw * 0.5f + inner * 0.5f;
    const float c2 = right - col_w * 0.5f, c1 = c2 - col_w;  // column centres: you, them
    faded_rect(dl, x0, y0, x0 + w, y0 + h, with_alpha(IM_COL32(8, 7, 6, 232), alpha));
    faded_rect(dl, x0, y0, x0 + w, y0 + 1.0f, with_alpha(kRule, alpha));
    faded_rect(dl, x0, y0 + h - 1.0f, x0 + w, y0 + h, with_alpha(kRule, alpha));
    float y = y0 + 24 * k;
    text(dl, g_serif_bold, ts, sw * 0.5f - text_w(g_serif_bold, ts, title) * 0.5f, y, with_alpha(kGold, alpha), title);
    y += ts + 6 * k;
    text(dl, g_serif, ss, sw * 0.5f - text_w(g_serif, ss, subtitle) * 0.5f, y, with_alpha(IM_COL32(205, 198, 184, 255), alpha),
         subtitle);
    y += ss + 16 * k;
    faded_rect(dl, sw * 0.5f - 260 * k, y, sw * 0.5f + 260 * k, y + 1.0f, with_alpha(kRule, alpha));
    y += 22 * k;
    // Column heads: our mark is plain light, theirs is their mark and colour (T34).
    const std::string you = "You";
    text(dl, g_serif_bold, hs, c1 - text_w(g_serif_bold, hs, you) * 0.5f, y, with_alpha(kText, alpha), you);
    const float pw = text_w(g_serif_bold, hs, partner);
    player_mark(dl, c2 - pw * 0.5f - 11 * k, y + hs * 0.55f, 5.0f * k, with_alpha(g_partner, alpha));
    text(dl, g_serif_bold, hs, c2 - pw * 0.5f, y, with_alpha(g_partner, alpha), partner);
    y += hs + 18 * k;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        if (i % 2 == 0) {
            faded_rect(dl, left - 30 * k, y - 7 * k, right + 30 * k, y + ls + 7 * k,
                       with_alpha(IM_COL32(255, 240, 210, 12), alpha));
        }
        text(dl, g_serif, ls, left, y, with_alpha(IM_COL32(214, 208, 196, 255), alpha), r.label);
        if (r.theirs.empty()) {
            const float vw = text_w(g_serif_bold, ls, r.ours);
            text(dl, g_serif_bold, ls, (c1 + c2) * 0.5f - vw * 0.5f, y, with_alpha(kGold, alpha), r.ours);
        } else {
            text(dl, g_serif_bold, ls, c1 - text_w(g_serif_bold, ls, r.ours) * 0.5f, y, with_alpha(kText, alpha), r.ours);
            text(dl, g_serif_bold, ls, c2 - text_w(g_serif_bold, ls, r.theirs) * 0.5f, y, with_alpha(g_partner, alpha),
                 r.theirs);
        }
        y += row_h;
    }
    y += 12 * k;
    const float fw = rich_text(nullptr, 0, 0, fs, footer, kText);
    rich_text(dl, sw * 0.5f - fw * 0.5f, y, fs, footer, with_alpha(IM_COL32(200, 192, 176, 255), alpha * 0.9f));
}

void veil(float sw, float sh, const std::string& count, const std::string& lines, float alpha) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float k = std::max(0.6f, sh / 1080.0f);
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(sw, sh), with_alpha(IM_COL32(6, 5, 4, 246), alpha));
    // A faint warm glow behind the count, the game's loading-screen feel.
    const float cy = sh * 0.42f;
    faded_rect(dl, sw * 0.5f - 520 * k, cy - 90 * k, sw * 0.5f + 520 * k, cy + 150 * k,
               with_alpha(IM_COL32(40, 30, 18, 120), alpha));
    const float hs = 22.0f * k, cs = 120.0f * k, ls = 23.0f * k;
    const std::string head = "Counting";
    text(dl, g_serif, hs, sw * 0.5f - text_w(g_serif, hs, head) * 0.5f, cy - 70 * k,
         with_alpha(IM_COL32(205, 195, 175, 255), alpha), head);
    text(dl, g_serif_bold, cs, sw * 0.5f - text_w(g_serif_bold, cs, count) * 0.5f, cy - 38 * k, with_alpha(kGold, alpha),
         count);
    float y = cy + cs - 20 * k;
    faded_rect(dl, sw * 0.5f - 240 * k, y, sw * 0.5f + 240 * k, y + 1.0f, with_alpha(kRule, alpha));
    y += 18 * k;
    size_t at = 0;
    while (at <= lines.size()) {
        const size_t nl = lines.find('\n', at);
        const std::string l = lines.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        const float lw = rich_text(nullptr, 0, 0, ls, l, kText);
        rich_text(dl, sw * 0.5f - lw * 0.5f, y, ls, l, with_alpha(kText, alpha));
        y += ls + 12 * k;
        if (nl == std::string::npos) break;
        at = nl + 1;
    }
}

void strip(float sw, float y, const std::string& body, float alpha) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float k = std::max(0.6f, sw / 1920.0f);
    const float bs = 18.0f * k;
    const float bw = rich_text(nullptr, 0, 0, bs, body, kText);
    const float w = bw + 220.0f * k, h = bs + 18 * k;
    const float x0 = sw * 0.5f - w * 0.5f, x1 = sw * 0.5f + w * 0.5f;
    faded_rect(dl, x0, y, x1, y + h, with_alpha(IM_COL32(8, 7, 6, 170), alpha));
    faded_rect(dl, x0, y + h - 1.0f, x1, y + h, with_alpha(kRule, alpha * 0.7f));
    rich_text(dl, sw * 0.5f - bw * 0.5f, y + 8 * k, bs, body, with_alpha(kText, alpha));
}

void title_card(float sw, float sh, const std::string& title, const std::string& line, unsigned int color,
                float alpha) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float k = std::max(0.6f, sh / 1080.0f);
    const float ts = 64.0f * k, ls = 22.0f * k, cy = sh * 0.30f;
    const float tw = text_w(g_serif_bold, ts, title);
    faded_rect(dl, sw * 0.5f - 420 * k, cy - 8 * k, sw * 0.5f + 420 * k, cy + ts + (line.empty() ? 12 : 48) * k,
               with_alpha(IM_COL32(8, 7, 6, 150), alpha));
    faded_rect(dl, sw * 0.5f - 300 * k, cy + ts + 2 * k, sw * 0.5f + 300 * k, cy + ts + 3 * k, with_alpha(kRule, alpha));
    text(dl, g_serif_bold, ts, sw * 0.5f - tw * 0.5f, cy, with_alpha(color, alpha), title);
    if (!line.empty()) {
        const float lw = rich_text(nullptr, 0, 0, ls, line, kText);
        rich_text(dl, sw * 0.5f - lw * 0.5f, cy + ts + 12 * k, ls, line, with_alpha(kText, alpha));
    }
}

void boss_bar(float sw, float sh, const std::string& name, float frac, const std::string& note) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float k = std::max(0.6f, sh / 1080.0f);
    const float w = 720 * k, h = 9 * k, ns = 21 * k;
    const float x0 = sw * 0.5f - w * 0.5f, y = sh * 0.045f + ns + 6 * k;
    frac = std::clamp(frac, 0.0f, 1.0f);
    const float nw = text_w(g_serif, ns, name);
    text(dl, g_serif, ns, sw * 0.5f - nw * 0.5f, y - ns - 4 * k, kGold, name);
    dl->AddRectFilled(ImVec2(x0 - 2 * k, y - 2 * k), ImVec2(x0 + w + 2 * k, y + h + 2 * k), IM_COL32(0, 0, 0, 170));
    dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + w, y + h), IM_COL32(40, 34, 30, 200));
    dl->AddRectFilledMultiColor(ImVec2(x0, y), ImVec2(x0 + w * frac, y + h), IM_COL32(150, 22, 18, 255),
                                IM_COL32(196, 40, 32, 255), IM_COL32(150, 22, 18, 255), IM_COL32(110, 14, 12, 255));
    dl->AddRect(ImVec2(x0 - 2 * k, y - 2 * k), ImVec2(x0 + w + 2 * k, y + h + 2 * k), with_alpha(kRule, 0.8f), 0.0f,
                1.0f);
    // The ornament at both ends: small diamonds.
    for (float cx : {x0 - 9 * k, x0 + w + 9 * k}) {
        const float cy = y + h * 0.5f, r = 5 * k;
        dl->AddQuadFilled(ImVec2(cx, cy - r), ImVec2(cx + r, cy), ImVec2(cx, cy + r), ImVec2(cx - r, cy), kGold);
    }
    if (!note.empty()) {
        const float ls = 17 * k, lw = rich_text(nullptr, 0, 0, ls, note, kText);
        rich_text(dl, sw * 0.5f - lw * 0.5f, y + h + 8 * k, ls, note, kText);
    }
}

void set_partner(unsigned int colour, bool round_mark) {
    g_partner = colour;
    g_partner_round = round_mark;
}

unsigned int partner_colour() { return g_partner; }

void kill_feed(float sw, float sh, const std::vector<std::string>& lines, const std::vector<bool>& ours) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float k = std::max(0.6f, sh / 1080.0f);
    const float ls = 18 * k;
    float y = sh * 0.20f;
    for (size_t i = 0; i < lines.size(); ++i) {
        const float x = sw - 40 * k - text_w(g_serif, ls, lines[i]);
        text(dl, g_serif, ls, x + 1, y + 1, IM_COL32(0, 0, 0, 200), lines[i]);
        text(dl, g_serif, ls, x, y, i < ours.size() && ours[i] ? kText : g_partner, lines[i]);
        y += ls * 1.35f;
    }
}

void chat_log(float sw, float sh, const std::vector<std::string>& lines) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float k = std::max(0.6f, sh / 1080.0f);
    const float x = 34 * k, ls = 17 * k;
    float y = sh * 0.555f - 14 * k - ls * 1.3f * static_cast<float>(lines.size());
    (void)sw;
    for (const auto& l : lines) {
        text(dl, g_serif, ls, x + 1, y + 1, IM_COL32(0, 0, 0, 200), l);
        text(dl, g_serif, ls, x, y, l.rfind("You: ", 0) == 0 ? kText : g_partner, l);  // T34: theirs in their colour
        y += ls * 1.3f;
    }
}

void party_frame(float sw, float sh, const std::string& name, float hp, float stamina, const std::string& status,
                 double t) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float k = std::max(0.6f, sh / 1080.0f);
    const float x = 34 * k, y = sh * 0.555f, w = 230 * k, ns = 20 * k, bh = 7 * k, sth = 3 * k;
    const bool low = hp >= 0.0f && hp < 0.25f;
    // Backing: a soft dark wash so it reads on bright ground, like the game's own corner texts.
    dl->AddRectFilledMultiColor(ImVec2(x - 14 * k, y - 6 * k), ImVec2(x + w + 18 * k, y + ns + bh + sth + 34 * k),
                                IM_COL32(0, 0, 0, 120), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 120));
    const float r = 5 * k, cy = y + ns * 0.55f;
    player_mark(dl, x, cy, r, g_partner);
    text(dl, g_serif, ns, x + 12 * k, y, g_partner, name);
    float by = y + ns + 6 * k;
    if (hp >= 0.0f) {
        const float f = std::clamp(hp, 0.0f, 1.0f);
        dl->AddRectFilled(ImVec2(x - 1 * k, by - 1 * k), ImVec2(x + w + 1 * k, by + bh + 1 * k), IM_COL32(0, 0, 0, 170));
        dl->AddRectFilled(ImVec2(x, by), ImVec2(x + w, by + bh), IM_COL32(40, 34, 30, 200));
        dl->AddRectFilledMultiColor(ImVec2(x, by), ImVec2(x + w * f, by + bh), IM_COL32(150, 22, 18, 255),
                                    IM_COL32(196, 40, 32, 255), IM_COL32(150, 22, 18, 255), IM_COL32(110, 14, 12, 255));
        ImU32 edge = with_alpha(kRule, 0.8f);
        if (low) {  // pulse
            const float a = 0.55f + 0.45f * static_cast<float>(std::sin(t * 6.0));
            edge = IM_COL32(230, 60, 48, static_cast<int>(255 * a));
        }
        dl->AddRect(ImVec2(x - 1 * k, by - 1 * k), ImVec2(x + w + 1 * k, by + bh + 1 * k), edge, 0.0f, low ? 2.0f : 1.0f);
        by += bh + 4 * k;
    }
    if (stamina >= 0.0f) {
        const float f = std::clamp(stamina, 0.0f, 1.0f);
        dl->AddRectFilled(ImVec2(x, by), ImVec2(x + w, by + sth), IM_COL32(40, 34, 30, 200));
        dl->AddRectFilled(ImVec2(x, by), ImVec2(x + w * f, by + sth), IM_COL32(214, 196, 120, 230));
        by += sth + 5 * k;
    }
    const std::string line = low ? (status.empty() ? std::string("Badly hurt") : "Badly hurt  -  " + status) : status;
    if (!line.empty()) text(dl, g_serif, 15 * k, x, by, low ? IM_COL32(236, 120, 104, 255) : kText, line);
}

void hand_medallion(ImDrawList* dl, float x, float y, float s, int hand, const std::string& caption, unsigned int color,
                    float alpha) {
    const ImU32 c = with_alpha(color, alpha), dark = with_alpha(IM_COL32(10, 9, 8, 210), alpha);
    const float r = s * 0.5f;
    dl->AddCircleFilled(ImVec2(x, y), r, dark, 40);
    dl->AddCircle(ImVec2(x, y), r, c, 40, 2.0f);
    dl->AddCircle(ImVec2(x, y), r - 4, with_alpha(kRule, alpha * 0.6f), 40, 1.0f);
    const float u = s / 10.0f;  // drawing unit
    auto finger = [&](float bx, float by, float tx, float ty, float w) {
        dl->AddLine(ImVec2(bx, by), ImVec2(tx, ty), c, w);
        dl->AddCircleFilled(ImVec2(tx, ty), w * 0.5f, c);
    };
    if (hand == 2) {  // rock: a fist, knuckles on top
        dl->AddRectFilled(ImVec2(x - 2.4f * u, y - 1.2f * u), ImVec2(x + 2.4f * u, y + 2.4f * u), c, 1.2f * u);
        for (int i = 0; i < 4; ++i) dl->AddCircleFilled(ImVec2(x - 1.8f * u + i * 1.2f * u, y - 1.3f * u), 0.75f * u, c);
        dl->AddLine(ImVec2(x - 2.0f * u, y + 0.4f * u), ImVec2(x + 0.6f * u, y + 0.4f * u), dark, 0.5f * u);
    } else if (hand == 1) {  // paper: an open hand
        dl->AddRectFilled(ImVec2(x - 2.0f * u, y - 0.2f * u), ImVec2(x + 2.0f * u, y + 2.8f * u), c, 1.0f * u);
        for (int i = 0; i < 4; ++i) {
            const float fx = x - 1.5f * u + i * 1.0f * u;
            finger(fx, y + 0.5f * u, fx + (i - 1.5f) * 0.25f * u, y - (i == 1 || i == 2 ? 3.4f : 2.8f) * u, 0.85f * u);
        }
        finger(x - 1.8f * u, y + 1.4f * u, x - 3.2f * u, y - 0.2f * u, 0.85f * u);
    } else if (hand == 0) {  // scissors: two fingers apart
        dl->AddRectFilled(ImVec2(x - 1.8f * u, y + 0.4f * u), ImVec2(x + 1.8f * u, y + 3.0f * u), c, 1.0f * u);
        finger(x - 0.5f * u, y + 1.0f * u, x - 1.9f * u, y - 3.2f * u, 0.9f * u);
        finger(x + 0.5f * u, y + 1.0f * u, x + 1.9f * u, y - 3.2f * u, 0.9f * u);
    } else if (hand == -2) {  // picked, hidden: a seal
        dl->AddCircleFilled(ImVec2(x, y), 2.2f * u, c, 24);
        dl->AddCircle(ImVec2(x, y), 1.4f * u, dark, 24, 0.5f * u);
    } else {  // still choosing
        const float qs = s * 0.6f, qw = text_w(g_serif_bold, qs, "?");
        text(dl, g_serif_bold, qs, x - qw * 0.5f, y - qs * 0.55f, c, "?");
    }
    if (!caption.empty()) {
        const float cs = std::max(14.0f, s * 0.3f), cw = text_w(g_serif, cs, caption);
        text(dl, g_serif, cs, x - cw * 0.5f, y + r + 4, with_alpha(kText, alpha), caption);
    }
}

void choice_menu(ImDrawList* dl, float screen_w, float screen_h, const std::string& name, const std::string& subtitle,
                 const std::vector<std::string>& items, size_t pick) {
    // B16b: the shop menu's look (the merchant's place over the name, then the stacked choices, the chosen one framed).
    const float bw = 340.0f, bh = 48.0f, gap = 10.0f, x = screen_w * 0.17f;
    const float total = 70.0f + static_cast<float>(items.size()) * (bh + gap);
    float y = std::max(40.0f, screen_h * 0.5f - total * 0.5f);
    const float ss = 19.0f, ns = 30.0f;
    text(dl, g_serif, ss, x + (bw - text_w(g_serif, ss, subtitle)) * 0.5f, y, IM_COL32(232, 200, 130, 255), subtitle);
    y += ss + 14.0f;
    text(dl, g_serif_bold, ns, x + (bw - text_w(g_serif_bold, ns, name)) * 0.5f, y, kText, name);
    y += ns + 22.0f;
    for (size_t i = 0; i < items.size(); ++i) {
        const bool sel = i == pick;
        const bool dim = items[i].find("(coming soon)") != std::string::npos;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw, y + bh), sel ? IM_COL32(58, 52, 42, 235) : IM_COL32(16, 15, 14, 215));
        dl->AddRect(ImVec2(x, y), ImVec2(x + bw, y + bh),
                    sel ? IM_COL32(232, 200, 130, 255) : IM_COL32(90, 84, 76, 200), 0.0f, sel ? 2.0f : 1.0f);
        const float fs = 20.0f, tw = text_w(g_serif_bold, fs, items[i]);
        text(dl, g_serif_bold, fs, x + (bw - tw) * 0.5f, y + (bh - fs) * 0.5f - 1.0f,
             dim ? IM_COL32(130, 124, 116, 255) : (sel ? IM_COL32(245, 236, 220, 255) : kText), items[i]);
        y += bh + gap;
    }
}

void hold_prompt(ImDrawList* dl, float x, float y, float progress, const std::string& key, const std::string& label,
                 float alpha) {
    // B16b (user 2026-11-09): the game's hold prompt -- the key in a ring that fills while it is held -- with a label.
    const float r = 15.0f, ks = 17.0f, ls = 21.0f;
    const ImU32 dark = with_alpha(IM_COL32(10, 9, 8, 200), alpha), rim = with_alpha(IM_COL32(222, 214, 200, 230), alpha),
                fill = with_alpha(IM_COL32(232, 200, 130, 255), alpha);
    dl->AddCircleFilled(ImVec2(x, y), r, dark, 40);
    dl->AddCircle(ImVec2(x, y), r, rim, 40, 1.5f);
    const float p = std::clamp(progress, 0.0f, 1.0f);
    if (p > 0.0f) {
        const float a0 = -1.5707963f, a1 = a0 + 6.2831853f * p;
        dl->PathArcTo(ImVec2(x, y), r, a0, a1, 40);
        dl->PathStroke(fill, 3.5f);
    }
    const float kw = text_w(g_serif_bold, ks, key);
    text(dl, g_serif_bold, ks, x - kw * 0.5f, y - ks * 0.55f, p >= 1.0f ? fill : rim, key);
    if (!label.empty()) {
        text(dl, g_serif_bold, ls, x + r + 8.0f, y - ls * 0.55f, with_alpha(IM_COL32(245, 240, 230, 255), alpha), label);
    }
}

void nameplate(ImDrawList* dl, float x, float y, const std::string& name, float frac, const std::string& distance,
               float alpha, float scale) {
    const float ns = 19.0f * scale, ds = 14.0f * scale, r = 4.5f * scale;
    dl->AddQuadFilled(ImVec2(x, y - r - 1), ImVec2(x + r + 1, y), ImVec2(x, y + r + 1), ImVec2(x - r - 1, y),
                      with_alpha(IM_COL32(0, 0, 0, 170), alpha));
    player_mark(dl, x, y, r, with_alpha(g_partner, alpha));
    const float nw = text_w(g_serif, ns, name);
    float ty = y - r - 8 * scale - (frac >= 0 ? 8 * scale : 0) - ns;
    text(dl, g_serif, ns, x - nw * 0.5f, ty, with_alpha(IM_COL32(240, 236, 228, 255), alpha), name);
    if (frac >= 0) {
        const float w = 66 * scale, h = 4 * scale, by = y - r - 10 * scale;
        frac = std::clamp(frac, 0.0f, 1.0f);
        dl->AddRectFilled(ImVec2(x - w * 0.5f - 1, by - 1), ImVec2(x + w * 0.5f + 1, by + h + 1),
                          with_alpha(IM_COL32(0, 0, 0, 180), alpha));
        dl->AddRectFilled(ImVec2(x - w * 0.5f, by), ImVec2(x - w * 0.5f + w * frac, by + h),
                          with_alpha(IM_COL32(186, 44, 36, 255), alpha));
    }
    if (!distance.empty()) {
        const float dw = text_w(g_serif, ds, distance);
        text(dl, g_serif, ds, x - dw * 0.5f, y + r + 4 * scale, with_alpha(IM_COL32(205, 198, 186, 220), alpha), distance);
    }
}

void death_screen(float sw, float sh, const std::string& title, const std::string& question,
                  const std::string& choices) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float k = std::max(0.6f, sh / 1080.0f);
    // The game greys the world out and rims it in dark red (its own screen, 1.0.0.3549). An overlay cannot take the
    // colour away, so a heavy cool-grey veil stands in for it, with the red rim on all four edges.
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(sw, sh), IM_COL32(30, 30, 32, 170));
    const ImU32 rim = IM_COL32(58, 4, 4, 150), clear = IM_COL32(58, 4, 4, 0);
    const float ry = sh * 0.13f, rx = sw * 0.09f;
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(sw, ry), rim, rim, clear, clear);
    dl->AddRectFilledMultiColor(ImVec2(0, sh - ry), ImVec2(sw, sh), clear, clear, rim, rim);
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(rx, sh), rim, clear, clear, rim);
    dl->AddRectFilledMultiColor(ImVec2(sw - rx, 0), ImVec2(sw, sh), clear, rim, rim, clear);
    const float ts = 40.0f * k, qs = 22.0f * k, cs = 20.0f * k;
    const float cy = sh * 0.44f;
    const float tw = text_w(g_serif, ts, title);
    text(dl, g_serif, ts, sw * 0.5f - tw * 0.5f, cy - ts * 0.4f, IM_COL32(196, 48, 40, 255), title);
    const float qw = text_w(g_serif, qs, question);
    text(dl, g_serif, qs, sw * 0.5f - qw * 0.5f, cy + ts * 0.95f + 6 * k, IM_COL32(236, 232, 226, 255), question);
    const float cw = rich_text(nullptr, 0, 0, cs, choices, IM_COL32(220, 216, 210, 255));
    rich_text(dl, sw * 0.5f - cw * 0.5f, cy + ts * 0.95f + 70 * k, cs, choices, IM_COL32(220, 216, 210, 255));
}

}  // namespace cdmp::render::ui
